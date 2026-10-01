#include "starfox/vr/openxr_input.hpp"
#include "starfox/vr/startup_menu.hpp"
#include "starfox/vr/game_input.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <source_location>
#include <vector>
using namespace starfox::vr;
namespace {
void require(bool value,const std::source_location where=std::source_location::current()) {if(!value) throw std::runtime_error("VR input assertion failed at line "+std::to_string(where.line()));}
template<class T>T handle(uintptr_t n) {return reinterpret_cast<T>(n);}
unsigned created{},destroyed{},suggestions{},syncs{},fail_create{};
bool fail_read{},active=true,held{},attach_failed{},unsupported{};
bool independent_buttons{};std::array<bool,18> button_states{};
unsigned haptic_applies{},haptic_stops{};bool fail_haptic{},fail_haptic_stop{};
float haptic_amplitude{};XrDuration haptic_duration{};
bool haptic_api_valid=true;
XrResult sync_result=XR_SUCCESS;XrVector2f axis{};
std::array<XrPath,2> mock_hands{},mock_profiles{};
XrResult XRAPI_PTR create_set(XrInstance,const XrActionSetCreateInfo* info,XrActionSet* out) {
    require(std::strcmp(info->actionSetName,"starfox")==0);*out=handle<XrActionSet>(1);return XR_SUCCESS;
}
XrResult XRAPI_PTR destroy_set(XrActionSet) {++destroyed;return XR_SUCCESS;}
XrResult XRAPI_PTR create_action(XrActionSet,const XrActionCreateInfo* info,XrAction* out) {
    ++created;if(fail_create && created==fail_create) return XR_ERROR_RUNTIME_FAILURE;
    const unsigned index=(created-1)%18;
    const auto expected_type=(index==9 || index==10)?XR_ACTION_TYPE_POSE_INPUT
        :index==0?XR_ACTION_TYPE_VECTOR2F_INPUT
        :index==17?XR_ACTION_TYPE_VIBRATION_OUTPUT:XR_ACTION_TYPE_BOOLEAN_INPUT;
    require(info->actionType==expected_type);
    if(index==17) require(info->countSubactionPaths==2);
    *out=handle<XrAction>(index+1);return XR_SUCCESS;
}
std::vector<std::string> paths;
XrResult XRAPI_PTR path(XrInstance,const char* name,XrPath* out) {
    require(name[0]=='/');paths.emplace_back(name);*out=paths.size();
    if(std::strcmp(name,"/user/hand/left")==0) mock_hands[0]=*out;
    if(std::strcmp(name,"/user/hand/right")==0) mock_hands[1]=*out;
    return XR_SUCCESS;
}
XrPath path_value(const char* name) {
    for(std::size_t i=paths.size();i>0;--i)
        if(paths[i-1]==name) return static_cast<XrPath>(i);
    return XR_NULL_PATH;
}
XrResult XRAPI_PTR suggest(XrInstance,const XrInteractionProfileSuggestedBinding* info) {
    ++suggestions;require(info->countSuggestedBindings==8
        || info->countSuggestedBindings==15 || info->countSuggestedBindings==19);
    if(info->countSuggestedBindings==19) {
        require(paths.at(info->interactionProfile-1)
            == "/interaction_profiles/valve/frame_controller_valve");
        const std::array<std::pair<unsigned,const char*>,19> expected{{
            {0,"/user/hand/left/input/thumbstick"},
            {1,"/user/hand/right/input/a/click"},
            {2,"/user/hand/right/input/b/click"},
            {3,"/user/hand/right/input/x/click"},
            {4,"/user/hand/right/input/y/click"},
            {5,"/user/hand/right/input/menu/click"},
            {6,"/user/hand/left/input/bumper/click"},
            {7,"/user/hand/right/input/bumper/click"},
            {8,"/user/hand/left/input/view/click"},
            {11,"/user/hand/left/input/thumbstick/click"},
            {12,"/user/hand/right/input/thumbstick/click"},
            {9,"/user/hand/left/input/aim/pose"},
            {10,"/user/hand/right/input/aim/pose"},
            {13,"/user/hand/left/input/dpad_up/click"},
            {14,"/user/hand/left/input/dpad_down/click"},
            {15,"/user/hand/left/input/dpad_left/click"},
            {16,"/user/hand/left/input/dpad_right/click"},
            {17,"/user/hand/left/output/haptic"},
            {17,"/user/hand/right/output/haptic"}}};
        for(unsigned i=0;i<info->countSuggestedBindings;++i) {
            const auto& binding=info->suggestedBindings[i];
            const auto action=static_cast<unsigned>(reinterpret_cast<uintptr_t>(binding.action)-1);
            const auto& name=paths.at(binding.binding-1);
            // The Frame profile has dedicated View/Menu actions. It must not
            // inherit the Touch left-grip Select / right-squeeze Menu aliases.
            require(name!="/user/hand/left/input/squeeze/value"
                && name!="/user/hand/right/input/squeeze/value");
            require(action==expected[i].first && name==expected[i].second);
        }
    } else if(info->countSuggestedBindings==15) {
        const auto& profile=paths.at(info->interactionProfile-1);
        const bool index=profile=="/interaction_profiles/valve/index_controller";
        require(index || profile=="/interaction_profiles/oculus/touch_controller");
        const std::array<const char*,15> expected{
            "/user/hand/left/input/thumbstick", "/user/hand/right/input/a/click",
            "/user/hand/right/input/b/click",
            index?"/user/hand/left/input/a/click":"/user/hand/left/input/x/click",
            index?"/user/hand/left/input/b/click":"/user/hand/left/input/y/click",
            "/user/hand/right/input/squeeze/value",
            index?"/user/hand/left/input/trigger/click":"/user/hand/left/input/trigger/value",
            index?"/user/hand/right/input/trigger/click":"/user/hand/right/input/trigger/value",
            "/user/hand/left/input/squeeze/value", "/user/hand/left/input/aim/pose",
            "/user/hand/right/input/aim/pose", "/user/hand/left/input/thumbstick/click",
            "/user/hand/right/input/thumbstick/click",
            "/user/hand/left/output/haptic",
            "/user/hand/right/output/haptic"};
        std::array<bool,13> seen{};
        for(unsigned i=0;i<info->countSuggestedBindings;++i) {
            const auto& binding=info->suggestedBindings[i];const auto& name=paths.at(binding.binding-1);
            const auto action=reinterpret_cast<uintptr_t>(binding.action)-1;
            if(i>=13U) require(action==17U && name==expected[i]);
            else {
                require(action<13U);require(!seen[action]);seen[action]=true;
                require(name==expected[action]);
            }
        }
    } else {
        require(paths.at(info->interactionProfile-1)
            =="/interaction_profiles/khr/simple_controller");
        require(info->suggestedBindings[6].action==handle<XrAction>(18)
            && paths.at(info->suggestedBindings[6].binding-1)=="/user/hand/left/output/haptic");
        require(info->suggestedBindings[7].action==handle<XrAction>(18)
            && paths.at(info->suggestedBindings[7].binding-1)=="/user/hand/right/output/haptic");
    }
    return unsupported?XR_ERROR_PATH_UNSUPPORTED:XR_SUCCESS;
}
XrResult XRAPI_PTR attach(XrSession,const XrSessionActionSetsAttachInfo* info) {require(info->countActionSets==1);return attach_failed?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;}
XrResult XRAPI_PTR sync(XrSession,const XrActionsSyncInfo* info) {++syncs;require(info->countActiveActionSets==1);return sync_result;}
XrResult XRAPI_PTR boolean(XrSession,const XrActionStateGetInfo* info,XrActionStateBoolean* out) {
    if(fail_read && info->action==handle<XrAction>(6)) return XR_ERROR_RUNTIME_FAILURE;
    const auto action=reinterpret_cast<uintptr_t>(info->action);
    out->isActive=active;
    out->currentState=action>=14 && action<=17
        ?independent_buttons && button_states.at(action)
        :independent_buttons?button_states.at(action):held;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR vector(XrSession,const XrActionStateGetInfo* info,XrActionStateVector2f* out) {
    require(info->action==handle<XrAction>(1));out->isActive=active;out->currentState=axis;return XR_SUCCESS;
}
XrResult XRAPI_PTR current_profile(XrSession,XrPath hand,XrInteractionProfileState* out) {
    const auto index=hand==mock_hands[0]?0U:hand==mock_hands[1]?1U:2U;
    require(index<2U);out->interactionProfile=mock_profiles[index];return XR_SUCCESS;
}
XrResult XRAPI_PTR apply_haptic(XrSession,const XrHapticActionInfo* info,
    const XrHapticBaseHeader* header) {
    haptic_api_valid &= info->action==handle<XrAction>(18);
    const auto* vibration=reinterpret_cast<const XrHapticVibration*>(header);
    haptic_api_valid &= vibration->frequency==XR_FREQUENCY_UNSPECIFIED;
    haptic_amplitude=vibration->amplitude;haptic_duration=vibration->duration;
    ++haptic_applies;
    return fail_haptic?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult XRAPI_PTR stop_haptic(XrSession,const XrHapticActionInfo* info) {
    haptic_api_valid &= info->action==handle<XrAction>(18);++haptic_stops;
    return fail_haptic_stop?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult XRAPI_PTR create_space(XrSession,const XrActionSpaceCreateInfo* info,XrSpace* out) {
    require(info->poseInActionSpace.orientation.w==1);
    *out=handle<XrSpace>(1);return XR_SUCCESS;
}
XrResult XRAPI_PTR destroy_space(XrSpace) {return XR_SUCCESS;}
bool pointer_tracked=true;
XrResult XRAPI_PTR locate(XrSpace,XrSpace,XrTime,XrSpaceLocation* out) {
    out->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if(pointer_tracked) out->locationFlags|=XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    out->pose.orientation.w=1;return XR_SUCCESS;
}
}
int main() try {
    {
        StartupMenu menu;using Page=StartupMenu::Page;
        VrControls press;press.fire=true;
        const auto click=[&] {menu.sample({},true);menu.sample(press,true);};
        require(menu.labels()[0].find("EXPERIENCE")==0 && menu.labels()[3]=="OPTIONS" && menu.labels()[4]=="START GAME");
        menu.selection=1;click();require(!menu.unlocked_pace);menu.sample(press,true);require(!menu.unlocked_pace);
        menu.selection=2;click();require(!menu.msu_music && menu.labels()[2]=="MSU-1 MUSIC: NOT FOUND");
        menu.msu_available=true;click();require(menu.msu_music);
        menu.sample(press,true);require(menu.msu_music);
        click();require(!menu.msu_music);
        menu.selection=3;click();require(menu.page==Page::options && menu.selection==0);
        menu.sample(press,true);require(menu.page==Page::options);
        click();require(menu.page==Page::cheats);
        require(menu.labels()[0].find("GOD MODE")==0 && menu.labels()[1].find("LEVEL SELECT")==0 && menu.labels()[6]=="BACK");
        click();require(menu.god_mode);menu.sample(press,true);require(menu.god_mode);
        menu.selection=2;
        for(unsigned expected:{1U,2U,0U}) {click();require(menu.default_laser==expected);menu.sample(press,true);require(menu.default_laser==expected);}
        menu.selection=1;menu.level_choices[0]={0,11,21};click();require(menu.selected_level==11);
        menu.selection=3;click();require(menu.infinite_bombs);
        menu.selection=4;click();require(menu.infinite_boost);
        menu.selection=5;click();require(menu.infinite_lives);
        menu.sample(press,true);require(menu.infinite_lives);
        menu.selection=6;click();require(menu.page==Page::options && menu.open);
        menu.selection=1;click();require(menu.crosshair_colour==1);
        menu.selection=2;click();require(menu.swap_face_buttons);
        menu.sample(press,true);require(menu.swap_face_buttons);
        VrControls original;original.fire=true;original.bomb=true;original.menu=true;
        original.steer={.25F,-.5F};
        auto swapped=menu.gameplay_controls(original);
        require(!swapped.fire && !swapped.bomb && swapped.boost && swapped.brake && swapped.menu);
        require(swapped.steer.x==original.steer.x && swapped.steer.y==original.steer.y);
        click();require(!menu.swap_face_buttons);
        require(menu.gameplay_controls(original).fire && menu.gameplay_controls(original).bomb);
        menu.selection=3;click();require(menu.music_volume==0);
        click();require(menu.music_volume==10);
        menu.selection=4;click();require(menu.sfx_volume==0);
        menu.selection=5;click();require(menu.language==1);
        require(menu.localized_labels()[5]==U"言語: 日本語");
        require(menu.first_visible_row()==0);
        menu.selection=6;require(menu.first_visible_row()==1);
        menu.selection=7;require(menu.first_visible_row()==2);
        menu.selection=8;require(menu.first_visible_row()==3);
        VrControls down;down.steer.y=-1;
        menu.sample({},true);menu.sample(down,true);require(menu.selection==9);
        menu.sample(down,true);require(menu.selection==9);
        VrControls up;up.steer.y=1;
        menu.sample({},true);menu.sample(up,true);require(menu.selection==8);
        for(unsigned language=0;language<6;++language) {
            menu.language=language;
            for(const auto page:{Page::main,Page::options,Page::cheats,Page::three_d,Page::two_d}) {
                menu.page=page;
                require(menu.localized_labels().size()==menu.row_count());
                for(const auto& label:menu.localized_labels()) require(!label.empty());
                if(menu.language>=1 && menu.language<=4 && (page==Page::two_d || page==Page::three_d))
                    require(!menu.localized_labels().front().starts_with(U"WORLD EFFECT")
                        && !menu.localized_labels().front().starts_with(U"MODEL EFFECT"));
            }
        }
        menu.page=Page::options;menu.selection=6;click();require(menu.page==Page::three_d && !menu.ray_tracing);
        require(menu.row_count()==4 && menu.labels().back()=="BACK" && !menu.preview);
        click();require(menu.model_effect==1 && !menu.ray_tracing);
        menu.selection=2;click();require(menu.preview);menu.sample(press,true);require(menu.preview);
        click();require(!menu.preview);
        menu.selection=3;click();require(!menu.ray_tracing && menu.page==Page::options);
        menu.ray_tracing_available=true;click();require(menu.page==Page::three_d);
        menu.selection=3;click();require(menu.ray_tracing);
        menu.sample(press,true);require(menu.ray_tracing);
        menu.selection=4;click();require(menu.page==Page::options && menu.selection==6);
        menu.selection=7;click();require(menu.page==Page::two_d);
        click();require(menu.world_effect==4);
        menu.selection=1;click();require(menu.world_intensity==0);
        menu.selection=2;click();require(menu.preview);
        menu.selection=3;click();require(menu.enhanced_sky && menu.page==Page::two_d && menu.row_count()==5);
        menu.sample(press,true);require(menu.enhanced_sky);
        menu.selection=4;click();require(menu.page==Page::options);
        menu.selection=8;click();require(menu.steer_sensitivity_index==1
            && menu.labels()[8]=="STICK SENSITIVITY: 40%");
        VrControls steer;steer.steer={1.F,-.5F};
        const auto softened=menu.gameplay_controls(steer);
        require(std::abs(softened.steer.x-.4F)<.001F
            && std::abs(softened.steer.y+.2F)<.001F);
        menu.selection=9;click();require(menu.page==Page::main && menu.selection==3);
        menu.alternate_available=true;menu.selection=0;click();require(menu.extended && menu.selected_level==0);
        menu.open_runtime();require(menu.selection==4 && menu.page==Page::main && menu.labels()[4]=="RESUME");
        menu.selection=0;click();require(menu.extended && menu.labels()[0].find("LOCKED")!=std::string::npos);
        menu.selection=4;click();require(!menu.open);
        menu.language=5;menu.default_laser=2;menu.crosshair_colour=7;
        for(unsigned style:{14U,15U,16U}) {
            menu.model_effect=style;menu.world_effect=style;
            StartupMenu effect_copy;
            require(effect_copy.restore_preferences(menu.preferences()));
            require(effect_copy.model_effect==style && effect_copy.world_effect==style);
            require(StartupMenu::style_name(style)!="OFF");
        }
        require(StartupMenu::next_style(13)==14 && StartupMenu::next_style(16)==0);
        require(StartupMenu::next_style(13,true)==14 && StartupMenu::next_style(16,true)==0);
        menu.music_volume=30;menu.sfx_volume=70;menu.msu_music=true;
        const auto preferences=menu.preferences();
        StartupMenu restored;
        require(restored.restore_preferences(preferences));
        require(restored.preferences()==preferences);
        require(restored.steer_sensitivity_index==1);
        require(restored.ray_tracing && !restored.ray_tracing_available);
        require(restored.enhanced_sky);
        require(!restored.ray_tracing_enabled());
        restored.page=Page::three_d;
        require(restored.row_count()==4 && restored.labels().back()=="BACK" && !restored.preview);
        restored.ray_tracing_available=true;
        require(restored.ray_tracing_enabled());
        restored.ray_tracing=false;
        require(!restored.ray_tracing_enabled());
        restored.ray_tracing=true;restored.ray_tracing_available=false;
        restored.page=Page::main;
        std::array<uint8_t,16> legacy{};std::copy(preferences.begin(),preferences.begin()+16,legacy.begin());
        legacy[4]=1;legacy[11]&=1;legacy[15]&=1;
        StartupMenu migrated;require(migrated.restore_preferences(legacy) && !migrated.ray_tracing && !migrated.infinite_lives);
        auto version4=preferences;version4[4]=4;version4[11]&=3;
        require(migrated.restore_preferences(version4) && !migrated.enhanced_sky
            && migrated.model_effect==menu.model_effect && migrated.world_effect==menu.world_effect);
        require(restored.open && !restored.runtime && !restored.extended
            && !restored.alternate_available && restored.page==Page::main
            && restored.selection==0 && restored.selected_level==0);
        // Invalid fields reject the entire record without partial mutation.
        for(size_t index=0;index<preferences.size();++index) {
            auto corrupt=preferences;corrupt[index]=255;
            const auto revision=restored.revision;
            require(!restored.restore_preferences(corrupt));
            require(restored.preferences()==preferences && restored.revision==revision);
        }
        require(!restored.restore_preferences(std::span(preferences).first(15)));
        require(restored.preferences()==preferences);
    }
    InputApi api{create_set,destroy_set,create_action,path,suggest,attach,sync,boolean,
        vector,current_profile,apply_haptic,stop_haptic,create_space,destroy_space,locate};
    OpenXrInput input(api);
    require(!input.poll(true));require(input.initialize(handle<XrInstance>(1),handle<XrSession>(2)));
    require(created==18 && suggestions==3);
    independent_buttons=true;
    require(input.poll(true) && !input.controls().reset_pressed);
    button_states[12]=button_states[13]=true;
    require(input.poll(true) && !input.controls().reset_pressed);
    button_states[7]=button_states[8]=true;
    require(input.poll(true) && !input.controls().reset_pressed); // Sticks first is not the chord.
    button_states[12]=button_states[13]=false;require(input.poll(true));
    button_states[12]=true;require(input.poll(true) && !input.controls().reset_pressed);
    button_states[13]=true;require(input.poll(true) && input.controls().reset_pressed);
    require(input.poll(true) && !input.controls().reset_pressed); // Never repeat while held.
    button_states[14]=true;
    require(input.poll(true) && input.controls().steer.y>0.99F);
    button_states[14]=false;button_states[15]=true;
    require(input.poll(true) && input.controls().steer.y<-.99F);
    button_states[15]=false;button_states[16]=button_states[17]=true;
    require(input.poll(true) && input.controls().steer.x==0.0F);
    button_states[16]=button_states[17]=false;
    require(input.poll(false));require(input.poll(true) && !input.controls().reset_pressed);
    independent_buttons=false;
    require(input.poll(false)); // Start the existing focus tests with unarmed buttons.
    require(input.aim_poses(handle<XrSpace>(2),1)[0].has_value());
    require(!input.aim_poses(XR_NULL_HANDLE,1)[0].has_value());
    pointer_tracked=false;require(!input.aim_poses(handle<XrSpace>(2),1)[0].has_value());pointer_tracked=true;
    held=true;axis={1,1};require(input.poll(true));
    require(input.controls().fire && !input.controls().menu_pressed);
    require(input.controls().select && !input.controls().select_pressed);
    require(std::abs(std::hypot(input.controls().steer.x,input.controls().steer.y)-1)<1e-6);
    held=false;axis={.1F,0};require(input.poll(true));require(input.controls().steer.x==0);
    held=true;require(input.poll(true));require(input.controls().menu_pressed);
    require(input.controls().select_pressed);
    require(input.poll(true));require(!input.controls().menu_pressed);
    require(!input.controls().select_pressed);
    const auto old_syncs=syncs;require(input.poll(false));require(syncs==old_syncs && !input.controls().fire);
    require(input.poll(true));require(!input.controls().menu_pressed);
    active=false;require(input.poll(true));require(!input.controls().fire && input.controls().steer.x==0);
    active=true;axis={std::numeric_limits<float>::quiet_NaN(),0};require(input.poll(true));require(input.controls().steer.x==0);
    require(!input.controls().menu_pressed); // Reconnected held button is not a fresh press.
    require(!input.controls().select_pressed);
    fail_read=true;require(!input.poll(true));require(!input.controls().fire && !input.controls().menu);
    require(input.status().find("Read button")!=std::string::npos);
    fail_read=false;sync_result=XR_SESSION_NOT_FOCUSED;
    require(input.poll(true) && !input.focused() && !input.controls().fire);
    sync_result=XR_ERROR_RUNTIME_FAILURE;require(!input.poll(true));
    require(!input.focused() && !input.controls().fire);
    sync_result=XR_SUCCESS;require(input.poll(true) && input.focused());
    input.close();require(destroyed==1);
    created=0;fail_create=3;require(!input.initialize(handle<XrInstance>(1),handle<XrSession>(3)));require(destroyed==2);
    fail_create=0;created=0;attach_failed=true;require(!input.initialize(handle<XrInstance>(1),handle<XrSession>(4)));require(destroyed==3);
    created=0;attach_failed=false;unsupported=true;require(input.initialize(handle<XrInstance>(1),handle<XrSession>(5)));
    input.close();require(destroyed==4);
    unsupported=false;created=0;const auto frame_suggestions=suggestions;
    sync_result=XR_SUCCESS;
    require(input.initialize(handle<XrInstance>(1),handle<XrSession>(6),true));
    require(created==18 && suggestions==frame_suggestions+4);
    mock_profiles={path_value("/interaction_profiles/oculus/touch_controller"),
        path_value("/interaction_profiles/oculus/touch_controller")};
    require(input.poll(true) && input.haptics_available());
    const starfox::simulation::RumbleEffect authored{0x1111U,0x8888U,40U};
    require(input.apply_haptics(authored) && haptic_applies==2);
    require(std::abs(haptic_amplitude-float(0x8888U)/65535.F)<.00001F
        && haptic_duration==40'000'000 && haptic_api_valid);
    const auto stops_before_focus=haptic_stops;
    require(input.poll(false) && !input.focused()
        && haptic_stops==stops_before_focus+2);
    const auto applies_before_unfocused=haptic_applies;
    require(!input.apply_haptics(authored) && haptic_applies==applies_before_unfocused);
    require(input.poll(true) && input.focused());
    require(input.apply_haptics(authored));
    fail_haptic=true;
    require(!input.apply_haptics(authored) && haptic_stops==stops_before_focus+4);
    fail_haptic=false;
    require(input.apply_haptics(authored));
    fail_haptic_stop=true;input.stop_haptics();
    require(haptic_stops==stops_before_focus+6
        && input.status().find("Stop OpenXR haptics failed")!=std::string::npos);
    fail_haptic_stop=false;
    mock_profiles={path_value("/interaction_profiles/valve/frame_controller_valve"),
        path_value("/interaction_profiles/valve/frame_controller_valve")};
    require(input.poll(true) && input.haptics_available());
    require(input.apply_haptics(authored)); // The advertised Frame binding is usable too.
    mock_profiles={XR_NULL_PATH,XR_NULL_PATH};
    const auto applies_before_unbound=haptic_applies;
    require(input.poll(true) && input.focused() && !input.haptics_available());
    require(!input.apply_haptics(authored) && haptic_applies==applies_before_unbound);
    input.close();require(destroyed==5);

    {
        const auto bit=vr_control_bit;
        VrControls xr,pad;
        xr.active_actions=bit(VrControlAction::steer)|bit(VrControlAction::fire)
            |bit(VrControlAction::menu);
        pad.active_actions=xr.active_actions;
        xr.steer={0.0F,0.0F};pad.steer={0.8F,0.0F};
        xr.fire=false;pad.fire=true;
        xr.menu=true;xr.menu_pressed=false;pad.menu=true;pad.menu_pressed=true;
        auto selected=select_vr_control_sources(xr,pad);
        require(selected.steer.x==0.0F && !selected.fire && !selected.menu_pressed);
        xr.active_actions &= ~bit(VrControlAction::fire);
        xr.active_actions &= ~bit(VrControlAction::menu);
        selected=select_vr_control_sources(xr,pad);
        require(selected.fire && selected.menu_pressed);

        // A usable XR source wins only for its own active action. The desktop
        // can still supply Menu when its XR binding is inactive.
        xr.active_actions=bit(VrControlAction::fire);
        xr.fire=false;xr.menu=false;xr.menu_pressed=false;
        pad.menu=true;pad.menu_pressed=true;
        selected=select_vr_control_sources(xr,pad);
        require(!selected.fire && selected.menu && selected.menu_pressed);

        DesktopControlEdges edges;
        VrControls held;
        held.active_actions=bit(VrControlAction::menu)|bit(VrControlAction::select)
            |bit(VrControlAction::roll_left)|bit(VrControlAction::roll_right)
            |bit(VrControlAction::stick_left)|bit(VrControlAction::stick_right);
        held.menu=held.select=held.roll_left=held.roll_right=true;
        held.stick_left=held.stick_right=true;
        auto edge=edges.sample(held);
        require(edge.menu_pressed && edge.select_pressed && edge.reset_pressed);
        static_cast<void>(edges.sample(held)); // Held while unfocused; discard this sample.
        edge=edges.sample(held); // Same level after focus resume is not a new press.
        require(!edge.menu_pressed && !edge.select_pressed && !edge.reset_pressed);
        selected=select_vr_control_sources({},edge);
        require(selected.menu && !selected.menu_pressed
            && selected.select && !selected.select_pressed && !selected.reset_pressed);
        static_cast<void>(edges.sample({}));
        edge=edges.sample(held);
        require(edge.menu_pressed && edge.select_pressed && edge.reset_pressed);
    }
    VrGameInput game_input;VrControls controls;
    controls.fire=true;controls.bomb=true;controls.boost=true;controls.brake=true;
    controls.roll_left=true;controls.roll_right=true;controls.steer={-1,1};
    controls.menu=true;game_input.sample(controls);
    auto tick=game_input.consume();
    const auto expected=starfox::input::y|starfox::input::a|starfox::input::x|starfox::input::b
        |starfox::input::left_shoulder|starfox::input::right_shoulder|starfox::input::left|starfox::input::up;
    require(tick.held==expected && tick.pressed==expected && tick.released==0);
    game_input.sample(controls);tick=game_input.consume();require(tick.pressed==0 && tick.held==expected);
    controls.menu_pressed=true;game_input.sample(controls);tick=game_input.consume();require(tick.pressed==starfox::input::start);
    controls.menu_pressed=false;game_input.sample(controls);tick=game_input.consume();require(tick.pressed==0 && (tick.held&starfox::input::start));
    game_input.sample({});tick=game_input.consume();require(tick.held==0 && tick.released==(expected|starfox::input::start));
    controls={};controls.fire=true;game_input.sample(controls);game_input.sample({});
    tick=game_input.consume();require(tick.held==0 && tick.pressed==starfox::input::y && tick.released==starfox::input::y);
    game_input.reset();tick=game_input.consume();require(tick.held==0 && tick.pressed==0 && tick.released==0);
    controls={};controls.select=true;game_input.sample(controls);
    require(game_input.consume().held==0); // Held during focus regain is ignored.
    controls.select_pressed=true;game_input.sample(controls);
    tick=game_input.consume();require(tick.pressed==starfox::input::select);
    controls.select_pressed=false;game_input.sample(controls);
    tick=game_input.consume();require(tick.pressed==0 && tick.held==starfox::input::select);
    game_input.sample({});tick=game_input.consume();require(tick.released==starfox::input::select);
    {
        VrGameInput face_buttons;
        controls={};controls.fire=true;face_buttons.sample(controls);
        require(face_buttons.consume().held==starfox::input::y); // Frame A fires.
        face_buttons.reset();controls={};controls.bomb=true;face_buttons.sample(controls);
        require(face_buttons.consume().held==starfox::input::a); // Frame B bombs.
        face_buttons.reset();controls={};controls.boost=true;face_buttons.sample(controls);
        require(face_buttons.consume().held==starfox::input::x); // Frame X boosts.
        face_buttons.reset();controls={};controls.brake=true;face_buttons.sample(controls);
        require(face_buttons.consume().held==starfox::input::b); // Frame Y brakes.
    }
    std::cout<<"VR action lifecycle, focus, deadzone and edge tests passed (injected runtime)\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
