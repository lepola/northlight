#pragma once
#include "world_gi.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Portable, camera-independent static caster transport. GPU resources belong to
// the consumer; this worker only reads the already-filtered FGS2/FGS3 cache.
namespace StaticShadow {
using NorthlightGI::Vec3;
struct Vertex { float x=0,y=0,z=0,u=0,v=0; };
static_assert(sizeof(Vertex)==20,"Compact shadow vertex ABI");
struct Batch { uint32_t firstIndex=0,indexCount=0,material=0; Vec3 low,high; };
struct Model {
    std::string key;
    uint64_t contentRevision=0;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    // Indices and compact alpha masks are prepared by the file worker.
    std::vector<uint16_t> uploadIndices16;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> uploadAlpha;
    std::shared_ptr<const Model> drawModel;
    bool uploadPrepared=false;
    std::vector<Batch> batches;
    std::vector<NorthlightGI::WorldMaterial> materials;
    std::vector<uint64_t> materialKeys;
    size_t bytes=0;
    bool index16=false;
    size_t indexCount()const{return uploadPrepared&&index16?uploadIndices16.size():indices.size();}
    uint32_t indexAt(size_t i)const{return uploadPrepared&&index16?uploadIndices16[i]:indices[i];}
    uint8_t alphaAt(size_t material,size_t pixel)const{
        if(uploadPrepared&&material<uploadAlpha.size()&&uploadAlpha[material]){
            const auto& alpha=*uploadAlpha[material];return pixel<alpha.size()?alpha[pixel]:255;
        }
        const auto& rgba=materials[material].texels();
        return pixel<rgba.size()/4?rgba[pixel*4+3]:255;
    }
};
struct Placement {
    uint64_t uid=0;
    uint32_t category=0;
    std::string modelKey;
    float matrix[9]={1,0,0,0,1,0,0,0,1}; // row-major, column vector
    Vec3 translation,low,high;
};
struct Request {
    std::string map;
    Vec3 center;
    Vec3 directions[2]={{0,0,1},{0,0,1}}; // toward the source
    bool active[2]={true,false}; // may include an upcoming source for prewarm
    bool allowLoads=true; // admission pressure never removes already-ready casters
};
struct Stats {
    uint64_t sharedModelSources=0,compactDecodes=0;
    uint64_t metadataReads=0,modelReads=0,modelReuses=0,publications=0,cancellations=0,failures=0,deferred=0;
    uint64_t bytesRead=0,cpuBytes=0,cpuPeak=0,transientPeak=0,largestModel=0,metadataBytes=0,metadataPeak=0;
    uint32_t cells=0,tiles=0,placements=0,models=0,pendingModels=0,pendingTiles=0;
    double readMs=0,packMs=0,selectionMs=0,maxJobMs=0,latencyMs=0;
    bool complete=false;
};
struct Snapshot {
    uint64_t mapGeneration=0,revision=0;
    std::string map;
    std::vector<Placement> placements;
    std::map<std::string,std::shared_ptr<const Model>> models;
    Stats stats;
    // Index-based groups survive vector relocation. The identity marker rejects
    // copied snapshots whose placements may have been edited by a caller.
    std::map<std::string,std::vector<size_t>> placementGroups;
    const Snapshot* preparedIdentity=nullptr;
    uint64_t geometryRevision=0;
    size_t retirementBytes=0;
};
struct Limits {
    size_t residentBytes=96u*1024u*1024u;
    size_t maxDecodeBytes=64u*1024u*1024u;
    size_t metadataBytes=32u*1024u*1024u;
    unsigned cellSize=128;
    unsigned retainSeconds=30;
};
// Same conservative light-space box as the cached 240u far shadow frame,
// plus 96u publication/cache drift margin. No camera vector is an input.
bool selected(const Request&,Vec3 low,Vec3 high,float margin=96.f);
Vec3 transformPoint(const Placement&,Vec3);
// Conservative drawable bounds, including local/world float cancellation and
// shader roundoff. Invalid inputs return nonfinite bounds (culling fails open).
void transformBounds(const Placement&,Vec3 low,Vec3 high,Vec3& outLow,Vec3& outHigh);
bool packModel(NorthlightGI::WorldScene&&,const std::string& key,Model&,std::string& error);
// Reads an already validated immutable decode without taking its payload ownership.
bool packSharedModel(const NorthlightGI::WorldScene&,const std::string& key,Model&,std::string& error);
class Streamer {
public:
    explicit Streamer(std::string cacheRoot,Limits limits=Limits(),NorthlightGI::SceneSourceProvider sourceProvider={});
    ~Streamer();
    Streamer(const Streamer&)=delete;
    Streamer& operator=(const Streamer&)=delete;
    void request(const Request&);
    // Frame-boundary recovery when the consumer has no drawable world. Cancels
    // publication, frees worker-owned caches asynchronously, and prevents reads
    // until resumed. External immutable snapshot holders stay valid.
    void setPaused(bool);
    std::shared_ptr<const Snapshot> snapshot() const;
    // Native offline tests/diagnostics only; never block the render thread.
    bool waitIdle(unsigned timeoutMs=30000) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
