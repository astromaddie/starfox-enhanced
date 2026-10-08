#include "starfox/vr/cockpit.hpp"
#include "starfox/compat/bit_cast.hpp"
#include "starfox/vr/source_sprites.hpp"
#include "starfox/assets/runtime_bundle.hpp"
#include "starfox/audio/spc700_audio.hpp"
#include "starfox/render/scaled_text_renderer.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <stdexcept>
using namespace starfox;using namespace starfox::vr;
namespace {
void require(bool value,const std::source_location& where=std::source_location::current()) {
    if(!value)throw std::runtime_error("Cockpit geometry regression at "+std::to_string(where.line()));
}
std::array<float,3> point(const Matrix4& m,const float* p) {
    std::array<float,3> result{};
    for(unsigned r=0;r<3;++r) {result[r]=m[12+r];for(unsigned c=0;c<3;++c)result[r]+=m[c*4+r]*p[c];}
    return result;
}
void near(float a,float b,const std::source_location& where=std::source_location::current()) {require(std::abs(a-b)<.001F,where);}
void verify_rig() {
    const auto imported=cockpit_arwing_packet();require(imported.geometry.vertices.size()==497*3);
    const auto dark=cockpit_arwing_packet(false,0),linear=cockpit_arwing_packet(true);
    for(size_t i=0;i<imported.geometry.vertices.size();++i) {
        const auto& v=imported.geometry.vertices[i];
        require(!v.texture[3] && !v.visibility_enabled && !v.group_enabled && v.color[3]==1);
        for(unsigned a=0;a<3;++a) {
            require(std::isfinite(v.position[a]));near(dark.geometry.vertices[i].color[a],0);
            const float c=v.color[a];near(linear.geometry.vertices[i].color[a],c<=.04045F?c/12.92F:std::pow((c+.055F)/1.055F,2.4F));
        }
    }
    for(const auto bounds:{std::array{-2.F,9.F},std::array{-9.F,2.F},std::array{-2.F,2.F}}) {
        const auto trimmed=cockpit_arwing_packet(false,15,bounds);require(!trimmed.geometry.vertices.empty());
        for(const auto& v:trimmed.geometry.vertices)require(v.position[0]>=bounds[0]-.0001F && v.position[0]<=bounds[1]+.0001F);
    }
    require(cockpit_arwing_repair_packet(DrawPacket{}).geometry.line_vertices.empty());
    std::vector<DrawPacket> hud(1);hud[0].model=overlay_panel_matrix();mount_cockpit_instruments(hud);
    const float anchor[]{76,175,0};const auto mounted=point(hud[0].model,anchor);
    near(mounted[0],-.015F);near(mounted[1],-.75F);near(mounted[2],-1.048F);
    GameSceneSnapshot scene;scene.flow=simulation::GameFlowState::gameplay;scene.pilot_tracking=true;
    scene.view_matrix={32767,0,0,0,32767,0,0,0,32767};scene.pilot_reference.emplace();
    scene.pilot_reference->rotation_matrix={0,32767,0,-32767,0,0,0,0,32767};
    PresentationPreferences prefs;prefs.cockpit=true;
    for(bool follow:{false,true}) {
        prefs.follow_ship_rotation=follow;
        const auto rig=presentation_instrument_matrix(scene,scene,1,prefs);
        const float cabin_reference[]{.76F,1.75F,0};
        const auto centre=point(rig,cabin_reference); // Rotation changes ship-local points only when not following.
        if(follow) {near(centre[0],.76F);near(centre[1],1.75F);}else {near(centre[0],1.75F);near(centre[1],-.76F);}
        auto calibrated=prefs;calibrated.origin_x=16;calibrated.origin_y=4;calibrated.origin_z=-10;
        const auto offset=presentation_instrument_matrix(scene,scene,1,calibrated);
        const float origin[]{.16F,.04F,-.1F};const auto transformed=point(rig,origin);
        for(unsigned i=0;i<3;++i)near(offset[12+i],-transformed[i]);
        for(unsigned scale:{0U,5U}) {
            calibrated.world_scale=scale;require(presentation_instrument_matrix(scene,scene,1,calibrated)==offset);
        }
        XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.fov={-.7F,.7F,.7F,-.7F};
        const auto seated=eye_camera(view,1,.05F).value();view.pose.position={.16F,.04F,0};
        const auto leaned=eye_camera(view,1,.05F).value();
        const float cabin_point[]{.3F,-.4F,-1.5F};
        const auto a=point(multiply_matrix(seated.view,rig),cabin_point);
        const auto b=point(multiply_matrix(leaned.view,rig),cabin_point);
        near(b[0]-a[0],-.16F);near(b[1]-a[1],-.04F);
    }
}
void dump(const std::string& file,std::span<const DrawPacket> cabin,std::span<const DrawPacket> hud,
    const Matrix4& rig,const Matrix4& world,std::span<const DrawPacket> source) {
    std::ofstream out(file);out<<"{\"rig\":[";
    for(unsigned i=0;i<16;++i)out<<(i?",":"")<<rig[i];out<<"],\"world\":[";
    for(unsigned i=0;i<16;++i)out<<(i?",":"")<<world[i];out<<"],\"packets\":[";bool first=true;
    const auto packets=[&](auto items,const char* group) {for(const auto& p:items) {
        if(!first)out<<',';first=false;out<<"{\"group\":\""<<group<<"\",\"model\":[";
        for(unsigned i=0;i<16;++i)out<<(i?",":"")<<p.model[i];out<<"],\"vertices\":[";bool vertex_first=true;
        for(const auto& v:p.geometry.vertex_view()) {
            if(!vertex_first)out<<',';vertex_first=false;out<<"{\"p\":["<<v.position[0]<<','<<v.position[1]<<','<<v.position[2]<<"],\"c\":[";
            for(unsigned i=0;i<4;++i)out<<(i?",":"")<<v.color[i];out<<"],\"uv\":["<<v.uv[0]<<','<<v.uv[1]<<"],\"t\":[";
            for(unsigned i=0;i<4;++i)out<<(i?",":"")<<v.texture[i];out<<"]}";
        }
        out<<"],\"lines\":[";bool lfirst=true;
        for(const auto& v:p.geometry.line_view()) {out<<(lfirst?"":",")<<'[';lfirst=false;
            for(unsigned c=0;c<3;++c)out<<(c?",":"")<<v.position[c];
            for(float c:v.color)out<<','<<c;out<<']';}
        out<<"],\"texels\":[";bool tfirst=true;for(auto t:p.geometry.texel_view()){out<<(tfirst?"":",")<<t;tfirst=false;}out<<"]}";
    }};
    packets(cabin,"cabin");packets(hud,"hud");packets(source,"world");out<<"]}\n";
}
// The attract intro's scaled credits text is placed in the game camera's plane
// (flat model-space quads), not in each eye's head-rolled billboard basis.
void verify_scaled_text(const assets::RomImage& rom,const assets::SymbolMap& symbols) {
    simulation::GameSimulation game(rom,symbols,"TITLEMAP");audio::Spc700Audio audio;
    GameSceneHistory history(game,rom,symbols);
    const auto text_count=[&]{unsigned n=0;for(const auto& o:history.current()->objects)n+=(o.object.strategy_flags[0]&0x40U)!=0;return n;};
    for(unsigned tick=0;tick<3000 && text_count()<8;++tick) {
        auto result=game.tick({});(void)audio.render_logic_tick(result.audio_port_writes);
        game.synchronize_apu_output_ports(audio.output_ports());history.capture();
    }
    const auto& scene=*history.current();
    require(scene.flow==simulation::GameFlowState::intro && text_count()>=8 && !world_panel_scene(scene));
    SourceModels models(rom,symbols,true,true);
    const auto world=models.assemble_world_interpolated(scene,scene,1,false,true);require(world.pending.empty());
    unsigned glyphs=0;
    for(size_t i=0;i<world.handles.size();++i) {
        const auto object=std::find_if(scene.objects.begin(),scene.objects.end(),[&](const auto& o){return o.handle==world.handles[i];});
        if(object==scene.objects.end() || !(object->object.strategy_flags[0]&0x40U)) continue;
        const auto& packet=world.packets[i];const auto vertices=packet.geometry.vertex_view();
        if(vertices.empty()) continue;
        const double depth=-double(packet.model[14])*256,size=127+starfox::bit_cast<int8_t>(object->object.texture_scroll_x);
        const float side=float(std::trunc(size*256./depth)*depth/256.);
        require(side>0 && vertices.size()%6==0);
        for(size_t q=0;q<vertices.size();q+=6) {
            float low[2]{1e9F,1e9F},high[2]{-1e9F,-1e9F};
            for(size_t k=q;k<q+6;++k) {
                const auto& v=vertices[k];
                require((v.texture[3]&1024U) && !(v.texture[3]&(4U|134217728U)) && v.position[2]==0
                    && v.billboard[0]==0 && v.billboard[1]==0);
                for(unsigned a=0;a<2;++a) {low[a]=std::min(low[a],v.position[a]);high[a]=std::max(high[a],v.position[a]);}
            }
            near(high[0]-low[0],side);near(high[1]-low[1],side);++glyphs;
        }
    }
    require(glyphs>0);
}
void cartridge(const assets::RomImage& rom,const assets::SymbolMap& symbols,const std::string& evidence) {
    verify_scaled_text(rom,symbols);
    simulation::GameSimulation game(rom,symbols,"LEVEL1_1",{},true);audio::Spc700Audio audio;
    game.set_timing_mode(simulation::TimingMode::unlocked_20_fps);
    GameSceneHistory history(game,rom,symbols);
    PresentationPreferences prefs;prefs.cockpit=true;
    for(unsigned tick=0;tick<2400 && !pilot_view_active(*history.current(),prefs);++tick) {
        auto result=game.tick({});(void)audio.render_logic_tick(result.audio_port_writes);
        game.synchronize_apu_output_ports(audio.output_ports());history.capture();
    }
    const auto scene=*history.current();require(pilot_view_active(scene,prefs));
    SourceModels models(rom,symbols,true,true);CockpitGeometry cockpit(rom,symbols);
    const auto defaults=models.assemble_world_interpolated(scene,scene,1,false,true);
    auto world=models.assemble_world_interpolated(scene,scene,1,false,true,true);require(world.pending.empty());
    require(defaults.handles==world.handles);
    for(size_t i=0;i<world.handles.size();++i)if(world.handles[i]!=scene.player)
        require(same_draw_geometry(std::span(&world.packets[i],1),std::span(&defaults.packets[i],1)));
    for(const auto& model:world.compute_models)
        require(model.packet_index<world.handles.size() && world.handles[model.packet_index]==model.key && model.key!=scene.player);
    const auto before=world;const auto saved=game.save_state();
    auto off=prefs;off.cockpit=false;require(cockpit.assemble(world,scene,off).empty());
    require(same_draw_geometry(world.packets,before.packets));
    const auto cabin=cockpit.assemble(world,scene,prefs);require(cabin.size()==1);
    // The cockpit replaces the 48-face cutscene Arwing packet (MY_DEMOS);
    // the chase view keeps the in-flight ship on the GPU source path.
    {
        const auto slot=[&](const SourceModelPackets& packets) {
            const auto found=std::find(packets.handles.begin(),packets.handles.end(),scene.player);
            require(found!=packets.handles.end());return size_t(found-packets.handles.begin());
        };
        require(std::any_of(defaults.compute_models.begin(),defaults.compute_models.end(),
            [&](const auto& model){return model.packet_index==slot(defaults);})); // in-flight ship on the GPU source path
        require(before.packets[slot(before)].geometry.vertex_view().size()==56*3);
        // Every native wing-loss shape trims only its corresponding replacement side.
        assets::ShapeDecoder reference(rom,symbols);
        const auto extent=[&](const char* name) {
            std::array<int,2> out{};
            for(const auto& vertex:reference.decode_by_name(symbols,name).vertices) {
                out[0]=std::min(out[0],int(vertex.x));out[1]=std::max(out[1],int(vertex.x));
            }
            return out;
        };
        const auto intact=extent("MYSHIP_4");
        for(const char* shape:{"MYSHIP_L","MYSHIP_R","MYSHIP_B"}) {
            auto damaged=game.restored_state(saved);
            damaged->objects().at(damaged->player()).shape=uint16_t(symbols.find(shape).at(0));
            GameSceneHistory damaged_history(*damaged,rom,symbols);const auto& damaged_scene=*damaged_history.current();
            require(pilot_view_active(damaged_scene,prefs));
            auto damaged_world=models.assemble_world_interpolated(damaged_scene,damaged_scene,1,false,true,true);
            const auto damaged_cabin=cockpit.assemble(damaged_world,damaged_scene,prefs);require(damaged_cabin.size()==1);
            float low=1e9F,high=-1e9F;
            for(const auto& v:damaged_cabin[0].geometry.vertices) {const auto q=point(damaged_cabin[0].model,v.position);low=std::min(low,q[0]);high=std::max(high,q[0]);}
            const auto live=extent(shape);constexpr float span=5.201948F*cockpit_arwing_scale;
            near(low,float(live[0])/std::abs(intact[0])*span);
            near(high,float(live[1])/std::abs(intact[1])*span);
        }
    }
    require(cabin[0].geometry.vertices.size()==497*3);
    const auto expected_hull=cockpit_arwing_packet();
    require(same_draw_geometry(cabin,std::span(&expected_hull,1)) && cabin[0].model==identity_matrix);
    std::array<float,3> lowest{1e9F,1e9F,1e9F},highest{-1e9F,-1e9F,-1e9F};
    std::vector<std::array<std::array<float,3>,3>> hull;
    for(size_t i=0;i<cabin[0].geometry.vertices.size();i+=3) {
        auto& triangle=hull.emplace_back();
        for(unsigned k=0;k<3;++k) {
            triangle[k]=point(cabin[0].model,cabin[0].geometry.vertices[i+k].position);
            for(unsigned a=0;a<3;++a) {lowest[a]=std::min(lowest[a],triangle[k][a]);highest[a]=std::max(highest[a],triangle[k][a]);}
        }
    }
    require(lowest[2]<-3.5F && highest[2]>1.5F && lowest[0]<-1.5F && highest[0]>1.5F);
    const auto sub=[](auto a,auto b){return std::array<float,3>{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    const auto cross=[](auto a,auto b){return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    const auto dot=[](auto a,auto b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    // Seated head clearance: sampled triangle surfaces stay outside 0.35 m.
    for(const auto& t:hull)for(unsigned u=0;u<=20;++u)for(unsigned v=0;u+v<=20;++v) {
        const float a=u/20.F,b=v/20.F;std::array<float,3> q{};
        for(unsigned c=0;c<3;++c)q[c]=t[0][c]+(t[1][c]-t[0][c])*a+(t[2][c]-t[0][c])*b;
        require(dot(q,q)>.35F*.35F);
    }
    // A small central sightline cone must stay open after removing the panes.
    for(float x:{-.1F,0.F,.1F})for(float y:{-.1F,0.F,.1F}) {
        const std::array<float,3> ray{x,y,-1.F};
        for(const auto& t:hull) {
            const auto e1=sub(t[1],t[0]),e2=sub(t[2],t[0]),h=cross(ray,e2);const float det=dot(e1,h);
            if(std::abs(det)<1e-9F)continue;
            const auto s0=sub(std::array<float,3>{0,0,0},t[0]);const float u=dot(s0,h)/det;
            const auto q=cross(s0,e1);const float v=dot(ray,q)/det,distance=dot(e2,q)/det;
            require(!(u>=0 && v>=0 && u+v<=1 && distance>0));
        }
    }
    // The hull never covers the mounted instrument face seen from the seat.
    for(float x=-.229F;x<=.203F;x+=.012F)for(float y=-.86F;y<=-.644F;y+=.011F) {
        const std::array<float,3> target{x,y,-1.048F+(y+.75F)*(.0377F/.238F)};
        for(const auto& t:hull) {
            const auto e1=sub(t[1],t[0]),e2=sub(t[2],t[0]),h=cross(target,e2);const float det=dot(e1,h);
            if(std::abs(det)<1e-9F)continue;
            const auto s0=sub(std::array<float,3>{0,0,0},t[0]);const float u=dot(s0,h)/det;
            const auto q=cross(s0,e1);const float v=dot(target,q)/det,distance=dot(e2,q)/det;
            require(!(u>=0 && v>=0 && u+v<=1 && distance>0 && distance<1));
        }
    }
    for(size_t i=0;i<world.handles.size();++i) {
        if(world.handles[i]==scene.player)require(world.packets[i].geometry.vertex_view().empty() && world.packets[i].model==identity_matrix);
        else require(same_draw_geometry(std::span(&world.packets[i],1),std::span(&before.packets[i],1)) && world.packets[i].model==before.packets[i].model);
    }
    require(game.save_state()==saved);
    for(const auto& model:world.compute_models)
        require(model.packet_index<world.handles.size() && world.handles[model.packet_index]==model.key);
    for(const auto& p:cabin)for(const auto& v:p.geometry.vertex_view())require(!v.visibility_enabled && !v.group_enabled);
    // All replacement triangles have area, retain opaque colours, and disable
    // cartridge visibility groups so turning the head exposes the whole cabin.
    for(const auto& packet:cabin)for(size_t i=0;i<packet.geometry.vertices.size();i+=3) {
        const auto a=point(packet.model,packet.geometry.vertices[i].position);
        const auto b=point(packet.model,packet.geometry.vertices[i+1].position);
        const auto c=point(packet.model,packet.geometry.vertices[i+2].position);
        const auto normal=cross(sub(b,a),sub(c,a));require(dot(normal,normal)>1e-12F);
        for(unsigned j=0;j<3;++j)require(packet.geometry.vertices[i+j].color[3]==1);
    }
    render::ScaledTextRenderer text(rom,symbols);auto hud=layout_a_instrument_packets(rom,symbols,scene,text);
    const auto original_hud=hud;mount_cockpit_instruments(hud,scene.meters.extended);require(hud.size()==original_hud.size()+1 && same_draw_geometry(std::span(hud).subspan(1),original_hud));
    const auto fits_face=[&](const auto& packets) {for(const auto& packet:std::span(packets).subspan(1))for(const auto& vertex:packet.geometry.vertex_view()) {
        const auto position=point(packet.model,vertex.position);
        require(position[0]>=-.229F && position[0]<=.203F && position[1]>=-.861F && position[1]<=-.643F);
    }};
    const auto verify_backing=[&](const auto& packets) {
        const auto& backing=packets[0];require(backing.model==identity_matrix && !backing.geometry.vertices.empty() && backing.geometry.vertices.size()%6==0);
        for(const auto& packet:std::span(packets).subspan(1))for(const auto& vertex:packet.geometry.vertex_view()) {
            const auto p=point(packet.model,vertex.position);
            bool covered=false;
            for(size_t i=0;i<backing.geometry.vertices.size();i+=6) {
                const auto& low=backing.geometry.vertices[i];const auto& high=backing.geometry.vertices[i+2];
                if(p[0]<low.position[0]+.011F || p[0]>high.position[0]-.011F
                    || p[1]<low.position[1]+.011F || p[1]>high.position[1]-.011F)continue;
                const float surface=low.position[2]+(p[1]-low.position[1])*(high.position[2]-low.position[2])/(high.position[1]-low.position[1]);
                near(p[2]-surface,.002F);covered=true;
            }
            require(covered);
        }
        // Bands never bridge empty rows: each pair has a clear vertical gap.
        for(size_t i=0;i<backing.geometry.vertices.size();i+=6)for(size_t j=i+6;j<backing.geometry.vertices.size();j+=6) {
            const auto& v=backing.geometry.vertices;
            require(v[i+2].position[1]<v[j].position[1] || v[j+2].position[1]<v[i].position[1]);
        }
        for(const auto& vertex:backing.geometry.vertex_view()) {
            near(vertex.color[0],16.F/255);near(vertex.color[1],26.F/255);near(vertex.color[2],34.F/255);
        }
    };
    fits_face(hud);verify_backing(hud);
    for(unsigned bombs:{0U,scene.meters.extended?5U:3U}) {
        auto variation=game.restored_state(saved);variation->map().write_native_word(symbols.find(scene.meters.extended?"SPECWEPCNTONE":"SPECWEPCNT").at(0),uint16_t(bombs));
        (void)variation->tick({});(void)variation->tick({});GameSceneHistory varied_history(*variation,rom,symbols);
        for(bool full:{false,true})for(unsigned comms=0;comms<3;++comms) {
            auto varied=*varied_history.current();varied.meters.damage=full?varied.meters.player_health_max:0;
            varied.dialogue.active=comms!=0;varied.dialogue.text_visible=true;varied.dialogue.three_lines=comms==2;
            varied.dialogue.text_address=symbols.find("MSG_1").at(0);
            auto packets=layout_a_instrument_packets(rom,symbols,varied,text);
            mount_cockpit_instruments(packets,varied.meters.extended);fits_face(packets);verify_backing(packets);
            require(packets[1].model==hud[1].model && packets[2].model==hud[2].model);
            if(!evidence.empty() && bombs==0 && !full && comms==2) {
                auto view=prefs;view.follow_ship_rotation=true;
                dump(evidence+"-comms.json",cabin,packets,presentation_instrument_matrix(scene,scene,1,view),
                    presentation_scene_matrix(scene,scene,1,view),world.packets);
            }
        }
    }
    // Boss meters retain the existing top-row placement and packet ordering.
    // They are intentionally not cropped into the approved single-player band.
    auto boss_scene=scene;boss_scene.meters.boss_health=100;boss_scene.meters.boss_max_health=180;
    auto boss_hud=layout_a_instrument_packets(rom,symbols,boss_scene,text);
    const auto boss_source=boss_hud;
    require(boss_hud.size()==original_hud.size());
    require(boss_hud[1].geometry.vertex_view().size()>original_hud[1].geometry.vertex_view().size());
    mount_cockpit_instruments(boss_hud,boss_scene.meters.extended);verify_backing(boss_hud);
    require(boss_hud[0].geometry.vertices.size()>=12); // Boss and compact instrument rows remain separate.
    require(boss_hud.size()==boss_source.size()+1 && same_draw_geometry(std::span(boss_hud).subspan(1),boss_source));
    bool outside_band=false;
    for(const auto& vertex:boss_hud[2].geometry.vertex_view())
        outside_band|=point(boss_hud[2].model,vertex.position)[1]>-.643F;
    require(outside_band);
    if(!evidence.empty()) {
        auto view=prefs;view.follow_ship_rotation=true;
        dump(evidence+"-boss.json",cabin,boss_hud,presentation_instrument_matrix(scene,scene,1,view),
            presentation_scene_matrix(scene,scene,1,view),world.packets);
    }
    for(unsigned inactive=0;inactive<3;++inactive) {
        auto other=scene;if(inactive==0)other.pilot_tracking=false;if(inactive==1)other.pilot_reference.reset();if(inactive==2)other.flow=simulation::GameFlowState::title;
        require(cockpit.assemble(world,other,prefs).empty());
    }
    if(!evidence.empty())for(bool follow:{false,true}) {
        prefs.follow_ship_rotation=follow;
        dump(evidence+(follow?"-follow.json":"-existing-rotation.json"),cabin,hud,
            presentation_instrument_matrix(scene,scene,1,prefs),presentation_scene_matrix(scene,scene,1,prefs),world.packets);
    }
    // Seed the same native FLASHPLAYER initializer as the existing desktop
    // capture fixture. Native ticks own its shape, material, blink and lifetime.
    const auto flash=game.objects().allocate_after();require(flash!=0);
    auto& effect=game.objects().at(flash);const auto ship=game.objects().at(game.player());
    effect.world_x=ship.world_x;effect.world_y=ship.world_y;effect.world_z=ship.world_z;
    effect.shape=ship.shape;effect.colour_table=ship.colour_table;
    effect.rotation_x=ship.rotation_x;effect.rotation_y=ship.rotation_y;effect.rotation_z=ship.rotation_z;
    effect.strategy_address=symbols.find("FLASHPLAYER_ISTRAT").at(0);
    unsigned visible=0,hidden=0;bool captured=false;
    for(unsigned tick=0;tick<80 && game.objects().is_active(flash);++tick) {
        auto result=game.tick({});(void)audio.render_logic_tick(result.audio_port_writes);
        game.synchronize_apu_output_ports(audio.output_ports());history.capture();
        const auto now=*history.current();
        if(!pilot_view_active(now,prefs))continue;
        auto native=models.assemble_world_interpolated(*history.previous(),now,.5,false,true,true);
        require(native.pending.empty());
        const auto slot=std::find(native.handles.begin(),native.handles.end(),flash);
        if(slot==native.handles.end()) {++hidden;continue;}
        const auto index=size_t(slot-native.handles.begin());const auto source=native.packets[index];
        if(source.geometry.vertex_view().empty() && source.geometry.line_view().empty()) {++hidden;continue;}
        ++visible;const auto before_effect=game.save_state();
        const auto attached=cockpit.assemble(native,now,prefs);require(game.save_state()==before_effect);
        require(native.packets[index].geometry.vertex_view().empty() && native.packets[index].geometry.line_view().empty());
        const auto expected=cockpit_arwing_repair_packet(source);
        const auto actual=std::find_if(attached.begin()+1,attached.end(),[&](const auto& packet){
            return same_draw_geometry(std::span(&packet,1),std::span(&expected,1)) && packet.model==expected.model;
        });require(actual!=attached.end());
        require(actual->geometry.vertex_view().empty() && !actual->geometry.line_view().empty());
        const auto native_lines=source.geometry.line_view();require(!native_lines.empty());
        // The known native effect is monochrome; every edge must retain its live ink.
        for(const auto& v:native_lines)for(unsigned c=0;c<4;++c)near(v.color[c],native_lines[0].color[c]);
        for(const auto& v:actual->geometry.line_view()) {
            for(unsigned c=0;c<4;++c)near(v.color[c],native_lines[0].color[c]);
            require(!v.visibility_enabled && !v.group_enabled);
            bool attached_to_mesh=false;
            for(const auto& solid:expected_hull.geometry.vertex_view()) {
                float delta=0;for(unsigned a=0;a<3;++a)delta+=std::abs(v.position[a]/.998F-solid.position[a]);
                attached_to_mesh|=delta<.001F;
            }
            require(attached_to_mesh);
        }
        for(bool follow:{false,true})for(unsigned scale:{0U,5U}) {
            auto calibrated=prefs;calibrated.follow_ship_rotation=follow;calibrated.world_scale=scale;
            calibrated.origin_x=16;calibrated.origin_y=4;calibrated.origin_z=-10;
            const auto rig=presentation_instrument_matrix(*history.previous(),now,.5,calibrated);
            require(actual->model==cabin[0].model);
            for(const auto& v:actual->geometry.line_view()) {
                const auto p=point(rig,v.position);
                for(float coordinate:p)require(std::isfinite(coordinate));
            }
        }
        if(!captured && !evidence.empty()) {
            for(bool follow:{false,true}) {
                auto view=prefs;view.follow_ship_rotation=follow;
                dump(evidence+(follow?"-repair-follow.json":"-repair-existing-rotation.json"),attached,hud,
                    presentation_instrument_matrix(*history.previous(),now,.5,view),
                    presentation_scene_matrix(*history.previous(),now,.5,view),native.packets);
            }
            captured=true;
        }
    }
    require(visible>0 && hidden>0);
    std::cout<<(scene.meters.extended?"EX":"Original")<<" seeded native repair flash: "<<visible
        <<" visible and "<<hidden<<" hidden phases, native colour and replacement mesh registration passed\n";
    std::cout<<(scene.meters.extended?"EX":"Original")<<" bundle cockpit: "
        <<cabin[0].geometry.vertices.size()/3<<" imported hull/interior/frame triangles; source state/other objects/HUD art unchanged\n";
}
}
int main(int argc,char** argv) try {
    verify_rig();
    if(argc>=3 && std::string_view(argv[1])=="--bundle") {
        std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});require(bytes.size()>12);
        uint32_t manifest=0;for(unsigned i=0;i<4;++i)manifest|=uint32_t(bytes[8+i])<<(8*i);
        // Positive structure/CRC integration check; application validates its embedded manifest separately.
        const auto bundle=assets::decode_runtime_bundle(bytes,manifest);
        for(bool ex:{false,true})cartridge(assets::RomImage(ex?bundle.starfox_ex_rom:bundle.original_rom),
            assets::SymbolMap::parse(ex?bundle.starfox_ex_symbols:bundle.original_symbols),argc==4?std::string(argv[3])+(ex?"-ex":"-original"):"");
    } else if(argc==3)cartridge(assets::RomImage::load(argv[1]),assets::SymbolMap::load(argv[2]),"");
    else require(argc==1);
    std::cout<<"Cockpit geometry/clip/material/mount/calibration/follow/head-tracking checks passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
