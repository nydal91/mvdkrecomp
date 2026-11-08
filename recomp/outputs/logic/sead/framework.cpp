// sead framework core, reconstructed from exefs/main.
//
//   0x00439324  sead::Framework::Framework
//   0x004398d0  sead::GameFramework::GameFramework
//   0x00439920  (default GameFramework +0x80 callback)
//   0x004375d8  sead::GameFrameworkNx::GameFrameworkNx
//   0x004395bc  sead::Framework::run                       (vtable +0x20)
//   0x00439e70  sead::Framework::runImpl_                  (vtable +0x58, empty)
//   0x004394e0  sead::Framework::InitializeArg ctor        (inline in header)
//   0x004394ec  sead::Framework::RunArg ctor               (inline in header)
//   0x0043b7c8  sead::TaskBase::CreateArg::CreateArg
//   0x0043bde8  sead::TaskMgr::InitializeArg::InitializeArg
//   0x0043c170  sead::TaskMgr::initialize
//
// Assembly: recompilation/inputs/assembly/sub_<addr>.asm

#include <new>

#include "recomp/sead.hpp"

namespace sead {

// Singleton toggled by the default GameFramework callback (0x017587B8).
class Unk17587B8;
extern Unk17587B8* gUnk17587B8;
void Unk17587B8_onTrue(Unk17587B8* self);   // sub_45ee80
void Unk17587B8_onFalse(Unk17587B8* self);  // sub_45ee98

// -----------------------------------------------------------------------------------
// 0x00439324
// -----------------------------------------------------------------------------------
Framework::Framework()
    : reserve_reset_(false),
      pad_09_{},
      unk_10(0),
      reset_event_(),
      task_mgr_(nullptr),
      method_tree_mgr_(nullptr),
      method_tree_mgr_heap_(nullptr) {}

// -----------------------------------------------------------------------------------
// 0x00439920 — default value of GameFramework::unk_80_callback
// -----------------------------------------------------------------------------------
static void DefaultUnk80Callback(bool flag) {
    if (flag)
        Unk17587B8_onTrue(gUnk17587B8);
    else
        Unk17587B8_onFalse(gUnk17587B8);
}

// -----------------------------------------------------------------------------------
// 0x004398d0
// -----------------------------------------------------------------------------------
GameFramework::GameFramework()
    : Framework(),
      unk_50(0),
      pad_54_(0),
      unk_58(cEmptyString),
      unk_60(cEmptyString),
      unk_68(cEmptyString),
      unk_70(0),
      unk_78(0),
      unk_80_callback(&DefaultUnk80Callback) {}

// -----------------------------------------------------------------------------------
// 0x004375d8
// -----------------------------------------------------------------------------------
GameFrameworkNx::GameFrameworkNx(const CreateArg& arg)
    : GameFramework(),
      arg_(arg),  // 0x437600..0x437628: five ldp/stp pairs = 0x50 bytes
      unk_0d8(0) {
    unk_0e0_tick = nn::os::GetSystemTick();
    unk_0e8 = 0;
    // unk_0f0 default-constructed: vptr, {1,1}, 0, {1,1}, 0, 0
    unk_120_tick = nn::os::GetSystemTick();
    frame_start_tick_ = nn::os::GetSystemTick();
    frame_budget_ticks_ = 0;
    frame_margin_ticks_ = 0;
    for (auto& b : unk_140) b = 0;
    unk_188 = 0;
    unk_190 = 0;
    for (auto& b : unk_198) b = 0;
    unk_1d8 = 0;
    unk_1e0 = cEmptyString;
    unk_1e8 = 0;
    unk_220 = 0x01000001;
    unk_224 = 0xFF00;
    // unk_228: vptr only.

    // NX disables the base class's default callback.
    unk_80_callback = nullptr;

    // 0x4376c4: per-frame time budget in system ticks. 0.985 of a frame for work and
    // 0.006 of a frame of margin, at 60 / vsync_interval frames per second. The math is
    // done in single precision, exactly as the binary does it (ucvtf/scvtf/fdiv/fmul,
    // fcvtzs).
    if (arg.vsync_interval != 0) {
        const float frequency = static_cast<float>(gTickFrequency);
        const float fps = 60.0f / static_cast<float>(arg.vsync_interval);
        frame_budget_ticks_ = static_cast<int64_t>((0.985f / fps) * frequency);
        frame_margin_ticks_ = static_cast<int64_t>((0.006f / fps) * frequency);
    }
}

// -----------------------------------------------------------------------------------
// 0x0043b7c8 — copies the class ID and zero-fills the rest, except for the {-1, 1} pair
// at +0xA8 (`mov x8,#0x1ffffffff; stp x8,xzr,[x0,#0xa8]`).
// -----------------------------------------------------------------------------------
TaskBase::CreateArg::CreateArg(const TaskClassID& id)
    : class_id(id), unk_10{}, param(nullptr), unk_88{}, unk_a8(-1), unk_ac(1), unk_b0(0) {}

// -----------------------------------------------------------------------------------
// 0x0043bde8 (+0x0C is left unwritten in the binary)
// -----------------------------------------------------------------------------------
TaskMgr::InitializeArg::InitializeArg(const TaskBase::CreateArg& root_task_create_arg)
    : unk_00(0x20),
      prepare_stack_size(0x10000),  // 64 KiB unless RunArg overrides it
      prepare_priority(-1),
      root_task_arg(&root_task_create_arg),
      heap(nullptr),
      parent_framework(nullptr) {}

// -----------------------------------------------------------------------------------
// 0x0043c170 — new(arg.heap, 8) TaskMgr(arg). No null check after the allocation,
// same as the binary.
// -----------------------------------------------------------------------------------
TaskMgr* TaskMgr::initialize(const InitializeArg& arg) {
    void* const storage = ::operator new(sizeof(TaskMgr), arg.heap, 8);
    return new (storage) TaskMgr(arg);
}

// -----------------------------------------------------------------------------------
// 0x004395bc — Framework::run
// -----------------------------------------------------------------------------------
void Framework::run(Heap* heap, const TaskBase::CreateArg& root_task_arg,
                    const RunArg& run_arg) {
    initRun_(heap);  // vtable +0x50

    // 0x4395f0..0x43964c: MethodTreeMgr gets its own heap, shrunk once it's built.
    {
        const SafeString name{"sead::MethodTreeMgr"};
        Heap* const method_tree_heap =
            ExpHeap::create(0, name, heap, 8, Heap::cHeapDirection_Forward, false);
        method_tree_mgr_ = createMethodTreeMgr_(method_tree_heap);  // vtable +0x68
        method_tree_heap->adjust();
        method_tree_mgr_heap_ = method_tree_heap;
    }

    // 0x439644..0x43967c
    TaskMgr::InitializeArg mgr_arg(root_task_arg);
    if (run_arg.prepare_stack_size != 0) mgr_arg.prepare_stack_size = run_arg.prepare_stack_size;
    if (run_arg.prepare_priority != -1) mgr_arg.prepare_priority = run_arg.prepare_priority;
    mgr_arg.heap = heap;
    mgr_arg.parent_framework = this;
    task_mgr_ = TaskMgr::initialize(mgr_arg);

    postTaskMgrInitialize_();  // vtable +0x60
    runImpl_(heap);            // vtable +0x58, the main loop on NX
}

// -----------------------------------------------------------------------------------
// 0x00439e70 — base implementation is a bare `ret`.
// -----------------------------------------------------------------------------------
void Framework::runImpl_(Heap* /*heap*/) {}

}  // namespace sead
