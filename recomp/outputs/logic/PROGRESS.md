# Recompilation progress — exefs/main

Addresses are offsets in the decompressed image (`inputs/main.bin`, from `scripts/nso2bin.py`).

| Address | Function | File |
|---|---|---|
| 0x003a8d90 | nninitStartup | game/main.cpp |
| 0x003a8df0 | nnMain | game/main.cpp |
| 0x003a89c0 | GameMain | game/main.cpp |
| 0x003a8d40 | CreateRootTask | game/main.cpp |
| 0x00e6d5fc | InitializeGameFramework | game/main.cpp |
| 0x00ef5830 | EnableAppletNotifications | game/main.cpp |
| 0x00e6d620 | AppFramework::AppFramework | game/main.cpp |
| 0x00ef5854 | AppletState::AppletState | game/main.cpp |
| 0x004881fc | FramePacerConfig ctor | game/app_framework.hpp |
| 0x00488220 | FramePacer ctor | game/app_framework.hpp |
| 0x0048826c | FramePacer::configure | game/app_framework.hpp |
| 0x0042f3d0 | operator new(size, Heap*, align) | sead/heap.cpp |
| 0x0042f95c | ListNode_insertBefore | sead/heap.cpp |
| 0x00444690 | IDisposer::IDisposer | sead/heap.cpp |
| 0x004448c8 | ExpHeap::create (+ inlined ExpHeap ctor) | sead/heap.cpp |
| 0x00448224 | Heap::Heap | sead/heap.cpp |
| 0x00449428 | HeapMgr::getCurrentHeap | sead/heap.cpp |
| 0x0045b710 | CriticalSection ctor | sead/heap.cpp |
| 0x0045b7fc / 0x0045b80c | CriticalSection lock/unlock | include/recomp/sead.hpp |
| 0x00439324 | Framework::Framework | sead/framework.cpp |
| 0x004398d0 | GameFramework::GameFramework | sead/framework.cpp |
| 0x00439920 | default GameFramework callback | sead/framework.cpp |
| 0x004375d8 | GameFrameworkNx::GameFrameworkNx | sead/framework.cpp |
| 0x004375c4 | GameFrameworkNx::initialize | include/recomp/sead.hpp |
| 0x004395bc | Framework::run | sead/framework.cpp |
| 0x00439e70 | Framework::runImpl_ (empty) | sead/framework.cpp |
| 0x004394e0 / 0x004394ec | InitializeArg / RunArg ctors | include/recomp/sead.hpp |
| 0x0043b7c8 | TaskBase::CreateArg ctor | sead/framework.cpp |
| 0x0043bde8 | TaskMgr::InitializeArg ctor | sead/framework.cpp |
| 0x0043c170 | TaskMgr::initialize | sead/framework.cpp |

| 0x0042f944 / 0x0042f974 | ListNode_insertAfter / ListNode_erase | sead/heap.cpp |
| 0x00444730 | IDisposer::~IDisposer | sead/heap.cpp |
| 0x0045b78c | CriticalSection::~CriticalSection | sead/heap.cpp |
| 0x00444b74 | ExpHeap::tryCreate | sead/heap.cpp |
| 0x004487e4 | Heap::pushBackChild_ | sead/heap.cpp |
| 0x0044862c / 0x004486a4 | Heap::appendDisposer_ / removeDisposer_ | sead/heap.cpp |
| 0x00448e30 / 0x00448f24 | HeapMgr::initialize (size / arena) | sead/heap_mgr.cpp |
| 0x004495e0 | Arena::initialize | sead/heap_mgr.cpp |
| 0x00445084 | ExpHeap::tryAlloc | sead/exp_heap.cpp |
| 0x00445248 / 0x00445374 / 0x00445514 / 0x004455e0 | allocFromHead_/HeadAligned_/Tail_/TailAligned_ | sead/exp_heap.cpp |
| 0x004456e4 / 0x004457f8 / 0x004459a8 / 0x00445ac8 | findFreeBlock{Head,HeadAligned,Tail,TailAligned}_ | sead/exp_heap.cpp |
| 0x00445cdc / 0x00445cec | free / freeAndGetAllocatableSize | sead/exp_heap.cpp |
| 0x00445f14 | recycleBlock_ (free-list merge) | sead/exp_heap.cpp |
| 0x0044636c | resizeBack | sead/exp_heap.cpp |
| 0x00444eb0 | adjust | sead/exp_heap.cpp |
| 0x00446958 / 0x00446a54 | findAdjustable{Tail,Head}Block_ | sead/exp_heap.cpp |
| 0x0044679c / 0x004466f4 | getMaxAllocatableSize / getFreeSize | sead/exp_heap.cpp |
| 0x00444d5c, 0x004466d8..0x004466ec, 0x00447108..0x00447128 | isInclude, getters, is* | sead/exp_heap.cpp |

Remaining ExpHeap/Heap methods trap with RECOMP_TODO (sead/unimplemented.cpp).

## Testing on device
`tests/run_on_device.sh` builds `heap_test` with the NDK, pushes it over adb and runs it.
It boots HeapMgr, replays GameMain's four subsystem heaps (create → alloc → adjust),
checks alignment/tail/failure paths, and runs 9 × 20k-op randomized stress passes (every
alloc/merge mode) with full structural verification. Last run on the NX729J (Android 15):
2.86M checks, 0 failures.

## Next up (called from what's done, highest leverage first)
- 0x00438120 GameFrameworkNx::initRun_, 0x00439bd4 Framework::initialize (root heaps, HeapMgr)
- 0x0043be08 TaskMgr ctor (0x298 bytes): spawns the prepare thread and the root task
- ExpHeap resizeFront 0x004461b8, tryRealloc 0x00446474, freeAll 0x00444db8, destroy 0x00444ccc; HeapMgr::findContainHeap 0x00448fe4
- ThreadMgr (0x0045be48 / 0x0045c484), ResourceMgr, FileDeviceMgr: rest of Framework::initialize 0x00439bd4
- 0x00437768 GameFrameworkNx::initializeGraphicsSystem (nv:: graphics → needs Vulkan shim)
- 0x003a9008 RootTask ctor (0x550 bytes): the game's own top-level task
