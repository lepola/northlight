#pragma once
#include "actor_deformation.h"
#include "actor_texture.h"
#include "world_gi.h"
#include <chrono>
#include <memory>

// Immutable, CPU-only packets. Texture readback and stream snapshots are complete
// before queueing. Texture decoding and resolve() run on the existing GI worker,
// never the render thread. No D3D interfaces or game pointers survive in this job.
namespace NorthlightActorGeometry {
struct Result {std::shared_ptr<const NorthlightGI::WorldScene> scene;std::uint64_t hash=0;unsigned texturesDecoded=0;std::size_t textureEncodedBytes=0;double decodeMs=0,geometryMs=0,hashMs=0;};
inline std::uint64_t sceneHash(const NorthlightGI::WorldScene& scene){
    std::uint64_t hash=0xcbf29ce484222325ull;
    auto mix=[&](std::uint32_t value){for(unsigned j=0;j<4;++j){hash^=(value>>(j*8))&255;hash*=0x100000001b3ull;}};
    auto scalar=[&](float value){std::uint32_t bits;std::memcpy(&bits,&value,4);mix(bits);};
    // Pose data stays full precision. Quantization affects only generation keys
    // so inverse-camera roundoff does not continuously invalidate probe lighting.
    for(auto& v:scene.vertices){mix(std::uint32_t(std::int32_t(std::llround(v.position.x*32))));mix(std::uint32_t(std::int32_t(std::llround(v.position.y*32))));mix(std::uint32_t(std::int32_t(std::llround(v.position.z*32))));scalar(v.u);scalar(v.v);}
    for(auto& t:scene.triangles){mix(t.v0);mix(t.v1);mix(t.v2);mix(t.material);}
    for(auto& m:scene.materials){scalar(m.albedo.x);scalar(m.albedo.y);scalar(m.albedo.z);scalar(m.alphaCutoff);mix(m.width);mix(m.height);mix(m.addressU);mix(m.addressV);
        for(auto byte:m.rgba){hash^=byte;hash*=0x100000001b3ull;}}
    return hash;
}
struct Packet {
    NorthlightDrawSnapshot::Mesh mesh; // owned fallback for transient UP/dynamic captures
    std::shared_ptr<const NorthlightDrawSnapshot::Mesh> sharedMesh;
    const NorthlightDrawSnapshot::Mesh& meshView()const{return sharedMesh?*sharedMesh:mesh;}
    NorthlightActorDeformation::Program position,uv;
    std::vector<D3DVERTEXELEMENT9> elements;
    std::array<float,1024> constants{};
    std::array<float,16> inverseView{};
    NorthlightGI::WorldMaterial material;
    NorthlightActorTexture::Snapshot texture;
    bool hasUV=false,alphaTest=false;
};
struct ActorJob {
    NorthlightGI::Vec3 center;
    std::vector<Packet> packets;
    Result resolve()const{
        using namespace NorthlightGI;
        const auto resolveStart=std::chrono::steady_clock::now();double decodeMs=0;
        auto scene=std::make_shared<WorldScene>();
        // One bounded, metadata-only cache per worker; it keeps no jobs or
        // mutable resources alive and does not memoize any evaluated pose.
        thread_local NorthlightActorDeformation::PreparedBindingCache bindingCache;
        unsigned evaluated=0,texturesDecoded=0;std::size_t textureEncodedBytes=0;
        for(auto& packet:packets){
            const auto& mesh=packet.meshView();if(mesh.primitiveCount==0||mesh.primitiveCount>87380||mesh.vertexCount>16384-evaluated)continue;
            if(mesh.topology!=D3DPT_TRIANGLELIST&&mesh.topology!=D3DPT_TRIANGLESTRIP)continue;
            unsigned required=mesh.topology==D3DPT_TRIANGLELIST?mesh.primitiveCount*3:mesh.primitiveCount+2;
            if(mesh.indexed?mesh.indices.size()<required:mesh.vertexCount<required)continue;
            evaluated+=mesh.vertexCount;
            std::vector<NorthlightActorDeformation::Position> positions;
            auto positionBindings=bindingCache.acquire(packet.position,mesh,packet.elements.data(),packet.elements.size());
            if(!positionBindings||!NorthlightActorDeformation::worldPositionsPrepared(packet.position,mesh,*positionBindings,packet.constants.data(),packet.inverseView.data(),positions))continue;
            std::vector<std::array<float,2>> uvs;
            auto uvBindings=packet.hasUV?bindingCache.acquire(packet.uv,mesh,packet.elements.data(),packet.elements.size()):nullptr;
            bool exactUV=uvBindings&&NorthlightActorDeformation::textureUVsPrepared(packet.uv,mesh,*uvBindings,packet.constants.data(),uvs);
            if(packet.alphaTest&&!exactUV)continue;
            std::vector<WorldTriangle> triangles;triangles.reserve(mesh.primitiveCount);
            auto index=[&](UINT i){return mesh.indexed?mesh.indices[i]:i;};
            for(UINT triangle=0;triangle<mesh.primitiveCount;++triangle){UINT a,b,c;
                if(mesh.topology==D3DPT_TRIANGLELIST){a=index(triangle*3);b=index(triangle*3+1);c=index(triangle*3+2);}
                else if(mesh.topology==D3DPT_TRIANGLESTRIP){a=index(triangle);b=index(triangle+1);c=index(triangle+2);if(triangle&1)std::swap(a,b);}else continue;
                if(a>=positions.size()||b>=positions.size()||c>=positions.size()||a==b||b==c||c==a)continue;
                auto& pa=positions[a];auto& pb=positions[b];auto& pc=positions[c];
                if(std::max({pa.x,pb.x,pc.x})<center.x-48||std::min({pa.x,pb.x,pc.x})>center.x+48||std::max({pa.y,pb.y,pc.y})<center.y-48||std::min({pa.y,pb.y,pc.y})>center.y+48||std::max({pa.z,pb.z,pc.z})<center.z-48||std::min({pa.z,pb.z,pc.z})>center.z+48)continue;
                triangles.push_back({a,b,c,0});}
            if(triangles.empty())continue;
            unsigned offset=unsigned(scene->vertices.size()),material=unsigned(scene->materials.size());WorldMaterial m=packet.material;
            if(exactUV&&!packet.texture.bytes.empty()){
                const auto& texture=packet.texture;
                const auto decodeStart=std::chrono::steady_clock::now();
                const bool decoded=NorthlightActorTexture::decode(texture.bytes.data(),texture.bytes.size(),texture.width,texture.height,texture.pitch,texture.format,m.rgba);
                decodeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-decodeStart).count();
                if(decoded){
                    m.width=texture.width;m.height=texture.height;m.albedo=Vec3(1,1,1);
                    ++texturesDecoded;textureEncodedBytes+=texture.bytes.size();
                }else{m.rgba.clear();m.width=m.height=0;m.albedo=Vec3(.35f,.35f,.35f);}
            }
            if(packet.alphaTest&&m.rgba.empty())continue;
            if(!exactUV){m.rgba.clear();m.width=m.height=0;m.albedo=Vec3(.35f,.35f,.35f);}
            scene->materials.push_back(std::move(m));
            for(unsigned j=0;j<positions.size();++j){auto& p=positions[j];WorldVertex v;v.position=Vec3(p.x,p.y,p.z);if(exactUV){v.u=uvs[j][0];v.v=uvs[j][1];}scene->vertices.push_back(v);}
            for(auto t:triangles){t.v0+=offset;t.v1+=offset;t.v2+=offset;t.material=material;
                auto& a=scene->vertices[t.v0];auto& b=scene->vertices[t.v1];auto& c=scene->vertices[t.v2];Vec3 normal=cross(b.position-a.position,c.position-a.position);
                a.normal=a.normal+normal;b.normal=b.normal+normal;c.normal=c.normal+normal;scene->triangles.push_back(t);}
            for(unsigned i=offset;i<scene->vertices.size();++i)scene->vertices[i].normal=normalized(scene->vertices[i].normal);
        }
        const auto hashStart=std::chrono::steady_clock::now();const double geometryMs=std::chrono::duration<double,std::milli>(hashStart-resolveStart).count()-decodeMs;
        std::uint64_t hash=sceneHash(*scene);const double hashMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hashStart).count();
        return {std::move(scene),hash,texturesDecoded,textureEncodedBytes,decodeMs,geometryMs,hashMs};
    }
};
} // namespace NorthlightActorGeometry
