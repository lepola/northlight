// Manual comparison with a preserved pre-optimization probe_blend.h.
#include "probe_blend.h"
#define NorthlightProbeBlend ReferenceProbeBlend
#include "probe_blend_reference.h"
#undef NorthlightProbeBlend
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
using namespace NorthlightGI;
static uint32_t rng=739;
static uint32_t next(){return rng=rng*1664525u+1013904223u;}
int main(int argc,char** argv){
    unsigned compared=0;volatile unsigned sink=0;
    for(unsigned n:{14u,20u,22u}){
        assert(configureProbeLayout(probeLayoutFor(n)));
        NorthlightProbeBlend::Mirror a;ReferenceProbeBlend::Mirror b;
        std::vector<ProbeAtlasEntry> atlas(probeLayout().atlasSize());
        for(unsigned frame=0;frame<80;++frame){
            const char* map=frame<40?"A":"B";a.begin(map);b.begin(map);
            for(auto& e:atlas){
                if(frame%7==0){e.occupied=next()%5!=0;e.probe.valid=next()%9!=0;e.key={int32_t(next()%150),int32_t(next()%150),int32_t(next()%150)};}
                if(frame%3==0&&next()%3==0)for(auto& s:e.probe.sh)s={float(int(next()%100)-50)/31,float(next()%100)/43,float(next()%100)/53};
            }
            const float now=frame*.016f;
            assert(a.publish(atlas,now)==b.publish(atlas,now));
            for(size_t i=0;i<atlas.size();++i){
                assert(!std::memcmp(a.previous(i),b.previous(i),12*sizeof(float)));
                const float as=a.start(i),bs=b.start(i);assert(!std::memcmp(&as,&bs,4));++compared;
            }
            if(frame%29==0){std::vector<ProbeAtlasEntry> wrong(3);assert(a.publish(wrong,now)==b.publish(wrong,now));}
            if(frame%31==0){a.reset();b.reset();}
        }
    }
    if(argc==1){std::printf("{\"pass\":true,\"slots_exact\":%u}\n",compared);return 0;}
    assert(argc==2&&std::strcmp(argv[1],"--benchmark")==0);
    double times[2][7];
    for(unsigned r=0;r<7;++r)for(unsigned mode=0;mode<2;++mode){
        NorthlightProbeBlend::Mirror a;ReferenceProbeBlend::Mirror b;
        a.begin("A");b.begin("A");std::vector<ProbeAtlasEntry> atlas(probeLayout().atlasSize());
        for(size_t i=0;i<atlas.size();++i){auto& e=atlas[i];e.occupied=e.probe.valid=true;e.key={int(i),0,0};for(auto& s:e.probe.sh)s={1,2,3};}
        a.publish(atlas,0);b.publish(atlas,0);
        const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<240;++frame){
            for(size_t i=frame%17;i<atlas.size();i+=17)atlas[i].probe.sh[frame%4].x+=.01f;
            sink+=mode?a.publish(atlas,frame*.016f):b.publish(atlas,frame*.016f);
        }
        times[mode][r]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/240;
    }
    std::sort(times[0],times[0]+7);std::sort(times[1],times[1]+7);(void)sink;
    std::printf("{\"pass\":true,\"slots_exact\":%u,\"old_ms\":%.6f,\"new_ms\":%.6f,\"ratio\":%.4f}\n",compared,times[0][3],times[1][3],times[1][3]/times[0][3]);
}
