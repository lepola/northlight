#include "world_local_lights.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
using namespace NorthlightLocalLights;
int main(int argc,char** argv) {
    assert(argc==3);const std::string fixture=argv[1],real=argv[2];
    std::vector<Light> lights;
    assert(readFile(fixture+"/Alpha.fgl",lights)&&lights.size()==3);
    assert(lights[0].position[0]==0&&lights[0].sourceId==1&&lights[1].sourceId==2);
    for(int i=0;i<10;++i) {
        std::vector<Light> preserved=lights;
        assert(!readFile(fixture+"/bad"+std::to_string(i)+".fgl",preserved));
        assert(preserved.size()==lights.size()&&preserved[0].sourceId==1);
    }
    Cache cache(fixture);float p[3]={};
    assert(cache.loadLights("Alpha",p,0,lights)&&lights.size()==1&&lights[0].sourceId==1);
    // Source2 lies30 away with radius10: influence intersects a20-unit region.
    assert(cache.loadLights("Alpha",p,20,lights)&&lights.size()==2);
    assert(lights[0].sourceId==1&&lights[1].sourceId==2);
    p[0]=25;assert(cache.loadLights("Alpha",p,40,lights)&&lights.size()==2);
    assert(lights[0].sourceId==1&&lights[1].sourceId==2);
    assert(!cache.loadLights("../Alpha",p,40,lights)&&lights.empty());
    assert(!cache.loadLights("Missing",p,40,lights)&&lights.empty());
    assert(cache.loadLights("Beta",p,40,lights)&&lights.size()==1&&lights[0].sourceId==4);
    assert(cache.totalCount()==1);
    p[0]=std::numeric_limits<float>::quiet_NaN();assert(!cache.loadLights("Beta",p,40,lights)&&lights.empty());
    p[0]=0;assert(!cache.loadLights("Beta",p,-1,lights));cache.clear();assert(cache.totalCount()==0);
    assert(readFile(fixture+"/Alpha.fgl",lights));const auto& l=lights[0];
    assert(attenuation(l,0)==1&&attenuation(l,l.attenuationStart)==1);
    assert(std::fabs(attenuation(l,(l.attenuationStart+l.attenuationEnd)/2)-.5f)<1e-6f);
    assert(attenuation(l,l.attenuationEnd)==0&&attenuation(l,1000)==0);
    // Compare the cache query against the full sphere test, including tangent
    // boundaries and negative coordinates; keep the old float subtraction.
    std::mt19937 rng(291029);std::uniform_real_distribution<float> pos(-90000,90000),rad(.12f,2000);
    std::vector<Light> source;
    for(unsigned i=0;i<1000;++i)source.push_back({{pos(rng),pos(rng),pos(rng)},{1,1,1},0,rad(rng),i,1,1});
    {std::ofstream f(fixture+"/Random.fgl",std::ios::binary);const uint32_t h[]={1,sizeof(Light),uint32_t(source.size())};
        f.write("FGL1",4);f.write(reinterpret_cast<const char*>(h),sizeof h);f.write(reinterpret_cast<const char*>(source.data()),source.size()*sizeof(Light));assert(f);}
    for(unsigned query=0;query<1000;++query){float eye[]={pos(rng),pos(rng),pos(rng)};const float radius=rad(rng);
        if(query<source.size()/3){for(unsigned k=0;k<3;++k)eye[k]=source[query].position[k];eye[0]+=source[query].attenuationEnd+radius;}
        assert(cache.loadLights("Random",eye,radius,lights));size_t found=0;
        for(const auto& candidate:source){double squared=0;for(unsigned k=0;k<3;++k){double d=candidate.position[k]-eye[k];squared+=d*d;}
            const double reach=double(radius)+candidate.attenuationEnd;
            if(squared<=reach*reach){assert(found<lights.size()&&lights[found].sourceId==candidate.sourceId);++found;}}
        assert(found==lights.size());}
    size_t actual=0;
    if(!real.empty())for(const char* map:{"Azeroth","Kalimdor","Expansion01","Northrend"}) {
        assert(readFile(real+"/"+map+".fgl",lights));actual+=lights.size();
    }
    std::printf("{\"actual_cache_records_validated\":%zu,\"corrupt_caches_rejected\":10,\"map_isolation_and_query_boundaries\":true,\"stable_source_order\":true}\n",actual);
}
