// 0.3.197: same-key probe re-publication blend mirror (task 13): previous SH, start time, born untouched.
#include "probe_blend.h"
#include "probe_activation.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace NorthlightGI;
namespace PB=NorthlightProbeBlend;
static ProbeAtlasEntry entry(ProbeGridKey key,float light,bool valid=true,bool occupied=true){ProbeAtlasEntry e;e.key=key;e.occupied=occupied;e.probe.valid=valid;
    for(int k=0;k<4;++k)e.probe.sh[k]={light+k,light*2+k,light*3+k};return e;}
static void sh(const ProbeAtlasEntry& e,float out[12]){for(int k=0;k<4;++k){out[k]=e.probe.sh[k].x;out[4+k]=e.probe.sh[k].y;out[8+k]=e.probe.sh[k].z;}}
static bool equal(const float* a,const float* b){return !std::memcmp(a,b,12*sizeof(float));}
static bool near(const float* a,const float* b){for(int i=0;i<12;++i)if(std::fabs(a[i]-b[i])>1e-5f)return false;return true;}
static std::vector<ProbeAtlasEntry> blank(){return std::vector<ProbeAtlasEntry>(probeLayout().atlasSize());}
static void run(){
    const ProbeGridKey key{3,-2,5};const size_t slot=probeAtlasIndex(key);float expect[12],old[12],shown[12];
    PB::Mirror mirror;NorthlightProbeActivation activation;mirror.begin("Azeroth");activation.begin("Azeroth");
    auto atlas=blank();
    // First residency: no blend, previous equals the value, born fades as before.
    atlas[slot]=entry(key,1);mirror.publish(atlas,10);const float born=activation.update(slot,atlas[slot],10);
    sh(atlas[slot],expect);assert(mirror.start(slot)==PB::None&&equal(mirror.previous(slot),expect)&&born==10);
    // Empty slots stay out of the blend entirely.
    assert(mirror.start(slot+1)==PB::None);
    // Same key, changed SH: previous is the old value, blend starts now, born is unchanged.
    sh(atlas[slot],old);atlas[slot]=entry(key,5);assert(mirror.publish(atlas,12)==1);
    assert(equal(mirror.previous(slot),old)&&mirror.start(slot)==12&&activation.update(slot,atlas[slot],12)==born);
    // Identical re-publication keeps the in-flight blend untouched.
    assert(mirror.publish(atlas,12.1f)==0&&mirror.start(slot)==12&&equal(mirror.previous(slot),old));
    // Re-publication mid-blend: previous is what was displayed at that moment (no pop).
    float cur[12];sh(atlas[slot],cur);const float t=(12.1f-12)/PB::BlendSeconds;
    for(int i=0;i<12;++i)shown[i]=old[i]+(cur[i]-old[i])*t;
    atlas[slot]=entry(key,9);assert(mirror.publish(atlas,12.1f)==1);
    assert(near(mirror.previous(slot),shown)&&mirror.start(slot)==12.1f&&activation.update(slot,atlas[slot],12.1f)==born);
    // Re-publication after the blend finished: previous is the old current value.
    sh(atlas[slot],old);atlas[slot]=entry(key,20);assert(mirror.publish(atlas,13)==1);assert(near(mirror.previous(slot),old));
    // Different key in the same slot (collision): no blend, born restarts.
    const ProbeGridKey other{3+int(probeLayout().atlas),-2,5};assert(probeAtlasIndex(other)==slot);
    atlas[slot]=entry(other,3);assert(mirror.publish(atlas,14)==0);sh(atlas[slot],expect);
    assert(mirror.start(slot)==PB::None&&equal(mirror.previous(slot),expect)&&activation.update(slot,atlas[slot],14)==14);
    // Invalid or unoccupied entries clear the slot; the same key returning is a new residency.
    atlas[slot]=entry(other,3,false);assert(mirror.publish(atlas,15)==0&&mirror.start(slot)==PB::None);
    atlas[slot]=entry(other,8);assert(mirror.publish(atlas,16)==0&&mirror.start(slot)==PB::None);sh(atlas[slot],expect);assert(equal(mirror.previous(slot),expect));
    atlas[slot]=entry(other,8,true,false);assert(mirror.publish(atlas,17)==0&&mirror.start(slot)==PB::None);
    // Map change and reset drop every slot: nothing blends across them.
    atlas[slot]=entry(other,8);mirror.publish(atlas,18);atlas[slot]=entry(other,2);assert(mirror.publish(atlas,19)==1);
    mirror.begin("Kalimdor");atlas[slot]=entry(other,4);assert(mirror.publish(atlas,20)==0&&mirror.start(slot)==PB::None);
    atlas[slot]=entry(other,6);assert(mirror.publish(atlas,21)==1);mirror.reset();atlas[slot]=entry(other,7);assert(mirror.publish(atlas,22)==0&&mirror.start(slot)==PB::None);
    // reset() forgets the map too, so the next begin() starts clean; a repeated begin() with the same map keeps the state.
    atlas[slot]=entry(other,9);assert(mirror.publish(atlas,23)==1);mirror.begin("Kalimdor");mirror.begin("Kalimdor");assert(mirror.start(slot)==PB::None);
    mirror.begin("Azeroth");atlas[slot]=entry(other,10);mirror.begin("Azeroth");assert(mirror.publish(atlas,24)==0);
    // A wrongly sized atlas blends nothing.
    std::vector<ProbeAtlasEntry> small(3);assert(mirror.publish(small,25)==0&&mirror.start(slot)==PB::None);
    // The shader's "no blend" start saturates to 1 for any realistic time.
    assert(std::fmin(std::fmax((0.f-PB::None)/PB::BlendSeconds,0.f),1.f)==1.f);
}
static void bench(unsigned n){
    ProbeLayout layout=probeLayoutFor(n);if(!configureProbeLayout(layout)){std::printf("bench probeBlend atlas=%u unsupported\n",n);return;}
    PB::Mirror mirror;mirror.begin("Azeroth");auto atlas=blank();unsigned filled=0;
    for(int z=0;z<int(layout.nz);++z)for(int y=0;y<int(layout.n);++y)for(int x=0;x<int(layout.n);++x){ProbeGridKey key{x,z,y};atlas[probeAtlasIndex(key)]=entry(key,float(x+y+z));++filled;}
    mirror.publish(atlas,1);double best=1e30;unsigned blended=0;
    for(int rep=0;rep<50;++rep){for(auto& e:atlas)if(e.occupied&&(rep&1)==0)e.probe.sh[0].x+=1;else if(e.occupied)e.probe.sh[0].x-=1; /* every probe changes */
        const auto a=std::chrono::steady_clock::now();blended=mirror.publish(atlas,1+rep*.1f);const auto b=std::chrono::steady_clock::now();
        best=std::min(best,std::chrono::duration<double,std::micro>(b-a).count());}
    assert(blended==filled);
    // Steady state: nothing changed, only the comparison pass.
    double idle=1e30;for(int rep=0;rep<50;++rep){const auto a=std::chrono::steady_clock::now();assert(mirror.publish(atlas,100.f)==0);const auto b=std::chrono::steady_clock::now();idle=std::min(idle,std::chrono::duration<double,std::micro>(b-a).count());}
    std::printf("bench probeBlend atlas=%u slots=%zu allChangedPublishUs=%.1f unchangedPublishUs=%.1f\n",layout.atlas,layout.atlasSize(),best,idle);
}
int main(){
    run();
    // Re-run on a non-default layout: the mirror follows probeLayout().
    for(unsigned n:{14u,22u}){bench(n);}
    assert(configureProbeLayout(probeLayoutFor(14)));run();
    std::puts("PASS probe blend");
}
