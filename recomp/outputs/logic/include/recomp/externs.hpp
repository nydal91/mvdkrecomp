// Game-side functions referenced by recompiled code but not recompiled yet.
// Naming: `fn_<address>` for unidentified routines; a descriptive name where the body was
// inspected (address kept in the comment so it can be cross-checked).
#pragma once

#include <cstddef>
#include <cstdint>

#include "recomp/sead.hpp"

namespace recomp {

// ---- Memory bootstrap (nninitStartup) --------------------------------------------
// sub_d8db94: records the malloc-arena size (w0) and an extra reserve (w1) in globals,
// queries nn::os::QueryMemoryInfo and returns the size to pass to SetMemoryHeapSize.
size_t ConfigureMemoryLayout(uint32_t malloc_arena_size, uint32_t extra_reserve, void* a2,
                             void* a3, void* a4, uint64_t a5, void* a6);
// sub_3e5870: constructs + initializes the global nn::mem::StandardAllocator behind malloc.
void InitializeMallocArena(uintptr_t address, size_t size);
// sub_437590: nn::os::SetMemoryAllocatorForThreadLocal(aligned_alloc wrapper, free).
void InstallThreadLocalAllocator();
// sub_d8dd54: memoryHeapSize - (mallocArenaSize + extraReserve).
size_t ComputeRootHeapSize();

// ---- Static registration groups called first thing in GameMain ---------------------
// Each is a flat list of 2..40+ calls into per-module registration routines.
void fn_738974();
void fn_7a6ff0();
void fn_92d900();
void fn_f113fc();
void fn_738994();
void fn_f798c8();
void fn_e980c8();

// ---- Data-driven configuration ------------------------------------------------------
// sub_f11380: ExpHeap "HeapSizeDB" under `parent`, loads "System/HeapSize/".
void LoadHeapSizeDB(sead::Heap* parent);
// sub_f11214: RegionLanguageMgr ("System/RegionLangMask.txt",
// "Local/System/RegionLangConfig.txt") and "AddressTable".
void InitializeRegionAndAddressTable(sead::Heap* parent);

// ---- Singletons (sead SEAD_SINGLETON_DISPOSER pattern) ------------------------------
struct AccountMgr;                                  // 0x40 bytes, sInstance @ 0x0176b840
AccountMgr* AccountMgr_createInstance(sead::Heap*); // sub_ef5724 (also nn::account::Initialize)
void AccountMgr_openPreselectedUser(AccountMgr*);   // sub_ef57c8 (TryOpenPreselectedUser, flags |= 3)

struct GameSystem;                                  // 0x28 bytes, sInstance @ 0x017556f0
GameSystem* GameSystem_createInstance(sead::Heap*); // sub_36ded8
extern GameSystem* GameSystem_sInstance;            // 0x017556f0
extern GameSystem* g_GameSystem;                    // 0x0176a1d0 (mirror written by GameMain)

// ---- Root task ------------------------------------------------------------------------
class RootTask;                                                                // 0x550 bytes
RootTask* ConstructRootTask(void* storage, const sead::TaskConstructArg& arg); // sub_3a9008

}  // namespace recomp
