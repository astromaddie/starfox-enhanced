#pragma once
#include "starfox/vr/source_models.hpp"
#include <unordered_map>
namespace starfox::vr {
// Supplied SNES Arwing in pilot metres; uniform fit preserves source proportions.
inline constexpr float cockpit_arwing_scale=1.75F;
inline constexpr std::array<float,3> cockpit_arwing_eye_obj{0.F,1.05F,.40F};
DrawPacket cockpit_arwing_packet(bool srgb=false,unsigned brightness=15,
    std::optional<std::array<float,2>> keep_x=std::nullopt);
// The native effect owns visibility and colour; its cockpit artwork follows the replacement hull.
DrawPacket cockpit_arwing_repair_packet(const DrawPacket& native);
Matrix4 cockpit_instrument_mount(bool extended=false) noexcept;
void mount_cockpit_instruments(std::vector<DrawPacket>&,bool extended=false,
    bool srgb=false,unsigned brightness=15);
class CockpitGeometry {
public:
    CockpitGeometry(const assets::RomImage&,const assets::SymbolMap&);
    // Replaces the pilot-view hull only; simulation and chase-view art stay native.
    std::vector<DrawPacket> assemble(SourceModelPackets&,const GameSceneSnapshot&,
        const PresentationPreferences&,bool srgb=false);
private:
    assets::ShapeDecoder decoder_;
    uint32_t flash_player_{};
    std::optional<std::array<int,2>> intact_x_; // MYSHIP_4 x extent, source units
    std::unordered_map<uint32_t,std::array<int,2>> live_x_;
    std::optional<std::array<float,2>> damaged_extent(uint32_t live_shape);
    std::array<int,2> x_extent(uint32_t shape);
    std::array<std::optional<DrawPacket>,32> cabin_;
};
}
