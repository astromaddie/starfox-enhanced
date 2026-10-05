#pragma once
#include "sfvr/sfvr_settings.h"
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

namespace starfox::vr {
// Standard env overrides (steam-frame-vr-port standard, section 7):
// `SFX_VR_<KEY>` for a canonical sfvr settings key, named by
// sfvr_settings_env_name("SFX", key). A variable that is unset, empty or
// unparseable is ignored; a numeric value is clamped to the key's registry
// range. Overrides win over the saved preference without being written to it.
//
// This port honours: haptics (SFX_VR_HAPTICS, 0..1), refresh_rate
// (SFX_VR_REFRESH_RATE, Hz) and timing_gpu (SFX_VR_TIMING_GPU, 0/1). Other
// registry keys are not implemented by this port and are ignored, except the
// unattended diagnostics below.
using EnvGetter=const char*(*)(const char*);
inline const char* process_getenv(const char* name) noexcept {return std::getenv(name);}

inline std::optional<std::string> env_override_text(const char* key,
    EnvGetter getter=process_getenv) {
    char name[64];
    if(!sfvr_settings_env_name("SFX",key,name,sizeof name)) return std::nullopt;
    const char* value=getter(name);
    if(!value) return std::nullopt;
    std::string text(value);
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return std::nullopt;
    const auto last=text.find_last_not_of(" \t\r\n");
    return text.substr(first,last-first+1);
}
inline std::optional<float> env_override_float(const char* key,
    EnvGetter getter=process_getenv) {
    const auto text=env_override_text(key,getter);
    if(!text) return std::nullopt;
    char* end{};
    const double value=std::strtod(text->c_str(),&end);
    if(end==text->c_str() || *end!='\0' || !std::isfinite(value)) return std::nullopt;
    return sfvr_settings_clamp_float(sfvr_settings_find(key),static_cast<float>(value));
}
inline std::optional<bool> env_override_bool(const char* key,
    EnvGetter getter=process_getenv) {
    const auto text=env_override_text(key,getter);
    if(!text) return std::nullopt;
    const int value=sfvr_settings_parse_bool(text->c_str(),-1);
    if(value<0) return std::nullopt;
    return value!=0;
}
// Unattended diagnostics, env only (never in the menu or the saved file):
// force_render (SFX_VR_FORCE_RENDER, 0/1) renders and plays in standby or
// unfocused; diag_yaw (SFX_VR_DIAG_YAW, degrees about +Y, -360..360) turns the
// rendered head; autostart (SFX_VR_AUTOSTART, e.g. LEVEL1_1) skips the startup
// menu; exit_after (SFX_VR_EXIT_AFTER, seconds in game, not a registry key)
// quits through QUIT TO STEAM; overlap_eyes (SFX_VR_OVERLAP_EYES, 0/1, not a
// registry key) submits eye 1 without waiting for eye 0's fence;
// visibility_mask (SFX_VR_VISIBILITY_MASK, 0/1, not a registry key) masks each
// eye's hidden area with XR_KHR_visibility_mask. Unset or unparseable leaves
// each one off. timing_gpu (SFX_VR_TIMING_GPU) is a standard
// key, but it also adds the eye pass segment timestamps, so it shows here too.
struct DiagnosticOverrides {
    bool force_render{},overlap_eyes{},timing_gpu{},visibility_mask{};
    float yaw_degrees{};
    std::optional<std::string> autostart;
    std::optional<float> exit_after_seconds;
    bool active() const noexcept {return force_render || yaw_degrees!=0 || autostart || exit_after_seconds || overlap_eyes || timing_gpu || visibility_mask;}
    std::string describe() const {
        std::ostringstream line;line<<"[vr] diagnostic overrides:";
        if(force_render) line<<" force_render=1";
        if(yaw_degrees!=0) line<<" diag_yaw="<<yaw_degrees;
        if(autostart) line<<" autostart="<<*autostart;
        if(exit_after_seconds) line<<" exit_after="<<*exit_after_seconds<<'s';
        if(overlap_eyes) line<<" overlap_eyes=1";
        if(timing_gpu) line<<" timing_gpu=1";
        if(visibility_mask) line<<" visibility_mask=1";
        return line.str();
    }
};
inline DiagnosticOverrides diagnostic_overrides(EnvGetter getter=process_getenv) {
    DiagnosticOverrides result;
    result.force_render=env_override_bool("force_render",getter).value_or(false);
    result.yaw_degrees=env_override_float("diag_yaw",getter).value_or(0.F);
    result.autostart=env_override_text("autostart",getter);
    if(const auto seconds=env_override_float("exit_after",getter); seconds && *seconds>0)
        result.exit_after_seconds=seconds;
    result.overlap_eyes=env_override_bool("overlap_eyes",getter).value_or(false);
    result.timing_gpu=env_override_bool("timing_gpu",getter).value_or(false);
    result.visibility_mask=env_override_bool("visibility_mask",getter).value_or(false);
    return result;
}
}
