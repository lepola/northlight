#include <chrono>
struct FrozenClock {
    using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<FrozenClock,duration>;
    static time_point now()noexcept{return time_point(duration(1000000));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK FrozenClock
#include "pose_metadata_reference.h"
#define NorthlightAutoDisplacement NorthlightAutoDisplacementUncached
#include "pose_metadata_control.h"
#undef NorthlightAutoDisplacement
#include <fstream>
#include <random>

static NorthlightActorDeformation::Program loadPoseProgram(const char* path){
    std::ifstream file(path,std::ios::binary);std::vector<uint32_t> words;uint32_t word;
    while(file.read(reinterpret_cast<char*>(&word),4))words.push_back(word);
    NorthlightActorDeformation::Program p;assert(NorthlightActorDeformation::compile(words.data(),words.size(),p,false,2));return p;
}
struct PoseDeclaration:Declaration {
    D3DVERTEXELEMENT9 layout[4]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,28,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
};
static std::shared_ptr<const NorthlightDrawSnapshot::Mesh> poseMesh(unsigned generation){
    auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=96;mesh->primitiveCount=32;
    mesh->streams[0].stride=32;mesh->streams[0].bytes.resize(96*32);
    for(unsigned i=0;i<96;++i){float values[]={float(i%3),float(i%7),float(i%11)+float(generation),.4f,.3f,.2f,.1f};
        auto* data=mesh->streams[0].bytes.data()+i*32;std::memcpy(data,values,28);for(unsigned k=0;k<4;++k)data[28+k]=static_cast<unsigned char>((i+k)%4);}
    return mesh;
}
static void posePalette(float* constants,unsigned frame){
    std::fill(constants,constants+1024,0.f);
    const float angle=float(frame)*.013f,c=std::cos(angle),s=std::sin(angle);
    for(unsigned bone=0;bone<75;++bone){const unsigned r=31+3*bone;
        constants[4*r]=c;constants[4*r+1]=s;constants[4*r+3]=float(bone)*.2f;
        constants[4*(r+1)]=-s;constants[4*(r+1)+1]=c;constants[4*(r+2)+2]=1;constants[4*(r+2)+3]=frame%4?100.f:0.f;}
}
void DisplacementStateTestAccess::poseMetadataChecks(const char* path){
    auto program=loadPoseProgram(path);assert(NorthlightReplayBounds::SkinEnvelope::supports(program));
    CaptureShader<IDirect3DVertexShader9> shaders[6];PoseDeclaration declarations[2];
    NorthlightAutoDisplacement prepared(nullptr);NorthlightAutoDisplacementUncached uncached(nullptr);
    auto owner=poseMesh(0);size_t compared=0;
    for(unsigned frame=0;frame<200;++frame){
        prepared.endFrame();uncached.endFrame();posePalette(prepared.constants,frame);std::copy(prepared.constants,prepared.constants+1024,uncached.constants);
        if(frame%13==0)owner=poseMesh(frame%29); // Every replacement remains a distinct immutable owner.
        if(frame==70){prepared.reset();uncached.reset();}
        for(unsigned draw=0;draw<12;++draw){auto* shader=&shaders[draw%6];auto& declaration=declarations[draw%2];
            NorthlightReplayBounds::Bounds a,b;
            const auto sa=prepared.poseEnvelope(program,owner,declaration.layout,4,prepared.constants,a,shader,&declaration);
            const auto sb=uncached.poseEnvelope(program,owner,declaration.layout,4,uncached.constants,b,shader,&declaration);
            assert(sa==sb&&a.valid==b.valid&&!std::memcmp(a.low,b.low,sizeof a.low)&&!std::memcmp(a.high,b.high,sizeof a.high));
            assert(prepared.envelopeBudget.operations==uncached.envelopeBudget.operations&&prepared.envelopeBudget.vertices==uncached.envelopeBudget.vertices);
            ++compared;
        }
    }
    assert(prepared.poseMetadata.entries()==6&&prepared.poseMetadata.bytes()<(2u<<20));
    for(auto& shader:shaders)assert(shader.refs==2);
    for(auto& decl:declarations)assert(decl.refs==4);
    // Registration invalidates metadata even if the replacement is unsupported.
    prepared.registerMaterial(&shaders[0],nullptr,0,5);assert(prepared.poseMetadata.entries()==5&&shaders[0].refs==1);
    prepared.setMemoryPressure(true);assert(!prepared.poseMetadata.entries()&&!prepared.poseMetadata.bytes());
    for(auto& shader:shaders)assert(shader.refs==1);
    for(auto& decl:declarations)assert(decl.refs==1);
    prepared.reset();
    // Failed optional preparation falls through to the original complete-key path.
    prepared.endFrame();posePalette(prepared.constants,20);NorthlightReplayBounds::Bounds a,b;
    failAllocationAfter=0;prepared.poseEnvelope(program,owner,declarations[0].layout,4,prepared.constants,a,&shaders[0],&declarations[0]);failAllocationAfter=-1;
    assert(!prepared.poseMetadata.entries()&&prepared.poseMetadata.stats().fallbacks==1);
    for(unsigned i=0;i<4;++i){prepared.endFrame();prepared.poseEnvelope(program,owner,declarations[0].layout,4,prepared.constants,a,&shaders[0],&declarations[0]);}
    assert(prepared.poseMetadata.entries()==1);
    prepared.reset();assert(shaders[0].refs==1&&declarations[0].refs==1);
    std::printf("MATERIAL_POSE_METADATA_PASS comparisons=%zu exact_status bounds_bits current_pose rotations owner_replacement quotas invalidation pressure reset allocation_fallback COM_lifetime\n",compared);
}
template<class G>double DisplacementStateTestAccess::poseMetadataTiming(const NorthlightActorDeformation::Program& program,unsigned draws,bool rotating){
    CaptureShader<IDirect3DVertexShader9> shaders[160];PoseDeclaration declaration;G cache(nullptr);
    std::vector<std::shared_ptr<const Mesh>> sources;for(unsigned i=0;i<16;++i)sources.push_back(poseMesh(i));
    double total=0;unsigned valid=0;
    for(unsigned frame=0;frame<240;++frame){cache.endFrame();posePalette(cache.constants,frame);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned draw=0;draw<draws;++draw){const auto& source=sources[(draw+frame)%sources.size()];NorthlightReplayBounds::Bounds bounds;
            auto* shader=&shaders[rotating?(frame*4+draw)%160:0];
            const auto status=cache.poseEnvelope(program,source,declaration.layout,4,cache.constants,bounds,shader,&declaration);valid+=status==NorthlightReplayBounds::Status::Valid;}
        if(frame>=40)total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    assert(valid);cache.reset();for(auto& shader:shaders)assert(shader.refs==1);assert(declaration.refs==1);return total/200;
}
int main(int argc,char** argv){
    assert(argc>=3);const std::string mode=argv[1];if(mode=="checks"){DisplacementStateTestAccess::poseMetadataChecks(argv[2]);return 0;}
    assert(argc==5);const auto program=loadPoseProgram(argv[2]);const unsigned draws=unsigned(std::atoi(argv[3]));const std::string workload=argv[4];const bool rotating=workload=="rotating";
    const double ms=mode=="prepared"?DisplacementStateTestAccess::poseMetadataTiming<NorthlightAutoDisplacement>(program,draws,rotating):DisplacementStateTestAccess::poseMetadataTiming<NorthlightAutoDisplacementUncached>(program,draws,rotating);
    std::printf("{\"variant\":\"%s\",\"workload\":\"%s\",\"draws\":%u,\"ms_per_frame\":%.9f}\n",mode.c_str(),workload.c_str(),draws,ms);
}
