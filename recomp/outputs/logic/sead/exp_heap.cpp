// sead::ExpHeap allocator, reconstructed from exefs/main.
//
//   0x00444d5c  isInclude              0x004456e4  findFreeBlockHead_
//   0x00444eb0  adjust                 0x004457f8  findFreeBlockHeadAligned_
//   0x00445084  tryAlloc               0x004459a8  findFreeBlockTail_
//   0x00445248  allocFromHead_         0x00445ac8  findFreeBlockTailAligned_
//   0x00445374  allocFromHeadAligned_  0x00445cdc  free
//   0x00445514  allocFromTail_         0x00445cec  freeAndGetAllocatableSize
//   0x004455e0  allocFromTailAligned_  0x00445f14  recycleBlock_
//   0x0044636c  resizeBack             0x004466d8  getStartAddress
//   0x004466e0  getEndAddress          0x004466ec  getSize
//   0x004466f4  getFreeSize            0x0044679c  getMaxAllocatableSize
//   0x00446958  findAdjustableTailBlock_   0x00446a54  findAdjustableHeadBlock_
//   0x00447108  isEmpty   0x00447118 isFreeable   0x00447120 isResizable   0x00447128 isAdjustable
//
// Layout:
//   * The free list is kept in address order. The used list is in allocation order (back
//     for forward heaps, front for reverse heaps) until adjust() sorts it.
//   * A block is MemBlock (0x20) + `pad` + `size` bytes. The user pointer is
//     header + 0x20 + pad. When pad != 0, the qword just before the user pointer holds
//     `header | 1` so free() can find the header.
//   * A leftover piece is only split off into its own free block if it's more than 0x20
//     bytes (room for a header plus at least 1 byte); otherwise it stays in the allocation.

#include <atomic>
#include <cstdint>

#include "recomp/sead.hpp"

namespace sead {

namespace {

using MemBlock = ExpHeap::MemBlock;

constexpr size_t kHeader = sizeof(MemBlock);  // 0x20
constexpr size_t kMinSplit = kHeader + 1;     // 0x21: `cmp xN, #0x21; b.hs split`
constexpr uint16_t kFreeMagic = 0xFFFF;

inline uintptr_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

inline uintptr_t alignUp(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }

inline MemBlock* blockOf(const OffsetList& list, const ListNode* node) {
    return reinterpret_cast<MemBlock*>(addr(node) - list.offset);
}

inline ListNode* nodeOf(const OffsetList& list, MemBlock* block) {
    return reinterpret_cast<ListNode*>(addr(block) + list.offset);
}

inline ListNode* sentinel(OffsetList& list) { return &list.start_end; }
inline const ListNode* sentinel(const OffsetList& list) { return &list.start_end; }

// Fresh header as written inline in several places: prev/next = 0, the u32 at +0x10
// (magic + pad) = 0, then the size. unk_14 is left untouched, as in the binary.
inline MemBlock* initBlock(void* where, size_t size) {
    auto* const b = static_cast<MemBlock*>(where);
    b->list_node = {};
    b->magic = 0;
    b->pad = 0;
    b->size = size;
    return b;
}

// User pointer -> header (inline in free/resizeBack):
//   ldur x8,[p,#-8]; tst x8,#1; csel hdr, p-0x20, x8-1, eq
inline MemBlock* blockFromUserPtr(void* ptr) {
    const uintptr_t marker = *reinterpret_cast<const uintptr_t*>(addr(ptr) - 8);
    return reinterpret_cast<MemBlock*>((marker & 1) ? marker - 1 : addr(ptr) - kHeader);
}

inline void writePadMarker(MemBlock* b, size_t pad) {
    *reinterpret_cast<uintptr_t*>(addr(b) + kHeader + pad - 8) = addr(b) | 1;
}

// Shared selection loop for the four search routines. The binary has four copies of each
// mode; they differ only in the fit test and the direction of travel.
//   FirstFit (0)       first block that fits
//   BestFit  (1)       smallest fitting block; ties keep the earlier one (`b.hi`)
//   WorstFit (2)       largest fitting block; ties keep the earlier one (`b.lo`)
//   anything else      first fit, but the list is scanned to the end
template <typename Iter, typename Fits>
MemBlock* selectBlock(ExpHeap::AllocMode mode, Iter&& for_each, Fits&& fits) {
    MemBlock* best = nullptr;
    switch (mode) {
        case ExpHeap::AllocMode::FirstFit:
            for_each([&](MemBlock* b) {
                if (fits(b)) { best = b; return false; }
                return true;
            });
            break;
        case ExpHeap::AllocMode::BestFit:
            for_each([&](MemBlock* b) {
                if (fits(b) && (!best || best->size > b->size)) best = b;
                return true;
            });
            break;
        case ExpHeap::AllocMode::WorstFit:
            for_each([&](MemBlock* b) {
                if (fits(b) && (!best || best->size < b->size)) best = b;
                return true;
            });
            break;
        default:
            for_each([&](MemBlock* b) {
                if (fits(b) && !best) best = b;
                return true;
            });
            break;
    }
    return best;
}

}  // namespace

// -----------------------------------------------------------------------------------
// Free-block search
// -----------------------------------------------------------------------------------

// Head searches walk sentinel.next -> ... and stop at the sentinel.
MemBlock* ExpHeap::findFreeBlockHead_(size_t size) const {
    const ListNode* const s = sentinel(free_list_);
    if (s->next == s) return nullptr;
    auto walk = [&](auto&& visit) {
        for (const ListNode* n = s->next; n != s; n = n->next)
            if (!visit(blockOf(free_list_, n))) return;
    };
    return selectBlock(alloc_mode_, walk, [&](MemBlock* b) { return b->size >= size; });
}

MemBlock* ExpHeap::findFreeBlockHeadAligned_(size_t size, int32_t align) const {
    const ListNode* const s = sentinel(free_list_);
    if (s->next == s) return nullptr;
    const uintptr_t a = static_cast<uint32_t>(align);
    auto walk = [&](auto&& visit) {
        for (const ListNode* n = s->next; n != s; n = n->next)
            if (!visit(blockOf(free_list_, n))) return;
    };
    // Must fit `size` after the user pointer is moved up to the alignment.
    auto fits = [&](MemBlock* b) {
        if (b->size < size) return false;
        const uintptr_t data = addr(b->data());
        return b->size >= size + (alignUp(data, a) - data);
    };
    return selectBlock(alloc_mode_, walk, fits);
}

// Tail searches start from sentinel.prev, only when count > 0, and stop at the sentinel.
MemBlock* ExpHeap::findFreeBlockTail_(size_t size) const {
    const ListNode* const s = sentinel(free_list_);
    if (free_list_.count <= 0 || !s->prev) return nullptr;
    auto walk = [&](auto&& visit) {
        for (const ListNode* n = s->prev; n != s; n = n->prev)
            if (!visit(blockOf(free_list_, n))) return;
    };
    return selectBlock(alloc_mode_, walk, [&](MemBlock* b) { return b->size >= size; });
}

MemBlock* ExpHeap::findFreeBlockTailAligned_(size_t size, int32_t align) const {
    const ListNode* const s = sentinel(free_list_);
    if (free_list_.count <= 0 || !s->prev) return nullptr;
    const uintptr_t mask = static_cast<uint32_t>(align - 1);
    auto walk = [&](auto&& visit) {
        for (const ListNode* n = s->prev; n != s; n = n->prev)
            if (!visit(blockOf(free_list_, n))) return;
    };
    // Allocating from the end: the slack is whatever is left after the aligned start,
    // i.e. (end - size) mod align. The binary does this sum in 32 bits, which gives the
    // same result once masked.
    auto fits = [&](MemBlock* b) {
        if (b->size < size) return false;
        const uintptr_t slack = (addr(b->end()) - size) & mask;
        return b->size >= size + slack;
    };
    return selectBlock(alloc_mode_, walk, fits);
}

// -----------------------------------------------------------------------------------
// Allocation primitives
// -----------------------------------------------------------------------------------

// Used blocks go to the back for forward heaps and to the front for reverse heaps.
void ExpHeap::pushUsedBlock_(MemBlock* block) {
    if (direction_ == cHeapDirection_Forward)
        ListNode_insertBefore(sentinel(use_list_), nodeOf(use_list_, block));
    else
        ListNode_insertAfter(sentinel(use_list_), nodeOf(use_list_, block));
    ++use_list_.count;
}

// 0x00445248
MemBlock* ExpHeap::allocFromHead_(size_t size) {
    MemBlock* const block = findFreeBlockHead_(size);
    if (!block) return nullptr;

    const size_t old_size = block->size;
    block->size = size;
    const size_t remain = old_size - size;
    const uint16_t pad = block->pad;

    // Remember the following free block so the remainder keeps address order.
    ListNode* const node = nodeOf(free_list_, block);
    ListNode* const next_node = node->next;
    MemBlock* const next = next_node == sentinel(free_list_) ? nullptr
                                                             : blockOf(free_list_, next_node);
    ListNode_erase(node);
    --free_list_.count;
    pushUsedBlock_(block);

    if (remain >= kMinSplit) {
        MemBlock* const rest = initBlock(reinterpret_cast<uint8_t*>(block) + kHeader + pad + size,
                                         remain - kHeader);
        rest->magic = kFreeMagic;
        ListNode_insertBefore(next ? nodeOf(free_list_, next) : sentinel(free_list_),
                              nodeOf(free_list_, rest));
        ++free_list_.count;
    } else if (remain != 0) {
        block->size = old_size;  // too small to split: hand out the whole block
    }
    return block;
}

// 0x00445374
MemBlock* ExpHeap::allocFromHeadAligned_(size_t size, int32_t align) {
    MemBlock* const block = findFreeBlockHeadAligned_(size, align);
    if (!block) return nullptr;

    ListNode* const next_node = nodeOf(free_list_, block)->next;
    MemBlock* const next = next_node == sentinel(free_list_) ? nullptr
                                                             : blockOf(free_list_, next_node);
    const size_t old_size = block->size;
    const uintptr_t data = addr(block->data());
    const size_t padding = alignUp(data, static_cast<uint32_t>(align)) - data;
    const size_t consumed = padding + size;

    MemBlock* result;
    if (padding >= 0x10000) {
        // Too much padding for the u16 field: keep the front as its own free block
        // (it stays in the free list) and put a new header right before the aligned data.
        block->pad = 0;
        block->size = padding - kHeader;
        result = initBlock(reinterpret_cast<uint8_t*>(block) + padding, size);
    } else {
        block->pad = static_cast<uint16_t>(padding);
        if (padding & 0xFFFF) writePadMarker(block, padding);
        block->size = size;
        ListNode_erase(nodeOf(free_list_, block));
        --free_list_.count;
        result = block;
    }

    const size_t remain = old_size - consumed;
    pushUsedBlock_(result);

    if (remain >= kMinSplit) {
        MemBlock* const rest = initBlock(
            reinterpret_cast<uint8_t*>(result) + kHeader + result->pad + size, remain - kHeader);
        rest->magic = kFreeMagic;
        ListNode_insertBefore(next ? nodeOf(free_list_, next) : sentinel(free_list_),
                              nodeOf(free_list_, rest));
        ++free_list_.count;
    } else if (remain != 0) {
        result->size = remain + size;
    }
    return result;
}

// 0x00445514 — carve `size` bytes off the end of the chosen free block.
MemBlock* ExpHeap::allocFromTail_(size_t size) {
    MemBlock* const block = findFreeBlockTail_(size);
    if (!block) return nullptr;

    const size_t remain = block->size - size;
    if (remain >= kMinSplit) {
        block->size = remain - kHeader;
        MemBlock* const tail =
            initBlock(reinterpret_cast<uint8_t*>(block) + block->pad + remain, size);
        pushUsedBlock_(tail);
        return tail;
    }
    ListNode_erase(nodeOf(free_list_, block));
    --free_list_.count;
    pushUsedBlock_(block);
    return block;
}

// 0x004455e0
MemBlock* ExpHeap::allocFromTailAligned_(size_t size, int32_t align) {
    MemBlock* const block = findFreeBlockTailAligned_(size, align);
    if (!block) return nullptr;

    const uintptr_t data = addr(block->data());
    const size_t old_size = block->size;
    // 32-bit add then mask with (align-1), exactly as `sub w8,w11,w21; add w8,w8,w10`.
    const uint32_t slack =
        (static_cast<uint32_t>(old_size) - static_cast<uint32_t>(size) +
         static_cast<uint32_t>(data)) & static_cast<uint32_t>(align - 1);
    const size_t consumed = slack + size;
    const size_t remain = old_size - consumed;

    if (remain >= kMinSplit) {
        block->size = remain - kHeader;
        // The new header ends where the aligned data starts; the slack ends up after it.
        MemBlock* const tail = initBlock(reinterpret_cast<uint8_t*>(data) + remain - kHeader,
                                         consumed);
        pushUsedBlock_(tail);
        return tail;
    }
    block->pad = static_cast<uint16_t>(remain);
    if (remain & 0xFFFF) writePadMarker(block, remain);
    block->size = consumed;
    ListNode_erase(nodeOf(free_list_, block));
    --free_list_.count;
    pushUsedBlock_(block);
    return block;
}

// -----------------------------------------------------------------------------------
// 0x00445084 — tryAlloc
// -----------------------------------------------------------------------------------
void* ExpHeap::tryAlloc(size_t size, int32_t alignment) {
    HeapMgr* const mgr = HeapMgr::sInstancePtr;
    auto report_failure = [&](size_t alloc_size, int32_t alloc_alignment) {
        if (mgr && mgr->alloc_failed_cb_) {
            const AllocFailedCallbackArg arg{this, size, alignment, alloc_size, alloc_alignment};
            mgr->alloc_failed_cb_->invoke(&arg);
        }
    };

    size_t alloc_size = size > 8 ? size : 8;
    const uint32_t abs_alignment =
        static_cast<uint32_t>(alignment < 0 ? -alignment : alignment);
    if ((abs_alignment & (abs_alignment - 1)) != 0) {
        report_failure(alloc_size, alignment);
        return nullptr;
    }
    alloc_size = alignUp(alloc_size, 8);
    if (alloc_size < size) {  // overflow while rounding up
        report_failure(alloc_size, alignment);
        return nullptr;
    }

    ConditionalScopedLock lock(*this);

    // On a reverse heap the meaning of the alignment's sign flips.
    const int32_t directed = direction_ * alignment;
    int32_t used_alignment;
    MemBlock* block;
    if (directed < 0) {
        used_alignment = -directed;
        block = directed >= -8 ? allocFromTail_(alloc_size)
                               : allocFromTailAligned_(alloc_size, used_alignment);
    } else {
        used_alignment = directed;
        block = directed <= 8 ? allocFromHead_(alloc_size)
                              : allocFromHeadAligned_(alloc_size, directed);
    }

    if (!block) {
        report_failure(alloc_size, used_alignment);  // still under the heap lock
        return nullptr;
    }
    block->magic = heapId();
    return block->data();
}

// -----------------------------------------------------------------------------------
// 0x00445f14 — put `block` back into the address-ordered free list and merge it with
// any free neighbours. Returns the block that now holds the space.
// -----------------------------------------------------------------------------------
MemBlock* ExpHeap::recycleBlock_(MemBlock* block) {
    ListNode* const s = sentinel(free_list_);
    MemBlock* prev = nullptr;
    MemBlock* next = nullptr;

    // How to find the free neighbours: walk the (sorted) free list, or walk forward
    // through memory from the block until a free header turns up. Auto picks the list
    // walk while free blocks are rare compared with used ones.
    bool list_walk;
    switch (free_merge_mode_) {
        case FreeMergeMode::ListWalk: list_walk = true; break;
        case FreeMergeMode::AutoIfFew: list_walk = free_list_.count <= 1; break;
        default: list_walk = 3 * free_list_.count <= use_list_.count + 3; break;
    }

    if (list_walk) {
        if (s->next != s) {
            MemBlock* cur = blockOf(free_list_, s->next);
            if (addr(cur) > addr(block)) {
                next = cur;
            } else {
                for (;;) {
                    MemBlock* const candidate_prev = cur;
                    ListNode* const n = nodeOf(free_list_, cur)->next;
                    if (n == s) break;
                    cur = blockOf(free_list_, n);
                    if (addr(cur) > addr(block)) {
                        prev = candidate_prev;
                        next = cur;
                        break;
                    }
                }
            }
        }
    } else {
        const uintptr_t heap_end =
            addr(start_) + (direction_ == cHeapDirection_Forward ? size_ : size_ - sizeof(ExpHeap));
        for (auto* cur = reinterpret_cast<MemBlock*>(block->end()); addr(cur) < heap_end;
             cur = reinterpret_cast<MemBlock*>(cur->end())) {
            if (static_cast<int16_t>(cur->magic) == -1) {
                next = cur;
                ListNode* const p = nodeOf(free_list_, cur)->prev;
                if (p != s && addr(p) != static_cast<uintptr_t>(free_list_.offset))
                    prev = blockOf(free_list_, p);
                break;
            }
        }
    }

    if (!next) {
        // 0x446010: nothing free above us. Merge into the last free block if it ends
        // exactly here, else append.
        if (free_list_.count != 0) {
            MemBlock* const last = blockOf(free_list_, s->prev);
            if (last->end() == reinterpret_cast<uint8_t*>(block)) {
                last->size = last->size + kHeader + block->pad + block->size;
                return last;
            }
        }
        block->magic = kFreeMagic;
        ListNode_insertBefore(s, nodeOf(free_list_, block));
        ++free_list_.count;
        return block;
    }

    MemBlock* cur = block;
    bool merged_into_prev = false;
    if (prev && prev->end() == reinterpret_cast<uint8_t*>(block)) {
        prev->size = prev->size + kHeader + block->pad + block->size;
        cur = prev;
        merged_into_prev = true;
    }

    if (cur->end() == reinterpret_cast<uint8_t*>(next)) {
        if (!merged_into_prev) {
            cur->magic = kFreeMagic;
            ListNode_insertBefore(nodeOf(free_list_, next), nodeOf(free_list_, cur));
            ++free_list_.count;
        }
        cur->size = cur->size + next->pad + next->size + kHeader;
        ListNode_erase(nodeOf(free_list_, next));
        --free_list_.count;
        return cur;
    }
    if (merged_into_prev) return cur;

    cur->magic = kFreeMagic;
    ListNode_insertBefore(nodeOf(free_list_, next), nodeOf(free_list_, cur));
    ++free_list_.count;
    return cur;
}

// -----------------------------------------------------------------------------------
// 0x00445cdc / 0x00445cec — free
// -----------------------------------------------------------------------------------
void ExpHeap::free(void* ptr) {
    freeAndGetAllocatableSize(ptr, 8);  // virtual (+0x40), tail call
}

size_t ExpHeap::freeAndGetAllocatableSize(void* ptr, int32_t alignment) {
    if (!ptr) return 0;
    if (!isInclude(ptr)) return 0;  // virtual (+0x90)
    if (flag_ & cFlag_DisableFree) return 0;

    ConditionalScopedLock lock(*this);
    MemBlock* const block = blockFromUserPtr(ptr);
    // A null header triggers a debug walk of both lists that has no effect in this
    // build, then returns 0.
    if (!block) return 0;
    if (block->magic != heapId()) return 0;  // not ours, or already freed

    ListNode_erase(nodeOf(use_list_, block));
    --use_list_.count;
    block->size += block->pad;  // fold the alignment gap back into the block
    block->pad = 0;

    MemBlock* const merged = recycleBlock_(block);

    const uint32_t abs_alignment =
        static_cast<uint32_t>(alignment < 0 ? -alignment : alignment);
    if (abs_alignment <= 8) return merged->size;
    const uintptr_t data = addr(merged->data());
    return data + merged->size - alignUp(data, abs_alignment);
}

// -----------------------------------------------------------------------------------
// 0x0044636c — resizeBack: shrink a block in place (growing is refused). Returns the
// user pointer on success. adjust() only checks it for non-null.
// -----------------------------------------------------------------------------------
void* ExpHeap::resizeBack(void* ptr, size_t new_size) {
    if (!isInclude(ptr)) return nullptr;
    ConditionalScopedLock lock(*this);

    MemBlock* const block = blockFromUserPtr(ptr);
    const size_t wanted = alignUp(new_size, 8);
    if (block->size < wanted) return nullptr;
    const size_t diff = block->size - wanted;
    if (diff == 0 || diff <= 0x1F) return block->data();

    block->size = wanted;
    MemBlock* const rest = initBlock(block->end(), diff - kHeader);
    recycleBlock_(rest);
    return block->data();
}

// -----------------------------------------------------------------------------------
// 0x00444eb0 — adjust: give the trailing (forward) or leading (reverse) free space back
// to the parent. Returns the resulting size.
// -----------------------------------------------------------------------------------
size_t ExpHeap::adjust() {
    Heap* const parent = parent_;
    if (!parent) return size_;

    ConditionalScopedLock lock(*this);
    ConditionalScopedLock parent_lock(*parent);

    if (direction_ == cHeapDirection_Forward) {
        auto* const tail = static_cast<MemBlock*>(findAdjustableTailBlock_());  // +0xf0
        if (!tail) return size_;
        const size_t new_size = addr(tail) - addr(start_);
        ListNode* const node = nodeOf(free_list_, tail);
        ListNode_erase(node);
        --free_list_.count;
        if (parent->resizeBack(start_, new_size)) {  // +0x50
            size_ = new_size;
            return new_size;
        }
        ListNode_insertBefore(sentinel(free_list_), node);
        ++free_list_.count;
        return size_;
    }

    auto* const head = static_cast<MemBlock*>(findAdjustableHeadBlock_());  // +0xf8
    if (!head) return size_;
    const size_t new_size = size_ - (head->size + head->pad) - kHeader;
    ListNode* const node = nodeOf(free_list_, head);
    ListNode_erase(node);
    --free_list_.count;
    if (void* const new_start = parent->resizeFront(start_, new_size)) {  // +0x48
        size_ = new_size;
        std::atomic_thread_fence(std::memory_order_seq_cst);  // `dmb ish`
        start_ = new_start;
        return new_size;
    }
    ListNode_insertAfter(sentinel(free_list_), node);
    ++free_list_.count;
    return size_;
}

// Insertion sort of the used list by block address (inline in 0x446958 / 0x446a54).
void ExpHeap::sortUseListByAddress_() {
    ListNode* const s = sentinel(use_list_);
    ListNode* cur = s->next->next;  // sub_42fa3c(list, 1): node at index 1
    while (cur != s) {
        ListNode* const next = cur->next;
        ListNode* p = cur->prev;
        if (addr(p) < addr(cur)) {
            cur = next;
            continue;
        }
        do {
            p = p->prev;
        } while (p != s && addr(p) >= addr(cur));
        ListNode_erase(cur);
        ListNode_insertAfter(p, cur);
        cur = next;
    }
}

// 0x00446958 — the last free block, if it lies above every used block.
void* ExpHeap::findAdjustableTailBlock_() {
    if (use_list_.count >= 2) sortUseListByAddress_();
    MemBlock* const last_free = free_list_.count > 0 && free_list_.start_end.prev
                                    ? blockOf(free_list_, free_list_.start_end.prev)
                                    : nullptr;
    MemBlock* const last_used = use_list_.count > 0 && use_list_.start_end.prev
                                    ? blockOf(use_list_, use_list_.start_end.prev)
                                    : nullptr;
    return addr(last_free) > addr(last_used) ? last_free : nullptr;
}

// 0x00446a54 — the first free block, if it lies below every used block.
void* ExpHeap::findAdjustableHeadBlock_() {
    if (use_list_.count >= 2) sortUseListByAddress_();
    MemBlock* const first_free = free_list_.count > 0 && free_list_.start_end.next
                                     ? blockOf(free_list_, free_list_.start_end.next)
                                     : nullptr;
    MemBlock* const first_used = use_list_.count > 0 && use_list_.start_end.next
                                     ? blockOf(use_list_, use_list_.start_end.next)
                                     : nullptr;
    return (addr(first_free) < addr(first_used) || !first_used) ? first_free : nullptr;
}

// -----------------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------------

// 0x00444d5c — the managed range excludes the 0x100 header at whichever end it sits.
bool ExpHeap::isInclude(const void* ptr) const {
    const uintptr_t start = addr(start_);
    if (direction_ == cHeapDirection_Forward)
        return start + sizeof(ExpHeap) <= addr(ptr) && addr(ptr) < start + size_;
    return start <= addr(ptr) && addr(ptr) < start + size_ - sizeof(ExpHeap);
}

const void* ExpHeap::getStartAddress() const { return start_; }                       // 0x4466d8
const void* ExpHeap::getEndAddress() const { return static_cast<uint8_t*>(start_) + size_; }  // 0x4466e0
size_t ExpHeap::getSize() const { return size_; }                                    // 0x4466ec
bool ExpHeap::isEmpty() const { return use_list_.count == 0; }                       // 0x447108
bool ExpHeap::isFreeable() const { return true; }                                    // 0x447118
bool ExpHeap::isResizable() const { return true; }                                   // 0x447120
bool ExpHeap::isAdjustable() const { return true; }                                  // 0x447128

// 0x004466f4
size_t ExpHeap::getFreeSize() const {
    ConditionalScopedLock lock(*this);
    size_t total = 0;
    const ListNode* const s = sentinel(free_list_);
    for (const ListNode* n = s->next; n != s; n = n->next) total += blockOf(free_list_, n)->size;
    return total;
}

// 0x0044679c — the largest single allocation possible at `alignment`. Blocks under
// 8 bytes are ignored; ties keep the earlier block.
size_t ExpHeap::getMaxAllocatableSize(int32_t alignment) const {
    const uint32_t a = static_cast<uint32_t>(alignment < 0 ? -alignment : alignment);
    if ((a & (a - 1)) != 0) return 0;

    ConditionalScopedLock lock(*this);
    const ListNode* const s = sentinel(free_list_);
    MemBlock* best = nullptr;

    if (a <= 8) {
        for (const ListNode* n = s->next; n != s; n = n->next) {
            MemBlock* const b = blockOf(free_list_, n);
            if (b->size >= 8 && (!best || best->size < b->size)) best = b;
        }
        return best ? best->size : 0;
    }

    auto padding_of = [&](MemBlock* b) {
        const uintptr_t data = addr(b->data());
        return alignUp(data, a) - data;
    };
    for (const ListNode* n = s->next; n != s; n = n->next) {
        MemBlock* const b = blockOf(free_list_, n);
        if (b->size < 8 || b->size < padding_of(b) + 8) continue;
        if (!best || best->size < b->size) best = b;
    }
    return best ? best->size - padding_of(best) : 0;
}

}  // namespace sead
