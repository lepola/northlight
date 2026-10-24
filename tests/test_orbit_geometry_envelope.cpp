#include "fixtures/reference/rejected_orbit_envelope-fd6b3a4.h"
#include "scene_source_cache.h"
#include "world_probe_cache.h"
#include "world_probe_progress.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>
using namespace NorthlightGI;
using Envelope=NorthlightWorldStreaming::OrbitEnvelope;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point at){return std::chrono::duration<double,std::milli>(Clock::now()-at).count();}
thread_local bool northlightOrbitReferenceLoader=false;
struct Camera {uint64_t tick;Vec3 eye,forward;float distance;};
static bool same(Vec3 a,Vec3 b){return !std::memcmp(&a,&b,sizeof(a));}
static bool sameProbe(const Probe& a,const Probe& b){
    return same(a.position,b.position)&&!std::memcmp(a.sh,b.sh,sizeof(a.sh))&&
        !std::memcmp(a.moments,b.moments,sizeof(a.moments))&&a.samples==b.samples&&
        a.backFaceSamples==b.backFaceSamples&&a.valid==b.valid&&a.maxDistance==b.maxDistance;
}
static bool sameMaterial(const WorldMaterial& a,const WorldMaterial& b){
    return same(a.albedo,b.albedo)&&a.width==b.width&&a.height==b.height&&
        !std::memcmp(&a.alphaCutoff,&b.alphaCutoff,sizeof(float))&&a.addressU==b.addressU&&a.addressV==b.addressV&&
        a.terrain==b.terrain&&a.wmo==b.wmo&&a.rgba==b.rgba;
}
static void subset(const WorldScene& eye,const WorldScene& envelope){
    size_t found=0;std::map<std::pair<uint32_t,uint32_t>,bool> materials;
    for(const auto& b:envelope.triangles){if(found==eye.triangles.size())break;const auto& a=eye.triangles[found];
        if(std::memcmp(&eye.vertices[a.v0],&envelope.vertices[b.v0],sizeof(WorldVertex))||
           std::memcmp(&eye.vertices[a.v1],&envelope.vertices[b.v1],sizeof(WorldVertex))||
           std::memcmp(&eye.vertices[a.v2],&envelope.vertices[b.v2],sizeof(WorldVertex)))continue;
        auto key=std::make_pair(a.material,b.material);auto old=materials.find(key);
        if(old==materials.end())old=materials.emplace(key,sameMaterial(eye.materials[a.material],envelope.materials[b.material])).first;
        if(old->second)++found;
    }
    assert(found==eye.triangles.size());
}
static void unit(){
    Envelope policy;Vec3 out(17,18,19),eye(-50,0,0),self;
    assert(!policy.select("A",eye,&self,1000,out)&&same(out,Vec3(17,18,19)));
    for(uint32_t t=1100;t<2000;t+=100)assert(!policy.select("A",eye,&self,t,out));
    assert(policy.select("A",eye,&self,2000,out)&&same(out,self));
    assert(!policy.select("A",eye,nullptr,2010,out)&&!policy.valid);
    assert(!policy.select("A",eye,&self,2020,out));
    assert(!policy.select("B",eye,&self,2100,out)&&policy.map=="B");
    assert(!policy.select("B",eye,&self,3000,out)); // loading gap
    for(uint32_t t=3100;t<4000;t+=100)assert(!policy.select("B",eye,&self,t,out));
    assert(policy.select("B",eye,&self,4000,out));
    Vec3 moved(3,0,0);assert(!policy.select("B",eye,&moved,4010,out));
    for(uint32_t t=4100;t<5100;t+=100)policy.select("B",eye,&moved,t,out);
    assert(policy.select("B",Vec3(-57,0,0),&moved,5100,out));
    assert(!policy.select("B",Vec3(-57.01f,0,0),&moved,5110,out));
    Vec3 bad(std::numeric_limits<float>::quiet_NaN(),0,0);assert(!policy.select("B",eye,&bad,5120,out));
    policy.reset();const uint32_t start=0xfffffff0u;assert(!policy.select("A",eye,&self,start,out));
    for(uint32_t d=100;d<=1000;d+=100)policy.select("A",eye,&self,start+d,out);
    assert(policy.select("A",eye,&self,start+1000,out));
    unsigned enclosures=0;uint32_t random=0x19270abc;
    for(unsigned i=0;i<10000;++i){auto f=[&]{random=random*1664525u+1013904223u;return float(int32_t(random))/2147483648.f;};
        Vec3 c(f()*20000,f()*20000,f()*5000),delta(f()*60,f()*60,f()*60);
        if(!NorthlightWorldStreaming::within(c,c+delta,60))continue;
        const Vec3 e=c+delta,lo=Envelope::low(c),hi=Envelope::high(c),oldLo=e-Vec3(288,288,320),oldHi=e+Vec3(288,288,320);
        assert(lo.x<=oldLo.x&&lo.y<=oldLo.y&&lo.z<=oldLo.z&&hi.x>=oldHi.x&&hi.y>=oldHi.y&&hi.z>=oldHi.z);++enclosures;
    }
    std::printf("ORBIT_UNIT_PASS enclosures=%u freshness=controlled map=checked gap=checked motion=checked zoom=checked wrap=checked\n",enclosures);
}
static std::vector<std::string> files(const std::string& root,Vec3 c,int radius){
    const int tx=int(std::floor((17066.666666666666-c.y)/(1600./3))),ty=int(std::floor((17066.666666666666-c.x)/(1600./3)));
    std::vector<std::string> result;
    for(int y=ty-radius;y<=ty+radius;++y)for(int x=tx-radius;x<=tx+radius;++x){
        auto path=root+"/Northrend/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        if(std::filesystem::exists(path))result.push_back(path);
    }
    return result;
}
static void corpus(const std::string& root,const char* trace,unsigned probeStride){
    assert(probeStride>=1&&probeStride<=5600);
    FILE* input=std::fopen(trace,"rb");assert(input);char magic[4];uint32_t n=0;
    assert(std::fread(magic,1,4,input)==4&&!std::memcmp(magic,"NLC1",4)&&std::fread(&n,4,1,input)==1&&n>=4);
    std::vector<Camera> cameras(n);for(auto& c:cameras){float values[7];assert(std::fread(&c.tick,8,1,input)==1);assert(std::fread(values,4,7,input)==7);c.eye={values[0],values[1],values[2]};c.forward={values[3],values[4],values[5]};c.distance=values[6];}assert(std::fgetc(input)==EOF);std::fclose(input);
    const Vec3 center=cameras.front().eye+cameras.front().forward*cameras.front().distance;
    for(const auto& c:cameras)assert(NorthlightWorldStreaming::within(center,c.eye,Envelope::Radius));
    LocalSceneCache candidateCache({16u*1024u*1024u});SceneSourceCache candidateSources;
    auto provider=candidateSources.provider();auto revision=[&](const std::string& path){return candidateSources.findIdentity(path);};
    std::string error;BVH enlarged,previous;uint64_t requested=0,maxBlock=0;
    // Keep the old published region alive while constructing its replacement:
    // both must share the original 16 MiB optional acceleration budget.
    assert(candidateCache.build(files(root,cameras.front().eye,1),root+"/models","Northrend",cameras.front().eye-Vec3(288,288,320),cameras.front().eye+Vec3(288,288,320),previous,error,{},true,provider,revision));
    auto admission=[&](uint64_t bytes){requested+=bytes;maxBlock=std::max(maxBlock,bytes);return true;};
    auto start=Clock::now();assert(candidateCache.build(files(root,center,1),root+"/models","Northrend",Envelope::low(center),Envelope::high(center),enlarged,error,admission,true,provider,revision));
    const double expandedMs=ms(start);const auto expandedBytes=enlarged.retainedBytes();
    assert(candidateCache.stats().sharedLiveBytes<=16u*1024u*1024u);
    previous=BVH();
    // 60 orbit + 128 probe-corner norm + 4*256 transport/direct shadow +
    // ray biases fits inside 1280. Only this independent test oracle may
    // exceed runtime region-count limits; source payload limits stay intact.
    BVH reference;WorldScene broad;const auto referenceFiles=files(root,center,3);
    northlightOrbitReferenceLoader=true;
    const bool referenceLoaded=loadInstancedScenes(referenceFiles,root+"/models",center-Vec3(1280,1280,1280),center+Vec3(1280,1280,1280),broad,error);
    northlightOrbitReferenceLoader=false;
    if(!referenceLoaded)std::fprintf(stderr,"Broad reference failed: %s\n",error.c_str());assert(referenceLoaded);
    std::printf("ORBIT_REFERENCE triangles=%zu vertices=%zu sourceBytes=%zu radius=1280 runtimeCountGuards=unchanged\n",broad.triangles.size(),broad.vertices.size(),broad.retainedBytes());
    assert(reference.build(std::move(broad),error,true));
    LocalSceneCache baselineCache({16u*1024u*1024u});SceneSourceCache baselineSources;
    const auto oldProvider=baselineSources.provider();const auto oldRevision=[&](const std::string& path){return baselineSources.findIdentity(path);};
    uint64_t totalOldTriangles=0,baselineBytesPeak=0,queries=0,changed=0,referenceCorrect=0,closerToReference=0;double baselineMs=0;
    assert(configureProbeLayout(probeLayoutFor(20)));Lighting light;light.sunDirection=Vec3(.25492f,.25492f,.93275f);light.additionalDirections.push_back({Vec3(-.3f,-.3f,.9f),Vec3(.02f,.02f,.03f)});const auto lighting=prepareLighting(light);assert(lighting.valid());
    const BVH noActors;uint64_t probes=0,probeChanged=0,probeExactReference=0,probeRegressed=0,visibilityRegressed=0,pathQueries=0,pathChanged=0,pathExactReference=0;double oldBakeMs=0,newBakeMs=0;float maxProbeAxis=0,maxProbeNorm=0;
    const auto probeError=[](const Probe& p,const Probe& r){double e=0;for(unsigned k=0;k<4;++k){const float a[]={p.sh[k].x,p.sh[k].y,p.sh[k].z},b[]={r.sh[k].x,r.sh[k].y,r.sh[k].z};for(unsigned j=0;j<3;++j)e+=(double(a[j])-b[j])*(double(a[j])-b[j]);}return e;};
    const auto visibilityError=[](const Probe& p,const Probe& r){double e=0;for(unsigned k=0;k<6;++k){const double a=double(p.moments[k].mean)-r.moments[k].mean,b=double(p.moments[k].meanSquare)-r.moments[k].meanSquare;e+=a*a+b*b;}return e;};
    for(size_t sample=0;sample<cameras.size();++sample){const auto& c=cameras[sample];BVH old;start=Clock::now();
        assert(baselineCache.build(files(root,c.eye,1),root+"/models","Northrend",c.eye-Vec3(288,288,320),c.eye+Vec3(288,288,320),old,error,{},true,oldProvider,oldRevision));
        const double cost=ms(start);baselineMs+=cost;baselineBytesPeak=std::max<uint64_t>(baselineBytesPeak,old.retainedBytes());totalOldTriangles+=old.triangleCount();subset(old.scene(),enlarged.scene());
        uint32_t rng=0x1379abcd;auto random=[&]{rng=rng*1664525u+1013904223u;return float(rng>>8)/16777216.f;};
        for(unsigned i=0;i<8192;++i){const Vec3 origin=c.eye+Vec3((random()-.5f)*152,(random()-.5f)*152,(random()-.5f)*104),direction(random()-.5f,random()-.5f,random()-.5f);
            const auto a=old.trace(origin,direction,.0005f,256),b=enlarged.trace(origin,direction,.0005f,256),r=reference.trace(origin,direction,.0005f,256);++queries;
            if(a.hit)assert(b.hit&&b.distance<=a.distance);
            if(a.hit!=b.hit||a.distance!=b.distance||!same(a.albedo,b.albedo)||!same(a.normal,b.normal)){
                ++changed;if(b.hit==r.hit&&b.distance==r.distance&&same(b.albedo,r.albedo)&&same(b.normal,r.normal))++referenceCorrect;
                if(r.hit&&b.hit&&b.distance>=r.distance&&(!a.hit||b.distance<a.distance))++closerToReference;
            }
        }
        const auto window=probeWindowOrigin(c.eye);
        for(unsigned i=0;i<probeLayout().count();++i){const auto d=probeWindowPosition(window,i)-c.eye;maxProbeAxis=std::max({maxProbeAxis,std::fabs(d.x),std::fabs(d.y),std::fabs(d.z)});maxProbeNorm=std::max(maxProbeNorm,std::sqrt(dot(d,d)));}
        assert(maxProbeNorm<=128&&60+maxProbeNorm+4*light.maxDistance+4*light.rayBias<1280);
        for(unsigned i=0;i<probeLayout().count();i+=probeStride){const auto position=probeWindowPosition(window,i);ProbeGridKey key;assert(probeGridKey(position,key));StaticPathRecord oldPath,newPath,referencePath;
            start=Clock::now();const auto a=solveProbeMoving(old,position,lighting,64,probeSeed(key),noActors,oldPath,1);oldBakeMs+=ms(start);
            start=Clock::now();const auto b=solveProbeMoving(enlarged,position,lighting,64,probeSeed(key),noActors,newPath,1);newBakeMs+=ms(start);
            const auto r=solveProbeMoving(reference,position,lighting,64,probeSeed(key),noActors,referencePath,1);++probes;
            if(!sameProbe(a,b)){++probeChanged;probeExactReference+=sameProbe(b,r);probeRegressed+=probeError(b,r)>probeError(a,r);visibilityRegressed+=visibilityError(b,r)>visibilityError(a,r);}
            for(const auto* path:{&oldPath,&newPath})for(const auto& q:path->queries){const auto ah=old.trace(q.origin,q.direction,.0005f,q.maxDistance),bh=enlarged.trace(q.origin,q.direction,.0005f,q.maxDistance),rh=reference.trace(q.origin,q.direction,.0005f,q.maxDistance);++pathQueries;
                if(ah.hit)assert(bh.hit&&bh.distance<=ah.distance);
                if(ah.hit!=bh.hit||ah.distance!=bh.distance||!same(ah.albedo,bh.albedo)||!same(ah.normal,bh.normal)){++pathChanged;pathExactReference+=bh.hit==rh.hit&&bh.distance==rh.distance&&same(bh.albedo,rh.albedo)&&same(bh.normal,rh.normal);}
            }
        }
        std::printf("ORBIT_EYE sample=%zu triangles=%zu bytes=%zu buildMs=%.4f subset=1\n",sample,old.triangleCount(),old.retainedBytes(),cost);
    }
    std::printf("ORBIT_BAKE samples=%u probes=%llu rays=64 bounces=3 changed=%llu exactReference=%llu regressedSH=%llu regressedVisibility=%llu pathQueries=%llu pathChanged=%llu pathExactReference=%llu oldBakeMs=%.4f newBakeMs=%.4f maxProbeAxis=%.4f maxProbeNorm=%.4f referenceRadius=1280\n",n,(unsigned long long)probes,(unsigned long long)probeChanged,(unsigned long long)probeExactReference,(unsigned long long)probeRegressed,(unsigned long long)visibilityRegressed,(unsigned long long)pathQueries,(unsigned long long)pathChanged,(unsigned long long)pathExactReference,oldBakeMs,newBakeMs,maxProbeAxis,maxProbeNorm);
    // Refusal of the optional envelope leaves a prior scene untouched, and the
    // original eye crop can still be built without changing its admission path.
    BVH fallback;WorldScene sentinel;sentinel.vertices.resize(3);sentinel.vertices[1].position.x=1;sentinel.vertices[2].position.y=1;sentinel.materials.resize(1);sentinel.triangles.push_back({0,1,2,0});assert(fallback.build(std::move(sentinel),error));
    LocalSceneCache refused;assert(!refused.build(files(root,center,1),root+"/models","Northrend",Envelope::low(center),Envelope::high(center),fallback,error,[](uint64_t){return false;},true,provider,revision));assert(fallback.triangleCount()==1);
    std::printf("ORBIT_CORPUS_PASS samples=%u baselineTriangles=%llu expandedTriangles=%zu baselineBytesPeak=%llu expandedBytes=%zu sharedLiveBytes=%llu maxAdmissionBlock=%llu admissionRequestedBytes=%llu baselineBuildMs=%.4f envelopeBuildMs=%.4f queries=%llu changed=%llu exactReference=%llu nearerReference=%llu fallback=checked rawSelfFreshness=unavailable\n",n,(unsigned long long)totalOldTriangles,enlarged.triangleCount(),(unsigned long long)baselineBytesPeak,expandedBytes,(unsigned long long)candidateCache.stats().sharedLiveBytes,(unsigned long long)maxBlock,(unsigned long long)requested,baselineMs,expandedMs,(unsigned long long)queries,(unsigned long long)changed,(unsigned long long)referenceCorrect,(unsigned long long)closerToReference);
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);unit();if(argc==4)corpus(argv[1],argv[2],unsigned(std::strtoul(argv[3],nullptr,10)));else assert(argc==1);}
