#undef NDEBUG
#include "model_light_attachments.h"
#include "replay_shadow_policy.h"
#include "sampled_vertex_cache.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightLightAttachments;
static void matrix(float* bank,unsigned slot,float x,float y,float z,bool rotate=false){
    float* r=bank+4*(31+3*slot);std::fill(r,r+12,0.f);
    if(rotate){r[1]=-1;r[4]=1;}else r[0]=r[5]=1;
    r[10]=1;r[3]=x;r[7]=y;r[11]=z;
}
int main(int argc,char** argv){
    if(argc==3){Records invalid;return readFile(argv[1],invalid)?1:0;}
    assert(argc==2);Records loaded;assert(readFile(argv[1],loaded)&&loaded.size()==2);
    Tracker tracker;tracker.bind(std::make_shared<const Records>(loaded));
    float constants[1024]{},inverse[16]{};for(unsigned i=0;i<16;i+=5)inverse[i]=1;
    matrix(constants,0,10,20,30);matrix(constants,1,11,21,32,true);
    tracker.observe(loaded[0].witness,constants,31,inverse);
    assert(tracker.positions.size()==1&&tracker.positions.count(42));
    const std::array<float,3> expected={9,22,35};assert(tracker.positions.at(42)==expected);
    const NorthlightLocalLights::Light light{{1,2,3},{.2f,.5f,1},1,12,42,3,NorthlightLocalLights::CastShadow};
    std::vector<NorthlightLocalLights::Light> fixed{light},output;
    const auto& moved=tracker.apply(fixed,output);assert(moved.size()==1);
    assert(std::equal(expected.begin(),expected.end(),moved[0].position));
    assert(!std::memcmp(&moved[0].diffuse,&light.diffuse,sizeof(light)-sizeof(light.position)));
    assert(fixed[0].position[0]==1); // immutable worker publication
    auto wrong=loaded[0].witness;wrong[1]+=.01f;tracker.observe(wrong,constants,31,inverse);assert(tracker.positions.size()==1);
    // The other copy of the same lamp must be matched by its reference matrix.
    matrix(constants,0,100,20,30);matrix(constants,1,101,21,32,true);
    tracker.observe(loaded[0].witness,constants,31,inverse);assert(tracker.positions.size()==2&&tracker.positions.count(43));
    assert(tracker.positions.at(42)==expected&&tracker.positions.at(43)[0]==99);
    // Camera translation is removed by the same inverse view as model replay.
    inverse[12]=1000;inverse[13]=-100;matrix(constants,0,-990,120,30);matrix(constants,1,-989,121,32,true);
    tracker.observe(loaded[0].witness,constants,31,inverse);assert(tracker.positions.at(42)==expected);
    inverse[0]=inverse[5]=0;inverse[1]=1;inverse[4]=-1;
    matrix(constants,0,120,990,30);float* reference=constants+4*31;reference[0]=reference[5]=0;reference[1]=1;reference[4]=-1;
    matrix(constants,1,121,989,32);tracker.observe(loaded[0].witness,constants,31,inverse);assert(tracker.positions.at(42)==expected);
    inverse[0]=inverse[5]=1;inverse[1]=inverse[4]=0;
    float camera[]={9,22,35};tracker.prune(fixed,camera);assert(tracker.positions.size()==1);
    camera[0]=10000;tracker.prune(fixed,camera);assert(tracker.positions.empty());
    tracker.bind(nullptr);assert(!tracker.active()&&tracker.positions.empty());
    tracker.bind(std::make_shared<const Records>(loaded));
    matrix(constants,0,-980,120,30);tracker.observe(loaded[0].witness,constants,31,inverse);assert(tracker.positions.empty());
    matrix(constants,0,-990,120,30);constants[4*34]=std::numeric_limits<float>::quiet_NaN();
    tracker.observe(loaded[0].witness,constants,31,inverse);assert(tracker.positions.empty());
    tracker.observe(loaded[0].witness,constants,255,inverse);assert(tracker.positions.empty());
    using NorthlightReplayShadowPolicy::small;
    for(unsigned minimum:{0u,99u,100u,500u})for(unsigned count:{1u,30u,98u,99u,100u})assert(!small(true,count,minimum,true));
    assert(small(true,99,100)&&!small(true,99,99)&&!small(false,1,100));
    // Lane-specific proofs must never reuse a cached four-lane verdict.
    NorthlightActorDeformation::RigidBoneCache bones(8);NorthlightActorDeformation::Program program;program.inputs.push_back({1,2,0});
    auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->streams[0].stride=4;
    mesh->streams[0].bytes={1,7,8,9,1,2,3,4,1,0,0,0};
    D3DVERTEXELEMENT9 declaration[]={{0,0,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
    assert(bones.bone(program,*mesh,mesh,declaration,declaration,2,1)==1);
    assert(std::isnan(bones.bone(program,*mesh,mesh,declaration,declaration,2)));
    assert(bones.bone(program,*mesh,mesh,declaration,declaration,2,1)==1&&bones.hits==1);
    std::puts("PASS attachments: live pose, exact instance, inverse camera, immutable properties, invalid inputs, reset; rigid 99/100 threshold");
}
