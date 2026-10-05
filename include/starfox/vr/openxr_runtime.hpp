#pragma once
#include <openxr/openxr.h>
#include <string>
#include <vector>
namespace starfox::vr {
// Borrowed JNI references on Android. The caller retains valid global
// references until the runtime instance has been destroyed.
struct AndroidXrContext {void* java_vm{};void* application_context{};void* activity{};};
// Shared runtime discovery for the VR application. Creating an instance and
// querying the headset is deliberately separate from graphics/session setup.
class OpenXrRuntime {
public:
    ~OpenXrRuntime();
    OpenXrRuntime()=default;
    OpenXrRuntime(const OpenXrRuntime&)=delete;
    OpenXrRuntime& operator=(const OpenXrRuntime&)=delete;
    bool initialize(const AndroidXrContext* android=nullptr);
    XrInstance instance() const noexcept {return instance_;}
    XrSystemId system() const noexcept {return system_;}
    const std::vector<XrViewConfigurationView>& views() const noexcept {return views_;}
    const std::string& status() const noexcept {return status_;}
    bool supports_vulkan() const noexcept {return vulkan_;}
    bool supports_frame_controller_interaction() const noexcept {
        return frame_controller_interaction_;
    }
    // XR_FB_display_refresh_rate was advertised and enabled on the instance.
    bool supports_display_refresh_rate() const noexcept {return display_refresh_rate_;}
    // XR_KHR_visibility_mask was advertised and enabled; used only with SFX_VR_VISIBILITY_MASK.
    bool supports_visibility_mask() const noexcept {return visibility_mask_;}
private:
    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId system_{XR_NULL_SYSTEM_ID};
    bool vulkan_{};
    bool frame_controller_interaction_{};
    bool display_refresh_rate_{};
    bool visibility_mask_{};
    std::vector<XrViewConfigurationView> views_;
    std::string status_{"OpenXR not initialized"};
};
}
