#include "starfox/vr/cockpit.hpp"
#include <cmath>
#include "starfox/compat/bit_cast.hpp"
#include <stdexcept>
#include "cockpit_assets.inc"
namespace starfox::vr {
namespace {
// Presentation calibration only: leave the instrument face and lower cabin
// fixed, widening/lowering the window and blending their shared connections.
std::array<float,3> window_frame_position(std::array<float,3> p) {
    const float weight=std::clamp((p[1]+.72F)/.24F,0.F,1.F);
    p[0]*=1.F+.7F*weight;p[1]-=.16F*weight;return p;
}
SceneVertex flat_vertex(std::array<float,3> position,uint32_t rgb,bool srgb,unsigned brightness) {
    SceneVertex vertex{};std::copy(position.begin(),position.end(),vertex.position);
    for(unsigned c=0;c<3;++c) {
        float value=float((rgb>>(16-8*c))&255)/255.F*float(std::min(brightness,15U))/15.F;
        if(srgb)value=value<=.04045F?value/12.92F:std::pow((value+.055F)/1.055F,2.4F);
        vertex.color[c]=value;
    }
    vertex.color[3]=1;return vertex;
}
SceneVertex pilot_vertex(SceneVertex v) {
    v.position[0]/=256.F;v.position[1]/=-256.F;v.position[2]/=-256.F;
    // The cabin is seen from independent head poses, not the cartridge's eye.
    v.visibility_enabled=v.group_enabled=0;return v;
}
}
Matrix4 cockpit_instrument_mount(bool extended) noexcept {
    // EX's fixed Layout A band is 190 source pixels wide (Original fits 141).
    // Uniform, cartridge-stable fit retains artwork/aspect without health-driven resizing.
    const float pixel=.00305F*(extended?141.F/190.F:1.F);
    const float anchor_x=extended?101.F:76.F;
    return {pixel,0,0,0,0,-pixel,0,0,0,0,1,0,
        -.015F+(extended?.001525F:0.F)-anchor_x*pixel,-.852F+175*pixel,-1.243F,1};
}
void mount_cockpit_instruments(std::span<DrawPacket> packets,bool extended) {
    const auto overlay=overlay_panel_matrix();auto inverse=identity_matrix;
    inverse[0]=1/overlay[0];inverse[5]=1/overlay[5];
    inverse[12]=-overlay[12]/overlay[0];inverse[13]=-overlay[13]/overlay[5];inverse[14]=-overlay[14];
    const auto mount=multiply_matrix(cockpit_instrument_mount(extended),inverse);
    for(auto& packet:packets)packet.model=multiply_matrix(mount,packet.model);
}
DrawPacket cockpit_rear_packet(bool srgb,unsigned brightness) {
    DrawPacket out;
    for(const auto& triangle:cockpit_assets::rear) for(const auto& p:triangle.points)
        out.geometry.vertices.push_back(flat_vertex(window_frame_position(p),triangle.rgb,srgb,brightness));
    return out;
}
DrawPacket cockpit_front_packet(const assets::Shape& shape,bool srgb,unsigned brightness) {
    if(shape.faces.size()!=66)throw std::runtime_error("Unsupported C cockpit face topology");
    std::array<render::Rgba8,256> palette{};
    for(auto& colour:palette)colour={255,255,255,255};
    render::RenderPose pose;pose.z=250;pose.scale=2;
    DrawPacket decoded;std::string error;
    if(!build_draw_packet(shape,pose,palette,112,1,false,256,decoded,error))
        throw std::runtime_error("C cockpit decode: "+error);
    const auto vertices=decoded.geometry.vertex_view();
    if(vertices.size()!=std::size(cockpit_assets::front)*3 || !decoded.geometry.deferred.empty()
        || !decoded.geometry.line_view().empty())throw std::runtime_error("Unsupported C cockpit packet topology");
    // Geometry-only signature verified against both bundled variants. Their
    // palette/descriptor addresses differ, but positions and face ranges match.
    // Reject a different cabin before any index-based material assignment.
    uint64_t signature=14695981039346656037ULL;
    const auto word=[&](uint32_t value) {for(unsigned i=0;i<4;++i) {
        signature^=(value>>(i*8))&255;signature*=1099511628211ULL;
    }};
    for(const auto& v:vertices)for(float value:v.position)word(starfox::bit_cast<uint32_t>(value));
    for(const auto& r:decoded.geometry.ranges) {word(uint32_t(r.source_face));word(r.first_vertex);word(r.vertex_count);}
    if(signature!=0x1a590b6396dbcd5aULL)throw std::runtime_error("Unsupported C cockpit geometry signature");
    DrawPacket out;
    for(const auto& assignment:cockpit_assets::front) {
        const auto range=std::find_if(decoded.geometry.ranges.begin(),decoded.geometry.ranges.end(),
            [&](const auto& r){return r.source_face==assignment.face && assignment.first>=r.first_vertex
                && assignment.first+3<=r.first_vertex+r.vertex_count;});
        if(range==decoded.geometry.ranges.end() || assignment.first!=range->first_vertex+assignment.triangle*3)
            throw std::runtime_error("Unsupported C cockpit face/material mapping");
        for(unsigned i=0;i<3;++i) {
            const auto& v=vertices[assignment.first+i];
            out.geometry.vertices.push_back(flat_vertex(window_frame_position({v.position[0]*.015F,
                -v.position[1]*.015F-.27F,-v.position[2]*.015F-1.4F}),assignment.rgb,srgb,brightness));
        }
    }
    return out;
}
DrawPacket cockpit_ship_packet(const DrawPacket& source) {
    // The whole live ship surrounds the cabin: nose ahead, wings and tail
    // beside and behind the pilot. The native repair/upgrade wireframe shares
    // the player's source pose, so it uses the same rig with its blink state.
    auto out=source;const auto vertices=source.geometry.vertex_view(),lines=source.geometry.line_view();
    out.geometry.vertices.assign(vertices.begin(),vertices.end());out.geometry.shared_vertices.reset();
    out.geometry.line_vertices.assign(lines.begin(),lines.end());out.geometry.shared_line_vertices.reset();
    for(auto* stream:{&out.geometry.vertices,&out.geometry.line_vertices})for(auto& v:*stream)v=pilot_vertex(v);
    out.model={cockpit_ship_scale,0,0,0,0,cockpit_ship_scale,0,0,0,0,cockpit_ship_scale,0,
        -cockpit_seat_m[0],-cockpit_seat_m[1],-cockpit_seat_m[2],1};
    return out;
}
CockpitGeometry::CockpitGeometry(const assets::RomImage& rom,const assets::SymbolMap& symbols)
    :decoder_(rom,symbols),symbols_(symbols) {
    const auto& values=symbols.find("FLASHPLAYER_STRAT");if(!values.empty())flash_player_=values.front();
}
std::vector<DrawPacket> CockpitGeometry::assemble(SourceModelPackets& world,const GameSceneSnapshot& scene,
    const PresentationPreferences& preferences,bool srgb) {
    if(!pilot_view_active(scene,preferences))return {};
    const unsigned key=std::min(unsigned(scene.display_brightness),15U)+(srgb?16:0);
    if(!cabin_[key]) {
        if(!front_)front_=decoder_.decode_by_name(symbols_,"COCKPIT");
        cabin_[key]=std::array{cockpit_front_packet(*front_,srgb,scene.display_brightness),
            cockpit_rear_packet(srgb,scene.display_brightness)};
    }
    std::vector<DrawPacket> out{(*cabin_[key])[0],(*cabin_[key])[1]};
    for(size_t i=0;i<world.handles.size();++i) {
        const bool player=world.handles[i]==scene.player;
        const auto object=std::find_if(scene.objects.begin(),scene.objects.end(),
            [&](const auto& item){return item.handle==world.handles[i];});
        const bool repair=flash_player_ && object!=scene.objects.end() && object->object.strategy_address==flash_player_;
        if(!player && !repair)continue;
        if(std::any_of(world.compute_models.begin(),world.compute_models.end(),
            [&](const auto& model){return model.packet_index==i;}))
            throw std::runtime_error("Cockpit player rig requires source triangle geometry");
        out.push_back(cockpit_ship_packet(world.packets[i]));
        world.packets[i]=DrawPacket{};
    }
    return out;
}
}
