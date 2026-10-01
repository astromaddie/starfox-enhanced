#include "starfox/vr/openxr_session.hpp"
#include "starfox/vr/stereo_renderer.hpp"
#include "starfox/vr/startup_menu.hpp"
#include "starfox/vr/scene_interpolation.hpp"
#include "starfox/vr/source_sprites.hpp"
#include "starfox/render/hud_layout.hpp"
#include "starfox/render/scaled_text_renderer.hpp"
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
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
    bool render=true,tracked=true,fail_space=false,fail_locate=false;
    uint32_t submitted=99;
    XrTime submitted_time{};
    XrTime predicted_time{123456};
} fake;
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
XrResult XRAPI_PTR wait(XrSession,const XrFrameWaitInfo*,XrFrameState* out) {
    fake.calls.push_back(4);out->predictedDisplayTime=fake.predicted_time;out->predictedDisplayPeriod=11111;
    out->shouldRender=fake.render;return XR_SUCCESS;
}
XrResult XRAPI_PTR begin_frame(XrSession,const XrFrameBeginInfo*) {fake.calls.push_back(5);return XR_SUCCESS;}
XrResult XRAPI_PTR end_frame(XrSession,const XrFrameEndInfo* info) {
    fake.calls.push_back(7);fake.submitted=info->layerCount;fake.submitted_time=info->displayTime;
    require(info->environmentBlendMode==XR_ENVIRONMENT_BLEND_MODE_OPAQUE,"preferred opaque blend not selected");
    return XR_SUCCESS;
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
SessionApi api() {return {create,destroy,space,destroy_space,modes,poll,begin,stop,wait,begin_frame,end_frame,views};}
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
    *out=handle<XrSwapchain>(4);return XR_SUCCESS;
}
XrResult XRAPI_PTR swap_destroy(XrSwapchain) {return XR_SUCCESS;}
XrResult XRAPI_PTR images(XrSwapchain,uint32_t,uint32_t* count,XrSwapchainImageBaseHeader*) {
    *count=2;return XR_SUCCESS;
}
XrResult XRAPI_PTR acquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    ++acquisitions;*index=1;return XR_SUCCESS;
}
XrResult XRAPI_PTR image_wait(XrSwapchain,const XrSwapchainImageWaitInfo*) {
    return image_pending?XR_TIMEOUT_EXPIRED:XR_SUCCESS;
}
XrResult XRAPI_PTR release(XrSwapchain,const XrSwapchainImageReleaseInfo*) {
    ++released;return XR_SUCCESS;
}
}
int main() try {
    {
        using namespace starfox;
        GameSceneSnapshot before,now;before.flow=now.flow=simulation::GameFlowState::gameplay;
        before.view_matrix=now.view_matrix={32767,0,0,0,32767,0,0,0,32767};
        before.pilot_tracking=now.pilot_tracking=true;
        render::ObjectPresentationSnapshot pilot;
        pilot.transform={256,128,1024,0,0,0};pilot.rotation_matrix=now.view_matrix;
        pilot.generation=2;pilot.strategy_address=1;
        before.pilot_reference=now.pilot_reference=pilot;
        PresentationPreferences preferences;
        require(presentation_scene_matrix(before,now,.5,preferences)==identity_matrix,"Default camera changed");
        preferences.cockpit=true;
        const auto cockpit=presentation_scene_matrix(before,now,.5,preferences);
        require(std::abs(cockpit[12]+1)<.001F && std::abs(cockpit[13]-.5F)<.001F
            && std::abs(cockpit[14]-4)<.001F,"Pilot reference not in source view space");
        preferences.origin_x=25;
        const auto offset=presentation_scene_matrix(before,now,.5,preferences);
        require(std::abs(offset[12]-cockpit[12]+.25F)<.001F,"Calibrated origin changed wrong axis");
        const auto instruments=presentation_instrument_matrix(before,now,.5,preferences);
        require(std::abs(instruments[12]+.25F)<.001F,"Instruments did not stay attached to authored reference");
        preferences.world_scale=5;
        const auto large_offset=presentation_scene_matrix(before,now,.5,preferences);
        preferences.origin_x=0;
        const auto large_origin=presentation_scene_matrix(before,now,.5,preferences);
        require(std::abs(large_offset[12]-large_origin[12]+.25F)<.001F,
            "Physical cockpit calibration scaled with the world");
        preferences.world_scale=0;preferences.origin_x=25;
        now.pilot_tracking=false;
        require(presentation_scene_matrix(before,now,.5,preferences)==identity_matrix,"Script camera overridden");
        now.pilot_tracking=true;now.pilot_reference.reset();
        require(presentation_scene_matrix(before,now,.5,preferences)==identity_matrix,"Missing pilot reference invented");
        preferences.world_scale=5;
        const auto scaled=presentation_scene_matrix(before,now,.5,preferences);
        require(scaled[0]==2 && scaled[5]==2 && scaled[10]==2,"World scale missing");
        for(bool ex:{false,true}) {
            const auto layout=layout_a_hud(ex);
            const int label_y=(ex?186:183)+layout[render::HudElement::shield].y;
            const int bar_y=(ex?182:178)+16+layout[render::HudElement::shield].y;
            require(label_y==190 && bar_y>=201,"Shield source composition lost");
            require(192+layout[render::HudElement::comms].y<label_y,"Portrait still overlaps shield");
        }
        simulation::SnesPpuState ppu;ppu.main_screen=16;ppu.oam.fill(0);
        ppu.oam[0]=100;ppu.oam[1]=180;ppu.oam[2]=0x61; // reticle in HUD band
        ppu.oam[4]=24;ppu.oam[5]=183;ppu.oam[6]=0x77; // shield label
        const auto layout=layout_a_hud(false);
        const auto world=source_sprite_packet(ppu,15,{},false,nullptr,nullptr,SourceSpritePass::world);
        const auto hud=source_sprite_packet(ppu,15,{},false,nullptr,&layout,SourceSpritePass::hud);
        require(world.geometry.vertices.size()==6 && world.geometry.vertices[0].position[0]==100
            && world.geometry.vertices[0].position[1]==180,"Aim sprite moved with HUD");
        require(hud.geometry.vertices.size()==6 && hud.geometry.vertices[0].position[0]==39
            && hud.geometry.vertices[0].position[1]==190,"HUD label group offset wrong");
    }
    {
        // No cartridge pixels are active: gameplay must not acquire an opaque
        // decorative panel. This exercises the actual production HUD builder.
        const starfox::assets::RomImage rom(std::vector<uint8_t>(0x8000));
        const auto symbols=starfox::assets::SymbolMap::parse(
            "MSCALECHARS $008000\nMARIOMSGS $008000\nFONT0WID $008000\n"
            "FONT0FON $008000\nFONT0TRN $008000\nFACEDATA $008000\n");
        starfox::render::ScaledTextRenderer text(rom,symbols);
        GameSceneSnapshot scene;scene.ppu=std::make_shared<starfox::simulation::SnesPpuState>();
        const auto packets=layout_a_instrument_packets(rom,symbols,scene,text);
        for(const auto& packet:packets)
            require(packet.geometry.vertex_view().empty() && packet.geometry.line_view().empty(),
                "Inactive source HUD added geometry that obscures the world");
        require(!layout_a_surface(true).geometry.vertex_view().empty(),
            "Separate menu panel was removed with gameplay backing");
    }
    {
        using Flow=starfox::simulation::GameFlowState;
        GameSceneSnapshot scene;
        for(auto flow:{Flow::title,Flow::controls_type,Flow::controls_choice,Flow::planet_select,
            Flow::planet_travel,Flow::ex_pregame_menu}) {
            scene.flow=flow;require(world_panel_scene(scene),"Authored interface escaped whole-scene quad");
        }
        for(auto flow:{Flow::gameplay,Flow::training,Flow::intro}) {
            scene.flow=flow;require(!world_panel_scene(scene),"Live scene was flattened with menus");
        }
        scene.paused=true;require(world_panel_scene(scene),"Pause lost interface quad");
        scene.paused=false;scene.briefing.active=true;
        require(world_panel_scene(scene),"Briefing lost interface quad");
        const auto far=panel_matrix(),near=overlay_panel_matrix();
        require(std::abs(near[14]+.75F)<1e-6F && std::abs(far[14]+1.75F)<1e-6F,"Interface depths changed");
        for(unsigned i:{0U,5U,12U,13U})
            require(std::abs(near[i]/near[14]-far[i]/far[14])<1e-6F,"Near overlay changed angular layout");
        require(source_ui_layer_matrix(112,96,false)->at(14)==-2.F,"Source aiming plane moved");
        std::array<XrView,2> views{};for(auto& view:views)view.pose.orientation.w=1;
        views[0].pose.position.x=-.032F;views[1].pose.position.x=.032F;
        WorldPanelAnchor anchor;
        require(anchor.pose(views).position.z==-1.75F,"Initial menu depth wrong");
        for(auto& view:views)view.pose.position.z=1;
        require(anchor.pose(views).position.z==-1.75F,"Stable menu followed head translation");
        require(anchor.pose(views,.75F).position.z==.25F,"Near family retained far anchor");
        for(auto& view:views)view.pose.position.z=2;
        require(anchor.pose(views,.75F).position.z==.25F,"Stable overlay followed head translation");
        for(auto& view:views)view.pose.position.z=3;
        require(anchor.pose(views,1.75F).position.z==1.25F,"Far family retained near anchor");
    }
    OpenXrSession session(api());start(session);
    OpenXrSwapchains chains({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    std::array<XrViewConfigurationView,2> config{};
    for(auto& eye:config) {eye.recommendedImageRectWidth=eye.recommendedImageRectHeight=100;
        eye.maxImageRectWidth=eye.maxImageRectHeight=100;eye.maxSwapchainSampleCount=1;}
    const std::array<int64_t,1> preferred{43};
    require(chains.initialize(session.handle(),config,preferred),"Projection initialization failed");
    OpenXrQuad quad({formats,swap_create,swap_destroy,images,acquire,image_wait,release});
    require(quad.initialize(session.handle(),43),"Quad initialization failed");
    StereoRenderer renderer(session,chains,true);
    unsigned draws=0,compositions=0;
    const auto draw=[&](unsigned,uint32_t,const EyeCamera&,XrTime) {++draws;return StereoRenderer::EyeResult::complete;};
    StereoRenderer::Composition composition{
        [&](const StereoFrame& frame,std::vector<const XrCompositionLayerBaseHeader*>& layers) {
            ++compositions;
            require(frame.views[0].pose.position.x==-.032F && frame.views[1].pose.position.x==.032F
                && frame.views[0].fov.angleLeft==-.7F,"Compositor raw pose/FOV changed");
            const auto state=quad.acquire();
            if(state==ImageWait::waiting) return StereoRenderer::EyeResult::pending;
            require(state==ImageWait::ready,"Quad acquire failed");
            if(compositions==1) {renderer.request_recenter();return StereoRenderer::EyeResult::pending;}
            require(quad.release(),"Quad release failed");
            XrPosef pose{};pose.orientation.w=1;pose.position.z=-1.75F;
            const auto* layer=quad.layer(session.space(),pose);
            require(layer && layer->size.width==1.15F,"Physical panel width changed");
            layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(layer));
            return StereoRenderer::EyeResult::complete;
        },[&]{return quad.cancel();}};
    const auto first=renderer.step_async(draw,1,.05F,std::nullopt,composition);
    require(first==StereoRenderer::Result::waiting && draws==2 && released==2,"Quad pending released early");
    require(renderer.step_async(draw,1,.05F,std::nullopt,composition)==StereoRenderer::Result::submitted
        && draws==2 && released==3 && fake.submitted==2,"Quad retry rerendered eyes or omitted layer");
    require(renderer.step_async(draw,1,.05F,std::nullopt,composition)==StereoRenderer::Result::submitted
        && draws==4 && fake.submitted==2,"Recenter prevented next stereo frame");
    image_pending=true;
    require(quad.acquire()==ImageWait::waiting && !quad.image_index(),"Quad timeout exposed image");
    require(quad.cancel()==ImageWait::waiting,"Quad cancellation released unwaited image");
    image_pending=false;require(quad.cancel()==ImageWait::ready && !quad.layer(session.space(),{}),"Cancelled quad submitted");
    std::cout<<"Presentation camera, source HUD grouping and fenced projection+quad tests passed (no headset).\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
