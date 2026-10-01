#pragma once
#include <openxr/openxr.h>
#include "starfox/simulation/rumble_sequencer.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <optional>
namespace starfox::vr {
struct InputApi {
    PFN_xrCreateActionSet create_set{xrCreateActionSet};
    PFN_xrDestroyActionSet destroy_set{xrDestroyActionSet};
    PFN_xrCreateAction create_action{xrCreateAction};
    PFN_xrStringToPath path{xrStringToPath};
    PFN_xrSuggestInteractionProfileBindings suggest{xrSuggestInteractionProfileBindings};
    PFN_xrAttachSessionActionSets attach{xrAttachSessionActionSets};
    PFN_xrSyncActions sync{xrSyncActions};
    PFN_xrGetActionStateBoolean boolean{xrGetActionStateBoolean};
    PFN_xrGetActionStateVector2f vector{xrGetActionStateVector2f};
    PFN_xrGetCurrentInteractionProfile current_profile{xrGetCurrentInteractionProfile};
    PFN_xrApplyHapticFeedback apply_haptic{xrApplyHapticFeedback};
    PFN_xrStopHapticFeedback stop_haptic{xrStopHapticFeedback};
    PFN_xrCreateActionSpace create_space{xrCreateActionSpace};
    PFN_xrDestroySpace destroy_space{xrDestroySpace};
    PFN_xrLocateSpace locate{xrLocateSpace};
};
struct VrControls {
    XrVector2f steer{};
    bool fire{},bomb{},boost{},brake{},menu{},menu_pressed{},roll_left{},roll_right{};
    bool select{},select_pressed{};
    bool stick_left{},stick_right{},reset_pressed{};
    std::uint32_t active_actions{};
};

enum class VrControlAction : std::uint32_t {
    steer = 1U << 0U,
    fire = 1U << 1U,
    bomb = 1U << 2U,
    boost = 1U << 3U,
    brake = 1U << 4U,
    menu = 1U << 5U,
    roll_left = 1U << 6U,
    roll_right = 1U << 7U,
    select = 1U << 8U,
    stick_left = 1U << 9U,
    stick_right = 1U << 10U,
};
constexpr std::uint32_t vr_control_bit(VrControlAction action) noexcept {
    return static_cast<std::uint32_t>(action);
}
[[nodiscard]] VrControls select_vr_control_sources(
    const VrControls& openxr, const VrControls& desktop) noexcept;

// SDL's level-state sampler also runs while the XR session is unfocused; the
// caller discards those controls but retains these edge states for resume.
class DesktopControlEdges {
public:
    [[nodiscard]] VrControls sample(VrControls controls) noexcept {
        controls.menu_pressed=controls.menu&&!menu_;
        controls.select_pressed=controls.select&&!select_;
        const bool reset=controls.roll_left&&controls.roll_right
            &&controls.stick_left&&controls.stick_right;
        controls.reset_pressed=reset&&!reset_;
        menu_=controls.menu;select_=controls.select;reset_=reset;
        return controls;
    }
    void reset() noexcept {menu_=select_=reset_=false;}
private:
    bool menu_{},select_{},reset_{};
};

class OpenXrInput {
public:
    explicit OpenXrInput(InputApi api={}):api_(api) {}
    ~OpenXrInput();
    OpenXrInput(const OpenXrInput&)=delete;
    OpenXrInput& operator=(const OpenXrInput&)=delete;
    // Attach before session begin. OpenXR permits attachment only once per
    // session; reinitialization requires a fresh caller-owned session.
    bool initialize(XrInstance,XrSession,bool frame_interaction_enabled=false);
    bool poll(bool focused);
    [[nodiscard]] bool focused() const noexcept {return focused_;}
    // The authored dual-band sample maps to XR's single actuator channel by
    // max(low, high), with an unspecified frequency and the native 40 ms pulse.
    bool apply_haptics(const starfox::simulation::RumbleEffect&) noexcept;
    void stop_haptics() noexcept;
    [[nodiscard]] bool haptics_available() const noexcept;
    std::array<std::optional<XrPosef>,2> aim_poses(XrSpace base,XrTime time) const noexcept;
    void close() noexcept;
    const VrControls& controls() const noexcept {return controls_;}
    const std::string& status() const noexcept {return status_;}
private:
    InputApi api_;
    XrSession session_{};XrActionSet set_{};std::array<XrAction,18> actions_{};
    std::array<XrSpace,2> aim_spaces_{};
    std::array<XrPath,2> hands_{};
    std::array<XrPath,4> haptic_profiles_{};
    std::array<bool,2> haptic_bound_hands_{},haptic_started_hands_{};
    std::uint32_t haptic_profile_count_{};
    VrControls controls_{};bool menu_armed_{},select_armed_{};
    bool reset_armed_{},focused_{};
    std::string status_;
};
}
