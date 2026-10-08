#include "starfox/vr/cockpit.hpp"
#include <cmath>
#include <stdexcept>
#include <vector>
#include "cockpit_assets.inc"
namespace starfox::vr {
namespace {
SceneVertex flat_vertex(std::array<float,3> position,uint32_t rgb,bool srgb,unsigned brightness) {
    SceneVertex vertex{};std::copy(position.begin(),position.end(),vertex.position);
    for(unsigned c=0;c<3;++c) {
        float value=float((rgb>>(16-8*c))&255)/255.F*float(std::min(brightness,15U))/15.F;
        if(srgb)value=value<=.04045F?value/12.92F:std::pow((value+.055F)/1.055F,2.4F);
        vertex.color[c]=value;
    }
    vertex.color[3]=1;return vertex;
}
SceneVertex between(const SceneVertex& a,const SceneVertex& b,float t) {
    auto v=a;
    for(unsigned i=0;i<3;++i)v.position[i]=a.position[i]+(b.position[i]-a.position[i])*t;
    for(unsigned i=0;i<4;++i) {
        v.color[i]=a.color[i]+(b.color[i]-a.color[i])*t;
        v.odd_color[i]=a.odd_color[i]+(b.odd_color[i]-a.odd_color[i])*t;
    }
    for(unsigned i=0;i<2;++i)v.uv[i]=a.uv[i]+(b.uv[i]-a.uv[i])*t;
    return v;
}
// Keeps the part of a convex polygon on one side of an axis plane.
std::vector<SceneVertex> clip_axis(const std::vector<SceneVertex>& polygon,unsigned axis,float value,bool keep_above) {
    std::vector<SceneVertex> out;
    for(size_t i=0;i<polygon.size();++i) {
        const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];
        const bool in_a=keep_above?a.position[axis]>=value:a.position[axis]<=value;
        const bool in_b=keep_above?b.position[axis]>=value:b.position[axis]<=value;
        if(in_a)out.push_back(a);
        if(in_a!=in_b)out.push_back(between(a,b,(value-a.position[axis])/(b.position[axis]-a.position[axis])));
    }
    return out;
}
// Pieces lying on a cut plane have no area and would only add slivers.
bool has_area(const std::vector<SceneVertex>& polygon) {
    if(polygon.size()<3)return false;
    float normal[3]{};
    for(size_t i=1;i+1<polygon.size();++i) {
        const auto* a=polygon[0].position;const auto* b=polygon[i].position;const auto* c=polygon[i+1].position;
        const float u[]{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v[]{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        normal[0]+=u[1]*v[2]-u[2]*v[1];normal[1]+=u[2]*v[0]-u[0]*v[2];normal[2]+=u[0]*v[1]-u[1]*v[0];
    }
    return normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]>1e-14F;
}
}
Matrix4 cockpit_instrument_mount(bool extended) noexcept {
    // Fit the existing unmodified Layout A artwork to the supplied sloped dash.
    // The face rises 0.238 m over 0.0377 m in OBJ coordinates. Lift the art
    // 8 mm toward the pilot to avoid z fighting; preserve its authored aspect.
    const float pixel=.00305F*(extended?141.F/190.F:1.F);
    const float anchor_x=extended?101.F:76.F;
    constexpr float slope=.0377F/.238F;
    const float vertical=1.F/std::sqrt(1.F+slope*slope);
    constexpr float centre_y=-.75F;
    constexpr float centre_z=(-.9861F+.40F)*cockpit_arwing_scale
        +(centre_y-(.7299F-1.05F)*cockpit_arwing_scale)*slope+.008F;
    return {pixel,0,0,0,0,-pixel*vertical,-pixel*vertical*slope,0,0,0,1,0,
        -.015F+(extended?.001525F:0.F)-anchor_x*pixel,
        centre_y+175*pixel*vertical,centre_z+175*pixel*vertical*slope,1};
}
void mount_cockpit_instruments(std::vector<DrawPacket>& packets,bool extended,bool srgb,unsigned brightness) {
    const auto overlay=overlay_panel_matrix();auto inverse=identity_matrix;
    inverse[0]=1/overlay[0];inverse[5]=1/overlay[5];
    inverse[12]=-overlay[12]/overlay[0];inverse[13]=-overlay[13]/overlay[5];inverse[14]=-overlay[14];
    const auto mount=multiply_matrix(cockpit_instrument_mount(extended),inverse);
    for(auto& packet:packets)packet.model=multiply_matrix(mount,packet.model);
    // The supplied cyan console needs a dark inset under the native white art.
    // Follow its actual bounds (including portraits/boss meters) without changing
    // the artwork, layout or scale. Draw the backing first: HUD passes do not write depth.
    struct Band {float left,right,bottom,top;};
    std::vector<Band> bands;
    constexpr float padding=.012F;
    for(const auto& packet:packets) {
        const auto vertices=packet.geometry.vertex_view();
        for(size_t first=0;first+2<vertices.size();first+=3) {
            Band band{1e9F,-1e9F,1e9F,-1e9F};
            for(size_t i=first;i<first+3;++i) {
                const auto& m=packet.model;const auto* p=vertices[i].position;
                const float x=m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12];
                const float y=m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13];
                band.left=std::min(band.left,x);band.right=std::max(band.right,x);
                band.bottom=std::min(band.bottom,y);band.top=std::max(band.top,y);
            }
            band.left-=padding;band.right+=padding;band.bottom-=padding;band.top+=padding;
            // Only connected artwork rows share a backing. The distant native
            // boss row must not fill the empty space above the main instruments.
            for(size_t i=0;i<bands.size();) {
                const auto& other=bands[i];
                if(band.bottom<=other.top && band.top>=other.bottom) {
                    band.left=std::min(band.left,other.left);band.right=std::max(band.right,other.right);
                    band.bottom=std::min(band.bottom,other.bottom);band.top=std::max(band.top,other.top);
                    bands.erase(bands.begin()+i);i=0;
                } else ++i;
            }
            bands.push_back(band);
        }
    }
    if(bands.empty())return;
    const auto face=cockpit_instrument_mount(extended);
    const auto vertex=[&](float x,float y) {
        const float z=face[14]+(y-face[13])*face[6]/face[5]-.002F;
        return flat_vertex({x,y,z},0x101a22U,srgb,brightness);
    };
    DrawPacket backing;
    for(const auto& b:bands) {
        const std::array vertices{vertex(b.left,b.bottom),vertex(b.right,b.bottom),vertex(b.right,b.top),
            vertex(b.left,b.bottom),vertex(b.right,b.top),vertex(b.left,b.top)};
        backing.geometry.vertices.insert(backing.geometry.vertices.end(),vertices.begin(),vertices.end());
    }
    packets.insert(packets.begin(),std::move(backing));
}
CockpitGeometry::CockpitGeometry(const assets::RomImage& rom,const assets::SymbolMap& symbols)
    :decoder_(rom,symbols) {
    const auto& values=symbols.find("FLASHPLAYER_STRAT");if(!values.empty())flash_player_=values.front();
    if(const auto& intact=symbols.find("MYSHIP_4");!intact.empty())intact_x_=x_extent(intact.front());
}
std::array<int,2> CockpitGeometry::x_extent(uint32_t shape) {
    auto found=live_x_.find(shape);
    if(found==live_x_.end()) {
        std::array<int,2> extent{0,0};
        for(const auto& v:decoder_.decode(shape).vertices) {extent[0]=std::min(extent[0],int(v.x));extent[1]=std::max(extent[1],int(v.x));}
        found=live_x_.emplace(shape,extent).first;
    }
    return found->second;
}
// When the live ship loses a wing (MYSHIP_L/R/B are narrower than MYSHIP_4),
// retain the same fraction of the replacement mesh span on that side.
std::optional<std::array<float,2>> CockpitGeometry::damaged_extent(uint32_t live_shape) {
    if(!intact_x_) return std::nullopt;
    const auto e=x_extent(live_shape);
    if(e==*intact_x_) return std::nullopt;
    return std::array<float,2>{e[0]>(*intact_x_)[0]?e[0]/256.F:-1e6F,e[1]<(*intact_x_)[1]?e[1]/256.F:1e6F};
}
namespace {
std::array<float,3> arwing_position(const std::array<float,3>& p) {
    return {(p[0]-cockpit_arwing_eye_obj[0])*cockpit_arwing_scale,
        (p[1]-cockpit_arwing_eye_obj[1])*cockpit_arwing_scale,
        -(p[2]-cockpit_arwing_eye_obj[2])*cockpit_arwing_scale};
}
}
DrawPacket cockpit_arwing_packet(bool srgb,unsigned brightness,std::optional<std::array<float,2>> keep_x) {
    DrawPacket out;
    for(const auto& triangle:cockpit_assets::arwing) {
        std::vector<SceneVertex> polygon;
        for(const auto& p:triangle.points)polygon.push_back(flat_vertex(arwing_position(p),triangle.rgb,srgb,brightness));
        if(keep_x) {
            polygon=clip_axis(polygon,0,(*keep_x)[0],true);
            polygon=clip_axis(polygon,0,(*keep_x)[1],false);
        }
        if(!has_area(polygon))continue;
        for(size_t i=1;i+1<polygon.size();++i)for(size_t k:{size_t{0},i,i+1})out.geometry.vertices.push_back(polygon[k]);
    }
    // Authored console and low cabin modules share the pilot rig. Wing damage
    // clips only the exterior source ship, never the interior around the pilot.
    for(const auto& triangle:cockpit_assets::hybrid_interior)for(const auto& p:triangle.points)
        out.geometry.vertices.push_back(flat_vertex(p,triangle.rgb,srgb,brightness));
    return out;
}
DrawPacket cockpit_arwing_repair_packet(const DrawPacket& source) {
    DrawPacket out;out.preserve_native_colour=source.preserve_native_colour;out.shading=source.shading;
    // The native FLASHPLAYER uses one flat wire colour. An empty native packet
    // remains empty during its hidden phases. Copy the live material, not a new timer.
    const auto lines=source.geometry.line_view();const auto triangles=source.geometry.vertex_view();
    if(lines.empty() && triangles.empty())return out;
    const auto ink=!lines.empty()?lines.front():triangles.front();
    for(const auto& edge:cockpit_assets::arwing_edges)for(const auto& p:edge) {
        auto v=ink;const auto position=arwing_position(p);
        // A tiny pilot-directed bias makes edges visible on the depth-tested hull.
        for(unsigned a=0;a<3;++a)v.position[a]=position[a]*.998F;
        v.visibility_enabled=v.group_enabled=0;out.geometry.line_vertices.push_back(v);
    }
    return out;
}
std::vector<DrawPacket> CockpitGeometry::assemble(SourceModelPackets& world,const GameSceneSnapshot& scene,
    const PresentationPreferences& preferences,bool srgb) {
    if(!pilot_view_active(scene,preferences))return {};
    const unsigned key=std::min(unsigned(scene.display_brightness),15U)+(srgb?16:0);
    if(!cabin_[key])cabin_[key]=cockpit_arwing_packet(srgb,scene.display_brightness);
    std::vector<DrawPacket> out{*cabin_[key]};
    for(size_t i=0;i<world.handles.size();++i) {
        const bool player=world.handles[i]==scene.player;
        const auto object=std::find_if(scene.objects.begin(),scene.objects.end(),
            [&](const auto& item){return item.handle==world.handles[i];});
        const bool repair=flash_player_ && object!=scene.objects.end() && object->object.strategy_address==flash_player_;
        if(!player && !repair)continue;
        if(std::any_of(world.compute_models.begin(),world.compute_models.end(),
            [&](const auto& model){return model.packet_index==i;}))
            throw std::runtime_error("Cockpit player rig requires source triangle geometry");
        if(repair)out.push_back(cockpit_arwing_repair_packet(world.packets[i]));
        if(player && object!=scene.objects.end() && intact_x_) {
            if(auto extent=damaged_extent(object->object.shape)) {
                // Preserve the native fraction of each wing that remains. The
                // replacement's silhouette differs, so use its full source span.
                constexpr float half_span=5.201948F*cockpit_arwing_scale;
                (*extent)[0]*=256.F*half_span/std::abs((*intact_x_)[0]);
                (*extent)[1]*=256.F*half_span/std::abs((*intact_x_)[1]);
                out[0]=cockpit_arwing_packet(srgb,scene.display_brightness,extent);
            }
        }
        world.packets[i]=DrawPacket{};
    }
    return out;
}
}
