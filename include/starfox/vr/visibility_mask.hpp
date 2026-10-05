#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace starfox::vr {
// XR_KHR_visibility_mask (SFX_VR_VISIBILITY_MASK=1): the part of each eye image
// the lenses never show, as a hidden triangle mesh per primary stereo view.
// Points are in tangent space (the x/y of the view-space point on the z=-1
// plane), so they need the eye's projection only, never the head pose. Every
// runtime call goes through an injectable table; an empty table makes none.
struct VisibilityMaskApi {
    PFN_xrGetVisibilityMaskKHR get{};
    // Resolves xrGetVisibilityMaskKHR; empty for a null instance or when the
    // runtime has no such entry point.
    static VisibilityMaskApi from_instance(XrInstance instance);
    [[nodiscard]] bool available() const noexcept {return get!=nullptr;}
};

class VisibilityMask {
public:
    static constexpr std::uint32_t max_vertices=1U<<16,max_indices=3U<<16;
    explicit VisibilityMask(VisibilityMaskApi api={}):api_(api) {}
    // Fetch every stale eye (both, initially). True when any eye's mesh was
    // replaced, so the caller uploads again. A failed or empty fetch leaves that
    // eye without a mask until the next change event.
    bool refresh(XrSession session);
    // XrEventDataVisibilityMaskChangedKHR for a primary stereo view.
    void invalidate(std::uint32_t view) noexcept {if(view<2) stale_[view]=true;}
    [[nodiscard]] bool stale(unsigned eye) const noexcept {return eye<2 && stale_[eye];}
    // Unindexed triangle list: three tangent-space points per triangle.
    [[nodiscard]] std::span<const XrVector2f> triangles(unsigned eye) const noexcept {
        return eye<2?std::span<const XrVector2f>(triangles_[eye]):std::span<const XrVector2f>{};
    }
    [[nodiscard]] const std::string& status() const noexcept {return status_;}
private:
    bool fetch(XrSession,std::uint32_t eye);
    VisibilityMaskApi api_;
    std::array<std::vector<XrVector2f>,2> triangles_;
    std::array<bool,2> stale_{true,true};
    std::string status_{"Visibility mask not fetched"};
};
}
