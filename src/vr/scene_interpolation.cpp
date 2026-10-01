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
Matrix4 presentation_instrument_matrix(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,
    double alpha,const PresentationPreferences& preferences) {
    auto out=identity_matrix;
    if(!pilot_view_active(current,preferences)) return out;
    const auto& now=*current.pilot_reference;
    const auto* old=previous.pilot_reference?&*previous.pilot_reference:nullptr;
    if(!std::isfinite(alpha) || !old || !previous.pilot_tracking || previous.player!=current.player
        || previous.flow!=current.flow || old->generation!=now.generation
        || old->strategy_address!=now.strategy_address
        || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) alpha=1;
    alpha=std::clamp(alpha,0.,1.);
    const auto rotation=simulation::interpolate_rotation_matrix_q15(old?old->rotation_matrix:now.rotation_matrix,now.rotation_matrix,alpha);
    const auto view=simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,alpha);
    const auto authored=simulation::multiply_presentation_matrix_q15(rotation,view);
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
Matrix4 presentation_scene_matrix(const GameSceneSnapshot& previous,const GameSceneSnapshot& current,
    double alpha,const PresentationPreferences& preferences) {
    auto out=identity_matrix;
    const float scale=preferences.scale();out[0]=out[5]=out[10]=scale;
    if(!pilot_view_active(current,preferences)) return out;
    const auto& now=*current.pilot_reference;
    const auto* old=previous.pilot_reference?&*previous.pilot_reference:nullptr;
    if(!std::isfinite(alpha) || !old || !previous.pilot_tracking || previous.flow!=current.flow
        || previous.player!=current.player || old->generation!=now.generation
        || old->strategy_address!=now.strategy_address
        || timing::camera_transform_is_discontinuous(previous.camera,current.camera)) alpha=1;
    alpha=std::clamp(alpha,0.,1.);
    const auto player=timing::interpolate(old?old->transform:now.transform,now.transform,alpha);
    const auto camera=timing::interpolate(previous.camera,current.camera,alpha);
    const auto view=simulation::interpolate_rotation_matrix_q15(previous.view_matrix,current.view_matrix,alpha);
    const auto rotation=simulation::interpolate_rotation_matrix_q15(old?old->rotation_matrix:now.rotation_matrix,now.rotation_matrix,alpha);
    // Calibration is in physical centimetres even when source world scale changes.
    const double local[]{preferences.origin_x*2.56/scale,-preferences.origin_y*2.56/scale,-preferences.origin_z*2.56/scale};
    double point[]{player.x,player.y,player.z};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) point[r]+=local[c]*rotation[c*3+r]/32768.;
    const double origin[]{camera.x,camera.y,camera.z};
    double delta[3]{};
    for(unsigned i=0;i<3;++i) {
        delta[i]=std::fmod(point[i]-origin[i],65536.);
        if(delta[i]>32767.) delta[i]-=65536.;else if(delta[i]<-32768.) delta[i]+=65536.;
    }
    for(unsigned r=0;r<3;++r) {
        double component{};for(unsigned c=0;c<3;++c) component+=delta[c]*view[c*3+r]/32768.;
        out[12+r]=float(component/256.)*(r==0?-scale:scale);
    }
    if(preferences.follow_ship_rotation) {
        auto fixed=preferences;fixed.follow_ship_rotation=false;
        const auto ship=presentation_instrument_matrix(previous,current,alpha,fixed);
        // Translate to the calibrated pilot first, then rotate the whole source
        // scene into the ship frame. Tracking is composed later by application.
        out=multiply_matrix(inverse_pilot_rotation(ship),out);
    }
    return out;
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
    const auto axes=multiply_matrix(cockpit,source_view);
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
