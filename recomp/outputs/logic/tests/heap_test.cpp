// On-device test for the recompiled sead heap layer.
// Boots HeapMgr the way the game does, replays GameMain's subsystem-heap sequence, then
// stress-tests ExpHeap and checks its structural invariants after every operation.
//
// Build: CMake target `heap_test`. Run: adb push + adb shell (see tests/run_on_device.sh).

#include <pthread.h>

#include <algorithm>
#include <cinttypes>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "recomp/sead.hpp"

namespace sead {
// ThreadMgr isn't recompiled yet. getCurrentHeap() reads its TLS slot, so give it one
// that is always empty ("not a sead thread"), which matches the main thread at boot.
static ThreadMgr gTestThreadMgr{};
ThreadMgr* ThreadMgr::sInstance = &gTestThreadMgr;
}  // namespace sead

using sead::ExpHeap;
using sead::Heap;
using sead::HeapMgr;
using MemBlock = ExpHeap::MemBlock;

static int gFailures = 0;
static int gChecks = 0;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

static uintptr_t A(const void* p) { return reinterpret_cast<uintptr_t>(p); }

static MemBlock* blockOf(const sead::OffsetList& l, const sead::ListNode* n) {
    return reinterpret_cast<MemBlock*>(A(n) - l.offset);
}

// Full structural check:
//  * both lists' links are consistent and their counts match
//  * free list is in address order, free blocks have magic 0xFFFF, used blocks the heap ID
//  * every block lies inside the managed range and no two overlap
//  * walking memory header to header covers the managed range exactly
static bool verifyHeap(const ExpHeap* h, const char* when) {
    const int before = gFailures;
    struct Range { uintptr_t begin, end; bool free; };
    std::vector<Range> ranges;

    auto walk = [&](const sead::OffsetList& l, bool is_free) {
        int n = 0;
        uintptr_t last = 0;
        const sead::ListNode* s = &l.start_end;
        for (const sead::ListNode* node = s->next; node != s; node = node->next) {
            CHECK(node->next->prev == node, "%s: broken link", when);
            MemBlock* b = blockOf(l, node);
            if (is_free) {
                CHECK(b->magic == 0xFFFF, "%s: free block magic %04x", when, b->magic);
                CHECK(A(b) > last, "%s: free list out of order", when);
                last = A(b);
            } else {
                CHECK(b->magic == h->heapId(), "%s: used block magic %04x", when, b->magic);
            }
            ranges.push_back({A(b), A(b->end()), is_free});
            if (++n > 1'000'000) break;
        }
        CHECK(n == l.count, "%s: %s count %d but walked %d", when, is_free ? "free" : "use",
              l.count, n);
    };
    walk(h->free_list_, true);
    walk(h->use_list_, false);

    const uintptr_t lo = h->direction_ == Heap::cHeapDirection_Forward ? A(h->start_) + 0x100
                                                                       : A(h->start_);
    const uintptr_t hi = lo + h->size_ - 0x100;
    std::sort(ranges.begin(), ranges.end(),
              [](const Range& a, const Range& b) { return a.begin < b.begin; });
    uintptr_t cursor = lo;
    for (const Range& r : ranges) {
        CHECK(r.begin == cursor, "%s: gap/overlap at %#" PRIxPTR " (expected %#" PRIxPTR ")",
              when, r.begin, cursor);
        cursor = r.end;
    }
    CHECK(cursor == hi, "%s: blocks end at %#" PRIxPTR ", heap ends at %#" PRIxPTR, when,
          cursor, hi);
    return gFailures == before;
}

// ---------------------------------------------------------------------------------------
static ExpHeap* root() { return static_cast<ExpHeap*>(HeapMgr::getRootHeap(0)); }

static ExpHeap* subsystemHeap(const char* name) {
    const sead::SafeString s{name};
    return ExpHeap::create(0, s, HeapMgr::getRootHeap(0), 8, Heap::cHeapDirection_Forward,
                           false);
}

static void testBoot() {
    std::printf("[boot] HeapMgr::initialize(64 MiB)\n");
    HeapMgr::initialize(64u << 20);
    CHECK(HeapMgr::sInstancePtr == &HeapMgr::sInstance, "sInstancePtr not published");
    CHECK(HeapMgr::sRootHeaps.size == 1, "root heap count %d", HeapMgr::sRootHeaps.size);
    ExpHeap* r = root();
    CHECK(r != nullptr, "no root heap");
    if (!r) return;
    CHECK(std::strcmp(r->name_, "RootHeap") == 0, "root name %s", r->name_);
    CHECK(r->getSize() == (64u << 20), "root size %zx", r->getSize());
    CHECK(r->getFreeSize() == (64u << 20) - 0x120, "root free %zx", r->getFreeSize());
    CHECK(HeapMgr::sTicksPer10us == 192, "ticks per 10us %" PRId64, HeapMgr::sTicksPer10us);
    verifyHeap(r, "boot");
}

// Same order and parameters as GameMain (0x3a8aac..0x3a8c28): each heap takes all
// remaining space, gets an object, then adjust() gives the rest back to the root.
static void testGameMainHeapSequence() {
    std::printf("[gamemain] Framework / GraphicSystem / AccountMgr / GameSystem heaps\n");
    struct Step { const char* name; size_t object; };
    const Step steps[] = {{"Framework", 0x2A0}, {"GraphicSystem", 0x10000},
                          {"AccountMgr", 0x40}, {"GameSystem", 0x28}};
    const size_t root_free_before = root()->getFreeSize();
    size_t expected_used = 0;
    for (const Step& st : steps) {
        ExpHeap* h = subsystemHeap(st.name);
        CHECK(h != nullptr, "%s: create failed", st.name);
        if (!h) return;
        CHECK(h->parent_ == root(), "%s: wrong parent", st.name);
        CHECK(root()->getFreeSize() < 0x100, "%s: root not drained (free %zx)", st.name,
              root()->getFreeSize());
        void* obj = h->tryAlloc(st.object, 8);
        CHECK(obj && A(obj) % 8 == 0, "%s: object alloc", st.name);
        std::memset(obj, 0xAB, st.object);
        const size_t adjusted = h->adjust();
        // header 0x100 + block header 0x20 + object (8-aligned)
        const size_t want = 0x100 + 0x20 + ((st.object + 7) & ~size_t{7});
        CHECK(adjusted == want, "%s: adjust() -> %zx, want %zx", st.name, adjusted, want);
        CHECK(h->getFreeSize() == 0, "%s: free after adjust %zx", st.name, h->getFreeSize());
        verifyHeap(h, st.name);
        verifyHeap(root(), "root after adjust");
        expected_used += 0x20 + want;  // child heap occupies one root block
        std::printf("  %-14s heap=%p size=%#zx obj=%p\n", st.name, static_cast<void*>(h),
                    adjusted, obj);
    }
    CHECK(root()->getFreeSize() == root_free_before - expected_used,
          "root free %zx, expected %zx", root()->getFreeSize(), root_free_before - expected_used);
}

static void testAlignmentAndTail() {
    std::printf("[align] head/tail, small/large alignment, 64 KiB+ padding path\n");
    ExpHeap* h = subsystemHeap("AlignTest");
    if (!h) { CHECK(false, "create"); return; }
    const int32_t aligns[] = {4, 8, 16, 64, 256, 4096, 0x20000};
    std::vector<void*> ptrs;
    for (int32_t a : aligns) {
        for (int sign : {1, -1}) {
            void* p = h->tryAlloc(100, a * sign);
            CHECK(p && A(p) % static_cast<uintptr_t>(a < 8 ? 8 : a) == 0,
                  "alloc(100, %d) -> %p", a * sign, p);
            if (p) { std::memset(p, 0x5A, 100); ptrs.push_back(p); }
            verifyHeap(h, "after aligned alloc");
        }
    }
    // Head allocations sit below tail allocations.
    void* head = h->tryAlloc(32, 8);
    void* tail = h->tryAlloc(32, -8);
    CHECK(A(head) < A(tail), "head %p not below tail %p", head, tail);
    ptrs.push_back(head);
    ptrs.push_back(tail);
    for (void* p : ptrs) h->free(p);
    verifyHeap(h, "after freeing all");
    CHECK(h->free_list_.count == 1, "free list didn't coalesce (%d blocks)", h->free_list_.count);
    CHECK(h->getFreeSize() == h->getSize() - 0x120, "free %zx", h->getFreeSize());
    CHECK(h->getMaxAllocatableSize(8) == h->getFreeSize(), "max alloc %zx",
          h->getMaxAllocatableSize(8));
    CHECK(h->adjust() == 0x100, "empty heap should adjust down to its header");
}

struct CountingCallback : sead::IAllocFailedCallback {
    int calls = 0;
    sead::AllocFailedCallbackArg last{};
    void invoke(const sead::AllocFailedCallbackArg* a) override { ++calls; last = *a; }
};

static void testFailures() {
    std::printf("[fail] bad alignment, oversize, double free, foreign pointer\n");
    ExpHeap* h = subsystemHeap("FailTest");
    if (!h) { CHECK(false, "create"); return; }
    CountingCallback cb;
    HeapMgr::sInstance.alloc_failed_cb_ = &cb;
    CHECK(h->tryAlloc(16, 24) == nullptr, "non-power-of-two alignment accepted");
    CHECK(cb.calls == 1 && cb.last.request_alignment == 24, "callback on bad alignment");
    CHECK(h->tryAlloc(h->getSize(), 8) == nullptr, "oversize accepted");
    CHECK(cb.calls == 2 && cb.last.heap == h && cb.last.alloc_size == h->getSize(),
          "callback on oversize");
    HeapMgr::sInstance.alloc_failed_cb_ = nullptr;

    void* p = h->tryAlloc(64, 8);
    CHECK(h->freeAndGetAllocatableSize(p, 8) > 0, "first free");
    CHECK(h->freeAndGetAllocatableSize(p, 8) == 0, "double free not rejected");
    int on_stack = 0;
    CHECK(h->freeAndGetAllocatableSize(&on_stack, 8) == 0, "foreign pointer not rejected");
    verifyHeap(h, "after failure cases");
    h->adjust();
}

// Random alloc/free/resize against a shadow list of live allocations; every allocation
// is filled with a per-allocation byte pattern that is checked before it is freed.
static void testStress(ExpHeap::AllocMode mode, ExpHeap::FreeMergeMode merge, unsigned seed) {
    std::printf("[stress] alloc_mode=%d merge_mode=%d seed=%u\n", static_cast<int>(mode),
                static_cast<int>(merge), seed);
    ExpHeap* h = subsystemHeap("Stress");
    if (!h) { CHECK(false, "create"); return; }
    h->alloc_mode_ = mode;
    h->free_merge_mode_ = merge;

    struct Live { uint8_t* p; size_t n; uint8_t tag; };
    std::vector<Live> live;
    std::mt19937 rng(seed);
    const int32_t aligns[] = {8, 8, 8, 16, 32, 128, 4096, -8, -16, -256};
    int allocs = 0, frees = 0, shrinks = 0;
    for (int op = 0; op < 20000; ++op) {
        const unsigned r = rng() % 100;
        if (r < 55 || live.empty()) {
            const size_t n = 1 + rng() % ((rng() % 10 == 0) ? 65536 : 512);
            const int32_t a = aligns[rng() % std::size(aligns)];
            auto* p = static_cast<uint8_t*>(h->tryAlloc(n, a));
            if (!p) continue;
            const int32_t ua = a < 0 ? -a : a;
            CHECK(A(p) % static_cast<uintptr_t>(ua) == 0, "misaligned %p for %d", p, a);
            const uint8_t tag = static_cast<uint8_t>(rng());
            std::memset(p, tag, n);
            live.push_back({p, n, tag});
            ++allocs;
        } else if (r < 92) {
            const size_t i = rng() % live.size();
            const Live l = live[i];
            bool intact = true;
            for (size_t k = 0; k < l.n; ++k) intact &= l.p[k] == l.tag;
            CHECK(intact, "allocation %p (%zu bytes) corrupted", l.p, l.n);
            h->free(l.p);
            live[i] = live.back();
            live.pop_back();
            ++frees;
        } else {
            Live& l = live[rng() % live.size()];
            const size_t n = l.n / 2 + 1;
            if (h->resizeBack(l.p, n)) { l.n = n; ++shrinks; }
        }
        if (op % 500 == 0 && !verifyHeap(h, "stress")) {
            std::printf("  stopping at op %d\n", op);
            return;
        }
    }
    for (const Live& l : live) h->free(l.p);
    verifyHeap(h, "stress end");
    CHECK(h->use_list_.count == 0 && h->free_list_.count == 1,
          "not fully coalesced: used %d free %d", h->use_list_.count, h->free_list_.count);
    std::printf("  allocs=%d frees=%d shrinks=%d\n", allocs, frees, shrinks);
    h->adjust();
    verifyHeap(root(), "root after stress");
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("grande-recomp heap_test (sead::ExpHeap from exefs/main)\n");
    pthread_key_t key;
    pthread_key_create(&key, nullptr);  // empty slot = "not running on a sead::Thread"
    sead::gTestThreadMgr.thread_ptr_tls_.value = static_cast<uint32_t>(key);
    testBoot();
    if (gFailures == 0) {
        testGameMainHeapSequence();
        testAlignmentAndTail();
        testFailures();
        unsigned seed = 1;
        for (auto mode : {ExpHeap::AllocMode::FirstFit, ExpHeap::AllocMode::BestFit,
                          ExpHeap::AllocMode::WorstFit})
            for (auto merge : {ExpHeap::FreeMergeMode::Auto, ExpHeap::FreeMergeMode::ListWalk,
                               ExpHeap::FreeMergeMode::AutoIfFew})
                testStress(mode, merge, seed++);
    }
    std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
    std::printf(gFailures ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return gFailures ? 1 : 0;
}
