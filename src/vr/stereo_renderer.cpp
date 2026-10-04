#include "starfox/vr/stereo_renderer.hpp"
namespace starfox::vr {
StereoRenderer::Result StereoRenderer::step(const DrawEye& draw,
    float units, float near_plane, std::optional<float> far_plane) {
    return step_async([&](unsigned eye,uint32_t image,const EyeCamera& camera,XrTime time) {
        try {return draw && draw(eye,image,camera,time)?EyeResult::complete:EyeResult::failed;}
        catch(...) {return EyeResult::failed;}
    },units,near_plane,far_plane);
}
StereoRenderer::Result StereoRenderer::step_async(const AsyncDrawEye& draw,
    float units,float near_plane,std::optional<float> far_plane,const Composition& composition) {
    if(fatal_) return Result::error;
    if(!frame_) {
        if(!session_.poll_events()) return Result::error;
        if(!session_.running() || session_.exit_requested()) return Result::idle;
        frame_=session_.begin_frame();
        if(!frame_) return Result::error;
        next_eye_=0;cancelling_=false;display_period_=frame_->display_period;forced_=frame_->forced;
        overlap_=overlap_requested_;next_retired_=0;in_flight_={};
        if(frame_->tracking_origin_changed || recenter_pending_) {
            const bool keep_height=recenter_pending_ && !recenter_height_pending_
                && !frame_->tracking_origin_changed;
            position_anchor_.reset(recenter_pending_,keep_height);
            recenter_pending_=recenter_height_pending_=false;
        }
        if(!frame_->should_render) {
            frame_.reset();
            return session_.end_frame()?Result::skipped:Result::error;
        }
        auto scene_views=frame_->views;
        if(anchor_position_) {
            if(!position_anchor_.apply(scene_views,head_translation_)) cancelling_=true;
        }
        rotate_views_yaw(scene_views,diag_yaw_);
        for(unsigned eye=0;eye<2 && !cancelling_;++eye) {
            const auto camera=eye_camera(scene_views[eye],units,near_plane,far_plane);
            if(!camera) {cancelling_=true;break;}
            cameras_[eye]=*camera;
        }
        if(!images_.start_frame(*frame_,session_.space())) cancelling_=true;
    }
    if(overlap_) {
        if(const auto result=overlap_eyes(draw)) return *result;
    } else while(!cancelling_ && next_eye_<2) {
        const auto ready=images_.acquire_eye(next_eye_);
        if(ready==ImageWait::waiting) return Result::waiting;
        if(ready==ImageWait::error) {cancelling_=true;break;}
        auto rendered=EyeResult::failed;
        try {
            if(draw) rendered=draw(next_eye_,*images_.image_index(next_eye_),
                cameras_[next_eye_],frame_->display_time);
        } catch(...) {rendered=EyeResult::fatal;}
        if(rendered==EyeResult::fatal) {fatal_=true;return Result::error;}
        if(rendered==EyeResult::pending || rendered==EyeResult::submitted) return Result::waiting;
        if(rendered==EyeResult::failed || !images_.release_eye(next_eye_)) {cancelling_=true;break;}
        ++next_eye_;
    }
    std::vector<const XrCompositionLayerBaseHeader*> extra_layers;
    if(!cancelling_ && composition.draw) {
        EyeResult result=EyeResult::fatal;
        try {result=composition.draw(*frame_,extra_layers);} catch(...) {}
        if(result==EyeResult::fatal) {fatal_=true;return Result::error;}
        if(result==EyeResult::pending) return Result::waiting;
        if(result==EyeResult::failed) cancelling_=true;
    }
    if(cancelling_) {
        if(composition.cancel) {
            const auto state=composition.cancel();
            if(state==ImageWait::waiting) return Result::waiting;
            if(state==ImageWait::error) {fatal_=true;return Result::error;}
        }
        const auto cancelled=images_.cancel_frame();
        if(cancelled==ImageWait::waiting) return Result::waiting;
        if(cancelled==ImageWait::error) return Result::error;
        frame_.reset();session_.end_frame();return Result::error;
    }
    const auto* projection=images_.projection();
    if(!projection) {
        frame_.reset();session_.end_frame();return Result::error;
    }
    std::vector<const XrCompositionLayerBaseHeader*> layers{
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(projection)};
    layers.insert(layers.end(),extra_layers.begin(),extra_layers.end());
    frame_.reset();
    return session_.end_frame(layers)?Result::submitted:Result::error;
}
std::optional<StereoRenderer::Result> StereoRenderer::overlap_eyes(const AsyncDrawEye& draw) {
    const auto call=[&](unsigned eye) {
        try {return draw?draw(eye,*images_.image_index(eye),cameras_[eye],frame_->display_time):EyeResult::failed;}
        catch(...) {return EyeResult::fatal;}
    };
    while(!cancelling_ && next_eye_<2) {
        // Bounded image wait while eye 0 is in flight: the caller adds no pause then.
        const auto ready=images_.acquire_eye(next_eye_,next_eye_==1 && in_flight_[0]?1000000:0);
        if(ready==ImageWait::waiting) return Result::waiting;
        if(ready==ImageWait::error) {cancelling_=true;break;}
        const auto rendered=call(next_eye_);
        if(rendered==EyeResult::fatal) {fatal_=true;return Result::error;}
        if(rendered==EyeResult::pending) return Result::waiting;
        if(rendered==EyeResult::failed) {cancelling_=true;break;}
        in_flight_[next_eye_++]=rendered==EyeResult::submitted;
    }
    // Observe fences and release in eye order. A cancelled frame still drains
    // its submitted eyes, so cancel_frame never returns an image in GPU use.
    while(next_retired_<next_eye_) {
        if(in_flight_[next_retired_]) {
            const auto rendered=call(next_retired_);
            if(rendered==EyeResult::fatal) {fatal_=true;return Result::error;}
            if(rendered==EyeResult::pending || rendered==EyeResult::submitted) return Result::waiting;
            in_flight_[next_retired_]=false;
            if(rendered==EyeResult::failed) cancelling_=true;
        }
        if(!cancelling_ && !images_.release_eye(next_retired_)) cancelling_=true;
        ++next_retired_;
    }
    return std::nullopt;
}
CompositionLayers::EyeResult CompositionLayers::draw(const StereoFrame& frame,XrSpace space,bool pointer,
    const Quad& quad,const PointerEye& eye,std::vector<const XrCompositionLayerBaseHeader*>& layers) {
    auto& p=progress_;
    if(!p.started) {p.started=true;p.pointer=pointer;}
    if(!p.quad_done) {
        const XrCompositionLayerBaseHeader* layer{};
        const auto result=quad?quad(frame,layer):EyeResult::complete;
        if(result!=EyeResult::complete) return result;
        p.quad=layer;p.quad_done=true;
    }
    if(p.quad) layers.push_back(p.quad);
    if(p.pointer) {
        if(!p.pointer_started) {
            if(!pointer_.start_frame(frame,space)) return EyeResult::failed;
            p.pointer_started=true;
        }
        for(;p.pointer_eye<2;++p.pointer_eye) {
            const auto ready=pointer_.acquire_eye(p.pointer_eye);
            if(ready==ImageWait::waiting) return EyeResult::pending;
            if(ready==ImageWait::error || !eye) return EyeResult::failed;
            const auto result=eye(p.pointer_eye,*pointer_.image_index(p.pointer_eye),frame.display_time);
            if(result==EyeResult::submitted) return EyeResult::pending;
            if(result!=EyeResult::complete) return result;
            if(!pointer_.release_eye(p.pointer_eye)) return EyeResult::failed;
        }
        const auto* projection=pointer_.projection();
        if(!projection) return EyeResult::failed;
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(projection));
    }
    p={};return EyeResult::complete;
}
ImageWait CompositionLayers::cancel(const std::function<ImageWait()>& quad) {
    progress_={};
    // Both always run: an idle pointer swapchain holds nothing and returns ready.
    const auto a=quad?quad():ImageWait::ready;
    const auto b=pointer_.cancel_frame();
    if(a==ImageWait::error || b==ImageWait::error) return ImageWait::error;
    return a==ImageWait::waiting || b==ImageWait::waiting?ImageWait::waiting:ImageWait::ready;
}
}
