// Reconstructed from exefs/main (Switch NSO, module "Grande-S")
//
//   nninitStartup              0x003a8d90  (exported)
//   nnMain                     0x003a8df0  (exported, 4 bytes: `b 0x3a89c0`)
//   GameMain                   0x003a89c0  (static, the real entry point)
//   CreateRootTask             0x003a8d40  (root-task factory passed to Framework::run)
//   InitializeGameFramework    0x00e6d5fc
//   EnableAppletNotifications  0x00ef5830
//   AppFramework::AppFramework 0x00e6d620
//   AppletState::AppletState   0x00ef5854
//
// Assembly: recompilation/inputs/assembly/
//
// Startup:
//   1. nninitStartup: carve the malloc arena out of the OS heap.
//   2. GameMain: run static registration, initialize sead (root heaps), then create one
//      ExpHeap per subsystem (Framework, GraphicSystem, AccountMgr, GameSystem), shrinking
//      each with adjust() once its owner is built, and finally hand control to
//      Framework::run, which doesn't return while the game is running.

#include <cmath>
#include <cstdint>
#include <new>

#include "app_framework.hpp"
#include "recomp/externs.hpp"
#include "recomp/sead.hpp"

namespace recomp {

// -----------------------------------------------------------------------------------
// 0x00ef5854
// -----------------------------------------------------------------------------------
AppletState::AppletState()
    : operation_mode(nn::oe::GetOperationMode()),
      performance_mode(nn::oe::GetPerformanceMode()),
      focus_state(nn::oe::GetCurrentFocusState()),
      unk_0c(false) {
    gAppletState = this;
}

// -----------------------------------------------------------------------------------
// 0x00e6d620
// -----------------------------------------------------------------------------------
AppFramework::AppFramework(const CreateArg& arg)
    : sead::GameFrameworkNx(arg),  // sub_4375d8; copies only the first 0x50 bytes
      frame_pacer_(),              // sub_488220
      applet_state_() {            // sub_ef5854
    // The local config starts from FramePacerConfig defaults (sub_4881fc). The binary
    // then re-stores {1, 2} at +0x00, which are already the defaults.
    FramePacerConfig config;
    config.unk_00 = 1;
    config.unk_04 = 2;
    if (!std::isnan(arg.frame_pacer_unk_08)) config.unk_08 = arg.frame_pacer_unk_08;
    config.unk_10 = arg.frame_pacer_unk_10;
    frame_pacer_.configure(config);  // sub_48826c
}

// -----------------------------------------------------------------------------------
// 0x00ef5830 — opt in to the applet notifications the main loop polls with
// nn::oe::TryPopNotificationMessage.
// -----------------------------------------------------------------------------------
static void EnableAppletNotifications() {
    nn::oe::SetResumeNotificationEnabled(true);
    nn::oe::SetOperationModeChangedNotificationEnabled(true);
    nn::oe::SetPerformanceModeChangedNotificationEnabled(true);
}

// -----------------------------------------------------------------------------------
// 0x00e6d5fc
// -----------------------------------------------------------------------------------
static void InitializeGameFramework(const sead::Framework::InitializeArg& arg) {
    EnableAppletNotifications();
    sead::GameFrameworkNx::initialize(arg);  // sub_4375c4 (inlined into the tail call)
}

// -----------------------------------------------------------------------------------
// Root task
// -----------------------------------------------------------------------------------

// 0x003a8d40 — the TaskClassID factory.
//   ldr x8,[x0]              arg.heap_array
//   ldrsw x9,[x8,#0x24]      heap_array->primary_index
//   ldr x1,[x8,x9,lsl #3]    heap_array->heaps[primary_index]
//   new(heap, 8) RootTask(arg)          (no null check, same as the binary)
static sead::TaskBase* CreateRootTask(const sead::TaskConstructArg& arg) {
    void* const storage = ::operator new(0x550, arg.heap_array->getPrimaryHeap(), 8);
    return reinterpret_cast<sead::TaskBase*>(ConstructRootTask(storage, arg));
}

// Function-local static at 0x01756AB0 (guard 0x01756AA8, vtable 0x0167E9F0, methods at
// 0x3a8df4.. in this TU). The ctor clears the bool; GameMain sets it on every call.
class RootTaskParam final : public sead::TaskParameter {
public:
    bool enabled = false;  // +0x08
};

namespace {

// Each subsystem gets its own expandable heap, carved from root heap 0.
//   x0=0 (all remaining)  w3=8 (alignment)  w4=1 (forward)  w5=0 (no lock)
sead::ExpHeap* CreateSubsystemHeap(const char* name) {
    const sead::SafeString safe_name{name};
    return sead::ExpHeap::create(0, safe_name, sead::HeapMgr::getRootHeap(0), 8,
                                 sead::Heap::cHeapDirection_Forward, false);
}

}  // namespace

// -----------------------------------------------------------------------------------
// 0x003a89c0 — GameMain (nnMain tail-calls this)
// -----------------------------------------------------------------------------------
[[gnu::noinline]] static void GameMain() {
    // 0x3a89d8..0x3a89f0: static registration groups, in binary order.
    fn_738974();
    fn_7a6ff0();
    fn_92d900();
    fn_f113fc();
    fn_738994();
    fn_f798c8();
    fn_e980c8();

    // 0x3a89f4..0x3a8a08: the default heap size from the ctor (48 MiB) is replaced by
    // what's left after the malloc arena and reserve.
    sead::Framework::InitializeArg init_arg;
    init_arg.heap_size = ComputeRootHeapSize();
    InitializeGameFramework(init_arg);

    // 0x3a8a24..0x3a8a3c: data-driven configuration, both on root heap 0.
    LoadHeapSizeDB(sead::HeapMgr::getRootHeap(0));
    InitializeRegionAndAddressTable(sead::HeapMgr::getRootHeap(0));

    // ---- Framework (0x3a8a40..0x3a8b08) -------------------------------------------
    const AppFramework::CreateArg fw_arg{};
    sead::ExpHeap* const framework_heap = CreateSubsystemHeap("Framework");
    auto* const framework =
        new (::operator new(sizeof(AppFramework), framework_heap, 8)) AppFramework(fw_arg);
    framework_heap->adjust();

    // ---- Graphics (0x3a8b0c..0x3a8b70) ---------------------------------------------
    // 0x4434000044A00000 -> {1280.0f, 720.0f}: the virtual canvas size. The display
    // framebuffer (CreateArg) is 1920x1080.
    sead::ExpHeap* const graphics_heap = CreateSubsystemHeap("GraphicSystem");
    const sead::Vector2f virtual_size{1280.0f, 720.0f};
    framework->initializeGraphicsSystem(graphics_heap, graphics_heap, virtual_size);
    graphics_heap->adjust();

    // ---- Account (0x3a8b74..0x3a8bc0) ----------------------------------------------
    sead::ExpHeap* const account_heap = CreateSubsystemHeap("AccountMgr");
    AccountMgr_openPreselectedUser(AccountMgr_createInstance(account_heap));
    account_heap->adjust();

    // ---- GameSystem (0x3a8bc4..0x3a8c28) -------------------------------------------
    // createInstance's return value is ignored; the binary reloads sInstance and mirrors
    // it into a second global.
    sead::ExpHeap* const game_system_heap = CreateSubsystemHeap("GameSystem");
    GameSystem_createInstance(game_system_heap);
    g_GameSystem = GameSystem_sInstance;
    game_system_heap->adjust();

    // ---- Main loop (0x3a8c2c..0x3a8ca0) --------------------------------------------
    const sead::TaskClassID root_task_id{sead::TaskClassID::cFactory, &CreateRootTask};
    sead::TaskBase::CreateArg root_task_arg(root_task_id);

    sead::Framework::RunArg run_arg;        // reuses the TaskClassID stack slot
    run_arg.prepare_stack_size = 0x40000;   // 256 KiB (sead default is 64 KiB)

    static RootTaskParam s_root_task_param;  // __cxa_guard_acquire/release at 0x3a8d00
    s_root_task_param.enabled = true;
    root_task_arg.param = &s_root_task_param;

    // sead::Framework::run (vtable +0x20); doesn't return until the application exits.
    framework->run(sead::HeapMgr::getRootHeap(0), root_task_arg, run_arg);
}

}  // namespace recomp

// -----------------------------------------------------------------------------------
// 0x003a8d90 — nninitStartup: runs before C++ static init and before nnMain.
// -----------------------------------------------------------------------------------
extern "C" void nninitStartup() {
    constexpr uint32_t kMallocArenaSize = 0x2000000;  // 32 MiB

    // x5 = 0x1'5000'0000 (5.25 GiB); unclear what it caps, passed through unchanged.
    const size_t heap_size = recomp::ConfigureMemoryLayout(
        kMallocArenaSize, 0, nullptr, nullptr, nullptr, 0x150000000ull, nullptr);
    nn::os::SetMemoryHeapSize(heap_size);

    uintptr_t arena = 0;
    nn::os::AllocateMemoryBlock(&arena, kMallocArenaSize);  // result isn't checked
    recomp::InitializeMallocArena(arena, kMallocArenaSize);
    recomp::InstallThreadLocalAllocator();
}

// -----------------------------------------------------------------------------------
// 0x003a8df0 — nnMain: a single `b GameMain`.
// -----------------------------------------------------------------------------------
extern "C" void nnMain() {
    recomp::GameMain();
}
