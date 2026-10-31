#include "point_light_shadow.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <random>
using namespace NorthlightPointShadow;
int main(){
    NorthlightLocalLights::Light light={{-9533,40,71},{1,.8f,.4f},1,12,123,1,1};
    RefreshSchedule schedule;
    assert(schedule.due(100,light,7,1000,false));
    schedule.commit(100,light,7);
    assert(!schedule.due(100,light,7,1000,false));
    assert(!schedule.due(132,light,7,256,false));
    assert(schedule.due(133,light,7,256,false));
    assert(schedule.due(101,light,7,255,false));
    assert(schedule.due(101,light,8,1000,false));
    assert(schedule.due(101,light,7,1000,true));
    auto changed=light;changed.sourceId++;assert(schedule.due(101,changed,7,1000,false));
    changed=light;changed.position[0]+=.01f;assert(schedule.due(101,changed,7,1000,false));
    changed=light;changed.attenuationEnd+=.1f;assert(schedule.due(101,changed,7,1000,false));
    changed=light;changed.diffuse[0]=.5f;assert(!schedule.due(101,changed,7,1000,false));
    // A failed partial update must never expose/reuse a half-old cube.
    schedule.invalidate();assert(schedule.due(101,light,7,1000,false));
    schedule.commit(UINT32_MAX-10,light,7);
    assert(!schedule.due(21,light,7,1000,false));
    assert(schedule.due(22,light,7,1000,false));
    unsigned updates=0,reuses=0;
    schedule.invalidate();
    for(uint32_t now=0;now<1000;now+=11){
        if(schedule.due(now,light,7,1000,false)){++updates;schedule.invalidate();schedule.commit(now,light,7);}
        else ++reuses;
    }
    assert(updates==31&&reuses==60);
    float matrices[6][16];
    for(unsigned face=0;face<6;++face)assert(faceMatrix(light.position,.1f,light.attenuationEnd,face,matrices[face]));
    std::mt19937 rng(118);std::uniform_real_distribution<float> offset(-15,15),extent(0,.5f);
    size_t visibleChecks=0,referenceChecks=0;
    for(unsigned trial=0;trial<12000;++trial){
        float low[3],high[3];for(unsigned i=0;i<3;++i){low[i]=light.position[i]+offset(rng);high[i]=low[i]+extent(rng);}
        const auto mask=faceMask(low,high,true,matrices);
        assert(faceMask(low,high,false,matrices)==63);
        for(unsigned face=0;face<6;++face){
            assert(bool(mask&(1u<<face))==!clipReject(low,high,matrices[face]));++referenceChecks;
            // Independent homogeneous projection of interior and corner points:
            // every sampled point inside this face must retain the whole AABB.
            for(unsigned sample=0;sample<9;++sample){
                double clip[4]={};
                for(unsigned column=0;column<4;++column){clip[column]=matrices[face][12+column];
                    for(unsigned row=0;row<3;++row){double p=sample==8?(double(low[row])+high[row])*.5:(sample&(1u<<row))?high[row]:low[row];
                        clip[column]+=p*matrices[face][4*row+column];}}
                if(clip[0]>=-clip[3]&&clip[0]<=clip[3]&&clip[1]>=-clip[3]&&clip[1]<=clip[3]&&clip[2]>=0&&clip[2]<=clip[3]){
                    assert(mask&(1u<<face));++visibleChecks;
                }
            }
        }
    }
    float badLow[3]={NAN,0,0},high[3]={1,1,1};assert(faceMask(badLow,high,true,matrices)==63);
    assert(visibleChecks>1000);
    // 0.3.151 usable vs complete: a stale (cycling) schedule is due but its cube stays usable.
    {RefreshSchedule r;assert(!r.usable&&!r.complete);r.commit(5,light,7);assert(r.usable&&r.complete&&!r.due(5,light,7,1000,false));
     r.stale();assert(r.usable&&!r.complete&&r.due(5,light,7,1000,false));r.invalidate();assert(!r.usable&&!r.complete);}
    // Balanced face groups: disjoint, all six, at most n per group, ceil(6/n) groups, deterministic.
    unsigned partitions=0;
    for(unsigned n=0;n<=7;++n)for(unsigned trial=0;trial<200;++trial){
        unsigned weights[6];for(auto& w:weights)w=unsigned(rng()%3000);if(trial==0)for(auto& w:weights)w=0;
        unsigned masks[6],again[6];const unsigned groups=partitionFaces(n,weights,masks),per=n<1?1:n>6?6:n;
        assert(groups==(6+per-1)/per&&partitionFaces(n,weights,again)==groups);
        unsigned all=0;for(unsigned g=0;g<groups;++g){assert(masks[g]&&!(all&masks[g])&&unsigned(__builtin_popcount(masks[g]))<=per&&masks[g]==again[g]);all|=masks[g];}
        assert(all==63);if(per==6)assert(groups==1&&masks[0]==63);++partitions;}
    {unsigned crowd[6]={900,800,700,600,5,0},masks[6];
     // Crowds sit in the horizontal faces: the heavy faces are spread, never paired as +X/-X, +Y/-Y.
     assert(partitionFaces(3,crowd,masks)==2);unsigned load[2]={};for(unsigned g=0;g<2;++g)for(unsigned f=0;f<6;++f)if(masks[g]&(1u<<f))load[g]+=crowd[f];
     assert(std::max(load[0],load[1])<=1600&&std::max(load[0],load[1])<1500+700); /* naive (+X,-X,+Y)/(-Y,+Z,-Z): 2400/605 */
     assert(partitionFaces(2,crowd,masks)==3);for(unsigned g=0;g<3;++g)assert(__builtin_popcount(masks[g]&15)<=2&&!((masks[g]&3)==3));
     unsigned zero[6]={};assert(partitionFaces(2,zero,masks)==3&&masks[0]==(1u|8u)&&masks[1]==(2u|16u)&&masks[2]==(4u|32u));}
    // Face cycle: ceil(6/n) masks in order; completion only after the last; inactive = all six.
    {FaceCycle c;unsigned w[6]={};assert(!c.active()&&c.mask()==63&&c.advance());
     c.start(2,w,1234,9);assert(c.active()&&c.count==3&&c.startedAt==1234&&c.generation==9);unsigned seen=0;
     for(unsigned i=0;i<3;++i){const unsigned m=c.mask();assert(m&&!(seen&m));seen|=m;assert(c.advance()==(i==2));}
     assert(seen==63&&!c.active()&&c.mask()==63);c.start(6,w,1,1);assert(c.count==1&&c.mask()==63&&c.advance());
     c.start(1,w,1,1);assert(c.count==6);c.reset();assert(!c.active());}
    // Static face content: non-terrain records inside the sphere, per face (the batch predicate).
    {using namespace NorthlightLocalShadowSignature;NorthlightLocalLights::Light lamp=light;lamp.position[0]=lamp.position[1]=lamp.position[2]=0;lamp.attenuationEnd=10;
     float m[6][16];for(unsigned face=0;face<6;++face)assert(faceMatrix(lamp.position,.1f,lamp.attenuationEnd,face,m[face]));
     auto record=[](NorthlightGI::Vec3 lo,NorthlightGI::Vec3 hi,uint32_t seed,bool terrain=false){Record r;r.low=lo;r.high=hi;Hasher h;h.word(seed);r.content=h.finish(1);r.terrain=terrain;return r;};
     Records records={record({4,-.5f,-.5f},{5,.5f,.5f},1),record({-5,-.5f,-.5f},{-4,.5f,.5f},2),record({-.5f,-.5f,6},{.5f,.5f,7},3),
                      record({4,-.5f,-.5f},{5,.5f,.5f},4,true),record({30,0,0},{31,1,1},5),record({2,2,2},{3,3,3},6)};
     Digest base[6];faceContent(records,lamp,m,base);
     assert(base[0].triangles==2&&base[1].triangles==1&&base[4].triangles==2); /* +X: 1 and the +x+y+z corner box; terrain and the far box never count */
     unsigned withCorner=0;for(unsigned f=0;f<6;++f)withCorner+=!clipReject(&records[5].low.x,&records[5].high.x,m[f]);
     unsigned total=0;for(const auto& d:base)total+=unsigned(d.triangles);assert(total==1+1+1+withCorner);
     auto swapped=records;std::reverse(swapped.begin(),swapped.end());Digest same[6];faceContent(swapped,lamp,m,same);for(unsigned f=0;f<6;++f)assert(same[f]==base[f]);
     auto changed=records;changed[0]=record({4,-.5f,-.5f},{5,.5f,.5f},99);Digest after[6];faceContent(changed,lamp,m,after);
     for(unsigned f=0;f<6;++f)assert((after[f]!=base[f])==bool(!clipReject(&records[0].low.x,&records[0].high.x,m[f])));
     changed=records;changed[3].content.words[0]^=1;changed[4].content.words[0]^=1;faceContent(changed,lamp,m,after);for(unsigned f=0;f<6;++f)assert(after[f]==base[f]); /* terrain/outside: no rebuild */
     NorthlightGI::Vec3 bad={NAN,0,0};assert(!recordOutsideSphere(bad,{1,1,1},lamp)); /* invalid boxes fail open */
     // LocalShadowPS keeps a fragment iff |offset|^2 <= R^2, offset rebuilt from the face clip
     // position in float (x+h*w, y-h*w, w). Every kept fragment's record meets the digest sphere,
     // and the digest margin is tight (1%).
     {std::uniform_real_distribution<float> coordinate(-12,12);unsigned kept=0;const float h=1.f/256;
      for(unsigned trial=0;trial<200000;++trial){const float q[3]={coordinate(rng),coordinate(rng),coordinate(rng)};
        for(unsigned face=0;face<6;++face){float c[4];for(unsigned col=0;col<4;++col)c[col]=q[0]*m[face][col]+q[1]*m[face][4+col]+q[2]*m[face][8+col]+m[face][12+col];
            if(c[3]<.1f||std::fabs(c[0])>c[3]||std::fabs(c[1])>c[3]||c[2]<0||c[2]>c[3])continue; /* not rasterized in this face */
            const float o[3]={c[0]+h*c[3],c[1]-h*c[3],c[3]};
            if(lamp.attenuationEnd*lamp.attenuationEnd-(o[0]*o[0]+o[1]*o[1]+o[2]*o[2])<0)continue;++kept;
            assert(!recordOutsideSphere({q[0],q[1],q[2]},{q[0],q[1],q[2]},lamp,FaceContentSphereMargin));}}
      assert(kept>10000);}
     assert(!recordOutsideSphere({10.05f,0,0},{11,1,1},lamp,FaceContentSphereMargin)&&recordOutsideSphere({10.2f,0,0},{11,1,1},lamp,FaceContentSphereMargin));
     // staleFaces: content, not the generation, decides; unknown records keep the generation rule.
     bool known[6]={true,true,true,true,true,true},unknown[6]={};uint64_t serial[6]={7,7,7,7,7,7};Digest none[6];
     assert(staleFaces(base,known,serial,base,8)==0);           /* a mesh commit elsewhere: no face redrawn */
     faceContent(changed=records,lamp,m,after);changed[2]=record({-.5f,-.5f,6},{.5f,.5f,7},77);faceContent(changed,lamp,m,after);
     assert(staleFaces(base,known,serial,after,8)==(1u<<4));  /* only +Z */
     assert(staleFaces(base,known,serial,nullptr,8)==63); /* records lost at a commit: redraw */
     assert(staleFaces(none,unknown,serial,nullptr,7)==0&&staleFaces(none,unknown,serial,nullptr,8)==63&&staleFaces(none,unknown,serial,base,8)==63);}
    // Renderer protocol (world_point_rendering.inl renderPointShadow) for one lamp over frames:
    // faces drawn per frame, commit time, and the capture-skip prediction (due).
    unsigned cycleFrames=0;
    for(unsigned n:{6u,4u,3u,2u,1u}){RefreshSchedule sched;FaceCycle cycle;unsigned weights[6]={};bool cacheValid=false;auto lamp=light;
        auto frame=[&](uint32_t now,bool fresh,unsigned& faces)->bool{ /* false: reused, nothing drawn */
            const bool lightChanged=sched.light.sourceId!=lamp.sourceId||sched.light.attenuationEnd!=lamp.attenuationEnd||std::memcmp(sched.light.position,lamp.position,12);
            faces=0;if(!fresh)return false;
            if(!sched.due(now,lamp,7,1000,!cacheValid||lightChanged))return false;
            const bool cycling=n<6&&sched.usable&&!lightChanged&&cacheValid;
            faces=63;if(cycling){if(!cycle.active())cycle.start(n,weights,now,7);faces=cycle.mask();sched.stale();}else{cycle.reset();sched.invalidate();}
            for(unsigned f=0;f<6;++f)if(faces&(1u<<f))weights[f]=f<4?100:1;
            cacheValid=true;
            if(cycling&&!cycle.advance())return true;
            sched.commit(cycling?cycle.startedAt:now,lamp,7);return true;};
        unsigned faces=0;assert(frame(0,true,faces)&&faces==63&&sched.complete); /* first light: all six in one frame */
        assert(!frame(10,true,faces)); /* within 33 ms */
        uint32_t now=40;unsigned seen=0,frames=0;
        do{assert(frame(now,true,faces)&&faces&&!(seen&faces));seen|=faces;++frames;++now;
            if(seen!=63){assert(!sched.complete&&sched.usable&&sched.due(now,lamp,7,1000,false)); /* prediction stays true */
                unsigned before=cycle.next;assert(!frame(now,false,faces)&&faces==0&&cycle.next==before);++now;}} /* no replays: no progress */
        while(seen!=63);
        assert(frames==(6+n-1)/n&&sched.complete&&sched.updatedAt==40&&!sched.due(40+32,lamp,7,1000,false)&&sched.due(40+33,lamp,7,1000,false));
        cycleFrames+=frames;
        if(n<6){now=200;assert(frame(now,true,faces)&&faces!=63);lamp.position[0]+=.5f; /* moved mid-cycle: all six at once */
            assert(frame(now+1,true,faces)&&faces==63&&sched.complete&&!cycle.active());}
    }
    assert(cycleFrames==1+2+2+3+6);
    std::cout<<"point face cycles passed: "<<partitions<<" balanced partitions; 6 = one frame of 63; prediction due while incomplete; no progress without replays; moved light redraws six; per-face content digests\n";
    std::cout<<"point schedule passed: source/map-resource/generation/position/range changes, failure, wrap; "
             <<updates<<" complete updates + "<<reuses<<" reuses at 91 input frames; "
             <<referenceChecks<<" legacy face decisions and "<<visibleChecks<<" visible-point enclosure checks\n";
}
