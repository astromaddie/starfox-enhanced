// Units that implement the cross-port Steam Frame VR standard on top of the
// vendored sfvr library: the [vr-perf] line, env overrides and refresh rate.
#include "starfox/vr/env_overrides.hpp"
#include "starfox/vr/perf_log.hpp"
#include "starfox/vr/refresh_rate.hpp"
#include "starfox/vr/startup_menu.hpp"
#include <cstring>
#include <vector>
#include <algorithm>
#include <limits>
#include <map>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
using namespace starfox::vr;
namespace {
void require(bool value,const std::source_location where=std::source_location::current()) {
    if(!value) throw std::runtime_error("VR standard assertion failed at line "+std::to_string(where.line()));
}
void perf_line() {
    PerfLog log;
    PerfLog::Frame frame;
    frame.logic_ms=1;frame.model_ms=2;frame.upload_ms=.5;frame.layer_ms=.25;
    frame.eye_ms={2.,4.};
    // 10 s window: no line until the window has elapsed.
    for(int i=0;i<10;++i) {log.add_frame(i*.5,frame);require(!log.poll(i*.5));}
    // The 11th frame lands after the window and is the one that closes it.
    auto line=log.poll(10.0);
    require(line.has_value());
    require(line->starts_with("[vr-perf] fps=1.0 missed=0 cpu=3.75ms logic=1.00 model=2.00 upload=0.50 layer=0.25 eye=2.00/4.00ms gpu=n/a"));
    require(!log.poll(10.1)); // Next window has started.
    // GPU is the left+right sum when both eyes have one; missed frames count.
    frame.gpu_ms={3.,4.5};frame.missed=true;
    log.add_frame(10.5,frame);
    line=log.poll(20.5);
    require(line && line->find("missed=1")!=std::string::npos && line->find("gpu=7.50ms")!=std::string::npos);
    // Unmeasured stages count as zero rather than poisoning the means.
    PerfLog partial;PerfLog::Frame sparse;partial.add_frame(0,sparse);
    line=partial.poll(10.0);
    require(line && line->find("cpu=0.00ms logic=0.00")!=std::string::npos);
    // A stalled window still reports: a stall is data.
    require(partial.poll(20.0)->starts_with("[vr-perf] fps=0.0"));
}
std::map<std::string,std::string> fake_env;
const char* fake_getenv(const char* name) {
    const auto found=fake_env.find(name);
    return found==fake_env.end()?nullptr:found->second.c_str();
}
void env_overrides() {
    fake_env.clear();
    require(!env_override_float("haptics",fake_getenv)); // Unset.
    fake_env["SFX_VR_HAPTICS"]="0.25";
    require(env_override_float("haptics",fake_getenv)==.25F);
    fake_env["SFX_VR_HAPTICS"]=" 0.8 ";
    require(env_override_float("haptics",fake_getenv)==.8F); // Whitespace ignored.
    fake_env["SFX_VR_HAPTICS"]="7";
    require(env_override_float("haptics",fake_getenv)==1.F); // Clamped to the registry range.
    fake_env["SFX_VR_HAPTICS"]="-1";
    require(env_override_float("haptics",fake_getenv)==0.F);
    for(const char* bad:{"","  ","loud","0.5x","nan","inf"}) {
        fake_env["SFX_VR_HAPTICS"]=bad;
        require(!env_override_float("haptics",fake_getenv));
    }
    fake_env["SFX_VR_TIMING_GPU"]="on";
    require(env_override_bool("timing_gpu",fake_getenv)==true);
    fake_env["SFX_VR_TIMING_GPU"]="0";
    require(env_override_bool("timing_gpu",fake_getenv)==false);
    fake_env["SFX_VR_TIMING_GPU"]="maybe";
    require(!env_override_bool("timing_gpu",fake_getenv));
    fake_env["SFX_VR_REFRESH_RATE"]="500";
    require(env_override_float("refresh_rate",fake_getenv)==144.F);
    // The variable name is the standard SFX_VR_<KEY>, from sfvr_settings_env_name.
    char name[64];
    require(sfvr_settings_env_name("SFX","haptics",name,sizeof name) && std::strcmp(name,"SFX_VR_HAPTICS")==0);
    // The menu: the override wins, is shown, and is never saved.
    StartupMenu menu;
    require(menu.haptics_strength()==.6F);
    menu.haptics_override=.3F;
    require(menu.haptics_strength()==.3F);
    menu.page=StartupMenu::Page::options;
    require(menu.labels()[9]=="HAPTICS STRENGTH: 30% ENV");
    require(menu.preferences()[27]==60);
    menu.haptics_override.reset();
    require(menu.labels()[9]=="HAPTICS STRENGTH: 60%");
}

std::vector<float> fake_rates{72.F,80.F,90.F,120.F};
std::vector<float> requested_rates;
float fake_current=0.F;
bool ignore_requests=false;
bool refuse_request=false;
XrResult XRAPI_PTR fake_enumerate(XrSession,uint32_t capacity,uint32_t* count,float* out) {
    *count=static_cast<uint32_t>(fake_rates.size());
    if(capacity) {
        if(capacity<fake_rates.size()) return XR_ERROR_SIZE_INSUFFICIENT;
        std::copy(fake_rates.begin(),fake_rates.end(),out);
    }
    return XR_SUCCESS;
}
XrResult XRAPI_PTR fake_get(XrSession,float* out) {*out=fake_current;return XR_SUCCESS;}
XrResult XRAPI_PTR fake_request(XrSession,float rate) {
    if(refuse_request) return XR_ERROR_RUNTIME_FAILURE;
    requested_rates.push_back(rate);if(!ignore_requests && rate>0) fake_current=rate;return XR_SUCCESS;
}
void refresh_rates() {
    const std::vector<float> offered{72.F,80.F,90.F,120.F};
    require(choose_refresh_rate(offered,90.F)==90.F);
    require(choose_refresh_rate(offered,100.F)==90.F); // Highest offered <= target.
    require(choose_refresh_rate(offered,200.F)==120.F);
    require(choose_refresh_rate(offered,72.F)==72.F);
    require(choose_refresh_rate(offered,60.F)==72.F); // None at or below: the closest above.
    require(!choose_refresh_rate({},90.F));
    const std::vector<float> junk{-1.F,0.F,std::numeric_limits<float>::quiet_NaN(),80.F};
    require(choose_refresh_rate(junk,90.F)==80.F);
    require(!RefreshApi{}.available());
    require(!RefreshApi::from_instance(XR_NULL_HANDLE).available());

    const RefreshApi api{fake_enumerate,fake_get,fake_request};
    XrSession session=reinterpret_cast<XrSession>(uintptr_t{2});
    // No extension: no request, a reason is kept.
    RefreshRate none;
    require(!none.request(session,90.F) && !none.requested() && !none.status().empty());
    require(!none.observe(100.0,true));

    fake_current=60.F;requested_rates.clear();
    RefreshRate refresh(api);
    require(refresh.request(session,RefreshRate::default_target));
    require(requested_rates==std::vector<float>{90.F} && refresh.requested()==90.F && refresh.current()==90.F);
    require(refresh.offered()==fake_rates);
    refuse_request=true;RefreshRate refused(api);
    require(!refused.request(session,90.F) && !refused.requested());
    refuse_request=false;
    fake_rates={72.F,80.F};requested_rates.clear();RefreshRate low_only(api);
    require(low_only.request(session,90.F) && requested_rates==std::vector<float>{80.F}); // Highest offered <= 90.
    fake_rates={72.F,80.F,90.F,120.F};

    // Governor: healthy windows never drop; first low window alone does not.
    RefreshRate governor(api);
    require(governor.request(session,90.F));requested_rates.clear();
    double t=1000.0;
    // Each phase is exactly 10 s of simulated time at `fps`, so the governor's
    // 10 s windows line up with the phases; a decision may land on any frame.
    const auto run_window=[&](double fps,bool focused=true) {
        std::optional<float> decision;
        const int frames=static_cast<int>(fps*10.0);
        const double start=t;
        for(int i=1;i<=frames;++i) {
            t=start+10.0*i/frames;
            if(const auto d=governor.observe(t,focused)) decision=d;
        }
        return decision;
    };
    require(!governor.observe(t,true)); // Starts the first window.
    require(!run_window(90));require(!run_window(89));require(!run_window(81.5)); // >= 90% of 90.
    require(!run_window(70));                        // One low window.
    require(!run_window(90));                        // Recovery resets the streak.
    require(!run_window(70));
    require(!run_window(60,false));                  // Unfocused: says nothing, resets.
    require(!run_window(70));
    const auto fallback=run_window(70);              // Second consecutive low focused window.
    require(fallback==80.F && !governor.fell_back());  // Next lower offered rate, not straight to 72.
    require(governor.request(session,*fallback) && requested_rates.back()==80.F && governor.current()==80.F);
    require(!governor.observe(t,true)); // A new request starts a fresh window.
    require(!run_window(60));
    const auto floor_step=run_window(60);
    require(floor_step==72.F && governor.fell_back());
    require(governor.request(session,*floor_step) && requested_rates.back()==72.F);
    for(int i=0;i<5;++i) require(!run_window(10)); // Nothing below the floor.
    // Choosing a rate again after reaching the floor governs again.
    require(governor.request(session,120.F) && !governor.fell_back() && governor.requested()==120.F);
    require(!governor.observe(t,true));require(!run_window(60));
    require(run_window(60)==90.F);

    // A 120 Hz target steps 120 -> 108 on the Frame's offered rates.
    fake_rates={72.F,80.F,90.F,96.F,108.F,120.F,144.F};fake_current=60.F;
    RefreshRate fast(api);require(fast.request(session,120.F) && fast.requested()==120.F);
    std::optional<float> step;
    for(int i=0;i<2*100*10+10 && !step;++i) {t+=.01;if(const auto d=fast.observe(t,true)) step=d;}
    require(step==108.F && !fast.fell_back());
    // A runtime that ignores the request: the next step goes below what was
    // asked for (108 -> 96), not back to the same 108.
    ignore_requests=true;require(fast.request(session,*step) && fast.requested()==108.F);
    step.reset();
    for(int i=0;i<2*100*10+10 && !step;++i) {t+=.01;if(const auto d=fast.observe(t,true)) step=d;}
    require(step==96.F);ignore_requests=false;
    // SYSTEM asks for no preference (0 Hz) and keeps current() reported.
    fast.release();require(!fast.requested() && requested_rates.back()==0.F);
    fake_current=144.F;for(int i=0;i<1200;++i) {t+=.01;require(!fast.observe(t,true));}
    require(fast.current()==144.F);
    fake_rates={72.F,80.F,90.F,120.F};

    // Already at or below the fallback: nothing to drop to.
    fake_rates={72.F};RefreshRate floor_rate(api);
    require(floor_rate.request(session,90.F) && floor_rate.requested()==72.F);
    std::optional<float> decision;
    for(int i=0;i<2000;++i) {t+=.05;if(const auto d=floor_rate.observe(t,true)) decision=d;}
    require(!decision && !floor_rate.fell_back());
    fake_rates={72.F,80.F,90.F,120.F};
}
}
void diagnostic_env() {
    // Unset: every diagnostic off, so the normal launch is unchanged.
    fake_env.clear();
    auto diag=diagnostic_overrides(fake_getenv);
    require(!diag.active() && !diag.force_render && diag.yaw_degrees==0 && !diag.autostart && !diag.exit_after_seconds);
    // Unrelated standard keys do not turn diagnostics on.
    fake_env["SFX_VR_HAPTICS"]="0.5";
    require(!diagnostic_overrides(fake_getenv).active());
    // TIMING_GPU does: it adds the eye pass segment timestamps.
    fake_env["SFX_VR_TIMING_GPU"]="1";diag=diagnostic_overrides(fake_getenv);
    require(diag.active() && diag.timing_gpu && diag.describe()=="[vr] diagnostic overrides: timing_gpu=1");
    fake_env["SFX_VR_TIMING_GPU"]="0";require(!diagnostic_overrides(fake_getenv).active());
    fake_env["SFX_VR_FORCE_RENDER"]="1";fake_env["SFX_VR_DIAG_YAW"]="90";
    fake_env["SFX_VR_AUTOSTART"]=" LEVEL1_1 ";fake_env["SFX_VR_EXIT_AFTER"]="60";
    diag=diagnostic_overrides(fake_getenv);
    require(diag.active() && diag.force_render && diag.yaw_degrees==90.F
        && diag.autostart==std::string("LEVEL1_1") && diag.exit_after_seconds==60.F);
    require(diag.describe()=="[vr] diagnostic overrides: force_render=1 diag_yaw=90 autostart=LEVEL1_1 exit_after=60s");
    // Off, garbage, out of range, zero or negative: each falls back on its own.
    fake_env["SFX_VR_FORCE_RENDER"]="0";fake_env["SFX_VR_DIAG_YAW"]="720";
    fake_env["SFX_VR_AUTOSTART"]="  ";fake_env["SFX_VR_EXIT_AFTER"]="0";
    diag=diagnostic_overrides(fake_getenv);
    require(!diag.force_render && diag.yaw_degrees==360.F && !diag.autostart && !diag.exit_after_seconds);
    require(diag.describe()=="[vr] diagnostic overrides: diag_yaw=360");
    fake_env["SFX_VR_FORCE_RENDER"]="please";fake_env["SFX_VR_DIAG_YAW"]="left";fake_env["SFX_VR_EXIT_AFTER"]="-5";
    require(!diagnostic_overrides(fake_getenv).active());
    fake_env["SFX_VR_EXIT_AFTER"]="1e9";
    require(diagnostic_overrides(fake_getenv).exit_after_seconds==1e9F); // Not a registry key: unclamped.
    char name[64];
    require(sfvr_settings_env_name("SFX","exit_after",name,sizeof name) && std::strcmp(name,"SFX_VR_EXIT_AFTER")==0);
    // SFX_VR_OVERLAP_EYES alone turns the line on; off or junk leaves it off.
    fake_env.clear();fake_env["SFX_VR_OVERLAP_EYES"]="1";
    diag=diagnostic_overrides(fake_getenv);
    require(diag.active() && diag.overlap_eyes && diag.describe()=="[vr] diagnostic overrides: overlap_eyes=1");
    for(const char* off:{"0","maybe",""}) {
        fake_env["SFX_VR_OVERLAP_EYES"]=off;require(!diagnostic_overrides(fake_getenv).overlap_eyes);
    }
    fake_env.clear();
}
void autostart_levels() {
    StartupMenu menu;
    menu.level_choices={{{0,11,12,13,21,31},{0,11,12,71}}};
    require(menu.level_choice("LEVEL1_1",false)==11U);
    require(menu.level_choice("level3_1",false)==31U); // Any case.
    require(menu.level_choice("LEVEL7_1",true)==71U);
    // Only the given cartridge's levels; OFF, junk and partial names miss.
    require(!menu.level_choice("LEVEL7_1",false) && !menu.level_choice("LEVEL2_1",true));
    for(const char* bad:{"","OFF","LEVEL0_0","LEVEL1_","LEVEL1_11","LEVEL11","INTROMAP"," LEVEL1_1"})
        require(!menu.level_choice(bad,false));
    // The names match what the level-select cheat shows.
    menu.selected_level=13;require(menu.level_choice(menu.level_name(),false)==13U);
    StartupMenu empty;require(!empty.level_choice("LEVEL1_1",false));
}
int main() try {
    perf_line();
    env_overrides();
    diagnostic_env();
    autostart_levels();
    refresh_rates();
    std::cout<<"VR standard units passed (perf line, env overrides, diagnostics, autostart, refresh rate)\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
