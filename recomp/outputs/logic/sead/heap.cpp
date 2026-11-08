// sead heap core, reconstructed from exefs/main.
//
//   0x0042f3d0  operator new(size_t, sead::Heap*, s32)
//   0x0042f95c  sead::ListNode_insertBefore
//   0x00444690  sead::IDisposer::IDisposer
//   0x004448c0  sead::ExpHeap::create (thunk) -> 0x004448c8 (body)
//   0x00448224  sead::Heap::Heap
//   0x00449428  sead::HeapMgr::getCurrentHeap
//   0x0045b710  sead::CriticalSection::CriticalSection
//
// Assembly: recompilation/inputs/assembly/sub_<addr>.asm

#include <atomic>
#include <bit>
#include <cstdlib>
#include <new>

#include "recomp/sead.hpp"

namespace sead {

// 0x017585D0. The low 16 bits are handed out as heap IDs; 0xFFFF is skipped.
static std::atomic<uint32_t> sHeapIdCounter{0};

// -----------------------------------------------------------------------------------
// 0x0042f95c
// -----------------------------------------------------------------------------------
void ListNode_insertBefore(ListNode* self, ListNode* node) {
    ListNode* const old_prev = self->prev;
    self->prev = node;
    node->prev = old_prev;
    node->next = self;
    if (old_prev) old_prev->next = node;
}

// 0x0042f944
void ListNode_insertAfter(ListNode* self, ListNode* node) {
    ListNode* const old_next = self->next;
    self->next = node;
    node->prev = self;
    node->next = old_next;
    if (old_next) old_next->prev = node;
}

// 0x0042f974
void ListNode_erase(ListNode* node) {
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    node->prev = nullptr;
    node->next = nullptr;
}

// -----------------------------------------------------------------------------------
// 0x00444690 — IDisposer(Heap* disposer_heap, HeapNullOption option)
// With no heap given, options 0 and 1 attach to whichever heap contains `this`, option 3
// to the current heap, and option 2 doesn't attach at all.
// -----------------------------------------------------------------------------------
IDisposer::IDisposer(Heap* disposer_heap, HeapNullOption option)
    : disposer_heap_(disposer_heap), list_node_{} {
    if (!disposer_heap) {
        HeapMgr* const mgr = HeapMgr::sInstancePtr;
        switch (option) {
            case HeapNullOption::AlwaysUseSpecifiedHeap:
            case HeapNullOption::UseSpecifiedOrContainHeap:
                if (!mgr) return;
                disposer_heap = mgr->findContainHeap(this);
                break;
            case HeapNullOption::UseSpecifiedOrCurrentHeap:
                if (!mgr) return;
                disposer_heap = mgr->getCurrentHeap();
                break;
            default:  // DoNotAppendDisposerIfNoHeapSpecified, and anything > 3
                return;
        }
        disposer_heap_ = disposer_heap;
        if (!disposer_heap) return;
    }
    disposer_heap->appendDisposer_(this);  // tail call to 0x0044862c
}

// -----------------------------------------------------------------------------------
// 0x00444730 — ~IDisposer. disposer_heap_ == 1 marks "already detached" so a second
// destruction is a no-op; a null heap is marked the same way without detaching.
// -----------------------------------------------------------------------------------
IDisposer::~IDisposer() {
    Heap* const heap = disposer_heap_;
    Heap* const kDetached = reinterpret_cast<Heap*>(uintptr_t{1});
    if (heap == kDetached) return;
    if (heap) heap->removeDisposer_(this);
    disposer_heap_ = kDetached;
}

// -----------------------------------------------------------------------------------
// 0x0045b78c — ~CriticalSection
// -----------------------------------------------------------------------------------
CriticalSection::~CriticalSection() {
    nn::os::FinalizeMutex(&mutex_);
}

// -----------------------------------------------------------------------------------
// 0x0045b710 — the critical section registers itself as a disposer of the given heap,
// then initializes a recursive mutex with lock level 0.
// -----------------------------------------------------------------------------------
CriticalSection::CriticalSection(Heap* disposer_heap)
    : IDisposer(disposer_heap, HeapNullOption::UseSpecifiedOrContainHeap), mutex_{} {
    nn::os::InitializeMutex(&mutex_, /*recursive=*/true, /*lock_level=*/0);
}

// -----------------------------------------------------------------------------------
// 0x00448224 — Heap::Heap
// -----------------------------------------------------------------------------------
Heap::Heap(const SafeString& name, Heap* parent, void* start, size_t size,
           HeapDirection direction, bool enable_lock, Heap* fallback_disposer_heap)
    : IDisposer(parent ? parent : fallback_disposer_heap,
                HeapNullOption::UseSpecifiedOrContainHeap),
      name_(name.str),
      start_(start),
      size_(size),
      parent_(parent),
      list_node_{},
      direction_(direction),
      pad_84_(0),
      cs_(parent ? parent : fallback_disposer_heap) {
    child_list_.init();
    disposer_list_.init();

    // 0x4482c4: ldxr/stxr loop. Retries if the store fails *or* the ID handed out is 0xFFFF.
    uint32_t old_counter = 0;
    do {
        old_counter = sHeapIdCounter.fetch_add(1, std::memory_order_relaxed);
    } while ((old_counter & 0xFFFF) == 0xFFFF);
    flag_ = cFlag_Bit2 | (old_counter << 16);  // `bfi w9, w8, #16, #16`

    // 0x4482f0: set/clear bit 0 atomically (ldxr/orr|and/stxr).
    std::atomic_ref<uint32_t> flag(flag_);
    if (enable_lock)
        flag.fetch_or(cFlag_EnableLock, std::memory_order_relaxed);
    else
        flag.fetch_and(~cFlag_EnableLock, std::memory_order_relaxed);

    // Set the intrusive-list offsets under the heap's own lock (only if locking is enabled).
    ConditionalScopedLock lock(*this);
    child_list_.offset = static_cast<int32_t>(offsetof(Heap, list_node_));          // 0x58
    disposer_list_.offset = static_cast<int32_t>(offsetof(IDisposer, list_node_));  // 0x10
}

// -----------------------------------------------------------------------------------
// ExpHeap ctor — fully inlined into ExpHeap::create in the binary (0x4449b0..0x4449f0 and
// the mirrored reverse path 0x444a40..0x444a80).
// -----------------------------------------------------------------------------------
ExpHeap::ExpHeap(const SafeString& name, Heap* parent, void* start, size_t size,
                 HeapDirection direction, bool enable_lock, Heap* fallback_disposer_heap)
    : Heap(name, parent, start, size, direction, enable_lock, fallback_disposer_heap),
      alloc_mode_(AllocMode::FirstFit),             // `strh wzr, [x, #0xcc]` clears both
      free_merge_mode_(FreeMergeMode::Auto),        // mode bytes
      pad_ce_(0) {
    free_list_.init();
    use_list_.init();
    ConditionalScopedLock lock(*this);
    free_list_.offset = static_cast<int32_t>(offsetof(MemBlock, list_node));  // 0
    use_list_.offset = static_cast<int32_t>(offsetof(MemBlock, list_node));   // 0
}

// 0x444a8c..0x444b58: the whole arena becomes one free block. For a forward heap it sits
// right after the ExpHeap header; for a reverse heap the header is at the end and the
// block starts at start_.
void ExpHeap::createFirstFreeBlock_() {
    ConditionalScopedLock lock(*this);

    auto* const block = static_cast<MemBlock*>(
        direction_ == cHeapDirection_Forward ? static_cast<void*>(this + 1) : start_);
    block->list_node = {};
    block->magic = 0xFFFF;
    block->pad = 0;
    block->size = size_ - sizeof(ExpHeap) - sizeof(MemBlock);  // size_ - 0x120

    auto* const node = reinterpret_cast<ListNode*>(reinterpret_cast<uint8_t*>(block) +
                                                   free_list_.offset);
    ListNode_insertBefore(&free_list_.start_end, node);
    ++free_list_.count;
}

// -----------------------------------------------------------------------------------
// 0x004448c8 — ExpHeap::create
// -----------------------------------------------------------------------------------
ExpHeap* ExpHeap::create(size_t size, const SafeString& name, Heap* parent, int32_t alignment,
                         HeapDirection direction, bool enable_lock) {
    // Smallest usable heap: the 0x100 header plus one 0x20 MemBlock (`cmp x24,#0x121; b.lo`).
    constexpr size_t kMinHeapSize = sizeof(ExpHeap) + sizeof(MemBlock) + 1;
    constexpr size_t kMinAlignment = 8;

    if (!parent) {
        parent = HeapMgr::sInstancePtr->getCurrentHeap();
        if (!parent) return nullptr;
    }

    // size == 0 means "everything the parent can still give". An explicit size is rounded
    // up to 8; the parent's maximum is rounded down.
    if (size == 0)
        size = parent->getMaxAllocatableSize(alignment) & ~(kMinAlignment - 1);
    else
        size = (size + kMinAlignment - 1) & ~(kMinAlignment - 1);
    if (size < kMinHeapSize) return nullptr;

    // `cneg w8,w20,mi; sub w9,w8,#1; tst w8,w9` (note: alignment 0 passes).
    const uint32_t abs_alignment =
        static_cast<uint32_t>(alignment < 0 ? -alignment : alignment);
    if ((abs_alignment & (abs_alignment - 1)) != 0) return nullptr;

    // The sign of the alignment tells the parent which end to allocate from.
    void* const start = parent->tryAlloc(size, direction * alignment);
    if (!start) return nullptr;

    // A reverse parent flips the meaning of direction for the child.
    if (parent->getDirection() == cHeapDirection_Reverse)
        direction = static_cast<HeapDirection>(-direction);

    void* const header = direction == cHeapDirection_Forward
                             ? start
                             : static_cast<uint8_t*>(start) + size - sizeof(ExpHeap);
    auto* const heap = new (header) ExpHeap(name, parent, start, size, direction, enable_lock);
    heap->createFirstFreeBlock_();

    parent->pushBackChild_(heap);  // vtable +0xd8
    return heap;
}

// -----------------------------------------------------------------------------------
// 0x00444b74 — ExpHeap::tryCreate: a parentless heap on raw memory (used for RootHeap).
// Same inline ctor + first-block setup as create(), always forward, no pushBackChild_.
// -----------------------------------------------------------------------------------
ExpHeap* ExpHeap::tryCreate(void* address, size_t size, const SafeString& name,
                            bool enable_lock, Heap* fallback_disposer_heap) {
    size &= ~size_t{7};
    if (size < sizeof(ExpHeap) + sizeof(MemBlock) + 1) return nullptr;
    auto* const heap = new (address) ExpHeap(name, nullptr, address, size,
                                             cHeapDirection_Forward, enable_lock,
                                             fallback_disposer_heap);
    heap->createFirstFreeBlock_();
    return heap;
}

// -----------------------------------------------------------------------------------
// 0x004487e4 — Heap::pushBackChild_: under the global heap-tree lock, then this heap's.
// -----------------------------------------------------------------------------------
void Heap::pushBackChild_(Heap* child) {
    HeapMgr::sHeapTreeLockCS.lock();
    {
        ConditionalScopedLock lock(*this);
        ListNode_insertBefore(&child_list_.start_end,
                              reinterpret_cast<ListNode*>(reinterpret_cast<uint8_t*>(child) +
                                                          child_list_.offset));
        ++child_list_.count;
    }
    HeapMgr::sHeapTreeLockCS.unlock();
}

// -----------------------------------------------------------------------------------
// 0x004486a4 — Heap::removeDisposer_
// -----------------------------------------------------------------------------------
void Heap::removeDisposer_(IDisposer* disposer) {
    ConditionalScopedLock lock(*this);
    ListNode_erase(reinterpret_cast<ListNode*>(reinterpret_cast<uint8_t*>(disposer) +
                                               disposer_list_.offset));
    --disposer_list_.count;
}

// -----------------------------------------------------------------------------------
// 0x0044862c — Heap::appendDisposer_
// -----------------------------------------------------------------------------------
void Heap::appendDisposer_(IDisposer* disposer) {
    ConditionalScopedLock lock(*this);
    ListNode_insertBefore(&disposer_list_.start_end,
                          reinterpret_cast<ListNode*>(reinterpret_cast<uint8_t*>(disposer) +
                                                      disposer_list_.offset));
    ++disposer_list_.count;
}

// -----------------------------------------------------------------------------------
// 0x00449428 — HeapMgr::getCurrentHeap
// -----------------------------------------------------------------------------------
Heap* HeapMgr::getCurrentHeap() const {
    if (Thread* const thread = ThreadMgr::sInstance->getCurrentThread())
        return thread->current_heap_;
    return no_thread_current_heap_;
}

}  // namespace sead

// -----------------------------------------------------------------------------------
// 0x0042f3d0 — operator new(size, heap, alignment)
// Before HeapMgr exists (static-init time) this falls through to the malloc arena.
// Unlike stock sead, a failed allocation returns nullptr instead of aborting.
// -----------------------------------------------------------------------------------
void* operator new(size_t size, sead::Heap* heap, int32_t alignment) noexcept {
    sead::HeapMgr* const mgr = sead::HeapMgr::sInstancePtr;
    if (!mgr) return std::aligned_alloc(static_cast<size_t>(alignment), size);
    if (!heap) {
        heap = mgr->getCurrentHeap();
        if (!heap) return nullptr;
    }
    return heap->tryAlloc(size, alignment);  // tail call via vtable +0x30
}
