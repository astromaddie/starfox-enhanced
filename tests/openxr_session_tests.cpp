#include "starfox/vr/openxr_session.hpp"
#include "starfox/vr/stereo_renderer.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>
using namespace starfox::vr;
namespace {
void require(bool v,const char* message) {if(!v) throw std::runtime_error(message);}
template<typename T> T handle(uintptr_t n) {return reinterpret_cast<T>(n);}
struct Fake {
    std::deque<XrSessionState> events;
    std::deque<XrEventDataReferenceSpaceChangePending> origin_changes;
    std::vector<int> calls;
    bool render=true,tracked=true,fail_space=false,fail_locate=false,fail_exit=false,refuse_layers=false;
    unsigned end_calls=0;
    uint32_t submitted=99;
    XrTime submitted_time{};
    XrTime predicted_time{123456};
    std::vector<const XrCompositionLayerBaseHeader*> layers;
} fake;
// Renderer event order: 10+eye queued, 20+eye pending poll, 30+eye fence
// observed, 40+eye failed, 50+eye image released, 60/61 composition draw/cancel, 7 end frame.
std::vector<int> order;
std::array<XrSwapchain,2> eye_chains{};
XrSwapchain waiting_chain{};
XrDuration image_timeout{};
XrResult XRAPI_PTR create(XrInstance,const XrSessionCreateInfo* info,XrSession* out) {
    require(info->next!=nullptr && info->systemId==7,"graphics binding/system not forwarded");
    fake.calls.push_back(1);*out=handle<XrSession>(2);return XR_SUCCESS;
}
XrResult XRAPI_PTR destroy(XrSession) {fake.calls.push_back(10);return XR_SUCCESS;}
XrResult XRAPI_PTR space(XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* out) {
    require(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL && info->poseInReferenceSpace.orientation.w==1,
        "tracking space is not identity LOCAL");
    fake.calls.push_back(2);if(fake.fail_space) return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<XrSpace>(3);return XR_SUCCESS;
}
XrResult XRAPI_PTR destroy_space(XrSpace) {fake.calls.push_back(9);return XR_SUCCESS;}
XrResult XRAPI_PTR modes(XrInstance,XrSystemId,XrViewConfigurationType type,uint32_t capacity,uint32_t* count,XrEnvironmentBlendMode* out) {
    require(type==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,"not stereo blend query");
    *count=2;if(capacity) {out[0]=XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;out[1]=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;}
    return XR_SUCCESS;
}
XrResult XRAPI_PTR poll(XrInstance,XrEventDataBuffer* out) {
    require(out->type==XR_TYPE_EVENT_DATA_BUFFER,"event buffer type not reset");
    if(!fake.origin_changes.empty()) {
        const auto e=fake.origin_changes.front();fake.origin_changes.pop_front();
        std::memcpy(out,&e,sizeof(e));return XR_SUCCESS;
    }
    if(fake.events.empty()) return XR_EVENT_UNAVAILABLE;
    XrEventDataSessionStateChanged e{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    e.session=handle<XrSession>(2);e.state=fake.events.front();fake.events.pop_front();
    std::memcpy(out,&e,sizeof(e));return XR_SUCCESS;
}
XrResult XRAPI_PTR begin(XrSession,const XrSessionBeginInfo* info) {
    require(info->primaryViewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,"session not stereo");
    fake.calls.push_back(3);return XR_SUCCESS;
}
XrResult XRAPI_PTR stop(XrSession) {fake.calls.push_back(8);return XR_SUCCESS;}
XrResult XRAPI_PTR request_exit(XrSession) {fake.calls.push_back(11);return fake.fail_exit?XR_ERROR_SESSION_NOT_RUNNING:XR_SUCCESS;}
XrResult XRAPI_PTR wait(XrSession,const XrFrameWaitInfo*,XrFrameState* out) {
    fake.calls.push_back(4);out->predictedDisplayTime=fake.predicted_time;out->predictedDisplayPeriod=11111;
    out->shouldRender=fake.render;return XR_SUCCESS;
}
XrResult XRAPI_PTR begin_frame(XrSession,const XrFrameBeginInfo*) {fake.calls.push_back(5);return XR_SUCCESS;}
XrResult XRAPI_PTR end_frame(XrSession,const XrFrameEndInfo* info) {
    fake.calls.push_back(7);order.push_back(7);fake.submitted=info->layerCount;
    fake.layers.assign(info->layers,info->layers+info->layerCount);fake.submitted_time=info->displayTime;++fake.end_calls;
    require(info->environmentBlendMode==XR_ENVIRONMENT_BLEND_MODE_OPAQUE,"preferred opaque blend not selected");
    return fake.refuse_layers && info->layerCount?XR_ERROR_LAYER_INVALID:XR_SUCCESS;
}
XrResult XRAPI_PTR views(XrSession,const XrViewLocateInfo* info,XrViewState* state,uint32_t capacity,uint32_t* count,XrView* out) {
    require(info->displayTime==fake.predicted_time && info->space==handle<XrSpace>(3) && capacity==2,"view location ignored predicted time/local space");
    fake.calls.push_back(6);if(fake.fail_locate) return XR_ERROR_RUNTIME_FAILURE;
    *count=2;state->viewStateFlags=fake.tracked?XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT:0;
    for(unsigned i=0;i<2;++i) {
        require(out[i].type==XR_TYPE_VIEW,"view not initialized");
        out[i].pose.orientation.w=1;out[i].pose.position.x=i?.032F:-.032F;
        out[i].fov={-.7F,.7F,.7F,-.7F};
    }
    return XR_SUCCESS;
}
SessionApi api() {return {create,destroy,space,destroy_space,modes,poll,begin,stop,wait,begin_frame,end_frame,views,request_exit};}
void start(OpenXrSession& session) {
    int graphics_binding=1;
    require(session.initialize(handle<XrInstance>(1),7,&graphics_binding),"session initialization failed");
    require(!session.running() && !session.begin_frame(),"session ran before READY");
    fake.events.push_back(XR_SESSION_STATE_READY);
    require(session.poll_events() && session.running(),"READY did not start session");
}
unsigned acquisitions{},released{};
bool image_pending{};
XrResult XRAPI_PTR formats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out) {
    *count=1;if(capacity) *out=43;return XR_SUCCESS;
}
XrResult XRAPI_PTR swap_create(XrSession,const XrSwapchainCreateInfo*,XrSwapchain* out) {
    static uintptr_t next=4;*out=handle<XrSwapchain>(next++);return XR_SUCCESS;
}
XrResult XRAPI_PTR swap_destroy(XrSwapchain) {return XR_SUCCESS;}
XrResult XRAPI_PTR images(XrSwapchain,uint32_t,uint32_t* count,XrSwapchainImageBaseHeader*) {
    *count=2;return XR_SUCCESS;
}
XrResult XRAPI_PTR acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    ++acquisitions;*index=1;return XR_SUCCESS;
}
XrResult XRAPI_PTR image_wait(XrSwapchain chain,const XrSwapchainImageWaitInfo* info) {
    image_timeout=info->timeout;
    return image_pending || chain==waiting_chain?XR_TIMEOUT_EXPIRED:XR_SUCCESS;
}
XrResult XRAPI_PTR release(XrSwapchain chain,const XrSwapchainImageReleaseInfo*) {
    for(unsigned eye=0;eye<2;++eye) if(chain==eye_chains[eye]) order.push_back(50+int(eye));
    ++released;return XR_SUCCESS;
}
void forced_frames() {
    fake=Fake{};
    OpenXrSession s(api());start(s);s.set_force_render(true);
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    // A runtime that wants the frame and tracks the head: unchanged, not forced.
    auto frame=s.begin_frame();require(frame && frame->should_render && !frame->forced,"forcing changed a normal frame");
    require(frame->views[0].fov.angleLeft==-.7F,"real views replaced");
    require(s.end_frame(layers) && fake.submitted==1,"normal frame not submitted");
    // Standby with valid tracking: real views, submitted, marked forced.
    fake.render=false;fake.calls.clear();
    frame=s.begin_frame();require(frame && frame->should_render && frame->forced,"standby frame not forced");
    require(fake.calls==std::vector<int>({4,5,6}),"standby views not located");
    require(frame->views[0].fov.angleLeft==-.7F && frame->views[0].pose.position.x==-.032F,"valid standby views replaced");
    require(s.end_frame(layers) && fake.submitted==1,"forced frame not submitted");
    // Untracked or failed location: synthetic views, never an exception.
    const auto synthetic=synthetic_stereo_views();
    for(const bool fail:{false,true}) {
        fake.tracked=fail;fake.fail_locate=fail;
        frame=s.begin_frame();require(frame && frame->should_render && frame->forced,"untracked frame not forced");
        for(unsigned eye=0;eye<2;++eye)
            require(std::memcmp(&frame->views[eye].pose,&synthetic[eye].pose,sizeof(XrPosef))==0
                && frame->views[eye].fov.angleLeft==synthetic[eye].fov.angleLeft,"synthetic views not used");
        require(s.end_frame(layers) && fake.submitted==1,"synthetic frame not submitted");
    }
    fake.render=true;
    frame=s.begin_frame();require(frame && frame->forced,"wanted frame with failed location not forced");
    require(s.end_frame(layers) && fake.submitted==1,"wanted synthetic frame not submitted");
    fake.fail_locate=false;fake.tracked=true;
    // A refused forced layer falls back to an empty frame and keeps going.
    fake.render=false;fake.refuse_layers=true;fake.end_calls=0;
    frame=s.begin_frame();require(frame && frame->forced,"refusal fixture not forced");
    require(s.end_frame(layers) && fake.end_calls==2 && fake.submitted==0 && s.forced_rejections()==1,
        "refused forced layers not retried empty");
    require(s.begin_frame().has_value() && s.end_frame(layers) && s.forced_rejections()==2,"refusal stopped frames");
    // A real frame's refusal is still an error, and forcing off restores the old skip.
    fake.render=true;fake.end_calls=0;
    require(s.begin_frame().has_value() && !s.end_frame(layers) && fake.end_calls==1,"normal refusal retried");
    fake.refuse_layers=false;fake.render=false;s.set_force_render(false);fake.calls.clear();
    frame=s.begin_frame();require(frame && !frame->should_render && !frame->forced,"forcing outlived its setting");
    require(fake.calls==std::vector<int>({4,5}) && s.end_frame(layers) && fake.submitted==0,"unforced standby changed");
}
void forced_renderer() {
    fake=Fake{};fake.render=false;fake.tracked=false;
    OpenXrSession session(api());start(session);session.set_force_render(true);
    OpenXrSwapchains chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    std::array<XrViewConfigurationView,2> config{};
    for(auto& eye:config) {
        eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=100;
        eye.maxImageRectWidth=eye.maxImageRectHeight=100;eye.maxSwapchainSampleCount=1;
    }
    const std::array<int64_t,1> preferred{43};
    require(chains.initialize(session.handle(),config,preferred),"forced swapchains failed");
    StereoRenderer renderer(session,chains);
    std::array<EyeCamera,2> cameras{};
    const auto draw=[&](unsigned eye,uint32_t,const EyeCamera& camera,XrTime) {cameras[eye]=camera;return true;};
    require(renderer.step(draw,1,.1F)==StereoRenderer::Result::submitted && renderer.last_frame_forced()
        && fake.submitted==1,"forced standby frame not rendered and submitted");
    // Synthetic left eye at x=-0.0315 looking down -Z.
    require(std::abs(cameras[0].view[12]-.0315F)<1e-6F && std::abs(cameras[0].view[14])<1e-6F,"synthetic camera wrong");
    // DIAG_YAW 180 turns the cameras about the head centre: the left eye
    // swaps sides and looks down +Z, while the submitted layer keeps its pose.
    renderer.set_diag_yaw(std::acos(-1.F));
    require(renderer.step(draw,1,.1F)==StereoRenderer::Result::submitted,"turned frame not submitted");
    require(std::abs(cameras[0].view[12]-.0315F)<1e-5F && std::abs(cameras[0].view[0]+1)<1e-5F
        && std::abs(cameras[0].view[10]+1)<1e-5F,"diagnostic yaw not applied to the camera");
    require(chains.projection() && chains.projection()->views[0].pose.orientation.w==1,"diagnostic yaw moved the layer");
    session.set_force_render(false);
    require(renderer.step(draw,1,.1F)==StereoRenderer::Result::skipped && !renderer.last_frame_forced(),"unforced standby rendered");
}
// SFX_VR_OVERLAP_EYES: both eyes queue before either fence is observed;
// images are released in eye order after their own fence, then end_frame.
void overlap_renderer() {
    fake=Fake{};image_pending=false;acquisitions=released=0;
    OpenXrSession session(api());start(session);
    OpenXrSwapchains chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    std::array<XrViewConfigurationView,2> config{};
    for(auto& eye:config) {
        eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=100;
        eye.maxImageRectWidth=eye.maxImageRectHeight=100;eye.maxSwapchainSampleCount=1;
    }
    const std::array<int64_t,1> preferred{43};
    require(chains.initialize(session.handle(),config,preferred),"overlap swapchains failed");
    eye_chains={chains.handle(0),chains.handle(1)};
    using Result=StereoRenderer::Result;using Eye=StereoRenderer::EyeResult;
    // Fake GPU: an eye's first call queues its work, later calls poll its fence.
    std::array<bool,2> queued{},fence{};
    std::optional<unsigned> fail_eye;bool fatal_poll=false;
    const auto draw=[&](unsigned eye,uint32_t index,const EyeCamera&,XrTime time) {
        require(index==1 && time==123456,"overlap eye arguments changed");
        if(!queued[eye]) {
            if(fail_eye==eye) {order.push_back(40+int(eye));return Eye::failed;}
            queued[eye]=true;order.push_back(10+int(eye));return Eye::submitted;
        }
        if(fatal_poll) return Eye::fatal;
        if(!fence[eye]) {order.push_back(20+int(eye));return Eye::pending;}
        queued[eye]=fence[eye]=false;order.push_back(30+int(eye));return Eye::complete;
    };
    const StereoRenderer::Composition composition{
        [&](const StereoFrame&,std::vector<const XrCompositionLayerBaseHeader*>&) {order.push_back(60);return Eye::complete;},
        [&] {order.push_back(61);return ImageWait::ready;}};
    StereoRenderer renderer(session,chains);
    renderer.set_overlap_eyes(true);
    require(!renderer.overlapping(),"overlap applied before a frame boundary");
    // Eye 1's image wait is bounded while eye 0 is in flight.
    order.clear();waiting_chain=eye_chains[1];
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting && renderer.overlapping(),"overlap frame not begun");
    require(order==std::vector<int>({10}) && image_timeout==1000000,"eye 1 image wait not bounded behind eye 0");
    waiting_chain={};
    renderer.set_overlap_eyes(false); // Latched: the frame in progress still overlaps.
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting && released==0,"queued eye released early");
    require(order==std::vector<int>({10,11,20}),"eye 1 not queued before eye 0's fence");
    fence[0]=true;
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting && released==1,"eye 0 not released after its fence");
    fence[1]=true;
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::submitted && fake.submitted==1,"overlap frame not submitted");
    require(order==std::vector<int>({10,11,20,30,50,21,31,51,60,7}),"overlap fence, release or end order wrong");
    // Serial again from the next frame: a queued eye is only pending, and eye 1
    // is not drawn until eye 0's fence is observed and its image released.
    order.clear();fence={true,true};
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting && !renderer.overlapping(),"serial frame changed");
    require(image_timeout==0 && order==std::vector<int>({10}),"serial frame treated submitted as complete");
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting,"serial eye 1 not pending");
    fence[1]=true;
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::submitted,"serial frame not submitted");
    require(order==std::vector<int>({10,30,50,11,31,51,60,7}),"serial order changed");
    // Eye 1 fails while eye 0 is in flight: eye 0's fence is drained before
    // cancel returns both images, and no layer is submitted.
    renderer.set_overlap_eyes(true);order.clear();fail_eye=1;
    const auto before=released;
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::waiting && released==before,"cancel released an image in flight");
    fence[0]=true;
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::error && released==before+2 && fake.submitted==0,
        "failed overlap frame leaked an image or submitted");
    require(order==std::vector<int>({10,41,20,30,61,50,51,7}) && !queued[0] && !renderer.frame_pending(),"overlap cancel order wrong");
    fail_eye.reset();fence={true,true};
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::submitted,"overlap did not recover after cancel");
    // An uncertain fence keeps both images for device teardown.
    order.clear();fatal_poll=true;fence={};
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::error && renderer.teardown_required(),
        "fatal overlap poll did not require teardown");
    require(order==std::vector<int>({10,11}),"fatal overlap frame released or ended");
    require(renderer.step_async(draw,1,.1F,std::nullopt,composition)==Result::error && order.size()==2,"fatal overlap frame reused");
    eye_chains={};
}
// The pause pointer is its own alpha-blended projection layer, submitted after
// the UI quad so nothing covers it, and absent (no acquire) when inactive.
void pointer_layer() {
    fake=Fake{};image_pending=false;acquisitions=released=0;
    OpenXrSession session(api());start(session);
    std::array<XrViewConfigurationView,2> config{};
    for(auto& eye:config) {
        eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=100;
        eye.maxImageRectWidth=eye.maxImageRectHeight=100;eye.maxSwapchainSampleCount=1;
    }
    const std::array<int64_t,1> preferred{43};
    OpenXrSwapchains chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    OpenXrSwapchains pointer_chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    require(chains.initialize(session.handle(),config,preferred),"pointer fixture eyes failed");
    for(auto& eye:config) eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=50;
    require(pointer_chains.initialize(session.handle(),config,preferred,XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT),
        "pointer swapchains failed");
    using Result=StereoRenderer::Result;using Eye=StereoRenderer::EyeResult;
    StereoRenderer renderer(session,chains);
    CompositionLayers composition(pointer_chains);
    XrCompositionLayerQuad quad_layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const auto* quad=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad_layer);
    bool pointer=true,show_quad=true;unsigned quad_draws=0;
    std::vector<unsigned> pointer_draws;std::optional<unsigned> pending_eye,failed_eye;
    const auto draw=[](unsigned,uint32_t,const EyeCamera&,XrTime) {return Eye::complete;};
    const StereoRenderer::Composition layers{
        [&](const StereoFrame& frame,std::vector<const XrCompositionLayerBaseHeader*>& out) {
            return composition.draw(frame,session.space(),pointer,[&](const StereoFrame&,const XrCompositionLayerBaseHeader*& layer) {
                ++quad_draws;if(show_quad) layer=quad;return Eye::complete;
            },[&](unsigned eye,uint32_t image,XrTime time) {
                require(image==1 && time==123456,"pointer eye arguments changed");
                pointer_draws.push_back(eye);
                if(failed_eye==eye) return Eye::failed;
                if(pending_eye==eye) {pending_eye.reset();return Eye::pending;}
                return Eye::complete;
            },out);
        },[&] {return composition.cancel([] {return ImageWait::ready;});}};
    const auto projection=[&] {return reinterpret_cast<const XrCompositionLayerBaseHeader*>(chains.projection());};
    const auto pointer_projection=[&] {return reinterpret_cast<const XrCompositionLayerBaseHeader*>(pointer_chains.projection());};
    // A pending pointer fence retries without drawing or acquiring the quad again.
    pending_eye=0;
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::waiting && quad_draws==1,"pending pointer eye not retried");
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::submitted && quad_draws==1,"pointer frame not submitted");
    require(pointer_draws==std::vector<unsigned>({0,0,1}) && released==4,"pointer eyes not drawn and released once each");
    require(fake.layers==std::vector<const XrCompositionLayerBaseHeader*>({projection(),quad,pointer_projection()}),
        "pointer layer not submitted last");
    const auto& submitted=*pointer_chains.projection();
    require(submitted.layerFlags==XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT && chains.projection()->layerFlags==0,
        "pointer layer not alpha blended, or the eye layer changed");
    require(submitted.space==chains.projection()->space && submitted.viewCount==2
        && submitted.views[1].subImage.imageRect.extent.width==50,"pointer layer space or images wrong");
    for(unsigned eye=0;eye<2;++eye)
        require(std::memcmp(&submitted.views[eye].pose,&chains.projection()->views[eye].pose,sizeof(XrPosef))==0
            && std::memcmp(&submitted.views[eye].fov,&chains.projection()->views[eye].fov,sizeof(XrFovf))==0,
            "pointer layer does not share the eye views");
    // Inactive: exactly the old layers, and the pointer swapchain is untouched.
    pointer=false;pointer_draws.clear();const auto before=acquisitions;
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::submitted && pointer_draws.empty()
        && acquisitions==before+2,"inactive pointer still drew or acquired");
    require(fake.layers==std::vector<const XrCompositionLayerBaseHeader*>({projection(),quad}),"inactive frame layers changed");
    pointer=true;show_quad=false;
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::submitted
        && fake.layers==std::vector<const XrCompositionLayerBaseHeader*>({projection(),pointer_projection()}),
        "pointer without a quad not submitted after the eyes");
    // A failed pointer eye cancels the frame and returns every image.
    failed_eye=1;const auto released_before=released;
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::error && fake.submitted==0
        && released==released_before+4,"failed pointer frame leaked an image or submitted");
    failed_eye.reset();pointer_draws.clear();
    require(renderer.step_async(draw,1,.1F,std::nullopt,layers)==Result::submitted && pointer_draws==std::vector<unsigned>({0,1})
        && fake.layers.size()==2,"pointer did not recover after cancel");
}
}
int main() try {
    forced_frames();
    forced_renderer();overlap_renderer();pointer_layer();acquisitions=released=0;
    fake=Fake{};
    {
        OpenXrSession s(api());start(s);
        XrEventDataReferenceSpaceChangePending change{XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING};
        change.session=s.handle();change.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
        change.changeTime=123457;fake.origin_changes.push_back(change);
        require(s.poll_events(),"origin event failed");
        auto frame=s.begin_frame();require(frame && !frame->tracking_origin_changed,"future origin applied early");
        require(s.end_frame(),"end future frame");
        change.changeTime=123456;fake.origin_changes.push_back(change);
        require(s.poll_events(),"due origin event failed");
        frame=s.begin_frame();require(frame && frame->tracking_origin_changed,"due origin not signaled");
        require(s.end_frame(),"end due frame");
        frame=s.begin_frame();require(frame && !frame->tracking_origin_changed,"origin signaled twice");
        require(s.end_frame(),"end repeated frame");
        fake.predicted_time=123457;fake.render=false;
        frame=s.begin_frame();require(frame && frame->tracking_origin_changed && !frame->should_render,
            "queued origin lost on invisible frame");
        require(s.end_frame(),"end invisible origin frame");
    }
    fake=Fake{};
    {
        OpenXrSession s(api());start(s);
        auto frame=s.begin_frame();require(frame && frame->should_render,"tracked frame not renderable");
        require(frame->display_period==11111 && frame->views[0].pose.position.x<0 && frame->views[1].pose.position.x>0,"stereo timing/eyes lost");
        require(!s.begin_frame() && !s.poll_events(),"double begin/event polling during frame accepted");
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
        require(s.end_frame(layers) && fake.submitted==1 && fake.submitted_time==123456,"projection not submitted at predicted time");
        require(!s.end_frame(),"double frame end accepted");
        fake.render=false;frame=s.begin_frame();require(frame && !frame->should_render,"invisible frame rendered");
        require(s.end_frame(layers) && fake.submitted==0,"invisible frame submitted layers");
        fake.render=true;fake.tracked=false;frame=s.begin_frame();require(frame && !frame->should_render,"invalid tracking rendered");
        require(s.end_frame(layers) && fake.submitted==0,"untracked frame submitted layers");
        fake.tracked=true;fake.fail_locate=true;
        require(!s.begin_frame() && fake.submitted==0 && fake.calls.back()==7,"locate failure leaked a begun frame");
        fake.fail_locate=false;
        fake.events.push_back(XR_SESSION_STATE_STOPPING);
        require(s.poll_events() && !s.running() && fake.calls.back()==8,"STOPPING did not end session");
        fake.events.push_back(XR_SESSION_STATE_READY);require(s.poll_events() && s.running(),"session could not restart");
        fake.events.push_back(XR_SESSION_STATE_LOSS_PENDING);
        require(s.poll_events() && s.exit_requested() && !s.begin_frame(),"loss did not stop frame production");
    }
    require(fake.calls[fake.calls.size()-2]==9 && fake.calls.back()==10,"space/session destruction order wrong");
    // Quit to Steam: xrRequestExitSession, then the runtime's STOPPING and
    // EXITING events end the session cleanly (xrEndSession in between).
    fake=Fake{};
    {
        OpenXrSession s(api());int binding=1;
        require(!s.request_exit(),"exit request accepted without a session");
        require(s.initialize(handle<XrInstance>(1),7,&binding),"exit fixture init failed");
        fake.events.push_back(XR_SESSION_STATE_READY);fake.events.push_back(XR_SESSION_STATE_FOCUSED);
        require(s.poll_events() && s.running() && !s.exit_requested() && !s.exit_in_progress(),"exit fixture not running");
        require(s.request_exit() && s.exit_in_progress() && !s.exit_requested()
            && fake.calls.back()==11 && s.running(),"running session did not request exit from the runtime");
        const auto calls=fake.calls.size();
        require(s.request_exit() && fake.calls.size()==calls,"exit request repeated");
        fake.events.push_back(XR_SESSION_STATE_STOPPING);
        require(s.poll_events() && !s.running() && !s.exit_requested() && fake.calls.back()==8,
            "STOPPING after exit request did not end the session");
        fake.events.push_back(XR_SESSION_STATE_EXITING);
        require(s.poll_events() && s.exit_requested() && !s.begin_frame(),"EXITING did not finish the exit");
    }
    fake=Fake{};
    {
        OpenXrSession s(api());int binding=1;
        require(s.initialize(handle<XrInstance>(1),7,&binding),"idle exit fixture init failed");
        require(s.request_exit() && s.exit_requested() && fake.calls.back()!=11,
            "a session that never began must exit without asking the runtime");
    }
    fake=Fake{};fake.fail_exit=true;
    {
        OpenXrSession s(api());int binding=1;
        require(s.initialize(handle<XrInstance>(1),7,&binding),"refused exit fixture init failed");
        fake.events.push_back(XR_SESSION_STATE_READY);require(s.poll_events() && s.running(),"refused exit fixture not running");
        require(!s.request_exit() && s.exit_requested() && !s.running()
            && s.status().find("Request OpenXR session exit")!=std::string::npos,"refused exit request not reported");
    }
    fake=Fake{};fake.fail_space=true;
    {
        OpenXrSession s(api());int binding=1;
        require(!s.initialize(handle<XrInstance>(1),7,&binding) && s.handle()==XR_NULL_HANDLE,"failed initialization retained session");
        require(fake.calls==std::vector<int>({1,2,10}),"failed space creation did not roll back session");
    }
    fake=Fake{};
    {
        OpenXrSession s(api());start(s);require(s.begin_frame().has_value(),"cleanup fixture failed");
        s.close();s.close();
        require(fake.calls==std::vector<int>({1,2,3,4,5,6,7,9,10}),"active-frame cleanup order or idempotence failed");
    }
    fake=Fake{};
    {
        OpenXrSession session(api());start(session);
        OpenXrSwapchains chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
        std::array<XrViewConfigurationView,2> config{};
        for(auto& eye:config) {
            eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=100;
            eye.maxImageRectWidth=eye.maxImageRectHeight=100;
            eye.maxSwapchainSampleCount=1;
        }
        const std::array<int64_t,1> preferred{43};
        require(chains.initialize(session.handle(),config,preferred),"renderer swapchains failed");
        StereoRenderer renderer(session,chains);
        using Result=StereoRenderer::Result;
        unsigned draws=0;
        const auto draw=[&](unsigned eye,uint32_t index,const EyeCamera& camera,XrTime time) {
            require(eye==draws && index==1 && time==123456,"eye render arguments changed");
            require(eye==0?camera.view[12]>0:camera.view[12]<0,"stereo camera translation lost");
            ++draws;return true;
        };
        image_pending=true;
        require(renderer.step(draw,1,.1F)==Result::waiting && draws==0,"pending image rendered");
        require(renderer.step(draw,1,.1F)==Result::waiting && acquisitions==1,"pending frame reacquired");
        image_pending=false;
        require(renderer.step(draw,1,.1F)==Result::submitted,"stereo frame not submitted");
        require(draws==2 && released==2 && fake.submitted==1,"incomplete stereo submission");
        require(renderer.step([](auto,auto,const auto&,auto){return false;},1,.1F)==Result::error,
            "renderer failure accepted");
        require(fake.submitted==0 && released==3,"failed frame leaked image or submitted stale layer");
        draws=0;require(renderer.step(draw,1,.1F)==Result::submitted,"failed frame prevented recovery");
        fake.tracked=false;
        require(renderer.step(draw,1,.1F)==Result::skipped && draws==2 && fake.submitted==0,
            "untracked frame rendered or submitted stale eyes");
        fake.tracked=true;
        unsigned async_calls=0;
        const auto releases_before=released;
        const auto async_draw=[&](unsigned eye,uint32_t,const EyeCamera&,XrTime) {
            ++async_calls;
            if(async_calls<3) {require(eye==0,"pending eye advanced");return StereoRenderer::EyeResult::pending;}
            return StereoRenderer::EyeResult::complete;
        };
        require(renderer.step_async(async_draw,1,.1F)==Result::waiting && released==releases_before,
            "GPU pending eye released early");
        require(renderer.step_async(async_draw,1,.1F)==Result::waiting && released==releases_before,
            "GPU pending retry released early");
        require(renderer.step_async(async_draw,1,.1F)==Result::submitted && released==releases_before+2,
            "completed asynchronous stereo frame not submitted");
        const auto fatal_draw=[](auto,auto,const auto&,auto){return StereoRenderer::EyeResult::fatal;};
        require(renderer.step_async(fatal_draw,1,.1F)==Result::error && renderer.teardown_required(),
            "uncertain GPU failure did not require teardown");
        require(released==releases_before+2,"uncertain GPU image released");
        require(renderer.step_async(async_draw,1,.1F)==Result::error && released==releases_before+2,
            "fatal frame was reused");
    }
    std::cout<<"OpenXR injected session lifecycle tests passed (not headset validation).\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
