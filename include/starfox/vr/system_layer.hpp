#pragma once
#include <chrono>

namespace starfox::vr {
// Cross-port system layer (steam-frame-vr-port standard, section 1): L View and
// R Menu timing. This is deliberately a plain value-in, events-out state
// machine with no OpenXR, haptic or renderer dependency, so it can be swapped
// for the shared sfvr library's sfvr_view without touching its callers.
//
//   L View short press (< 1 s)   -> view_tap (the game's Select / menu back).
//                                   Reported on release, because a press is
//                                   not a tap until it is known not to be a hold.
//   L View held 1 s              -> recentre (yaw + horizontal position).
//   L View held 3 s              -> recentre_height (recentre and recalibrate
//                                   standing height); a further step after the
//                                   1 s one, not instead of it.
//   L View + R Menu held 0.5 s   -> open_menu (this port's runtime menu).
//
// A View press that overlaps a Menu press belongs to the menu chord: it never
// produces a tap or a recentre. A hold that has already recentred is never also
// a tap. Controls held at construction or through a reset() (focus loss) are
// ignored until released, so they cannot fire on resume.
struct SystemLayerEvents {
    bool view_tap{},recentre{},recentre_height{},open_menu{};
};

class SystemLayer {
public:
    static constexpr double recentre_hold_seconds=1.0;
    static constexpr double height_hold_seconds=3.0;
    static constexpr double menu_chord_hold_seconds=0.5;

    // `now` is monotonic seconds; only differences are used.
    [[nodiscard]] SystemLayerEvents update(bool view_down,bool menu_down,double now) noexcept {
        SystemLayerEvents events;
        if(!armed_) {
            if(!view_down && !menu_down) armed_=true;
            return events;
        }
        if(view_down && !view_prev_) {
            view_since_=now;recentre_done_=height_done_=false;
            swallowed_=menu_down;
        }
        if(view_prev_ || view_down) {
            const double held=now-view_since_;
            if(view_down && menu_down && !recentre_done_) swallowed_=true;
            if(!swallowed_) {
                if(!recentre_done_ && held>=recentre_hold_seconds) {
                    recentre_done_=true;events.recentre=true;
                }
                if(recentre_done_ && !height_done_ && held>=height_hold_seconds) {
                    height_done_=true;events.recentre_height=true;
                }
            }
            if(!view_down && !swallowed_ && !recentre_done_) events.view_tap=true;
        }
        if(view_down && menu_down && !recentre_done_) {
            if(!chord_active_) {chord_active_=true;chord_since_=now;chord_done_=false;}
            if(!chord_done_ && now-chord_since_>=menu_chord_hold_seconds) {
                chord_done_=true;events.open_menu=true;
            }
        } else chord_active_=false;
        view_prev_=view_down;
        return events;
    }
    // Focus loss, session change or failure: drop all in-flight presses.
    void reset() noexcept {*this=SystemLayer{};}
    [[nodiscard]] static double steady_seconds() noexcept {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
private:
    bool armed_{},view_prev_{},swallowed_{},recentre_done_{},height_done_{};
    bool chord_active_{},chord_done_{};
    double view_since_{},chord_since_{};
};
}
