#pragma once
#include "starfox/vr/game_scene.hpp"
#include "starfox/vr/background_tiles.hpp"
#include "starfox/vr/game_input.hpp"
namespace starfox::vr {
simulation::MatrixQ15 landscape_scene_view(const GameSceneSnapshot&,const GameSceneSnapshot&,double alpha);
// Changes presentation space only. Scripted strategies retain source camera.
Matrix4 presentation_instrument_matrix(const GameSceneSnapshot&,const GameSceneSnapshot&,
    double alpha,const PresentationPreferences&);
Matrix4 presentation_scene_matrix(const GameSceneSnapshot&,const GameSceneSnapshot&,
    double alpha,const PresentationPreferences&);
// Evaluate once per source input consumption from the completed source state,
// never from XR time, interpolated display orientation, or physical head pose.
std::optional<SteeringMatrix> cockpit_steering_matrix(const GameSceneSnapshot&,const PresentationPreferences&);
Matrix4 landscape_camera_motion(const GameSceneSnapshot&,const GameSceneSnapshot&,double alpha);
struct SceneInterpolationRules {
    uint32_t trail{},flash_player{},crosshair{};
    uint16_t discrete_rotation_shape{};
    bool fixed_landscape_height{};
};
// Poses align with current.objects. Only geometry changes: source lighting,
// LOD depth, material state and source animation frames remain current.
std::vector<render::RenderPose> interpolate_scene_poses(const GameSceneSnapshot& previous,
    const GameSceneSnapshot& current,double alpha,const SceneInterpolationRules&,bool shadows=false);
}
