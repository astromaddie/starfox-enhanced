#include "starfox/vr/scene_interpolation.hpp"
#include "starfox/vr/background_tiles.hpp"
#include <cmath>
#include <stdexcept>
namespace starfox::vr {
namespace {
// Source Q15 rotation bases contain small scale/orthogonality errors. Invert
// the actual authored basis instead of transposing and doubling those errors.
Matrix4 inverse_pilot_rotation(const Matrix4& rotation) {
    const double a=rotation[0],b=rotation[4],c=rotation[8];
    const double d=rotation[1],e=rotation[5],f=rotation[9];
    const double g=rotation[2],h=rotation[6],i=rotation[10];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!std::isfinite(determinant) || std::abs(determinant)<1e-6) return identity_matrix;
    auto out=identity_matrix;
    const double rows[]{e*i-f*h,c*h-b*i,b*f-c*e,
        f*g-d*i,a*i-c*g,c*d-a*f,d*h-e*g,b*g-a*h,a*e-b*d};
    for(unsigned r=0;r<3;++r) for(unsigned col=0;col<3;++col)
        out[col*4+r]=float(rows[r*3+col]/determinant);
    return out;
}
}
simulation::MatrixQ15 landscape_scene_view(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha) {
    return simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,alpha);
}
namespace {
// Pilot inputs already interpolated for one display time.
struct PilotPose {
    timing::RenderTransform player,camera;
    simulation::MatrixQ15 view{},rotation{};
};
bool pilot_continuous(const GameSceneSnapshot& previous,const GameSceneSnapshot& current) noexcept {
    const auto* old=previous.pilot_reference?&*previous.pilot_reference:nullptr;
    const auto& now=*current.pilot_reference;
    return old && previous.pilot_tracking && previous.player==current.player && previous.flow==current.flow
        && old->generation==now.generation && old->strategy_address==now.strategy_address
        && !timing::camera_transform_is_discontinuous(previous.camera,current.camera);
}
PilotPose linear_pilot_pose(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha) {
    const auto& now=*current.pilot_reference;
    const auto* old=previous.pilot_reference?&*previous.pilot_reference:nullptr;
    if(!std::isfinite(alpha) || !pilot_continuous(previous,current)) alpha=1;
    alpha=std::clamp(alpha,0.,1.);
    return {timing::interpolate(old?old->transform:now.transform,now.transform,alpha),
        timing::interpolate(previous.camera,current.camera,alpha),
        simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,alpha),
        simulation::interpolate_rotation_matrix_q15(old?old->rotation_matrix:now.rotation_matrix,now.rotation_matrix,alpha)};
}
double wrapped_delta(double to,double from) noexcept {
    auto d=std::fmod(to-from,65536.);
    if(d>32767.) d-=65536.;else if(d<-32768.) d+=65536.;
    return d;
}
timing::RenderTransform lerp_position(const timing::RenderTransform& a,const timing::RenderTransform& b,double t) noexcept {
    auto out=a;
    out.x=a.x+wrapped_delta(b.x,a.x)*t;out.y=a.y+wrapped_delta(b.y,a.y)*t;out.z=a.z+wrapped_delta(b.z,a.z)*t;
    return out;
}
timing::RenderTransform exact_position(const timing::TransformSnapshot& value) noexcept {
    return timing::interpolate(value,value,1);
}
// Uniform quadratic B-spline through three consecutive ticks, by de Boor:
// position and velocity stay continuous across ticks, half a tick behind the
// linear path. A cut before `previous` restarts from it; a cut before `current`
// jumps to it, as the linear path does.
PilotPose smoothed_pilot_pose(const GameSceneSnapshot& older,const GameSceneSnapshot& previous,
    const GameSceneSnapshot& current,double alpha) {
    if(!std::isfinite(alpha) || !pilot_continuous(previous,current)) return linear_pilot_pose(previous,current,1);
    alpha=std::clamp(alpha,0.,1.);
    const auto& first=older.pilot_reference && pilot_continuous(older,previous)?older:previous;
    const auto blend_position=[&](auto get) {
        const auto a=exact_position(get(first)),b=exact_position(get(previous)),c=exact_position(get(current));
        return lerp_position(lerp_position(a,b,.5+alpha/2),lerp_position(b,c,alpha/2),alpha);
    };
    const auto blend_rotation=[&](auto get) {
        using simulation::interpolate_rotation_matrix_q15;
        return interpolate_rotation_matrix_q15(interpolate_rotation_matrix_q15(get(first),get(previous),.5+alpha/2),
            interpolate_rotation_matrix_q15(get(previous),get(current),alpha/2),alpha);
    };
    return {blend_position([](const GameSceneSnapshot& s){return s.pilot_reference->transform;}),
        blend_position([](const GameSceneSnapshot& s){return s.camera;}),
        blend_rotation([](const GameSceneSnapshot& s){return s.view_matrix;}),
        blend_rotation([](const GameSceneSnapshot& s){return s.pilot_reference->rotation_matrix;})};
}
Matrix4 instrument_matrix(const PilotPose& pose,const PresentationPreferences& preferences) {
    auto out=identity_matrix;
    const auto authored=simulation::multiply_presentation_matrix_q15(pose.rotation,pose.view);
    // D * authored * D converts the ship-local +Y-down/+Z-forward basis to XR.
    for(unsigned c=0;c<3;++c) for(unsigned r=0;r<3;++r)
        out[c*4+r]=float(authored[c*3+r])/32768.F*((c==0)==(r==0)?1.F:-1.F);
    const float origin[]{preferences.origin_x*.01F,preferences.origin_y*.01F,preferences.origin_z*.01F};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) out[12+r]-=out[c*4+r]*origin[c];
    // Instruments are ship-local. The same inverse camera basis cancels their
    // authored bank/pitch/yaw, while retaining calibration and local head motion.
    if(preferences.follow_ship_rotation) out=multiply_matrix(inverse_pilot_rotation(out),out);
    return out;
}
Matrix4 scene_matrix(const PilotPose& pose,const PresentationPreferences& preferences) {
    auto out=identity_matrix;
    const float scale=cockpit_world_scale(preferences);out[0]=out[5]=out[10]=scale;
    // Calibration is in physical centimetres even when source world scale changes.
    const double local[]{(preferences.origin_x*.01+cockpit_seat_m[0])*256/scale,
        -(preferences.origin_y*.01+cockpit_seat_m[1])*256/scale,
        -(preferences.origin_z*.01+cockpit_seat_m[2])*256/scale};
    double point[]{pose.player.x,pose.player.y,pose.player.z};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) point[r]+=local[c]*pose.rotation[c*3+r]/32768.;
    const double origin[]{pose.camera.x,pose.camera.y,pose.camera.z};
    double delta[3]{};
    for(unsigned i=0;i<3;++i) delta[i]=wrapped_delta(point[i],origin[i]);
    for(unsigned r=0;r<3;++r) {
        double component{};for(unsigned c=0;c<3;++c) component+=delta[c]*pose.view[c*3+r]/32768.;
        out[12+r]=float(component/256.)*(r==0?-scale:scale);
    }
    if(preferences.follow_ship_rotation) {
        auto fixed=preferences;fixed.follow_ship_rotation=false;
        // Translate to the calibrated pilot first, then rotate the whole source
        // scene into the ship frame. Tracking is composed later by application.
        out=multiply_matrix(inverse_pilot_rotation(instrument_matrix(pose,fixed)),out);
    }
    return out;
}
// Scene content places world points as F * view * (X - camera) / 256 metres,
// F = diag(1,-1,-1) (interpolate_scene_poses + game_model_matrix).
Matrix4 content_basis(const simulation::MatrixQ15& view) noexcept {
    auto out=identity_matrix;
    for(unsigned k=0;k<3;++k) for(unsigned r=0;r<3;++r) out[k*4+r]=float(view[k*3+r])/32768.F*(r==0?1.F:-1.F);
    return out;
}
}
Matrix4 presentation_instrument_matrix(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,
    double alpha,const PresentationPreferences& preferences) {
    if(!pilot_view_active(current,preferences)) return identity_matrix;
    return instrument_matrix(linear_pilot_pose(previous,current,alpha),preferences);
}
Matrix4 presentation_scene_matrix(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,
    double alpha,const PresentationPreferences& preferences) {
    if(!pilot_view_active(current,preferences)) {
        auto out=identity_matrix;out[0]=out[5]=out[10]=preferences.scale();return out;
    }
    return scene_matrix(linear_pilot_pose(previous,current,alpha),preferences);
}
Matrix4 presentation_instrument_matrix(const GameSceneSnapshot& older,const GameSceneSnapshot& previous,
    const GameSceneSnapshot& current,double alpha,const PresentationPreferences& preferences) {
    if(!pilot_view_active(current,preferences)) return identity_matrix;
    return instrument_matrix(smoothed_pilot_pose(older,previous,current,alpha),preferences);
}
Matrix4 presentation_scene_matrix(const GameSceneSnapshot& older,const GameSceneSnapshot& previous,
    const GameSceneSnapshot& current,double alpha,const PresentationPreferences& preferences) {
    if(!pilot_view_active(current,preferences)) return presentation_scene_matrix(previous,current,alpha,preferences);
    const auto pose=smoothed_pilot_pose(older,previous,current,alpha);
    // Content stays on the linear source camera (interpolate_scene_poses uses
    // the same alpha rules); move it onto the smoothed camera before the pilot.
    double content_alpha=std::isfinite(alpha)?std::clamp(alpha,0.,1.):1.;
    if(previous.flow!=current.flow || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) content_alpha=1;
    const auto camera=timing::interpolate(previous.camera,current.camera,content_alpha);
    const auto view=simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,content_alpha);
    const auto smoothed=content_basis(pose.view);
    auto correction=multiply_matrix(smoothed,inverse_pilot_rotation(content_basis(view)));
    const double offset[]{wrapped_delta(camera.x,pose.camera.x),wrapped_delta(camera.y,pose.camera.y),wrapped_delta(camera.z,pose.camera.z)};
    for(unsigned r=0;r<3;++r) {
        double component{};for(unsigned c=0;c<3;++c) component+=smoothed[c*4+r]*offset[c];
        correction[12+r]=float(component/256.);
    }
    return multiply_matrix(scene_matrix(pose,preferences),correction);
}
std::optional<SteeringMatrix> cockpit_steering_matrix(const GameSceneSnapshot& scene,
    const PresentationPreferences& preferences) {
    if(!preferences.follow_ship_rotation || !pilot_view_active(scene,preferences) || scene.paused) return {};
    auto physical=preferences;physical.world_scale=0;
    physical.origin_x=physical.origin_y=physical.origin_z=0;
    const auto cockpit=presentation_scene_matrix(scene,scene,1,physical);
    auto source_view=identity_matrix;
    for(unsigned c=0;c<3;++c) for(unsigned r=0;r<3;++r)
        source_view[c*4+r]=float(scene.view_matrix[c*3+r])/32768.F*((c==0)==(r==0)?1.F:-1.F);
    // Native steering moves on source world X/Y, not ship-local X/Y. Project
    // those axes through the same source-view/cockpit basis as world geometry.
    auto axes=multiply_matrix(cockpit,source_view);
    // Remove the cockpit's uniform world enlargement; steering needs only direction.
    for(unsigned i:{0U,1U,4U,5U})axes[i]/=cockpit_world_scale(physical);
    const float determinant=axes[0]*axes[5]-axes[4]*axes[1];
    // The source movement plane has no unique 2D inverse when viewed edge-on.
    if(!std::isfinite(determinant) || std::abs(determinant)<1e-6F) return {};
    // Native Up increases world Y (screen down); C/D invert it. Conjugation
    // retains that vertical preference on the displayed axis, including bank.
    const float vertical=(scene.control_type&2U)?1.F:-1.F;
    return SteeringMatrix{axes[0],vertical*axes[1],vertical*axes[4],axes[5]};
}
Matrix4 landscape_camera_motion(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,double alpha) {
    if(!std::isfinite(alpha)) throw std::invalid_argument("Invalid landscape camera fraction");
    alpha=std::clamp(alpha,0.,1.);
    if(previous.flow!=current.flow || !same_landscape_mapping(previous,current)
        || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) alpha=1.;
    const auto view=landscape_scene_view(previous,current,alpha);
    Matrix4 motion{};motion[15]=1;
    constexpr int sign[]{1,-1,-1};
    for(unsigned column=0;column<3;++column) for(unsigned row=0;row<3;++row)
        motion[column*4+row]=float(view[column*3+row])*sign[column]*sign[row]/32768.F;
    const auto camera=timing::interpolate(previous.camera,current.camera,alpha);
    const float delta=float(camera.y-current.camera.y)/256.F;
    for(unsigned row=0;row<3;++row) motion[12+row]=motion[4+row]*delta;
    return motion;
}
std::vector<render::RenderPose> interpolate_scene_poses(const GameSceneSnapshot& previous,
    const GameSceneSnapshot& current,double alpha,const SceneInterpolationRules& rules,bool shadows) {
    if(!std::isfinite(alpha)) throw std::invalid_argument("Invalid scene interpolation fraction");
    alpha=std::clamp(alpha,0.,1.);
    if(previous.flow!=current.flow || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) alpha=1;
    auto camera=timing::interpolate(previous.camera,current.camera,alpha);
    // Preserve source tracking and cinematic camera motion for every object.
    auto view=simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,alpha);
    const auto delta=[](double value,double origin) {
        auto d=std::fmod(value-origin,65536.);if(d>32767.) d-=65536.;else if(d< -32768.) d+=65536.;return d;
    };
    std::vector<render::RenderPose> poses;poses.reserve(current.objects.size());
    for(const auto& item:current.objects) {
        auto before=item.presentation,now=item.presentation;
        const auto old=previous.transforms.find(item.handle);
        if(old!=previous.transforms.end() && old->second.generation==now.generation
            && old->second.shape==now.shape && old->second.type==now.type && old->second.strategy_address==now.strategy_address)
            before=old->second;
        if(rules.crosshair && item.object.strategy_address==rules.crosshair) {
            const auto* station=render::reticle_previous_snapshot(now,current.transforms,previous.transforms,current.player);
            if(station) before=*station;
            else {
                before=now;
                const auto owner=current.transforms.find(current.player),old_owner=previous.transforms.find(current.player);
                if(owner!=current.transforms.end() && old_owner!=previous.transforms.end()) {
                    before.transform=timing::relative_birth_snapshot(now.transform,old_owner->second.transform,owner->second.transform);
                    before.rotation_matrix=old_owner->second.rotation_matrix;
                }
            }
        }
        if(rules.flash_player && item.object.strategy_address==rules.flash_player)
            render::anchor_player_overlay(before,now,previous.transforms,current.transforms,current.player);
        const auto object_alpha=rules.trail && item.object.strategy_address==rules.trail?1.:alpha;
        auto transform=timing::interpolate(before.transform,now.transform,object_alpha);
        if(rules.crosshair && item.object.strategy_address==rules.crosshair)
            transform.y+=std::lerp(double(previous.view_float_y),double(current.view_float_y),alpha);
        auto rotation=render::interpolate_object_rotation(before,now,object_alpha,rules.discrete_rotation_shape);
        auto pose=item.source_pose;
        pose.explosion_phase=render::interpolate_explosion_progress(
            old==previous.transforms.end()?nullptr:&old->second,now,object_alpha);
        if(shadows) {
            rotation[1]=rotation[4]=rotation[7]=0;
            auto source_rotation=item.presentation.rotation_matrix;
            source_rotation[1]=source_rotation[4]=source_rotation[7]=0;
            pose.source_lighting_matrix=simulation::multiply_matrix_q15(source_rotation,current.view_matrix);
            pose.simple_scaled_sprite=false;
            if(!(item.object.strategy_flags[0]&4U)) {
                transform.y=current.shadow_height;
                pose.force_colour=true;pose.forced_colour=9;
            }
        }
        const auto x=delta(transform.x,camera.x),y=delta(transform.y,camera.y),z=delta(transform.z,camera.z);
        pose.x=(x*view[0]+y*view[3]+z*view[6])/32768.;
        pose.y=(x*view[1]+y*view[4]+z*view[7])/32768.;
        pose.z=(x*view[2]+y*view[5]+z*view[8])/32768.;
        pose.rotation_matrix=alpha>0 && alpha<1?simulation::multiply_presentation_matrix_q15(rotation,view)
            :simulation::multiply_matrix_q15(rotation,view);
        pose.use_rotation_matrix=true;
        pose.continuous_geometry=object_alpha!=1 || !(rules.trail && item.object.strategy_address==rules.trail);
        pose.subpixel_projection=object_alpha>0 && object_alpha<1;
        poses.push_back(pose);
    }
    return poses;
}
}
