#include "world_streaming.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <vector>
#include <memory>
using namespace NorthlightWorldStreaming;
struct Resource {
    static int live;int id;
    explicit Resource(int value):id(value){++live;}
    ~Resource(){--live;}
};
int Resource::live=0;

// 0.3.169 protocol model with the production predicates: the render thread adopts and retires
// per frame, the builder refreshes on 32 units of lead-point travel and cancels a build more
// than 96 from the eye (after the BVH and after terrain), and the worker drops a delivery more
// than 96 from the eye after its adoption latency. Third-person eye at distance d behind the
// player; a flick turns the camera 180 degrees in 0.25 s, holds 2 s and turns back.
struct Coverage {float maxDistance=0,lateMax=0;unsigned skipped=0,lateSkipped=0,builds=0;std::vector<NorthlightGI::Vec3> centres;}; /* late: from 4 s, after the lead can arm */
static Coverage simulate(float speed,float buildSeconds,bool lead,float spinAt=-1,float seconds=14,float pivotDistance=34,bool anchored=false){
    using V=NorthlightGI::Vec3;const float d=34,dt=1.f/30,latency=.15f,pi=3.14159265f;
    auto pose=[&](float t,V& eye,V& pivot){const float p=std::max(0.f,t-.5f)*speed;float angle=0;
        if(spinAt>=0){const float s=t-spinAt;if(s>=0&&s<.25f)angle=pi*s/.25f;else if(s>=.25f&&s<2.25f)angle=pi;else if(s>=2.25f&&s<2.5f)angle=pi*(1-(s-2.25f)/.25f);}
        const V forward(std::cos(angle),std::sin(angle),0);eye=V(p-d*forward.x,-d*forward.y,0);pivot=V(eye.x+pivotDistance*forward.x,eye.y+pivotDistance*forward.y,0);};
    auto distance=[](V a,V b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));};
    const std::string map="Azeroth";Motion motion;Coverage out;
    V eye,pivot;pose(0,eye,pivot);V builtCenter=eye,published=eye,active=eye;bool haveActive=true,building=false,handing=false,wanted=false;
    V buildCenter,handCenter;float buildStart=0,handAt=0;int checks=0;long lastCell=LONG_MIN;
    for(int frame=0;frame*dt<seconds;++frame){const float t=frame*dt;pose(t,eye,pivot);
        motion.update(map,eye,pivot,uint32_t(1000+std::lround(t*1000)));
        const V target=anchored?anchoredCenter(eye,pivot,motion.velocity(),buildSeconds):lead?leadCenter(eye,motion.velocity(),buildSeconds):eye;
        const long cell=long(std::floor(eye.x/8))*100000+long(std::floor(eye.y/8));const bool request=cell!=lastCell;lastCell=cell;
        if(building){
            for(;checks<2&&t-buildStart>=(checks?.85f:.5f)*buildSeconds;++checks)if(!applicable(map,buildCenter,map,eye)){building=false;break;}
            if(building&&t-buildStart>=buildSeconds){building=false;handing=true;handCenter=buildCenter;handAt=t;builtCenter=buildCenter;}
        }
        if(!building&&(request||wanted)&&(wanted||needsGeometry(map,builtCenter,map,target))){building=true;buildCenter=target;buildStart=t;checks=0;wanted=false;++out.builds;out.centres.push_back(target);}
        if(handing&&t-handAt>=latency){handing=false;if(applicable(map,handCenter,map,eye))published=handCenter;else wanted=true;}
        if(adopts(map,published,true,haveActive,map,eye)){active=published;haveActive=true;}
        if(haveActive&&!retained(map,active,map,eye))haveActive=false;
        if(haveActive)out.maxDistance=std::max(out.maxDistance,distance(active,eye));else ++out.skipped;
        if(t>=4){if(haveActive)out.lateMax=std::max(out.lateMax,distance(active,eye));else ++out.lateSkipped;}
    }
    return out;
}

// Largest lead over a standing-still camera input; pose(t,eye,pivot) at 30 fps for 4 s.
template<class Pose> static float peakLead(Pose pose){
    Motion m;float peak=0;NorthlightGI::Vec3 eye,pivot;
    for(int frame=0;frame<120;++frame){const float t=frame/30.f;pose(t,eye,pivot);m.update("Azeroth",eye,pivot,uint32_t(1000+std::lround(t*1000)));
        const NorthlightGI::Vec3 c=leadCenter(eye,m.velocity(),1);peak=std::max(peak,std::sqrt((c.x-eye.x)*(c.x-eye.x)+(c.y-eye.y)*(c.y-eye.y)+(c.z-eye.z)*(c.z-eye.z)));}
    return peak;}
// GI request cadence of the render thread: probe-centre cell change (probe centre = eye + ahead
// along the view, GIProbeAhead) and, with the lead, reason 128. Returns {requests, requests from
// reason 128 alone}; lead=false is 0.3.168.
struct Cadence {unsigned requests=0,leadOnly=0;NorthlightGI::Vec3 lastCamera,lastCenter,lastProbe;};
template<class Pose> static Cadence requests(Pose pose,float seconds,bool lead,float ahead=0){
    Motion m;Cadence out;NorthlightGI::Vec3 eye,pivot;bool first=true;
    auto cell=[](NorthlightGI::Vec3 p){return NorthlightGI::Vec3(std::floor(p.x/8),std::floor(p.y/8),std::floor(p.z/8));};
    for(int frame=0;frame*(1/30.f)<seconds;++frame){const float t=frame/30.f;pose(t,eye,pivot);m.update("Azeroth",eye,pivot,uint32_t(1000+std::lround(t*1000)));
        const NorthlightGI::Vec3 center=lead?leadCenter(eye,m.velocity(),1):eye;
        const float fx=pivot.x-eye.x,fy=pivot.y-eye.y,fl=std::sqrt(fx*fx+fy*fy);const NorthlightGI::Vec3 probe(eye.x+(fl>0?ahead*fx/fl:0),eye.y+(fl>0?ahead*fy/fl:0),eye.z);
        const NorthlightGI::Vec3 a=cell(out.lastProbe),b=cell(probe);
        const bool cellMoved=first||a.x!=b.x||a.y!=b.y||a.z!=b.z,moved=lead&&leadMoved(out.lastCamera,out.lastCenter,eye,center);
        if(cellMoved||moved){++out.requests;out.leadOnly+=!cellMoved;out.lastCamera=eye;out.lastCenter=center;out.lastProbe=probe;first=false;}}
    return out;}
int main(){
    NorthlightGI::WorldScene scene;scene.vertices.resize(3);scene.materials.resize(1);
    scene.vertices[1].position.x=1;scene.vertices[2].position.y=1;
    scene.triangles.push_back({0,1,2,0});
    NorthlightWorldMesh::WorldMeshUploadPlan plan;std::string error;
    assert(NorthlightWorldMesh::buildUploadPlan(scene,plan,error));assert(!validate(plan,scene));
    unsigned rollbackCases=0,rejected=0;
    // Allocation failures at either stage, including APIs returning an allocated
    // object alongside failure, must release pending objects and retain old GPU
    // resources. A successful later attempt can commit without lost ownership.
    for(int failedStage=0;failedStage<2;++failedStage){
        auto committed=std::make_unique<Resource>(77);
        std::unique_ptr<Resource> vertex,index;int calls=0,rollbacks=0;const char* reason=nullptr;
        auto allocate=[&](uint32_t bytes,std::unique_ptr<Resource>& target){assert(bytes>0);target=std::make_unique<Resource>(calls+1);return calls++!=failedStage;};
        auto result=begin(plan,scene,[&](uint32_t n){return allocate(n,vertex);},[&](uint32_t n){return allocate(n,index);},[&]{vertex.reset();index.reset();++rollbacks;},reason);
        assert(result==(failedStage==0?BeginResult::VertexFailure:BeginResult::IndexFailure));
        assert(calls==failedStage+1&&rollbacks==1&&Resource::live==1&&committed->id==77);++rollbackCases;
        Retry retry;retry.fail(12345);assert(!retry.ready(13344)&&retry.ready(13345));
        result=begin(plan,scene,[&](uint32_t){vertex=std::make_unique<Resource>(10);return true;},[&](uint32_t){index=std::make_unique<Resource>(11);return true;},[&]{assert(false);},reason);
        assert(result==BeginResult::Ready&&Resource::live==3&&committed->id==77);
        committed.swap(vertex);vertex.reset();assert(committed->id==10&&Resource::live==2);
    }
    assert(Resource::live==0);
    auto reject=[&](const NorthlightWorldMesh::WorldMeshUploadPlan& bad,const NorthlightGI::WorldScene& source){
        int calls=0,rollbacks=0;const char* reason=nullptr;
        auto result=begin(bad,source,[&](uint32_t){++calls;return true;},[&](uint32_t){++calls;return true;},[&]{++rollbacks;},reason);
        assert(result==BeginResult::InvalidPlan&&reason&&calls==0&&rollbacks==1);++rejected;
    };
    auto bad=plan;bad.vertexBytes=0;reject(bad,scene);
    bad=plan;bad.indexBytes=uint64_t(UINT32_MAX)+1;reject(bad,scene);
    bad=plan;bad.vertexCount=0;reject(bad,scene);
    bad=plan;bad.triangleCount=2;reject(bad,scene);
    bad=plan;bad.indices.pop_back();reject(bad,scene);
    bad=plan;bad.batches[0].count=2;reject(bad,scene);
    bad=plan;bad.batches[0].material=1;reject(bad,scene);
    bad=plan;bad.materials[0].bgra.pop_back();reject(bad,scene);
    bad=plan;bad.batches[0].start=1;reject(bad,scene);
    auto anotherScene=scene;reject(plan,anotherScene);
    Retry retry;retry.fail(0xfffffff0u);assert(!retry.ready(983u)&&retry.ready(984u));retry.clear();assert(retry.ready(12));
    using V=NorthlightGI::Vec3;
    assert(applicable("Azeroth",V(),"Azeroth",V(96,0,0)));
    assert(!applicable("Azeroth",V(),"Azeroth",V(96.01f,0,0)));
    assert(!applicable("Azeroth",V(),"Kalimdor",V()));
    assert(!applicable("Azeroth",V(),"Azeroth",V(std::numeric_limits<float>::quiet_NaN(),0,0)));
    // A stale publication cannot be re-adopted on any subsequent stationary frame.
    unsigned adoptions=0;for(int frame=0;frame<1000;++frame)adoptions+=applicable("Azeroth",V(),"Azeroth",V(100,0,0));assert(adoptions==0);
    // 0.3.169 hold and adoption predicates.
    assert(retained("Azeroth",V(),"Azeroth",V(160,0,0))&&!retained("Azeroth",V(),"Azeroth",V(160.01f,0,0)));
    assert(retained("Azeroth",V(),"Azeroth",V(120,0,0))&&!applicable("Azeroth",V(),"Azeroth",V(120,0,0))); /* held, not applicable */
    assert(!retained("Azeroth",V(),"Kalimdor",V())); /* a map change retires at once */
    assert(!retained("Azeroth",V(),"Azeroth",V(std::numeric_limits<float>::quiet_NaN(),0,0)));
    assert(adopts("Azeroth",V(150,0,0),true,true,"Azeroth",V()));   /* a drawable replacement within the hard limit */
    assert(!adopts("Azeroth",V(161,0,0),true,true,"Azeroth",V()));  /* beyond it: never adopted, never held */
    assert(!adopts("Azeroth",V(),true,true,"Kalimdor",V()));
    assert(!adopts("Azeroth",V(),false,true,"Azeroth",V()));        /* an error snapshot never replaces a drawable one */
    assert(adopts("Azeroth",V(),false,false,"Azeroth",V()));        /* ... but replaces nothing or another error snapshot */
    // Frame sequence: A held at 120 while an error snapshot is published, then B adopted at 150.
    {struct S {V center;bool bvh;};S a{V(),true},error{V(100,0,0),false},b{V(140,0,0),true};const S* active=&a;unsigned frames=0,retired=0;
     auto frame=[&](const S* published,V eye){if(published&&published!=active&&adopts("Azeroth",published->center,published->bvh,active&&active->bvh,"Azeroth",eye))active=published;
         if(active&&!retained("Azeroth",active->center,"Azeroth",eye)){active=nullptr;++retired;}++frames;};
     frame(&error,V(120,0,0));assert(active==&a&&!retired);frame(&b,V(150,0,0));assert(active==&b);frame(&b,V(290,0,0));assert(active==&b);
     frame(&b,V(301,0,0));assert(!active&&retired==1);frame(&error,V(301,0,0));assert(!active); /* the error snapshot is 201 away */
     frame(&error,V(160,0,0));assert(active==&error&&frames==6);}
    // Geometry lead: none below 10 u/s, speed x expected build time, capped at 60.
    assert(leadCenter(V(1,2,3),V(9.9f,0,0),1).x==1);
    {const V c=leadCenter(V(),V(0,21.7f,0),1);assert(std::fabs(c.y-21.7f)<1e-4f&&c.x==0);}
    {const V c=leadCenter(V(),V(70,0,0),1.5f);assert(std::fabs(c.x-60)<1e-4f);}
    assert(leadCenter(V(5,0,0),V(std::numeric_limits<float>::quiet_NaN(),0,0),1).x==5&&leadCenter(V(5,0,0),V(70,0,0),0).x==5);
    // Safety: the lead keeps the eye inside the 96-unit coverage of any region whose centre is
    // within the refresh of the lead point; the held snapshot keeps the near cascade
    // (pivot <= eye+80, radius 48) inside the +-288 local box.
    static_assert(GeometryLeadMax+GeometryRefreshDistance<GeometryCoverageDistance,"lead cap");
    static_assert(GeometryRetainDistance+80+48<=288,"near cascade inside the local box");
    // Orbit centring shares the original total offset and coverage bounds.
    {const auto old=simulate(0,.35f,true,4),centered=simulate(0,.35f,true,4,14,34,true);
     assert(old.builds>=2&&centered.builds==1&&!centered.skipped);
     for(float speed:{7.f,21.7f,70.f}){
         const auto travel=simulate(speed,.35f,true,4,14,34,true);assert(!travel.skipped&&travel.maxDistance<=GeometryCoverageDistance);
     }
     assert(anchoredCenter(V(1,2,3),V(std::numeric_limits<float>::quiet_NaN(),0,0),V(),1).x==1);
     for(float distance:{0.f,12.f,34.f,80.f,1000.f})for(float speed:{0.f,7.f,70.f,200.f})for(int sign:{-1,1}){
         const V eye(17000,-17000,500),pivot(eye.x+distance*sign,eye.y,eye.z);
         const V c=anchoredCenter(eye,pivot,V(speed,0,0),1.5f),delta(c.x-eye.x,c.y-eye.y,c.z-eye.z);
         assert(std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z)<=GeometryLeadMax+.005f);
         assert(applicable("A",c,"A",eye));
     }}
    // Motion: a third-person flick moves the eye but not the pivot; a teleport or a gap resets.
    {Motion m;m.update("A",V(),V(34,0,0),1000);for(uint32_t t=1033;t<=1250;t+=33)m.update("A",V(34*(1-std::cos(3.14159f*(t-1000)/250.f)),0,0),V(34,0,0),t);
     const V v=m.velocity();assert(std::sqrt(v.x*v.x+v.y*v.y)<1e-3f);
     m=Motion();for(uint32_t t=0;t<=4000;t+=20)m.update("A",V(.0217f*t,0,0),V(.0217f*t+30,0,0),t);assert(std::fabs(m.velocity().x-21.7f)<.1f);
     m.update("A",V(500,0,0),V(530,0,0),4020);assert(m.velocity().x==0);                       /* teleport */
     m.update("A",V(501,0,0),V(531,0,0),4040);m.update("B",V(501,0,0),V(531,0,0),4060);assert(m.velocity().x==0&&m.map=="B"); /* map change */
     m.update("B",V(520,0,0),V(550,0,0),4800);assert(m.velocity().x==0);}                        /* loading gap */
    // M1: the lead arms only on sustained consistent travel. Standing still, none of these
    // camera or movement transients may produce a lead (then the builds equal a run without it).
    {const float pi=3.14159265f;
     auto flick=[&](float pd){return [=](float t,V& eye,V& pivot){const float s=t-.5f;const float angle=s<0?0:s<.25f?pi*s/.25f:s<2.25f?pi:s<2.5f?pi*(1-(s-2.25f)/.25f):0;
         const V f(std::cos(angle),std::sin(angle),0);eye=V(-34*f.x,-34*f.y,0);pivot=V(eye.x+pd*f.x,eye.y+pd*f.y,0);};};
     assert(peakLead(flick(34))==0&&peakLead(flick(20))==0&&peakLead(flick(12))==0);
     assert(peakLead([](float t,V& eye,V& pivot){const float d=30-25*std::min(1.f,std::max(0.f,(t-.5f)/.5f));eye=V(-d,0,0);pivot=V(30-d,0,0);})==0);   /* zoom 30->5 in 0.5 s */
     assert(peakLead([](float t,V& eye,V& pivot){const float d=t<1?34:14;eye=V(-d,0,0);pivot=V(34-d,0,0);})==0);                                    /* camera-collision snap of 20 */
     assert(peakLead([](float t,V& eye,V& pivot){const float p=t<1?0:20;eye=V(p-34,0,0);pivot=V(p,0,0);})==0);                                      /* blink or charge of 20 */
     // Consistent travel arms after 1 s: none while the smoothed speed ramps, 21.7 x 1 s once armed.
     Motion m;for(uint32_t t=0;t<=1200;t+=33)m.update("A",V(.0217f*t,0,0),V(.0217f*t+34,0,0),t);assert(!m.armed);
     for(uint32_t t=1233;t<=3000;t+=33)m.update("A",V(.0217f*t,0,0),V(.0217f*t+34,0,0),t);assert(m.armed&&std::fabs(leadCenter(V(),m.velocity(),1).x-21.7f)<.5f);}
    // M2: below the lead speed the GI request cadence is 0.3.168's; the stop after travel still
    // issues the lead-retraction request (liveness of the worker's waiting state).
    unsigned spinRequests[2]={},stopLeadOnly=0;
    {const float pi=3.14159265f;
     auto spin=[=](float t,V& eye,V& pivot){const float angle=2*pi*std::min(1.f,t/1.5f);const V f(std::cos(angle),std::sin(angle),0);eye=V(-34*f.x,-34*f.y,0);pivot=V(eye.x+12*f.x,eye.y+12*f.y,0);};
     for(float ahead:{0.f,20.f}){const auto a=requests(spin,3,false,ahead),b=requests(spin,3,true,ahead);assert(a.requests==b.requests&&!b.leadOnly);
         if(ahead>0){spinRequests[0]=a.requests;spinRequests[1]=b.requests;}}
     auto walk=[](float t,V& eye,V& pivot){eye=V(7*t-34,0,0);pivot=V(7*t,0,0);};
     assert(requests(walk,10,false).requests==requests(walk,10,true).requests);
     auto stop=[](float t,V& eye,V& pivot){const float p=21.7f*std::min(t,4.f);eye=V(p-34,0,0);pivot=V(p,0,0);};
     const auto c=requests(stop,8,true);stopLeadOnly=c.leadOnly;
     assert(c.leadOnly>0&&c.lastCenter.x==c.lastCamera.x&&c.lastCamera.x==21.7f*4-34);} /* the final request has no lead */
    // Protocol coverage. Flying mount 310 pct (21.7 u/s): every flick phase at every logged build
    // time, with an exact pivot distance (34) and a wrong one (20).
    unsigned phases=0;float flying=0,flyingOff=0;unsigned flyingOffSkipped=0;
    for(float pd:{34.f,20.f})for(float build:{.66f,1.f,1.17f,1.5f})for(int i=0;i<80;++i){const float at=2+i*.05f;++phases;
        const auto on=simulate(21.7f,build,true,at,14,pd);assert(!on.skipped&&on.maxDistance<GeometryRetainDistance);flying=std::max(flying,on.maxDistance);
        const auto off=simulate(21.7f,build,false,at,14,pd);flyingOff=std::max(flyingOff,off.maxDistance);flyingOffSkipped+=off.skipped;}
    assert(flyingOffSkipped>0); /* without the lead a 96-unit builder check drops the flick build */
    // GM speed 10 (70 u/s): straight travel for build times up to the logged fast-travel maximum,
    // once the lead has armed. Starting from a stop the first builds have no lead (1.17 s: as without it).
    float gm=0;unsigned gmStart=0;for(float build:{.66f,1.f,1.17f}){const auto on=simulate(70,build,true),off=simulate(70,build,false,-1,4);
        assert(!on.lateSkipped&&on.skipped<=off.skipped);gm=std::max(gm,on.lateMax);gmStart+=on.skipped;}
    // Walking: the lead is off (below 10 u/s), so build centres are unchanged.
    {const auto on=simulate(7,1,true,-1,60),off=simulate(7,1,false,-1,60);assert(on.builds==off.builds);
     for(size_t i=0;i<on.centres.size();++i)assert(on.centres[i].x==off.centres[i].x&&on.centres[i].y==off.centres[i].y);}
    // No extra builds: over 60 s the build count follows distance travelled (+1 for the lead start).
    unsigned builds[2][3]={};int column=0;for(float speed:{7.f,21.7f,70.f}){for(int lead=0;lead<2;++lead)builds[lead][column]=simulate(speed,1,lead!=0,-1,60).builds;
        assert(builds[1][column]<=builds[0][column]+1);++column;}
    std::printf("{\"allocation_rollback_cases\":%u,\"invalid_plans_rejected_before_allocation\":%u,\"retry_wraparound\":true,\"stale_publication_frames_rejected\":1000,\"old_resource_survives_failures\":true,\"hold_flying_phases\":%u,\"hold_flying_max\":%.1f,\"hold_flying_max_without_lead\":%.1f,\"hold_flying_skipped_without_lead\":%u,\"hold_gm_straight_max\":%.1f,\"hold_gm_start_skipped\":%u,\"spin_requests_0168_0169\":[%u,%u],\"stop_lead_requests\":%u,\"builds_60s_7_21_70\":[%u,%u,%u],\"builds_60s_with_lead\":[%u,%u,%u]}\n",rollbackCases,rejected,
        phases,flying,flyingOff,flyingOffSkipped,gm,gmStart,spinRequests[0],spinRequests[1],stopLeadOnly,builds[0][0],builds[0][1],builds[0][2],builds[1][0],builds[1][1],builds[1][2]);
}
