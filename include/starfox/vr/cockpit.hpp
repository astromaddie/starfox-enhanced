#pragma once
#include "starfox/vr/source_models.hpp"
namespace starfox::vr {
// Approved C presentation rig. Source geometry stays in the user's bundle.
Matrix4 cockpit_instrument_mount(bool extended=false) noexcept;
void mount_cockpit_instruments(std::span<DrawPacket>,bool extended=false);
DrawPacket cockpit_rear_packet(bool srgb=false,unsigned brightness=15);
// Complete live player or repair-flash geometry in the 12x ship/seat rig.
DrawPacket cockpit_ship_packet(const DrawPacket& source);
// Validates the authored front topology before applying approved face materials.
DrawPacket cockpit_front_packet(const assets::Shape&,bool srgb=false,unsigned brightness=15);
class CockpitGeometry {
public:
    CockpitGeometry(const assets::RomImage&,const assets::SymbolMap&);
    // Replaces the player with its complete ship-local hull and attaches
    // the exact native repair flash, preserving its complete source geometry.
    std::vector<DrawPacket> assemble(SourceModelPackets&,const GameSceneSnapshot&,
        const PresentationPreferences&,bool srgb=false);
private:
    assets::ShapeDecoder decoder_;
    const assets::SymbolMap& symbols_;
    uint32_t flash_player_{};
    std::optional<assets::Shape> front_;
    std::array<std::optional<std::array<DrawPacket,2>>,32> cabin_;
};
}
