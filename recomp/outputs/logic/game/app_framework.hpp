// The game's framework class: sead::GameFrameworkNx subclass, 0x2A0 bytes,
// vtable 0x01708608 (+0x10).
#pragma once

#include <cstdint>
#include <limits>

#include "recomp/sead.hpp"

namespace recomp {

// Frame pacing parameters, 0x14 bytes. Defaults from sub_4881fc.
struct FramePacerConfig {
    uint32_t unk_00 = 1;        // +0x00
    uint32_t unk_04 = 2;        // +0x04
    float unk_08 = 0.05f;       // +0x08 (0x3D4CCCCD)
    uint32_t unk_0c = 2;        // +0x0C
    uint32_t unk_10 = 6;        // +0x10
};
static_assert(sizeof(FramePacerConfig) == 0x14);

// Member at +0x248, 0x48 bytes. Name inferred from its config; ctor sub_488220.
class FramePacer {
public:
    // sub_48826c: copy the config, then seed both +0x14 and +0x18 from config.unk_00.
    void configure(const FramePacerConfig& config) {
        config_ = config;
        unk_14 = config.unk_00;
        unk_18 = config.unk_00;
    }

    FramePacerConfig config_{};   // +0x00
    uint32_t unk_14 = 0;          // +0x14
    uint32_t unk_18 = 0;          // +0x18
    uint32_t unk_1c = 0;          // +0x1C  (0x14..0x23 cleared by two `stur xzr`)
    uint32_t unk_20 = 0;          // +0x20
    bool unk_24 = false;          // +0x24
    // Embedded polymorphic list owner (vtable 0x01708758): {vptr, ListNode, s32}.
    struct Listeners {
        Listeners() { list.prev = list.next = &list; }
        virtual ~Listeners();
        sead::ListNode list;      // +0x08
        int32_t count = 0;        // +0x18
    } listeners_;                 // +0x28
};
static_assert(sizeof(FramePacer) == 0x48);

// Member at +0x290, 0x10 bytes; ctor sub_ef5854 snapshots applet state and publishes
// `this` through the global at 0x0176B848.
struct AppletState {
    AppletState();
    nn::oe::OperationMode operation_mode;       // +0x00
    nn::oe::PerformanceMode performance_mode;   // +0x04
    nn::oe::FocusState focus_state;             // +0x08
    bool unk_0c = false;                        // +0x0C
};
static_assert(sizeof(AppletState) == 0x10);
extern AppletState* gAppletState;  // 0x0176B848

class AppFramework : public sead::GameFrameworkNx {
public:
    // sead's 0x50-byte CreateArg plus two game-specific fields (stack slot x29-0x68,
    // 0x58 bytes).
    struct CreateArg : sead::GameFrameworkNx::CreateArg {
        // Only applied if not NaN (`fcmp s0,s0; b.vs`): NaN means "keep the default".
        float frame_pacer_unk_08 = std::numeric_limits<float>::quiet_NaN();  // +0x50
        uint32_t frame_pacer_unk_10 = 6;                                     // +0x54
    };
    static_assert(sizeof(CreateArg) == 0x58);

    explicit AppFramework(const CreateArg& arg);  // sub_e6d620

    void initRun_(sead::Heap* heap) override;     // sub_e6d838 -> tail call sub_438120

    FramePacer frame_pacer_;   // +0x248
    AppletState applet_state_; // +0x290
};
static_assert(offsetof(AppFramework, frame_pacer_) == 0x248);
static_assert(offsetof(AppFramework, applet_state_) == 0x290);
static_assert(sizeof(AppFramework) == 0x2A0);

}  // namespace recomp
