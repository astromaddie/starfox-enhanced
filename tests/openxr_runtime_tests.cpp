#include "starfox/vr/openxr_runtime.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
unsigned creates=0,destroys=0;bool fail_system=false,graphics=true,frame_extension=false,mask_extension=false;unsigned eye_count=2;
bool change_eye_count=false;
void check(bool value){if(!value) throw std::runtime_error("OpenXR runtime lifecycle assertion failed");}
}
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char*,uint32_t capacity,uint32_t* count,XrExtensionProperties* out) {
    *count=graphics?1U+frame_extension+mask_extension:0U;
    if(capacity && graphics) {
        std::strcpy(out[0].extensionName,"XR_KHR_vulkan_enable2");
        if(frame_extension && capacity>1)
            std::strcpy(out[1].extensionName,"XR_VALVE_frame_controller_interaction");
        if(mask_extension && capacity>1U+frame_extension)
            std::strcpy(out[1+frame_extension].extensionName,XR_KHR_VISIBILITY_MASK_EXTENSION_NAME);
    }
    return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info,XrInstance* out) {
    check(info->enabledExtensionCount==1U+frame_extension+mask_extension
        && std::strcmp(info->enabledExtensionNames[0],"XR_KHR_vulkan_enable2")==0);
    // Order: Vulkan, refresh rate, visibility mask, Frame controller.
    if(mask_extension) check(std::strcmp(info->enabledExtensionNames[1],XR_KHR_VISIBILITY_MASK_EXTENSION_NAME)==0);
    if(frame_extension) check(std::strcmp(info->enabledExtensionNames[1+mask_extension],
        "XR_VALVE_frame_controller_interaction")==0);
    ++creates;*out=reinterpret_cast<XrInstance>(uintptr_t(1));return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance instance) {check(instance!=XR_NULL_HANDLE);++destroys;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(XrInstance,XrInstanceProperties* out) {std::strcpy(out->runtimeName,"test runtime");return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance,const XrSystemGetInfo* info,XrSystemId* out) {
    check(info->formFactor==XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY);if(fail_system) return XR_ERROR_FORM_FACTOR_UNAVAILABLE;*out=9;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t capacity,uint32_t* count,XrViewConfigurationView* out) {
    check(type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO);*count=(capacity && change_eye_count)?1:eye_count;
    for(unsigned i=0;i<capacity;++i) out[i].recommendedImageRectWidth=1024;return XR_SUCCESS;
}
}
int main()try {
    using namespace starfox::vr;
    {
        OpenXrRuntime runtime;check(runtime.initialize());check(runtime.views().size()==2 && runtime.system()==9 && runtime.supports_vulkan()
            && !runtime.supports_frame_controller_interaction());
        check(runtime.initialize() && creates==2 && destroys==1);
        fail_system=true;check(!runtime.initialize());check(creates==3 && destroys==3);
        check(runtime.instance()==XR_NULL_HANDLE && runtime.system()==XR_NULL_SYSTEM_ID && runtime.views().empty() && !runtime.supports_vulkan());
        fail_system=false;eye_count=1;check(!runtime.initialize() && creates==4 && destroys==4);
        eye_count=2;graphics=false;check(!runtime.initialize() && creates==4);graphics=true;
        AndroidXrContext context;check(!runtime.initialize(&context) && creates==4);
        check(runtime.initialize());
        change_eye_count=true;check(!runtime.initialize());
        check(runtime.instance()==XR_NULL_HANDLE && runtime.views().empty() && !runtime.supports_vulkan());
        change_eye_count=false;check(runtime.initialize());
        frame_extension=true;
        check(runtime.initialize() && runtime.supports_frame_controller_interaction() && !runtime.supports_visibility_mask());
        // XR_KHR_visibility_mask: enabled only when advertised.
        mask_extension=true;
        check(runtime.initialize() && runtime.supports_visibility_mask() && runtime.supports_frame_controller_interaction());
        frame_extension=false;check(runtime.initialize() && runtime.supports_visibility_mask());
        mask_extension=false;check(runtime.initialize() && !runtime.supports_visibility_mask());
    }
    check(creates==11 && destroys==11);std::cout<<"OpenXR runtime initialization, optional Frame and visibility mask extensions, failure cleanup, reinitialization and destruction passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
