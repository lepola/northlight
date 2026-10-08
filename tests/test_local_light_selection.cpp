#include "local_light_selection.h"
#include "regional_fog.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
using namespace NorthlightLocalLightSelection;
static NorthlightLocalLights::Light lamp(unsigned id){
    NorthlightLocalLights::Light l{};l.position[0]=float(id*2);l.diffuse[0]=float(id);l.diffuse[1]=.5f;l.diffuse[2]=.25f;
    l.attenuationStart=1;l.attenuationEnd=10;l.sourceId=id;l.kind=2;return l;
}
template<unsigned N> void checkBatches(const Selection& s){
    unsigned seen=0;float total=0;
    for(unsigned first=0;first<s.count;first+=N){auto b=s.batch<N>(first);assert(b.count==std::min(N,s.count-first));
        for(unsigned i=0;i<N;++i){if(i<b.count){++seen;total+=b.color[i][0];assert(b.position[i]==s.position[first+i]);assert(b.fog[i]==s.fog[first+i]);}
            else for(unsigned j=0;j<4;++j)assert(b.position[i][j]==0&&b.color[i][j]==0&&b.fog[i][j]==0);}
    }
    assert(seen==s.count);float expected=0;for(unsigned i=0;i<s.count;++i)expected+=s.color[i][0];assert(total==expected);
    auto empty=s.batch<N>(s.count);assert(empty.count==0);for(auto c:empty.color)for(float v:c)assert(v==0);
}
// ---- 0.3.197: Tracker (soft cap, incumbent bias, time-based fades) ----
using Light=NorthlightLocalLights::Light;
// Camera at the origin: a lamp at x=score+10 with attenuationEnd 10 has (nominal) score `score`; the id-dependent y keeps positions unique.
static Light lampScore(unsigned id,float score){
    Light l{};l.position[0]=score+10;l.position[1]=float(id)*.01f;l.diffuse[0]=1;l.diffuse[1]=.5f;l.diffuse[2]=.25f;
    l.attenuationStart=1;l.attenuationEnd=10;l.sourceId=id;l.kind=2;return l;
}
static float shownOf(const Tracker& t,std::uint64_t id){for(unsigned i=0;i<t.count;++i)if(t.entries[i].id==id)return t.entries[i].shown;return 0.f;}
static bool selectedOf(const Tracker& t,std::uint64_t id){for(unsigned i=0;i<t.count;++i)if(t.entries[i].id==id)return t.entries[i].selected;return false;}
static float scoreOf(const Light& l,const float* camera){float d2=0;for(int i=0;i<3;++i){const float d=l.position[i]-camera[i];d2+=d*d;}return std::sqrt(d2)-l.attenuationEnd;}
static const float Step60=(1.f/60)/FadeSeconds;
struct Scene {std::vector<Light> lights;float camera[3]={};};
// Runs a motion and returns the largest per-frame change of any id's factor (ids 1..maxId). Also checks, every frame, that
// the output is consistent with the tracker state: colour = diffuse*factor, selected lights first and closest-first,
// fading lights only at the tail, and (continuous) no growth beyond limit+Spare.
static float motion(unsigned frames,unsigned maxId,unsigned limit,bool continuous,float dt,const std::function<Scene(unsigned)>& scene){
    Tracker t;float worst=0;std::vector<float> previous(maxId+1,0.f);
    for(unsigned f=0;f<frames;++f){
        const Scene sc=scene(f);
        if(!continuous)t.reset(); // a fresh tracker every frame: the pure target, without incumbents
        const Selection out=t.update(sc.lights,sc.camera,limit,dt,continuous&&f>0);
        assert(out.count<=Slots&&out.count>=t.fading&&t.fading<=Spare);
        const unsigned selected=out.count-t.fading;assert(selected<=limit);
        float last=-1e9f;
        for(unsigned i=0;i<out.count;++i){
            const Light* src=nullptr;for(const auto& l:sc.lights)if(l.position[0]==out.position[i][0]&&l.position[1]==out.position[i][1])src=&l;
            if(i<selected){assert(src&&selectedOf(t,src->sourceId));const float s=scoreOf(*src,sc.camera);assert(s>=last);last=s;}
            else{assert(!selectedOf(t,[&]{for(unsigned k=0;k<t.count;++k)if(t.entries[k].light.position[0]==out.position[i][0]&&t.entries[k].light.position[1]==out.position[i][1])return t.entries[k].id;return std::uint64_t(0);}()));}
            const std::uint64_t id=i<selected?src->sourceId:[&]{for(unsigned k=0;k<t.count;++k)if(t.entries[k].light.position[0]==out.position[i][0]&&t.entries[k].light.position[1]==out.position[i][1])return t.entries[k].id;return std::uint64_t(0);}();
            assert(id&&out.color[i][0]==shownOf(t,id));
        }
        for(unsigned id=1;id<=maxId;++id){const float v=shownOf(t,id);if(f)worst=std::max(worst,std::fabs(v-previous[id]));previous[id]=v;}
    }
    return worst;
}
static Selection stepFrames(Tracker& t,const std::vector<Light>& lights,const float* camera,unsigned limit,float dt,unsigned frames,bool continuous=true){
    Selection out;for(unsigned f=0;f<frames;++f)out=t.update(lights,camera,limit,dt,continuous);return out;
}
static bool sameSelection(const Selection& a,const Selection& b){
    return a.count==b.count&&a.nearest==b.nearest&&a.fogDistance==b.fogDistance&&a.position==b.position&&a.color==b.color&&a.fog==b.fog;
}
static void trackerTests(){
    static_assert(Slots==72&&Spare==8,"72 slots");
    assert(capBand(8)==2&&capBand(32)==8&&capBand(64)==16&&capBand(1)==1&&capBand(3)==1);
    // 1a. Target continuity without fades: every frame is a restart (continuous=false), so factor == target.
    const auto movingLamp=[](unsigned f){ // 33 lamps (rank 33 moves from 220 down through everyone to 90 and back)
        Scene s;for(unsigned i=1;i<=32;++i)s.lights.push_back(lampScore(i,100+2.f*(i-1)));
        const unsigned half=13000;const float x=f<=half?220-.01f*f:220-.01f*(2*half-f);s.lights.push_back(lampScore(33,x));return s;};
    const auto swapping=[](unsigned f){ // 40 lamps; the camera walks so that lamps around ranks 31/32 swap
        Scene s;for(unsigned i=1;i<=40;++i){Light l=lampScore(i,0);const float base=110+1.5f*i;l.position[0]=i%2?base:-base;s.lights.push_back(l);}
        s.camera[0]=-20+.005f*f;return s;};
    // Steps of .01 (moving lamp) and .005 (camera) units: the steepest band slope (1.5/width per unit) stays far below
    // .01 per step, so anything above .01 is a jump, not a gradient.
    const float a1=motion(26001,33,32,false,1.f/60,movingLamp),a2=motion(8001,40,32,false,1.f/60,swapping);
    assert(a1<=.01f&&a2<=.01f);
    // 1b. The same motions as one continuous stream: a factor moves at most dt/FadeSeconds per frame.
    const float b1=motion(26001,33,32,true,1.f/60,movingLamp),b2=motion(8001,40,32,true,1.f/60,swapping);
    assert(b1<=Step60+1e-6f&&b2<=Step60+1e-6f);
    for(unsigned limit:{8u,16u,64u}){assert(motion(8001,40,limit,true,1.f/60,swapping)<=Step60+1e-6f);}
    // 2. Fades. A lamp that appears ramps 0->1 linearly in FadeSeconds, 30 fps and 120 fps agree.
    {const float camera[3]={};std::vector<Light> lights;for(unsigned i=1;i<=20;++i)lights.push_back(lampScore(i,100+2.f*i));
     Tracker t;t.update(lights,camera,32,1.f/60,false);assert(shownOf(t,1)==1);
     lights.push_back(lampScore(21,90));
     Tracker t30=t,t120=t;
     for(unsigned f=1;f<=24;++f){auto out=t.update(lights,camera,32,1.f/60,true);
         const float want=std::min(1.f,float(f)*Step60);assert(std::fabs(shownOf(t,21)-want)<2e-5f);
         assert(out.count==21&&out.color[0][0]==shownOf(t,21)&&t.fading==0);} // closest lamp: first output
     assert(shownOf(t,21)==1);
     stepFrames(t30,lights,camera,32,1.f/30,5);stepFrames(t120,lights,camera,32,1.f/120,20); // 1/6 s
     assert(std::fabs(shownOf(t30,21)-shownOf(t120,21))<1e-5f&&std::fabs(shownOf(t30,21)-(1.f/6)/FadeSeconds)<1e-5f);
     // A lamp that goes away stays listed, at the tail, until its factor reaches 0 (0.4 s), then disappears.
     lights.erase(lights.begin()+4);                                  // id 5
     for(unsigned f=1;f<=24;++f){auto out=t.update(lights,camera,32,1.f/60,true);
         if(f<24){assert(t.fading==1&&out.count==21);assert(std::fabs(shownOf(t,5)-(1.f-float(f)*Step60))<2e-5f);assert(!selectedOf(t,5));
             assert(out.color[20][0]==shownOf(t,5)&&out.position[20][0]==100+2.f*5+10);}
         else assert(t.fading==0&&out.count==20&&shownOf(t,5)==0);}}
    // Fading lamps never move the batches of the selected ones, and Spare keeps the brightest (id breaks ties).
    {const float camera[3]={};std::vector<Light> lights;Tracker t;
     for(unsigned i=1;i<=20;++i){lights.push_back(lampScore(i,100+2.f*i));t.update(lights,camera,32,1.f/60,i>1);}
     const float s1=shownOf(t,1),s12=shownOf(t,12),s13=shownOf(t,13);assert(s1>s12&&s12>s13);
     lights.erase(lights.begin(),lights.begin()+12);                  // ids 1..12 vanish together
     auto out=t.update(lights,camera,32,1.f/60,true);
     assert(t.fading==Spare&&out.count==16);
     for(unsigned id=1;id<=8;++id)assert(shownOf(t,id)>0&&!selectedOf(t,id));
     for(unsigned id=9;id<=12;++id)assert(shownOf(t,id)==0);
     for(unsigned i=0;i<8;++i)assert(out.position[i][0]==100+2.f*(13+i)+10);}
    // 3. Reset (map change, camera jump, long gap, F10): factors jump to their targets and nothing fades.
    {const float camera[3]={};std::vector<Light> lights;for(unsigned i=1;i<=10;++i)lights.push_back(lampScore(i,100+2.f*i));
     Tracker t;t.update(lights,camera,32,1.f/60,false);lights.push_back(lampScore(11,50));lights.erase(lights.begin());
     auto out=t.update(lights,camera,32,1.f/60,false);assert(t.fading==0&&out.count==10&&shownOf(t,11)==1&&shownOf(t,1)==0);
     Tracker u;u.update(lights,camera,32,1.f/60,true);assert(std::fabs(shownOf(u,11)-Step60)<1e-6f); // a continuous first frame fades in from zero
     t.update(lights,camera,0,1.f/60,true);assert(t.count==0&&t.fading==0);}
    // 3b. A gap (hitch, F10 off, loading) at the same camera snaps without fades but keeps the incumbents: nothing moves.
    //     Lamps packed .25 apart around the cap edge, where forgetting the StickyBias used to drop a lamp from .79 to .04.
    for(const float spacing:{.25f,1.f,3.f}){const float camera[3]={};std::vector<Light> lights;
        for(unsigned i=1;i<=48;++i)lights.push_back(lampScore(i,100+spacing*float(i)));
        Tracker t;const Selection before=stepFrames(t,lights,camera,32,1.f/60,120);
        const Selection after=t.update(lights,camera,32,.3f,false);assert(sameSelection(before,after)&&t.fading==0);
        const Selection next=t.update(lights,camera,32,1.f/60,true);assert(sameSelection(before,next));}
    // 4. Under the cap the output is select()'s, bit for bit, from the first frame and across a smooth walk.
    {unsigned seed=12345;const auto rnd=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
     for(unsigned limit:{8u,16u,24u,32u,64u})for(unsigned n=0;n<=limit;++n){
         std::vector<Light> lights;for(unsigned i=1;i<=n;++i){Light l{};for(int k=0;k<3;++k){l.position[k]=(rnd()-.5f)*400;l.diffuse[k]=.2f+rnd();}
             l.attenuationEnd=5+rnd()*25;l.attenuationStart=l.attenuationEnd*(.1f+.5f*rnd());l.sourceId=i*3;l.kind=2;lights.push_back(l);}
         Tracker t;
         for(unsigned f=0;f<120;++f){float camera[3]={f*.3f,-f*.2f,f*.05f};
             assert(sameSelection(t.update(lights,camera,limit,1.f/60,f>0),select(lights,camera,limit))&&t.fading==0);}}}
    // 5. Equal scores: input order never matters, and consecutive frames repeat.
    {const float camera[3]={};std::vector<Light> lights;for(unsigned i=1;i<=40;++i){Light l=lampScore(i,100);l.position[1]=0;lights.push_back(l);}
     auto reversed=lights;std::reverse(reversed.begin(),reversed.end());
     for(unsigned limit:{8u,32u,40u,64u}){Tracker a,b;Selection first;
         for(unsigned f=0;f<60;++f){auto x=a.update(lights,camera,limit,1.f/60,f>0),y=b.update(reversed,camera,limit,1.f/60,f>0);assert(sameSelection(x,y));
             if(f==30)first=x;if(f>30)assert(sameSelection(x,first)||limit==40||limit==64);}}
     for(unsigned f=0;f<5;++f){Tracker a,b;auto x=stepFrames(a,lights,camera,32,1.f/60,f+1,false),y=stepFrames(b,reversed,camera,32,1.f/60,f+1,false);assert(sameSelection(x,y));}}
    // 7. fogDistance covers fading lamps too.
    {const float camera[3]={};std::vector<Light> lights{lampScore(1,100),lampScore(2,110)};Light big=lampScore(3,120);big.attenuationEnd=300;big.position[0]=420;lights.push_back(big);
     Tracker t;auto out=t.update(lights,camera,32,1.f/60,false);const float full=out.fogDistance;assert(full>=scoreOf(big,camera)+600);
     lights.pop_back();out=t.update(lights,camera,32,1.f/60,true);assert(t.fading==1&&out.fogDistance==full);
     stepFrames(t,lights,camera,32,1.f/60,30);assert(t.fading==0);assert(t.update(lights,camera,32,1.f/60,true).fogDistance==select(lights,camera).fogDistance&&select(lights,camera).fogDistance<full);}
}
// Performance reference (no timing asserts): a city-like crowd of 921 lamps, a camera walking 600 frames.
static void benchmark(){
    unsigned seed=777;const auto rnd=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
    std::vector<Light> lights;for(unsigned i=1;i<=921;++i){Light l{};float x,y;do{x=(rnd()-.5f)*1040;y=(rnd()-.5f)*1040;}while(x*x+y*y>520.f*520);
        l.position[0]=x;l.position[1]=y;l.position[2]=rnd()*30;for(int k=0;k<3;++k)l.diffuse[k]=.2f+rnd()*1.3f;
        l.attenuationEnd=5+rnd()*25;l.attenuationStart=l.attenuationEnd*(.1f+.6f*rnd());l.sourceId=i;l.kind=2;lights.push_back(l);}
    using clock=std::chrono::steady_clock;const unsigned frames=600;
    // Camera on a 150-unit circle at 60 fps: run speed (7 units/s), a fast mount (30 units/s) and a stress pace (90 units/s).
    for(const float speed:{7.f,30.f,90.f}){
        const float omega=speed/60/150;double legacyBest=1e30,trackerBest=1e30,checksum=0,fadingSum=0;unsigned fadingMax=0;
        for(unsigned rep=0;rep<5;++rep){
            auto t0=clock::now();
            for(unsigned f=0;f<frames;++f){const float camera[3]={150*std::cos(f*omega),150*std::sin(f*omega),2};const auto s=select(lights,camera,Limit);checksum+=s.count+s.color[0][0]+s.fogDistance;}
            auto t1=clock::now();Tracker t;
            for(unsigned f=0;f<frames;++f){const float camera[3]={150*std::cos(f*omega),150*std::sin(f*omega),2};const auto s=t.update(lights,camera,Limit,1.f/60,f>0);checksum+=s.count+s.color[0][0]+s.fogDistance;
                if(!rep){fadingSum+=t.fading;fadingMax=std::max(fadingMax,t.fading);}}
            auto t2=clock::now();
            legacyBest=std::min(legacyBest,std::chrono::duration<double,std::micro>(t1-t0).count()/frames);trackerBest=std::min(trackerBest,std::chrono::duration<double,std::micro>(t2-t1).count()/frames);
        }
        std::printf("bench lamps=921 frames=600 speed=%.0f legacyUsPerFrame=%.2f trackerUsPerFrame=%.2f fadingAvg=%.2f fadingMax=%u checksum=%.1f\n",speed,legacyBest,trackerBest,fadingSum/frames,fadingMax,checksum);
    }
}
int main(){
    const float camera[3]={};
    for(unsigned count=0;count<=64;++count){std::vector<NorthlightLocalLights::Light> lights;
        for(unsigned i=1;i<=count;++i)lights.push_back(lamp(i));
        auto s=select(lights,camera);assert(s.count==std::min(count,Limit));
        for(unsigned i=0;i<s.count;++i){assert(s.color[i][0]==float(i+1));s.fog[i][0]=.01f*(i+1);}
        checkBatches<DirectBatchSize>(s);checkBatches<FogBatchSize>(s);
        std::reverse(lights.begin(),lights.end());auto reversed=select(lights,camera);assert(s.position==reversed.position&&s.color==reversed.color);
    }
    auto a=lamp(1),b=lamp(2);b.position[0]=a.position[0];auto bad=lamp(3);bad.diffuse[0]=-1;auto far=lamp(4);far.position[0]=235;
    auto s=select({b,bad,far,a},camera);assert(s.count==2&&s.color[0][0]==1&&s.color[1][0]==2);
    // Activation is farther away while physical radii/falloff remain identical.
    float previous=1;
    for(int reach=0;reach<=225;++reach){
        auto l=lamp(1);l.position[0]=float(reach)+l.attenuationEnd;
        auto selected=select({l},camera);
        if(reach>=224){assert(selected.count==0);continue;}
        assert(selected.count==1&&selected.position[0][3]==10);
        assert(selected.color[0][3]==1.f/9);
        const float gain=selected.color[0][0];assert(gain<=previous&&previous-gain<.05f);previous=gain;
        if(reach<=192)assert(gain==1);
        if(reach==208)assert(gain==.5f);
        assert(selected.color[0][1]==gain*.5f&&selected.color[0][2]==gain*.25f);
    }
    assert(visibilityGain(224)==0&&visibilityGain(223)<.003f);
    auto large=lamp(1);large.position[0]=300;large.attenuationEnd=128;
    const auto scaled=select({large},camera);assert(scaled.count==1&&scaled.fogDistance==428);
    assert(select({},camera).fogDistance==128);
    assert(nightGain(0)==1&&std::fabs(nightGain(1)-.8f)<1e-6f);
    assert(nightGain(NorthlightRegionalFog::nightFactor(.5f))==1);
    assert(std::fabs(nightGain(NorthlightRegionalFog::nightFactor(0))-.8f)<1e-6f);
    float last=1;
    for(unsigned minute=18*60;minute<=20*60;++minute){float gain=nightGain(NorthlightRegionalFog::nightFactor(float(minute)/1440));assert(gain<=last&&last-gain<.003);last=gain;}
    // Scaling both fog energy and its soft cap must dim even saturated glow
    // by the same 20%, not merely move it closer to the old brightness cap.
    for(double energy:{0.,.001,.1,1.,100.}){
        const double day=energy*25*.3/(.3+energy*25),night=energy*20*.24/(.24+energy*20);
        assert(std::fabs(night-day*.8)<1e-10);
    }
    trackerTests();benchmark();
    std::puts("PASS local lights: 0..64 candidates, nearest 32, stable ties/order, partial 8/4 batches zero-filled with no dropped/duplicated lights, out-of-range/invalid rejection, smooth 20% night dimming including saturated fog glow; tracker: soft cap continuity, linear time fades, resets, gaps keep incumbents, under-cap identity with select(), tie determinism, fog over fading lamps");
}
