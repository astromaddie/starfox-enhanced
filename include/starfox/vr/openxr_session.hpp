#pragma once
#include <openxr/openxr.h>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace starfox::vr {
// Injectable dispatch permits lifecycle tests without pretending a headset
// or graphics driver is present. Normal callers use the OpenXR loader.
struct SessionApi {
    PFN_xrCreateSession create_session{xrCreateSession};
    PFN_xrDestroySession destroy_session{xrDestroySession};
    PFN_xrCreateReferenceSpace create_space{xrCreateReferenceSpace};
    PFN_xrDestroySpace destroy_space{xrDestroySpace};
    PFN_xrEnumerateEnvironmentBlendModes blend_modes{xrEnumerateEnvironmentBlendModes};
    PFN_xrPollEvent poll_event{xrPollEvent};
    PFN_xrBeginSession begin_session{xrBeginSession};
    PFN_xrEndSession end_session{xrEndSession};
    PFN_xrWaitFrame wait_frame{xrWaitFrame};
    PFN_xrBeginFrame begin_frame{xrBeginFrame};
    PFN_xrEndFrame end_frame{xrEndFrame};
    PFN_xrLocateViews locate_views{xrLocateViews};
    PFN_xrRequestExitSession request_exit{xrRequestExitSession};
};
struct StereoFrame {
    XrTime display_time{};
    XrDuration display_period{};
    bool should_render{};
    bool tracking_origin_changed{};
    bool forced{}; // SFX_VR_FORCE_RENDER drew what the runtime did not ask for.
    std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
};
class OpenXrSession {
public:
    explicit OpenXrSession(SessionApi api={}):api_(api) {}
    ~OpenXrSession();
    OpenXrSession(const OpenXrSession&)=delete;
    OpenXrSession& operator=(const OpenXrSession&)=delete;
    // Caller owns instance/device, performs graphics-requirements negotiation,
    // and supplies that runtime-compatible graphics binding. Both outlive us.
    bool initialize(XrInstance,XrSystemId,const void* graphics_binding);
    void close() noexcept;
    bool poll_events(); // transitions READY/STOPPING, records exit/loss
    // Ask the runtime to end the session cleanly (xrRequestExitSession). The
    // caller keeps pumping poll_events()/frames: the runtime answers with
    // STOPPING (we xrEndSession) then EXITING, which sets exit_requested().
    // A session that never began (or already stopped) has nothing to stop and
    // is marked exiting immediately. Returns false if the runtime refused.
    bool request_exit();
    bool exit_in_progress() const noexcept {return exit_asked_;}
    // SFX_VR_FORCE_RENDER: render every frame, with synthetic_stereo_views()
    // whenever the views are not valid; such frames are marked forced. A
    // runtime that refuses their layers gets an empty frame instead.
    void set_force_render(bool on) noexcept {force_render_=on;}
    unsigned forced_rejections() const noexcept {return forced_rejections_;}
    // Primary stereo views whose visibility mask changed since the last call,
    // one bit per view (XrEventDataVisibilityMaskChangedKHR).
    unsigned take_visibility_mask_changes() noexcept {auto views=mask_changes_;mask_changes_=0;return views;}
    std::optional<StereoFrame> begin_frame();
    // Submit only completed/released swapchain layers. Invisible/untracked
    // frames always submit zero layers, while retaining frame pacing.
    bool end_frame(std::span<const XrCompositionLayerBaseHeader* const> layers={});
    XrSession handle() const noexcept {return session_;}
    XrSpace space() const noexcept {return space_;}
    bool running() const noexcept {return running_;}
    bool exit_requested() const noexcept {return exit_;}
    XrSessionState state() const noexcept {return state_;}
    const std::string& status() const noexcept {return status_;}
private:
    SessionApi api_;
    XrInstance instance_{XR_NULL_HANDLE};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace space_{XR_NULL_HANDLE};
    XrSessionState state_{XR_SESSION_STATE_UNKNOWN};
    XrEnvironmentBlendMode blend_{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    bool running_{},exit_{},frame_active_{},renderable_{},exit_asked_{};
    bool force_render_{},forced_{};
    unsigned forced_rejections_{},mask_changes_{};
    XrTime frame_time_{};
    std::vector<XrTime> origin_changes_;
    std::string status_{"OpenXR session not initialized"};
};
}
