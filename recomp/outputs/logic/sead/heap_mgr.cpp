// sead::HeapMgr statics and bootstrap, reconstructed from exefs/main.
//
//   0x00448e30  HeapMgr::initialize(size_t)
//   0x00448f24  HeapMgr::initialize(Arena*)
//   0x004495e0  Arena::initialize
//
// Static layout in .bss (each object is at its binary address there):
//   0x017584F0 sInstance (0x18)   0x01758508 sInstancePtr   0x01758510 sArena
//   0x01758518 sDefaultArena (0x18)   0x01758530 sRootHeaps
//   0x01758590 sHeapTreeLockCS (0x40)  0x017585D0 heap ID counter  0x017585E0 sTicksPer10us

#include "recomp/sead.hpp"

namespace sead {

// Root-heap capacity: stock sead uses 4. The binary's static initializer for sRootHeaps
// hasn't been recompiled yet, so this value is unverified.
static constexpr int32_t kRootHeapMax = 4;
static Heap* sRootHeapsBuffer[kRootHeapMax];

HeapMgr HeapMgr::sInstance{};
HeapMgr* HeapMgr::sInstancePtr = nullptr;
Arena* HeapMgr::sArena = nullptr;
Arena HeapMgr::sDefaultArena{};
HeapPtrArray HeapMgr::sRootHeaps{0, kRootHeapMax, sRootHeapsBuffer};
CriticalSection HeapMgr::sHeapTreeLockCS{nullptr};
int64_t HeapMgr::sTicksPer10us = 0;

// 0x004495e0 — the OS hands out memory in 2 MiB units. The requested size, not the
// rounded one, is recorded.
void Arena::initialize(size_t requested) {
    constexpr size_t kGranule = 0x200000;
    nn::os::AllocateMemoryBlock(reinterpret_cast<uintptr_t*>(&start),
                                (requested + kGranule - 1) & ~(kGranule - 1));
    size = requested;
}

namespace {

// Shared tail of both initialize() overloads.
void createRootHeap(Arena* arena) {
    sead::HeapMgr::sInstance.alloc_failed_cb_ = nullptr;
    // smulh by 0x29F16B11C6D1E109, asr #14 = signed division by 100000.
    HeapMgr::sTicksPer10us = gTickFrequency / 100000;

    const SafeString name{"RootHeap"};
    ExpHeap* const root = ExpHeap::tryCreate(arena->start, arena->size, name, false, nullptr);

    HeapPtrArray& roots = HeapMgr::sRootHeaps;
    if (roots.size < roots.capacity) roots.ptrs[roots.size++] = root;  // silently dropped if full

    HeapMgr::sInstancePtr = &HeapMgr::sInstance;
}

}  // namespace

// 0x00448e30
void HeapMgr::initialize(size_t size) {
    sHeapTreeLockCS.lock();
    sArena = &sDefaultArena;
    sDefaultArena.initialize(size);
    createRootHeap(sArena);
    sHeapTreeLockCS.unlock();
}

// 0x00448f24
void HeapMgr::initialize(Arena* arena) {
    sArena = arena;
    createRootHeap(arena);
}

}  // namespace sead
