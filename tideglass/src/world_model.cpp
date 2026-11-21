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
            // Other batches in this model can depend only on independent bones with embedded tracks.
            omitted[b]=(U32(m,at+4)&~512u) || parent!=0xffff || !EmptyTrack(m,at+56) ||
                       U16(m,at+18)!=0xffff || U16(m,at+38)!=0xffff;
            if(omitted[b])continue;
            if(pass)std::copy(pivot,pivot+3,candidate.bones[b].pivot.begin());
            for(unsigned rotation=0;rotation<2;++rotation){
                const auto track=at+(rotation?36:16);if(EmptyTrack(m,track))continue;
                Table times,values,t,v;
                if(U16(m,track)>1 || U16(m,track+2)!=0xffff ||
                   !Array(m,track+4,8,304,times) || !Array(m,track+12,8,304,values) ||
                   times.count!=sequences.count || values.count!=times.count ||
                   !Array(m,times.At(canonical),4,304,t) || !Array(m,values.At(canonical),rotation?8:12,304,v) ||
                   !t.count || t.count!=v.count || t.count>65536 || (t.count>1 && U16(m,track)!=1))return ModelError::Pose;
                if(!pass && !charge(t.count,rotation?sizeof(ModelMotion::Rotation):sizeof(ModelMotion::Translation)))return ModelError::Budget;
                if(pass){if(rotation)candidate.bones[b].rotations.resize(t.count);else candidate.bones[b].translations.resize(t.count);}
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
                        if(pass)candidate.bones[b].translations[k]={time,value};
                    }
                }
            }
        }
    }
    if(candidate.Bytes()>limit)return ModelError::Budget;
    out=std::move(candidate);return ModelError::None;
}

}

static ModelError DecodeModel(world::Bytes m, world::Bytes s, std::size_t limit, StaticModel& output, ModelMotion* motion, bool rest=false)
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
    if (!rest && (!bones.count || bones.count>65536)) return ModelError::Pose;
    if (!motion && !rest && (U32(m, bones.At(0)+4) != 0 || U16(m, bones.At(0)+8) != 0xffff ||
        !EmptyTrack(m, bones.At(0)+16) || !EmptyTrack(m, bones.At(0)+36) ||
        !EmptyTrack(m, bones.At(0)+56))) return ModelError::Pose;
    if (!rest) {
    if (U32(m, 72) || U32(m, 96)) return ModelError::Material; // colour and texture-transform tracks
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
    for (uint32_t i = 0; i < lookup.count; ++i) {
        const auto source = U16(s, lookup.At(i));
        if (source >= verts.count) return ModelError::Geometry;
        const auto at = verts.At(source);
        auto& v = candidate.vertices[i];
        if (!Floats(m, at, v.position.data(), 3) || !Floats(m, at+20, v.normal.data(), 3) ||
            !Floats(m, at+32, v.uv.data(), 2)) return ModelError::Geometry;
        // Initial supported sources are fully weighted to one unanimated root.
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
        if (rest) {
            // A placed model keeps its solid and cut-out layers, from their first texture and at rest. A layer that
            // only makes sense animated or blended over the others (glow, reflection, fading) is left out, not the model.
            const auto opacityTrack = U16(m, opacityLookup.At(alpha));
            draw.indexCount = 0;
            if (draw.blend > 1 || U16(s, at+8) != 0xffff || U16(s, at+12) != 0 || U16(m, uvLookup.At(uv)) != 0 ||
                draw.texture >= textures.count || candidate.textures[draw.texture].name.empty() ||
                opacityTrack >= opacity.count || !FullOpacity(m, opacity.At(opacityTrack))) continue;
            draw.materialFlags &= 7;
            const auto sm = submeshes.At(sub);
            const uint32_t firstVertex = U16(s, sm+4), count = U16(s, sm+6), combo = U16(s, sm+14);
            draw.firstIndex = U16(s, sm+8); draw.indexCount = U16(s, sm+10);
            if (!count || firstVertex >= lookup.count || count > lookup.count-firstVertex ||
                !draw.indexCount || draw.indexCount%3 || draw.firstIndex%3 || draw.firstIndex >= triangles.count ||
                draw.indexCount > triangles.count-draw.firstIndex || combo > boneLookup.count) return ModelError::Geometry;
            for (uint32_t j = draw.firstIndex; j < draw.firstIndex+draw.indexCount; ++j)
                if (candidate.indices[j] < firstVertex || candidate.indices[j] >= firstVertex+count) return ModelError::Geometry;
            continue;
        }
        // Raw shader 0/16 with one texture is retained; GPU fidelity still needs
        // a live material comparison. Unlit/unfogged/two-sided flags have the same
        // meaning as the common renderer flags. No guessed multi-texture combiner.
        if (s.data[at] != 16 || s.data[at+1] != 0 || U16(s, at+8) != 0xffff || U16(s, at+12) != 0 ||
            U16(s, at+14) != 1 || (draw.shader != 0 && draw.shader != 16) || draw.texture >= textures.count ||
            (draw.materialFlags&~7u) || draw.blend > 1 || U16(m, uvLookup.At(uv)) != 0 ||
            U16(m, opacityLookup.At(alpha)) >= opacity.count || U16(m, animLookup.At(anim)) != 0xffff)
            return ModelError::Material;
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
    if (rest || partialMotion) {
        candidate.draws.erase(std::remove_if(candidate.draws.begin(),candidate.draws.end(),[](const ModelDraw& d){return !d.indexCount;}),candidate.draws.end());
        if (candidate.draws.empty()) return motion ? ModelError::Pose : ModelError::Material;
    }
    if (candidate.Bytes() > limit || (motion && animation.Bytes()>limit-candidate.Bytes()))return ModelError::Budget;
    if(motion){
        // A conservative envelope over the complete clip avoids bind-pose culling.
        // Normalised nonnegative vertex weights keep every blend inside this union.
        float extent=0;
        for(std::size_t i=0;i<candidate.vertices.size();++i)for(unsigned j=0;j<4;++j){
            const auto& influence=animation.vertices[i];if(!influence.weights[j])continue;
            const auto& bone=animation.bones[influence.bones[j]];double radius=0;
            for(unsigned a=0;a<3;++a){const double d=double(candidate.vertices[i].position[a])-bone.pivot[a];radius+=d*d;}
            const float r=static_cast<float>(std::sqrt(radius));
            for(unsigned a=0;a<3;++a){float shift=std::fabs(bone.pivot[a]);
                for(const auto& key:bone.translations)shift=std::max(shift,std::fabs(bone.pivot[a]+key.value[a]));
                extent=std::max(extent,r+shift);}
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
    for(const auto& bone:bones)n+=bone.translations.capacity()*sizeof(Translation)+bone.rotations.capacity()*sizeof(Rotation);
    return n;
}
ModelError DecodeStaticModel(Bytes m,Bytes s,std::size_t limit,StaticModel& output)
{return DecodeModel(m,s,limit,output,nullptr);}
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
bool SampleModelPose(const ModelMotion& motion,uint32_t milliseconds,ModelPose& output)
{
    if(!motion.duration || motion.bones.empty() || motion.bones.size()>output.size())return false;
    const auto time=milliseconds%motion.duration;
    for(unsigned b=0;b<motion.bones.size();++b){
        const auto& bone=motion.bones[b];const auto translation=Sample(bone.translations,time,std::array<float,3>{});
        auto q=Sample(bone.rotations,time,std::array<float,4>{0,0,0,1});
        double norm=0;for(auto v:q)norm+=double(v)*v;if(norm<1e-12)return false;
        for(auto& v:q)v=static_cast<float>(v/std::sqrt(norm));
        const auto x=q[0],y=q[1],z=q[2],w=q[3];auto& m=output[b];
        m={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),0,
           2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),0,
           2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),0};
        for(unsigned a=0;a<3;++a){m[a*4+3]=translation[a]+bone.pivot[a];
            for(unsigned k=0;k<3;++k)m[a*4+3]-=m[a*4+k]*bone.pivot[k];}
    }
    return true;
}
ModelVertex DeformModelVertex(const ModelVertex& source,const ModelMotion::Influence& influence,const ModelPose& pose)
{
    ModelVertex result;result.uv=source.uv;
    for(unsigned j=0;j<4;++j){if(!influence.weights[j])continue;
        const float weight=influence.weights[j]/255.f;const auto& m=pose[influence.bones[j]];
        for(unsigned a=0;a<3;++a){float p=m[a*4+3],n=0;
            for(unsigned k=0;k<3;++k){p+=m[a*4+k]*source.position[k];n+=m[a*4+k]*source.normal[k];}
            result.position[a]+=weight*p;result.normal[a]+=weight*n;}
    }
    // The vertex shader normalises after the instance transform.
    return result;
}
}
