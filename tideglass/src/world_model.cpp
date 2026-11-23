#include "world_model.h"
#include "world_asset_name.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace water::world
{
namespace
{
struct Table
{
    uint32_t count = 0, offset = 0, stride = 0;
    std::size_t At(uint32_t i) const { return std::size_t(offset)+std::size_t(i)*stride; }
};
uint32_t U32(Bytes b, std::size_t at) { uint32_t value = 0; b.U32(at, value); return value; }
uint16_t U16(Bytes b, std::size_t at) { uint16_t value = 0; b.U16(at, value); return value; }
bool Array(Bytes b, std::size_t at, uint32_t stride, std::size_t header, Table& out)
{
    if (!b.Contains(at, 8)) return false;
    out = {U32(b, at), U32(b, at+4), stride};
    return !out.count || (out.offset >= header && out.count <= b.size/stride &&
                          b.Contains(out.offset, std::size_t(out.count)*stride));
}
bool Floats(Bytes b, std::size_t at, float* dest, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (!b.F32(at+i*4, dest[i]) || !std::isfinite(dest[i])) return false;
    return true;
}
bool EmptyTrack(Bytes b, std::size_t at)
{
    return b.Contains(at, 20) && U16(b, at) == 0 && U16(b, at+2) == 0xffff &&
           U32(b, at+4) == 0 && U32(b, at+12) == 0;
}
bool FullOpacity(Bytes b, std::size_t at)
{
    if (EmptyTrack(b, at)) return true;
    Table times, values;
    if (!b.Contains(at, 20) || U16(b, at) != 0 || U16(b, at+2) != 0xffff ||
        !Array(b, at+4, 8, 304, times) || !Array(b, at+12, 8, 304, values) ||
        times.count != 1 || values.count != 1) return false;
    Table t, v;
    return Array(b, times.At(0), 4, 304, t) && Array(b, values.At(0), 2, 304, v) &&
           t.count == 1 && v.count == 1 && U32(b, t.At(0)) == 0 && U16(b, v.At(0)) == 32767;
}
// Validate without allocating; the scene must not be able to request parent or
// absolute paths through a model's external texture name.
bool TextureName(Bytes b, std::size_t at, uint32_t count)
{
    if (count < 5 || count > 1024 || !b.Contains(at, count) || b.data[at+count-1]) return false;
    if (!ValidAssetName({reinterpret_cast<const char*>(b.data+at), count-1})) return false;
    const auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? c+('a'-'A') : c; };
    return b.data[at+count-5] == '.' && lower(b.data[at+count-4]) == 'b' &&
           lower(b.data[at+count-3]) == 'l' && lower(b.data[at+count-2]) == 'p';
}
ModelError DecodeMotion(Bytes m,const Table& bones,uint32_t vertices,std::size_t limit,ModelMotion& out,std::array<bool,256>& omitted)
{
    Table sequences;
    if(bones.count>256 || !Array(m,28,64,304,sequences) || !sequences.count || sequences.count>256)return ModelError::Pose;
    unsigned canonical=sequences.count;
    for(unsigned i=0;i<sequences.count;++i)if(!(U32(m,sequences.At(i)+12)&64)){
        if(canonical!=sequences.count)return ModelError::Pose;canonical=i;
    }
    if(canonical==sequences.count || !(U32(m,sequences.At(canonical)+12)&32))return ModelError::Pose;
    const auto duration=U32(m,sequences.At(canonical)+4);
    if(!duration || duration>3600000)return ModelError::Pose;
    for(unsigned i=0;i<sequences.count;++i){unsigned next=i,steps=0;
        while(next!=canonical && steps++<sequences.count){
            if(next>=sequences.count || !(U32(m,sequences.At(next)+12)&64))return ModelError::Pose;
            next=U16(m,sequences.At(next)+62);
        }
        if(next!=canonical || U32(m,sequences.At(i)+4)!=duration)return ModelError::Pose;
    }
    std::size_t remaining=limit;
    const auto charge=[&](std::size_t count,std::size_t size){if(count>remaining/size)return false;remaining-=count*size;return true;};
    if(!charge(1,sizeof(ModelMotion)) || !charge(bones.count,sizeof(ModelMotion::Bone)) ||
       !charge(vertices,sizeof(ModelMotion::Influence)))return ModelError::Budget;
    // Scan twice: validate/admit all keys before allocating their retained payload.
    ModelMotion candidate;
    for(unsigned pass=0;pass<2;++pass){
        if(pass){candidate.duration=duration;candidate.bones.resize(bones.count);candidate.vertices.resize(vertices);}
        for(unsigned b=0;b<bones.count;++b){
            const auto at=bones.At(b);float pivot[3];
            const auto parent=U16(m,at+8);
            if((parent!=0xffff && parent>=bones.count) || !Floats(m,at+76,pivot,3))return ModelError::Pose;
            // A placement phase covers its local clip. Global growth cycles need a different clock.
            // Omit batches weighted to those bones while retaining independent geometry.
            omitted[b]=(U32(m,at+4)&~512u) || parent!=0xffff ||
                       U16(m,at+18)!=0xffff || U16(m,at+38)!=0xffff || (!EmptyTrack(m,at+56) && U16(m,at+58)!=0xffff);
            if(omitted[b])continue;
            if(pass)std::copy(pivot,pivot+3,candidate.bones[b].pivot.begin());
            for(unsigned component=0;component<3;++component){
                const bool rotation=component==1, scale=component==2;
                const auto track=at+16+component*20;if(EmptyTrack(m,track))continue;
                Table times,values,t,v;
                if(U16(m,track)>1 ||
                   !Array(m,track+4,8,304,times) || !Array(m,track+12,8,304,values) ||
                   times.count!=sequences.count || values.count!=times.count ||
                   !Array(m,times.At(canonical),4,304,t) || !Array(m,values.At(canonical),rotation?8:12,304,v) ||
                   !t.count || t.count!=v.count || t.count>65536 || (t.count>1 && U16(m,track)!=1))return ModelError::Pose;
                if(!pass && !charge(t.count,rotation?sizeof(ModelMotion::Rotation):sizeof(ModelMotion::Translation)))return ModelError::Budget;
                if(pass){if(rotation)candidate.bones[b].rotations.resize(t.count);else (scale?candidate.bones[b].scales:candidate.bones[b].translations).resize(t.count);}
                uint32_t previous=0;
                for(unsigned k=0;k<t.count;++k){
                    const auto time=U32(m,t.At(k));
                    if(time>duration || (k && time<=previous))return ModelError::Pose;previous=time;
                    if(rotation){
                        std::array<float,4> q;double norm=0;
                        for(unsigned a=0;a<4;++a){q[a]=float(U16(m,v.At(k)+a*2))*(2.f/65535.f)-1.f;norm+=double(q[a])*q[a];}
                        if(norm<.25 || norm>2.25)return ModelError::Pose;
                        for(auto& x:q)x=static_cast<float>(x/std::sqrt(norm));
                        if(pass)candidate.bones[b].rotations[k]={time,q};
                    }else{
                        std::array<float,3> value;if(!Floats(m,v.At(k),value.data(),3))return ModelError::Pose;
                        for(auto x:value)if(std::fabs(x)>100000)return ModelError::Pose;
                        if(pass)(scale?candidate.bones[b].scales:candidate.bones[b].translations)[k]={time,value};
                    }
                }
            }
        }
    }
    if(candidate.Bytes()>limit)return ModelError::Budget;
    out=std::move(candidate);return ModelError::None;
}

}

static ModelError ReadScrollingLayer(Bytes model, uint16_t transform, ScrollingLayer& result,std::size_t& remaining)
{
    Table transforms, sequences;
    if (!Array(model,96,60,304,transforms) || transform>=transforms.count ||
        !Array(model,28,64,304,sequences) || sequences.count!=1) return ModelError::Material;
    const auto track = transforms.At(transform);
    if (U16(model,track)!=1 ||
        !EmptyTrack(model,track+20) || !EmptyTrack(model,track+40)) return ModelError::Material;
    Table times, values, keys, offsets;
    if (!Array(model,track+4,8,304,times) || times.count!=1 ||
        !Array(model,track+12,8,304,values) || values.count!=1 ||
        !Array(model,times.At(0),4,304,keys) || (keys.count<2 || keys.count>65536) ||
        !Array(model,values.At(0),12,304,offsets) || offsets.count!=keys.count) return ModelError::Material;
    result.period=U32(model,keys.At(keys.count-1));
    if(!result.period || U32(model,keys.At(0)))return ModelError::Material;
    const auto global=U16(model,track+2);
    result.usesModelTime=global==0xffff;
    uint32_t duration=U32(model,sequences.At(0)+4);
    if (global!=0xffff) {
        Table periods;
        if (!Array(model,20,4,304,periods) || global>=periods.count) return ModelError::Material;
        duration=U32(model,periods.At(global));
    }
    if (result.period!=duration) return ModelError::Material;
    if(keys.count>remaining/sizeof(ScrollingLayer::Key))return ModelError::Budget;
    remaining-=keys.count*sizeof(ScrollingLayer::Key);
    result.keys.reserve(keys.count);
    for (unsigned i=0;i<keys.count;++i) {
        float v[3];
        if (!Floats(model,offsets.At(i),v,3) || v[0]!=0 || v[2]!=0 || (!i && v[1]!=0)) return ModelError::Material;
        const auto time=U32(model,keys.At(i));
        if(i && time<=result.keys.back().time)return ModelError::Material;
        result.keys.push_back({time,v[1]});
    }
    return ModelError::None;
}

static ModelError DecodeModel(world::Bytes m, world::Bytes s, std::size_t limit, StaticModel& output, ModelMotion* motion, bool rest=false, bool scroll=false)
{
    constexpr std::size_t Header = 304, SkinHeader = 48;
    if (!m.Contains(0, Header) || !s.Contains(0, SkinHeader) || std::memcmp(m.data, "MD20", 4) ||
        U32(m, 4) != 264 || std::memcmp(s.data, "SKIN", 4) || !U32(m, 68)) return ModelError::Header;
    Table bones, verts, textures, opacity, materials, boneLookup, texLookup, uvLookup, opacityLookup, animLookup;
    Table lookup, triangles, skinBones, submeshes, batches;
    if (!Array(m, 44, 88, Header, bones) || !Array(m, 60, 48, Header, verts) ||
        !Array(m, 80, 16, Header, textures) || !Array(m, 88, 20, Header, opacity) ||
        !Array(m, 112, 4, Header, materials) || !Array(m, 120, 2, Header, boneLookup) ||
        !Array(m, 128, 2, Header, texLookup) || !Array(m, 136, 2, Header, uvLookup) ||
        !Array(m, 144, 2, Header, opacityLookup) || !Array(m, 152, 2, Header, animLookup) ||
        !Array(s, 4, 2, SkinHeader, lookup) || !Array(s, 12, 2, SkinHeader, triangles) ||
        !Array(s, 20, 4, SkinHeader, skinBones) || !Array(s, 28, 48, SkinHeader, submeshes) ||
        !Array(s, 36, 24, SkinHeader, batches)) return ModelError::Layout;
    if (!verts.count || verts.count > 65536 || !lookup.count || lookup.count > 65536 ||
        skinBones.count != lookup.count || !triangles.count || triangles.count%3 ||
        !submeshes.count || !batches.count || textures.count > 65536) return ModelError::Geometry;
    // Additional unused bones (the river dock/boat have them) do not affect
    // vertices fully weighted to static root zero. Validate every used vertex
    // and skin palette below; do not infer animation support from bone count.
    const bool rigid = !rest || scroll;
    if (rigid && (!bones.count || bones.count>65536)) return ModelError::Pose;
    if (!motion && rigid && (U32(m, bones.At(0)+4) != 0 || U16(m, bones.At(0)+8) != 0xffff ||
        !EmptyTrack(m, bones.At(0)+16) || !EmptyTrack(m, bones.At(0)+36) ||
        !EmptyTrack(m, bones.At(0)+56))) return ModelError::Pose;
    if (!rest) {
    if (U32(m, 72) || (!motion && U32(m, 96))) return ModelError::Material; // colour and texture-transform tracks
    float pivot[3];
    if (!Floats(m, bones.At(0)+76, pivot, 3)) return ModelError::Pose;
    for (uint32_t i = 0; i < opacity.count; ++i)
        if (!FullOpacity(m, opacity.At(i))) return ModelError::Material;
    }

    // Admission is checked before any output allocation. Checked subtraction
    // also avoids size_t overflow on the 32-bit client target.
    std::size_t remaining = limit;
    const auto charge = [&](std::size_t count, std::size_t size) {
        if (count > remaining/size) return false;
        remaining -= count*size; return true;
    };
    if (!charge(1, sizeof(StaticModel)) || !charge(lookup.count, sizeof(ModelVertex)) ||
        !charge(triangles.count, sizeof(uint16_t)) || !charge(textures.count, sizeof(ModelTexture)) ||
        !charge(batches.count, sizeof(ModelDraw))) return ModelError::Budget;
    const bool animatedUv=motion && !rest && U32(m,96);
    if ((scroll || animatedUv) && !charge(batches.count,sizeof(ScrollingLayer))) return ModelError::Budget;
    for (uint32_t i = 0; i < textures.count; ++i) {
        const auto at = textures.At(i);
        const auto count = U32(m, at+8), offset = U32(m, at+12);
        if (U32(m, at) != 0 || (U32(m, at+4)&~3u) || offset < Header || !TextureName(m, offset, count)) {
            if (rest) continue; // a replaceable or unnamed texture: its layers are left out below
            return ModelError::Material;
        }
        // std::string may retain its small-string capacity even for tiny names.
        if (!charge(std::max<std::size_t>(count, std::string().capacity()+1), 1)) return ModelError::Budget;
    }
    ModelMotion animation;
    std::array<bool,256> omittedBones{};
    if (motion) {
        const auto error=DecodeMotion(m,bones,lookup.count,remaining,animation,omittedBones);
        if(error!=ModelError::None)return error;
    }
    StaticModel candidate;
    candidate.vertices.resize(lookup.count);
    candidate.indices.resize(triangles.count);
    candidate.textures.resize(textures.count);
    candidate.draws.resize(batches.count);
    if (scroll || animatedUv) candidate.scroll.reserve(batches.count);
    for (uint32_t i = 0; i < lookup.count; ++i) {
        const auto source = U16(s, lookup.At(i));
        if (source >= verts.count) return ModelError::Geometry;
        const auto at = verts.At(source);
        auto& v = candidate.vertices[i];
        if (!Floats(m, at, v.position.data(), 3) || !Floats(m, at+20, v.normal.data(), 3) ||
            !Floats(m, at+32, v.uv.data(), 2)) return ModelError::Geometry;
        // The rigid decoder accepts full weight on the root. Animated draws retain each vertex influence.
        if (motion) {
            auto& influence=animation.vertices[i];unsigned weight=0;
            for(unsigned j=0;j<4;++j){
                influence.weights[j]=m.data[at+12+j];influence.bones[j]=m.data[at+16+j];
                if(influence.weights[j] && influence.bones[j]>=bones.count)return ModelError::Pose;
                weight+=influence.weights[j];
            }
            if(weight!=255)return ModelError::Pose;
        } else if (!rest && (m.data[at+12] != 255 || m.data[at+13] || m.data[at+14] || m.data[at+15] || m.data[at+16]))
            return ModelError::Pose;
        const double normal2 = double(v.normal[0])*v.normal[0]+double(v.normal[1])*v.normal[1]+double(v.normal[2])*v.normal[2];
        if (normal2 < 0.25 || normal2 > 2.25) return ModelError::Geometry;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!i) candidate.minimum[axis] = candidate.maximum[axis] = v.position[axis];
            else { candidate.minimum[axis] = std::min(candidate.minimum[axis], v.position[axis]);
                   candidate.maximum[axis] = std::max(candidate.maximum[axis], v.position[axis]); }
        }
    }
    for (uint32_t i = 0; i < triangles.count; ++i) {
        const auto index = U16(s, triangles.At(i));
        if (index >= lookup.count) return ModelError::Geometry;
        candidate.indices[i] = index; // SKIN indices already refer to our expanded lookup order
    }
    for (uint32_t i = 0; i < textures.count; ++i) {
        const auto at = textures.At(i);
        const auto offset = U32(m, at+12), count = U32(m, at+8);
        auto& t = candidate.textures[i]; t.flags = U32(m, at+4);
        if (rest && (U32(m, at) != 0 || (U32(m, at+4)&~3u) || offset < Header || !TextureName(m, offset, count))) continue;
        CanonicalAssetName({reinterpret_cast<const char*>(m.data+offset), count-1}, t.name);
    }
    const bool partialMotion=motion && std::any_of(omittedBones.begin(),omittedBones.end(),[](bool omitted){return omitted;});
    for (uint32_t i = 0; i < batches.count; ++i) {
        const auto at = batches.At(i);
        const auto sub = U16(s, at+4), material = U16(s, at+10), texture = U16(s, at+16);
        const auto uv = U16(s, at+18), alpha = U16(s, at+20), anim = U16(s, at+22);
        auto& draw = candidate.draws[i]; draw.shader = U16(s, at+2);
        if (sub >= submeshes.count || U16(s, at+6) != sub || material >= materials.count ||
            texture >= texLookup.count || uv >= uvLookup.count || alpha >= opacityLookup.count ||
            anim >= animLookup.count) return ModelError::Layout;
        draw.texture = U16(m, texLookup.At(texture));
        draw.materialFlags = U16(m, materials.At(material)); draw.blend = U16(m, materials.At(material)+2);
        // An additive overlay may be omitted when its independently drawable base is present on the same submesh.
        if(motion && !rest && draw.blend==4 && i && U16(s,at+12)!=0 &&
           U16(s,batches.At(i-1)+4)==sub && candidate.draws[i-1].indexCount){
            draw.indexCount=0;
            continue;
        }
        if (rest) {
            // A placed model keeps its solid and cut-out layers, from their first texture and at rest. A layer that
            // only makes sense animated or blended over the others (glow, reflection, fading) is left out, not the model.
            const auto opacityTrack = U16(m, opacityLookup.At(alpha));
            draw.indexCount = 0;
            ScrollingLayer layer;
            if (scroll) {
                if (draw.blend<2) continue;
                if (draw.blend!=2 || (draw.materialFlags!=20 && draw.materialFlags!=17) || draw.shader!=0 || U16(s,at+14)!=1) return ModelError::Material;
                const auto parsed=ReadScrollingLayer(m,U16(m,animLookup.At(anim)),layer,remaining);
                if(parsed!=ModelError::None)return parsed;
            }
            if ((!scroll && draw.blend > 1) || U16(s, at+8) != 0xffff || U16(s, at+12) != 0 || U16(m, uvLookup.At(uv)) != 0 ||
                draw.texture >= textures.count || candidate.textures[draw.texture].name.empty() ||
                opacityTrack >= opacity.count || !FullOpacity(m, opacity.At(opacityTrack))) continue;
            draw.materialFlags &= 7;
            const auto sm = submeshes.At(sub);
            const uint32_t firstVertex = U16(s, sm+4), count = U16(s, sm+6), combo = U16(s, sm+14);
            draw.firstIndex = U16(s, sm+8); draw.indexCount = U16(s, sm+10);
            if (!count || firstVertex >= lookup.count || count > lookup.count-firstVertex ||
                !draw.indexCount || draw.indexCount%3 || draw.firstIndex%3 || draw.firstIndex >= triangles.count ||
                draw.indexCount > triangles.count-draw.firstIndex || combo > boneLookup.count) return ModelError::Geometry;
            const auto palette=U16(s,sm+12);
            if (scroll && (!palette || palette>boneLookup.count-combo)) return ModelError::Pose;
            for (uint32_t j=draw.firstIndex;j<draw.firstIndex+draw.indexCount;++j) {
                const auto vertex=candidate.indices[j];
                if (vertex<firstVertex || vertex>=firstVertex+count) return ModelError::Geometry;
                if (scroll) {
                    if(motion){
                        const auto& influence=animation.vertices[vertex];
                        for(unsigned k=0;k<4;++k)if(influence.weights[k]){
                            const auto bone=s.data[skinBones.At(vertex)+k];
                            if(bone>=palette || U16(m,boneLookup.At(combo+bone))!=influence.bones[k] ||
                               omittedBones[influence.bones[k]])return ModelError::Pose;
                        }
                        continue;
                    }
                    const auto weight=verts.At(U16(s,lookup.At(vertex)))+12;
                    const auto bone=s.data[skinBones.At(vertex)];
                    if (U32(m,weight)!=255 || m.data[weight+4]!=0 || bone>=palette ||
                        U16(m,boneLookup.At(combo+bone))!=0) return ModelError::Pose;
                }
            }
            if (scroll && motion && palette<=layer.palette.size() && animation.bones.size()>1) {
                bool supported=true, moving=false;
                for(unsigned k=0;k<palette;++k){
                    const unsigned index=U16(m,boneLookup.At(combo+k));
                    if(index>=animation.bones.size()){supported=false;break;}
                    const auto& bone=animation.bones[index];
                    supported &= bone.rotations.empty() && bone.translations.empty();
                    moving |= bone.scales.size()>1;
                    layer.palette[k]=uint8_t(index);
                }
                if(supported && moving && animation.bones[layer.palette[0]].scales.empty()) {
                    layer.paletteSize=palette;layer.vertices=count;
                    const auto first=verts.At(U16(s,lookup.At(firstVertex)));
                    std::copy_n(m.data+first,layer.firstVertex.size(),layer.firstVertex.begin());
                }
            }
            if (scroll) {draw.uvTrack=uint32_t(candidate.scroll.size());candidate.scroll.push_back(std::move(layer));}
            continue;
        }
        // Raw shader 0/16 with one texture is retained; GPU fidelity still needs
        // a live material comparison. Unlit/unfogged/two-sided flags have the same
        // meaning as the common renderer flags. No guessed multi-texture combiner.
        const auto transform=U16(m,animLookup.At(anim));
        const bool hasUv=animatedUv && transform!=0xffff;
        if ((s.data[at]!=16 && !(hasUv && !s.data[at])) || s.data[at+1] != 0 || U16(s, at+8) != 0xffff || U16(s, at+12) != 0 ||
            U16(s, at+14) != 1 || (draw.shader != 0 && draw.shader != 16) || draw.texture >= textures.count ||
            (draw.materialFlags&~7u) || draw.blend > 1 || U16(m, uvLookup.At(uv)) != 0 ||
            U16(m, opacityLookup.At(alpha)) >= opacity.count || (!hasUv && transform!=0xffff))
            return ModelError::Material;
        if(hasUv){
            ScrollingLayer layer;
            const auto parsed=ReadScrollingLayer(m,transform,layer,remaining);
            if(parsed!=ModelError::None)return parsed;
            draw.uvTrack=uint32_t(candidate.scroll.size());
            candidate.scroll.push_back(std::move(layer));
        }
        const auto sm = submeshes.At(sub);
        const uint32_t firstVertex = U16(s, sm+4), count = U16(s, sm+6), combo = U16(s, sm+14);
        draw.firstIndex = U16(s, sm+8); draw.indexCount = U16(s, sm+10);
        if (U16(s, sm+2) || !count || firstVertex >= lookup.count || count > lookup.count-firstVertex ||
            !draw.indexCount || draw.indexCount%3 || draw.firstIndex%3 || draw.firstIndex >= triangles.count ||
            draw.indexCount > triangles.count-draw.firstIndex || combo >= boneLookup.count)
            return ModelError::Geometry;
        const auto palette=U16(s,sm+12), influences=U16(s,sm+16);
        if(!palette || palette>boneLookup.count-combo || !influences || influences>4)return ModelError::Pose;
        if(!motion && (palette!=1 || influences!=1 || U16(s,sm+18)!=0))return ModelError::Geometry;
        bool drawable=true;
        for (uint32_t j = firstVertex; j < firstVertex+count; ++j) {
            if(!motion){
                const uint32_t bone=s.data[skinBones.At(j)];
                if(bone!=0 || U16(m,boneLookup.At(combo+bone))!=0)return ModelError::Pose;
            }else for(unsigned k=0;k<4;++k){
                const auto& influence=animation.vertices[j];if(!influence.weights[k])continue;
                const auto bone=s.data[skinBones.At(j)+k];
                if(k>=influences || bone>=palette || U16(m,boneLookup.At(combo+bone))!=influence.bones[k])return ModelError::Pose;
            }
        }
        for (uint32_t j = draw.firstIndex; j < draw.firstIndex+draw.indexCount; ++j) {
            const auto vertex=candidate.indices[j];
            if (vertex < firstVertex || vertex >= firstVertex+count) return ModelError::Geometry;
            if(partialMotion && drawable){
                const auto& influence=animation.vertices[vertex];
                for(unsigned k=0;k<4;++k)
                    if(influence.weights[k] && omittedBones[influence.bones[k]])drawable=false;
            }
        }
        if(!drawable)draw.indexCount=0;
    }
    if (rest || motion) {
        candidate.draws.erase(std::remove_if(candidate.draws.begin(),candidate.draws.end(),[](const ModelDraw& d){return !d.indexCount;}),candidate.draws.end());
        if (scroll && candidate.draws.size()!=candidate.scroll.size()) return ModelError::Material;
        if (scroll) for (unsigned i=0;i<candidate.draws.size();++i) {
            const auto& draw=candidate.draws[i];
            auto& centre=candidate.scroll[i].centre;
            for (unsigned j=0;j<draw.indexCount;++j) {
                const auto& vertex=candidate.vertices[candidate.indices[draw.firstIndex+j]];
                for (unsigned axis=0;axis<3;++axis) centre[axis]+=vertex.position[axis]/draw.indexCount;
            }
        }
        if (candidate.draws.empty()) return motion ? ModelError::Pose : ModelError::Material;
    }
    if (candidate.Bytes() > limit || (motion && animation.Bytes()>limit-candidate.Bytes()))return ModelError::Budget;
    if(motion){
        // A conservative envelope over the complete clip avoids bind-pose culling.
        // Normalised nonnegative vertex weights keep every blend inside this union.
        struct BoneExtent {float scale=1, shift=0;};
        std::array<BoneExtent,256> bounds{};
        for(std::size_t i=0;i<animation.bones.size();++i){
            const auto& bone=animation.bones[i];auto& bound=bounds[i];
            for(const auto& key:bone.scales)for(float value:key.value)
                bound.scale=std::max(bound.scale,std::fabs(value));
            for(unsigned a=0;a<3;++a){
                bound.shift=std::max(bound.shift,std::fabs(bone.pivot[a]));
                for(const auto& key:bone.translations)
                    bound.shift=std::max(bound.shift,std::fabs(bone.pivot[a]+key.value[a]));
            }
        }
        float extent=0;
        for(std::size_t i=0;i<candidate.vertices.size();++i)for(unsigned j=0;j<4;++j){
            const auto& influence=animation.vertices[i];if(!influence.weights[j])continue;
            const auto& bone=animation.bones[influence.bones[j]];double radius=0;
            for(unsigned a=0;a<3;++a){const double d=double(candidate.vertices[i].position[a])-bone.pivot[a];radius+=d*d;}
            const auto& bound=bounds[influence.bones[j]];
            extent=std::max(extent,static_cast<float>(std::sqrt(radius))*bound.scale+bound.shift);
        }
        if(!std::isfinite(extent))return ModelError::Pose;
        candidate.minimum.fill(-extent);candidate.maximum.fill(extent);
        *motion=std::move(animation);
    }
    output = std::move(candidate);
    return ModelError::None;
}

std::size_t ModelMotion::Bytes() const
{
    std::size_t n=sizeof(*this)+bones.capacity()*sizeof(Bone)+vertices.capacity()*sizeof(Influence);
    for(const auto& bone:bones)n+=(bone.translations.capacity()+bone.scales.capacity())*sizeof(Translation)+bone.rotations.capacity()*sizeof(Rotation);
    return n;
}
ModelError DecodeStaticModel(Bytes m,Bytes s,std::size_t limit,StaticModel& output)
{return DecodeModel(m,s,limit,output,nullptr);}
ModelError DecodeScrollingModel(Bytes m,Bytes s,std::size_t limit,StaticModel& output,ModelMotion* motion)
{return DecodeModel(m,s,limit,output,motion,true,true);}
ModelError DecodeRestModel(Bytes m,Bytes s,std::size_t limit,StaticModel& output)
{return DecodeModel(m,s,limit,output,nullptr,true);}
ModelError DecodeLoopingModel(Bytes m,Bytes s,std::size_t limit,StaticModel& mesh,ModelMotion& motion)
{return DecodeModel(m,s,limit,mesh,&motion);}
namespace
{
template<class Key> auto Sample(const std::vector<Key>& keys,uint32_t time,decltype(Key{}.value) fallback)
{
    if(keys.empty())return fallback;
    if(time<=keys.front().time)return keys.front().value;
    const auto next=std::upper_bound(keys.begin(),keys.end(),time,[](uint32_t t,const Key& k){return t<k.time;});
    if(next==keys.end())return keys.back().value;
    const auto& first=*(next-1);const float t=float(time-first.time)/float(next->time-first.time);
    auto last=next->value;
    if constexpr(std::tuple_size<decltype(last)>::value==4){
        float dot=0;for(unsigned a=0;a<4;++a)dot+=first.value[a]*last[a];
        if(dot<0)for(auto& v:last)v=-v;
    }
    for(unsigned a=0;a<last.size();++a)last[a]=first.value[a]+t*(last[a]-first.value[a]);
    return last;
}
}
// Fit a short run of native scales against the authored loop. An equally good
// match elsewhere in the loop can move in the opposite direction next frame.
bool MatchLoopPhase(const ModelMotion& motion,const ScrollingLayer& layer,const WaterPosePlacement& placement,uint32_t& phase)
{
    if(!motion.duration || motion.duration>60000 || !layer.paletteSize || layer.paletteSize>kWaterPoseBones ||
       placement.bones!=layer.paletteSize || placement.samples<2 || placement.samples>4)return false;
    const auto count=std::min(1000u,motion.duration);
    std::vector<float> errors(count);
    for(unsigned i=0;i<count;++i){
        const unsigned start=i*motion.duration/count;
        float error=0;
        for(unsigned n=0;n<placement.samples;++n){
            const auto& obs=placement.observed[n];
            const auto t=(start+(obs.tick-placement.observed[0].tick))%motion.duration;
            for(unsigned b=0;b<layer.paletteSize;++b){
                if(layer.palette[b]>=motion.bones.size())return false;
                const auto expected=Sample(motion.bones[layer.palette[b]].scales,t,std::array<float,3>{1,1,1});
                for(unsigned axis=0;axis<3;++axis){
                    const float d=expected[axis]-obs.scales[b][axis];error+=d*d;
                }
            }
        }
        errors[i]=error/placement.samples;
    }
    const auto best=unsigned(std::min_element(errors.begin(),errors.end())-errors.begin());
    if(!(errors[best]<.003f))return false;
    for(unsigned i=0;i<count;++i){
        const auto delta=i>best ? i-best : best-i;
        if(std::min(delta,count-delta)*motion.duration/count>100 && errors[i]<errors[best]+.001f)return false;
    }
    phase=best*motion.duration/count;
    return true;
}
bool SampleModelPose(const ModelMotion& motion,uint32_t milliseconds,ModelPose& output)
{
    if(!motion.duration || motion.bones.empty() || motion.bones.size()>output.size())return false;
    const auto time=milliseconds%motion.duration;
    for(unsigned b=0;b<motion.bones.size();++b){
        const auto& bone=motion.bones[b];const auto translation=Sample(bone.translations,time,std::array<float,3>{});
        auto q=Sample(bone.rotations,time,std::array<float,4>{0,0,0,1});
        double norm=0;for(auto v:q)norm+=double(v)*v;if(norm<1e-12)return false;
        for(auto& v:q)v=static_cast<float>(v/std::sqrt(norm));
        const auto x=q[0],y=q[1],z=q[2],w=q[3];auto& m=output[b].position;
        m={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0,
           2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0,
           2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0};
        const auto scale=Sample(bone.scales,time,std::array<float,3>{1,1,1});
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){
            const float r=m[row*4+col];
            output[b].normal[row*3+col]=std::fabs(scale[col])>1e-6f?r/scale[col]:0.f;
            m[row*4+col]=r*scale[col];
        }
        for(unsigned a=0;a<3;++a){m[a*4+3]=translation[a]+bone.pivot[a];
            for(unsigned k=0;k<3;++k)m[a*4+3]-=m[a*4+k]*bone.pivot[k];}
    }
    return true;
}
ModelVertex DeformModelVertex(const ModelVertex& source,const ModelMotion::Influence& influence,const ModelPose& pose)
{
    ModelVertex result;result.uv=source.uv;
    for(unsigned j=0;j<4;++j){if(!influence.weights[j])continue;
        const float weight=influence.weights[j]/255.f;const auto& bone=pose[influence.bones[j]];const auto& m=bone.position;
        for(unsigned a=0;a<3;++a){float p=m[a*4+3],n=0;
            for(unsigned k=0;k<3;++k){p+=m[a*4+k]*source.position[k];n+=bone.normal[a*3+k]*source.normal[k];}
            result.position[a]+=weight*p;result.normal[a]+=weight*n;}
    }
    // The vertex shader normalises after the instance transform.
    return result;
}
}
