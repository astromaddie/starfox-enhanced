#pragma once
#include <openxr/openxr.h>
#include "starfox/simulation/rumble_sequencer.hpp"
#include "starfox/vr/system_layer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
    // `select` / `select_pressed` are the L View *short press*: a one-poll tap
    // reported on release (see SystemLayer). `view_down` is the raw level.
    bool select{},select_pressed{},view_down{};
    bool stick_left{},stick_right{};
    // System layer one-shots (standard section 1). The recentre events are
    // applied by the application at the next stereo frame boundary;
    // recentre_height_pressed also recalibrates standing height.
    bool recentre_pressed{},recentre_height_pressed{},menu_chord_pressed{};
    std::uint32_t active_actions{};
    // Physical menu confirmation is independent of gameplay fire on Frame.
    bool menu_confirm{},menu_confirm_active{};
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

inline void desktop_face_buttons(VrControls& controls,bool steam_frame,
    bool south,bool east,bool west,bool north) noexcept {
    controls.fire=steam_frame?west:south;controls.bomb=east;
    controls.boost=steam_frame?north:west;controls.brake=steam_frame?south:north;
    controls.menu_confirm=south;controls.menu_confirm_active=true;
}

// SDL's level-state sampler also runs while the XR session is unfocused; the
// caller discards those controls but retains these edge states for resume.
// The View short/hold and Menu+View chord timing matches the OpenXR path.
class DesktopControlEdges {
public:
    [[nodiscard]] VrControls sample(VrControls controls,
        double now=SystemLayer::steady_seconds()) noexcept {
        controls.menu_pressed=controls.menu&&!menu_;
        menu_=controls.menu;
        const bool view=controls.select;
        const auto events=system_.update(view,controls.menu,now);
        controls.view_down=view;
        controls.select=controls.select_pressed=events.view_tap;
        controls.recentre_pressed=events.recentre||events.recentre_height;
        controls.recentre_height_pressed=events.recentre_height;
        controls.menu_chord_pressed=events.open_menu;
        return controls;
    }
    void reset() noexcept {menu_=false;system_.reset();}
private:
    bool menu_{};
    SystemLayer system_;
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
    // `now` is monotonic seconds for the View hold timing; tests inject it.
    bool poll(bool focused,double now=SystemLayer::steady_seconds());
    [[nodiscard]] bool focused() const noexcept {return focused_;}
    // The authored dual-band sample maps to XR's single actuator channel by
    // max(low, high), with an unspecified frequency and the native 40 ms pulse.
    bool apply_haptics(const starfox::simulation::RumbleEffect&) noexcept;
    void stop_haptics() noexcept;
    [[nodiscard]] bool haptics_available() const noexcept;
    // User strength 0..1 (default 0.6), applied to every OpenXR haptic output.
    void set_haptics_strength(float strength) noexcept {
        haptics_strength_=std::isfinite(strength)?std::clamp(strength,0.F,1.F):0.6F;
    }
    [[nodiscard]] float haptics_strength() const noexcept {return haptics_strength_;}
    std::array<std::optional<XrPosef>,2> aim_poses(XrSpace base,XrTime time) const noexcept;
    void close() noexcept;
    const VrControls& controls() const noexcept {return controls_;}
    const std::string& status() const noexcept {return status_;}
private:
    InputApi api_;
    XrPath frame_profile_{};
    XrSession session_{};XrActionSet set_{};std::array<XrAction,18> actions_{};
    std::array<XrSpace,2> aim_spaces_{};
    std::array<XrPath,2> hands_{};
    std::array<XrPath,4> haptic_profiles_{};
    std::array<bool,2> haptic_bound_hands_{},haptic_started_hands_{};
    std::uint32_t haptic_profile_count_{};
    // System haptic: 0.6 amplitude, 80 ms, both hands, scaled by the strength
    // setting. Not tracked as started rumble, so gameplay stop calls leave it.
    void pulse_system() noexcept;
    VrControls controls_{};bool menu_armed_{};
    SystemLayer system_;
    float haptics_strength_{0.6F};
    bool focused_{};
    std::string status_;
};
}
