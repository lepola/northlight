// Reuse the existing nonblocking query oracle, including assertions on flags.
#define main original_gpu_profile_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "test_gpu_profile.cpp"
#pragma clang diagnostic pop
#undef main
#include "material_profile.h"
static uint64_t nanos=0,clockReads=0;
static uint64_t clockNow(){++clockReads;return nanos;}
int main(){
    using P=NorthlightMaterialProfile;IDirect3DDevice9 d;
    {P p(&d,clockNow);p.beginFrame(1,false);{P::Scope off(&p,P::CaptureDraw,17,true);}p.endFrame();assert(!d.creates&&!clockReads);
     p.beginFrame(127,true);nanos=100;{P::Scope parent(&p,P::Admission,17);nanos=150;{P::Scope child(&p,P::Snapshot);nanos=200;}nanos=300;}p.endFrame();
     findLog("stage=admission domain=4 mode=1 calls=1 exclusiveMs=0.000150");findLog("stage=snapshot domain=4 mode=1 calls=1 exclusiveMs=0.000050");
     unsigned before=d.reads;p.poll();assert(d.reads-before==1);d.ready=true;p.poll();
     logs.clear();p.beginFrame(254,true);{P::Scope a(&p,P::CaptureDraw,17,true);a.work(100);}{P::Scope b(&p,P::NativeDraw,17,true);b.work(100);}p.endFrame();p.poll();
     findLog("stage=captureDraw domain=4 mode=1 spans=1 ms=1.000000 items=100");findLog("attempted=2 measured=2 complete=1 sumIntervalsMs=2.000000");
     p.reset();assert(!d.live);d.ready=false;
     for(unsigned i=0;i<3;++i){p.beginFrame(1000+i,true);{P::Scope a(&p,P::CaptureDraw,17,true);}p.endFrame();}
     before=d.creates;p.beginFrame(1003,true);{P::Scope a(&p,P::NativeDraw,17,true);}p.endFrame();assert(d.creates==before);before=d.reads;p.poll();assert(d.reads-before==3);
     d.ready=true;p.poll();p.reset();assert(!d.live);
     logs.clear();p.beginFrame(2000,true);for(unsigned i=0;i<4100;++i){P::Scope a(&p,P::CaptureDraw,17,true);}p.endFrame();p.poll();findLog("attempted=4100 measured=4096 complete=0");
     p.reset();d.failCreate=true;logs.clear();p.beginFrame(3000,true);{P::Scope a(&p,P::Admission);}p.endFrame();findLog("MATERIAL_CPU");assert(!d.live);d.failCreate=false;p.reset();
     logs.clear();p.beginFrame(4000,true);{P::Scope a(&p,P::CaptureDraw,17,true);}p.endFrame();d.disjoint=true;p.poll();findLog("discarded=disjoint");d.disjoint=false;p.reset();
     p.beginFrame(5000,true);{P::Scope a(&p,P::CaptureDraw,17,true);p.reset();}assert(!d.live);
     logs.clear();p.beginFrame(6000,true);nanos=0;
     {P::Scope bounds(&p,P::BoundsPose,19);nanos=100;{P::Scope envelope(&p,P::PoseEnvelope);nanos=150;}nanos=200;{P::Scope fallback(&p,P::PoseFallback);nanos=300;}nanos=500;}
     p.endFrame();findLog("stage=boundsPose domain=4 mode=3 calls=1 exclusiveMs=0.000350");
     findLog("stage=poseEnvelope domain=4 mode=3 calls=1 exclusiveMs=0.000050");findLog("stage=poseFallback domain=4 mode=3 calls=1 exclusiveMs=0.000100");
    }assert(!d.live);
    std::puts("MATERIAL_PROFILE_PASS disabled_zero_work exclusive_nested_cpu domains_modes nonblocking_gpu saturation coverage failure_reset");
}
