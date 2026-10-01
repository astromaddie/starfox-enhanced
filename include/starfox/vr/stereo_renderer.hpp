#pragma once
#include "starfox/vr/eye_camera.hpp"
#include "starfox/vr/openxr_swapchains.hpp"
#include <functional>
#include <vector>

namespace starfox::vr {
// Drives one predicted stereo frame using an API-specific eye renderer.
// The callback must finish its GPU work before returning (including false).
// No simulation tick belongs in this callback: both eyes see the same scene.
class StereoRenderer {
public:
    using DrawEye = std::function<bool(unsigned, uint32_t, const EyeCamera&, XrTime)>;
    enum class EyeResult {complete, pending, failed, fatal};
    // Pending callbacks are retried with the same image/camera/time. Failed
    // means no GPU work remains; fatal retains images until device teardown.
    using AsyncDrawEye = std::function<EyeResult(unsigned,uint32_t,const EyeCamera&,XrTime)>;
    struct Composition {
        // Called after both eye fences finish. Pending retains this stereo
        // frame. Returned headers must stay valid through end_frame.
        std::function<EyeResult(const StereoFrame&,std::vector<const XrCompositionLayerBaseHeader*>&)> draw;
        std::function<ImageWait()> cancel;
    };
    void request_recenter() noexcept {recenter_pending_=true;}
    void set_head_translation(float scale) noexcept {head_translation_=scale;}
    StereoRenderer(OpenXrSession& session, OpenXrSwapchains& images,bool anchor_position=false)
        :session_(session),images_(images),anchor_position_(anchor_position) {}
    enum class Result {idle, waiting, submitted, skipped, error};
    Result step(const DrawEye&, float units_per_metre, float near_plane,
        std::optional<float> far_plane=std::nullopt);
    Result step_async(const AsyncDrawEye&,float units_per_metre,float near_plane,
        std::optional<float> far_plane=std::nullopt,const Composition& composition={});
    bool frame_pending() const noexcept {return frame_.has_value();}
    bool teardown_required() const noexcept {return fatal_;}
    XrPosef anchored_pose(XrPosef pose) const noexcept {return anchor_position_?position_anchor_.anchored(pose):pose;}
private:
    OpenXrSession& session_;
    OpenXrSwapchains& images_;
    std::optional<StereoFrame> frame_;
    std::array<EyeCamera,2> cameras_{};
    unsigned next_eye_{};
    bool cancelling_{};
    bool fatal_{};
    bool anchor_position_{};
    bool recenter_pending_{};float head_translation_{1.F};
    PositionAnchor position_anchor_;
};
}
