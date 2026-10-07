#include "quality_settings.h"
#include "local_light_selection.h"
#include "point_light_shadow.h"
#include "world_math.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
using namespace NorthlightQuality;
static Settings parse(const char* own,const char* legacy,std::vector<std::string>& problems){
    std::istringstream a(own?own:""),b(legacy?legacy:"");return load(own?&a:nullptr,legacy?&b:nullptr,problems);
}
static Settings parse(const char* own,const char* legacy=nullptr){std::vector<std::string> p;return parse(own,legacy,p);}
/* Verbatim 0.3.136 code paths, for defaults-equal-today comparisons. */
static NorthlightGI::Vec3 oldQuantize(NorthlightGI::Vec3 direction){
    using namespace NorthlightGI;
    Vec3 q(std::round(direction.x*2048)/2048,std::round(direction.y*2048)/2048,std::round(direction.z*2048)/2048);
    if(dot(q,q)<1e-12f)return direction;
    return normalized(q);
}
/* Verbatim 0.3.139 selection: capacity 32. */
struct OldSelection {std::array<NorthlightLocalLightSelection::Constant,32> position{},color{},fog{};unsigned count=0;float nearest=0,fogDistance=128.f;};
static OldSelection oldSelect(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera,unsigned limit=32){
    using namespace NorthlightLocalLightSelection;limit=std::min(limit,32u);
    struct Pick {float score;const NorthlightLocalLights::Light* light;};
    const auto before=[](const Pick& a,const Pick& b){return a.score!=b.score?a.score<b.score:a.light->sourceId<b.light->sourceId;};
    std::array<Pick,32> picks{};unsigned count=0;
    for(const auto& l:lights){
        if(!NorthlightLocalLights::valid(l)||l.attenuationEnd<=.11f)continue;
        float d2=0;for(unsigned i=0;i<3;++i){const float d=l.position[i]-camera[i];d2+=d*d;}
        Pick p{std::sqrt(d2)-l.attenuationEnd,&l};if(p.score>=VisibilityEnd)continue;
        if(count<limit)picks[count++]=p;
        else if(limit){auto worst=std::max_element(picks.begin(),picks.begin()+limit,before);if(before(p,*worst))*worst=p;}
    }
    std::sort(picks.begin(),picks.begin()+count,before);
    OldSelection out;out.count=count;if(count)out.nearest=picks[0].score;
    for(unsigned i=0;i<count;++i){const auto& l=*picks[i].light;
        out.position[i]={l.position[0],l.position[1],l.position[2],l.attenuationEnd};
        const float gain=visibilityGain(picks[i].score);
        out.fogDistance=std::max(out.fogDistance,picks[i].score+2*l.attenuationEnd);
        out.color[i]={l.diffuse[0]*gain,l.diffuse[1]*gain,l.diffuse[2]*gain,1.f/std::max(l.attenuationEnd-std::min(l.attenuationStart,l.attenuationEnd*.9f),.05f)};
    }
    return out;
}
/* Byte-identical selection prefix and every shader batch the renderer uploads (8-light direct, 4-light fog). */
template<class S> static void sameAsOld(const S& n,const OldSelection& o){
    using namespace NorthlightLocalLightSelection;
    assert(n.count==o.count&&!std::memcmp(&n.nearest,&o.nearest,4)&&!std::memcmp(&n.fogDistance,&o.fogDistance,4));
    assert(!std::memcmp(n.position.data(),o.position.data(),sizeof o.position)&&!std::memcmp(n.color.data(),o.color.data(),sizeof o.color)&&!std::memcmp(n.fog.data(),o.fog.data(),sizeof o.fog));
    for(unsigned i=32;i<Capacity;++i)assert(n.position[i]==Constant{}&&n.color[i]==Constant{}&&n.fog[i]==Constant{});
    unsigned direct=0,fog=0;
    for(unsigned first=0;first<n.count;first+=DirectBatchSize){auto b=n.template batch<DirectBatchSize>(first);++direct;
        for(unsigned i=0;i<DirectBatchSize;++i){const bool used=first+i<o.count;assert(b.position[i]==(used?o.position[first+i]:Constant{})&&b.color[i]==(used?o.color[first+i]:Constant{}));}}
    for(unsigned first=0;first<n.count;first+=FogBatchSize){auto b=n.template batch<FogBatchSize>(first);++fog;
        for(unsigned i=0;i<FogBatchSize;++i){const bool used=first+i<o.count;assert(b.position[i]==(used?o.position[first+i]:Constant{})&&b.fog[i]==(used?o.fog[first+i]:Constant{}));}}
    assert(direct==(o.count+7)/8&&fog==(o.count+3)/4);
}
static bool oldDue(const NorthlightPointShadow::RefreshSchedule& s,uint32_t now,size_t replays){return replays<256||uint32_t(now-s.updatedAt)>=33;}
int main(){
    /* Defaults: absent files, empty file and Preset=Quality are the 0.3.136 constants, except the 0.3.167 FarShadowInterval 4
       and the 0.3.176 PointShadows 0 (off in every preset). */
    const Settings d{};
    assert(d.minSkinnedTriangles==100&&d.captureBudgetMiB==32&&d.actorShadowBudgetMiB==0&&d.farShadowInterval==4);
    assert(d.localLightLimit==NorthlightLocalLightSelection::Limit&&d.pointShadows==0&&d.pointShadowRefreshMs==0&&d.shadowDirectionSteps==2048);
    assert(parse(nullptr)==d&&parse("")==d&&parse("[Quality]\nPreset=Quality\n")==d&&preset(Preset::Quality)==d);
    for(const auto& k:Keys){assert(k.preset[0]==d.*k.field);for(unsigned p=0;p<3;++p)assert(k.preset[p]>=k.low&&k.preset[p]<=k.high);}
    { /* The shipped default file must be exactly Quality, with no warnings. */
        std::ifstream f("windows-package/northlight-quality.ini");assert(f);std::vector<std::string> problems;
        auto s=load(&f,nullptr,problems);assert(s==d&&s.preset==Preset::Quality&&problems.empty());}
    /* Presets and case-insensitive names, BOM, CRLF, inline comments. */
    assert(parse("[Quality]\nPreset=Balanced\n")==preset(Preset::Balanced));
    auto perf=parse("\xEF\xBB\xBF; c\r\n[ quality ]\r\npreset = PERFORMANCE ; low end\r\n");
    assert(perf==preset(Preset::Performance)&&perf.preset==Preset::Performance&&perf.farShadowInterval==6&&perf.pointShadows==0);
    assert(preset(Preset::Balanced).pointShadows==0&&parse("[Quality]\nPointShadows=1\n").pointShadows==1); /* 0.3.176: off in every preset, the key still turns them on */
    assert(parse("[Quality]\nPreset=Performance\nfarshadowinterval=1 # keep\n").farShadowInterval==1);
    /* Explicit keys override the preset regardless of order; other sections ignored. */
    auto s=parse("[Other]\nLocalLightLimit=8\n[Quality]\nLocalLightLimit=20\nPreset=Balanced\n");
    assert(s.localLightLimit==20&&s.farShadowInterval==5);
    /* Invalid values keep the preset value and are reported. */
    std::vector<std::string> problems;
    s=parse("[Quality]\nPreset=Balanced\nFarShadowInterval=17\nLocalLightLimit=-1\nPointShadows=yes\nMinSkinnedTriangles=50abc\nShadowDirectionSteps=\nBogus=1\nnoequals\n[broken\n",nullptr,problems);
    assert(s==preset(Preset::Balanced)&&problems.size()==8);
    problems.clear();s=parse("[Quality]\nPreset=Ultra\n",nullptr,problems);assert(s==d&&problems.size()==1);
    for(const auto& k:Keys){ /* range edges */
        std::string lo="[Quality]\n"+std::string(k.name)+"="+std::to_string(k.low)+"\n",hi="[Quality]\n"+std::string(k.name)+"="+std::to_string(k.high)+"\n";
        assert(parse(lo.c_str()).*k.field==k.low&&parse(hi.c_str()).*k.field==k.high);
        std::string over="[Quality]\n"+std::string(k.name)+"="+std::to_string(k.high+1)+"\n";assert(parse(over.c_str())==d);
        if(k.low){std::string under="[Quality]\n"+std::string(k.name)+"="+std::to_string(k.low-1)+"\n";assert(parse(under.c_str())==d);}}
    /* Legacy shadow-experiment.ini: same values and fallbacks as 0.3.136's GetPrivateProfileInt path. */
    const char* legacy="; 0.3.131 experiment\n[ShadowExperiment]\nMinSkinnedTriangles=50\nCaptureBudgetMiB=32\nActorShadowBudgetMiB=16\n";
    s=parse(nullptr,legacy);assert(s.minSkinnedTriangles==50&&s.captureBudgetMiB==32&&s.actorShadowBudgetMiB==16&&s.farShadowInterval==4);
    s=parse(nullptr,"[ShadowExperiment]\nMinSkinnedTriangles=501\nCaptureBudgetMiB=0\nActorShadowBudgetMiB=33\n");assert(s==d); /* 0.3.136: 0,32,0 */
    s=parse(nullptr,"[ShadowExperiment]\nCaptureBudgetMiB=16\n");assert(s.captureBudgetMiB==16&&s.minSkinnedTriangles==100);
    /* No northlight-quality.ini, or Preset=Quality: each machine exactly as today (Mac legacy file, Windows none). */
    assert(parse(nullptr,legacy)==parse("[Quality]\nPreset=Quality\n",legacy)&&parse(nullptr,nullptr)==d);
    s=parse(nullptr,legacy);assert(s.origin[0]=='l'&&s.origin[1]=='l'&&s.origin[2]=='l'&&s.origin[3]=='d');
    assert(describe(s).find("MinSkinnedTriangles=50(legacy)")!=std::string::npos&&describe(s).find("FarShadowInterval=4(default)")!=std::string::npos);
    /* Balanced/Performance: preset beats the legacy file, identically on Mac and Windows. */
    problems.clear();s=parse("[Quality]\nPreset=Performance\n",legacy,problems);
    assert(s==preset(Preset::Performance)&&s.minSkinnedTriangles==180&&s.actorShadowBudgetMiB==8&&s.captureBudgetMiB==32&&problems.size()==1);
    assert(parse("[Quality]\nPreset=Balanced\n",legacy)==parse("[Quality]\nPreset=Balanced\n")&&s.origin[2]=='p');
    /* Explicit keys beat both. */
    s=parse("[Quality]\nActorShadowBudgetMiB=4\n",legacy);assert(s.actorShadowBudgetMiB==4&&s.minSkinnedTriangles==50&&s.origin[2]=='f'&&s.origin[0]=='l');
    s=parse("[Quality]\nPreset=Performance\nActorShadowBudgetMiB=0\n",legacy);assert(s.actorShadowBudgetMiB==0&&s.minSkinnedTriangles==180&&s.origin[2]=='f');
    /* 0.3.141 diagnostics keys (0.3.169: Diagnostics defaults to 0 in every preset): default off/off, Diagnostics=0 forces the fate tracker off, invalid values kept. */
    assert(d.diagnostics==0&&d.shadowFateDiagnostics==0&&!shadowFate(d));
    s=parse("[Quality]\nShadowFateDiagnostics=1\n");assert(!shadowFate(s)&&describe(s).find("shadowFateEffective=0")!=std::string::npos);
    s=parse("[Quality]\nShadowFateDiagnostics=1\nDiagnostics=1\n");assert(shadowFate(s)&&describe(s).find("shadowFateEffective=1")!=std::string::npos);
    s=parse("[Quality]\nShadowFateDiagnostics=1\nDiagnostics=0\n");assert(s.diagnostics==0&&!shadowFate(s)&&describe(s).find("Diagnostics=0(file)")!=std::string::npos&&describe(s).find("shadowFateEffective=0")!=std::string::npos);
    for(const char* p:{"[Quality]\nPreset=Balanced\n","[Quality]\nPreset=Performance\n"})assert(parse(p).diagnostics==0&&!shadowFate(parse(p)));
    problems.clear();s=parse("[Quality]\nDiagnostics=2\nShadowFateDiagnostics=yes\n",nullptr,problems);assert(s==d&&problems.size()==2);
    /* ActorShadowRadius (yards): 40 / 35 / 20 (0.3.167); 0 = no limit; 0..200; not a legacy key. */
    assert(d.actorShadowRadius==40&&preset(Preset::Balanced).actorShadowRadius==35&&preset(Preset::Performance).actorShadowRadius==20);
    assert(parse("[Quality]\nActorShadowRadius=0\n").actorShadowRadius==0);
    s=parse("[Quality]\nPreset=Performance\nActorShadowRadius=40\n");assert(s.actorShadowRadius==40&&s.origin[21]=='f'&&std::string(Keys[21].name)=="ActorShadowRadius"&&describe(s).find("ActorShadowRadius=40(file)")!=std::string::npos);
    assert(describe(d).find("ActorShadowRadius=40(default)")!=std::string::npos&&parse("[Quality]\nActorShadowRadius=1\n").actorShadowRadius==1);
    problems.clear();s=parse("[Quality]\nActorShadowRadius=201\n",nullptr,problems);assert(s==d&&problems.size()==1);
    problems.clear();s=parse(nullptr,"[ShadowExperiment]\nActorShadowRadius=30\n",problems);assert(s==d&&problems.size()==1);
    /* Notepad UTF-16LE with BOM. */
    {std::string text="\xFF\xFE";for(char c:std::string("[Quality]\r\nPreset=Balanced\r\n")){text.push_back(c);text.push_back('\0');}
     std::istringstream in(narrow(text));std::vector<std::string> pr;assert(load(&in,nullptr,pr)==preset(Preset::Balanced)&&pr.empty());
     assert(narrow("[Quality]")=="[Quality]");}
    problems.clear();s=parse(nullptr,"[ShadowExperiment]\nPreset=Performance\nFarShadowInterval=3\n",problems);assert(s==d&&problems.size()==2);
    /* Knob code paths at their default are bit-identical to 0.3.136. */
    std::mt19937 rng(1377);std::uniform_real_distribution<float> u(-1,1),pos(-400,400),reach(.05f,40);
    for(int i=0;i<200000;++i){NorthlightGI::Vec3 v=NorthlightGI::normalized(NorthlightGI::Vec3(u(rng),u(rng),u(rng)));
        auto a=oldQuantize(v),b=NorthlightWorldMath::quantizeDirection(v,float(d.shadowDirectionSteps)),c=NorthlightWorldMath::quantizeDirection(v);
        assert(!std::memcmp(&a,&b,sizeof a)&&!std::memcmp(&a,&c,sizeof a));}
    for(int round=0;round<300;++round){
        std::vector<NorthlightLocalLights::Light> lights(size_t(rng()%90));uint64_t id=1;
        for(auto& l:lights){l={};for(int k=0;k<3;++k){l.position[k]=pos(rng);l.diffuse[k]=u(rng)+1;}l.attenuationStart=reach(rng)*.5f;l.attenuationEnd=reach(rng);l.sourceId=(rng()%4)?id++:1;l.kind=2;}
        const float camera[]={pos(rng)*.2f,pos(rng)*.2f,pos(rng)*.05f};
        auto a=NorthlightLocalLightSelection::select(lights,camera),b=NorthlightLocalLightSelection::select(lights,camera,d.localLightLimit);
        assert(a.count==b.count&&a.nearest==b.nearest&&a.fogDistance==b.fogDistance&&a.position==b.position&&a.color==b.color&&a.fog==b.fog);
        /* Default 32 is byte-identical to 0.3.139 (capacity 32), including every direct/fog batch. */
        sameAsOld(a,oldSelect(lights,camera));sameAsOld(b,oldSelect(lights,camera));
        for(unsigned limit:{8u,16u,24u})sameAsOld(NorthlightLocalLightSelection::select(lights,camera,limit),oldSelect(lights,camera,limit));
        /* 33..64: the closest-first extension of the default selection. */
        const auto all=NorthlightLocalLightSelection::select(lights,camera,64);
        for(unsigned limit:{33u,40u,48u,64u,65u,1000u}){auto r=NorthlightLocalLightSelection::select(lights,camera,limit);
            assert(r.count==std::min(all.count,std::min(limit,64u)));for(unsigned k=0;k<r.count;++k)assert(r.position[k]==all.position[k]&&r.color[k]==all.color[k]);}
        for(unsigned k=0;k<a.count;++k)assert(all.position[k]==a.position[k]&&all.color[k]==a.color[k]);
        for(unsigned limit:{8u,16u,24u}){auto r=NorthlightLocalLightSelection::select(lights,camera,limit);
            /* A lower limit keeps exactly the closest `limit` of the full selection, in order. */
            assert(r.count==std::min(a.count,limit));
            for(unsigned k=0;k<r.count;++k)assert(r.position[k]==a.position[k]&&r.color[k]==a.color[k]);}
    }
    { /* 64 visible lamps (the log shows 56 available in cities): 8 direct and 16 fog batches, all full. */
      std::vector<NorthlightLocalLights::Light> lamps(100);uint64_t id=1;
      for(auto& l:lamps){l={};l.position[0]=float(id%10)*6;l.position[1]=float(id/10)*6;l.position[2]=2;l.diffuse[0]=l.diffuse[1]=l.diffuse[2]=1;l.attenuationStart=2;l.attenuationEnd=8;l.sourceId=id++;l.kind=2;}
      const float camera[]={27,27,5};using namespace NorthlightLocalLightSelection;
      auto s=select(lamps,camera,64),def=select(lamps,camera);assert(s.count==64&&def.count==32);
      unsigned direct=0,fog=0;
      for(unsigned first=0;first<s.count;first+=DirectBatchSize){auto b=s.batch<DirectBatchSize>(first);assert(b.count==8);++direct;}
      for(unsigned first=0;first<s.count;first+=FogBatchSize){auto b=s.batch<FogBatchSize>(first);assert(b.count==4);++fog;}
      assert(direct==8&&fog==16);
      for(unsigned k=0;k<32;++k)assert(s.position[k]==def.position[k]&&s.color[k]==def.color[k]);
      float worst=-1e9f;for(unsigned k=0;k<64;++k){const float dx=s.position[k][0]-camera[0],dy=s.position[k][1]-camera[1],dz=s.position[k][2]-camera[2];const float r=std::sqrt(dx*dx+dy*dy+dz*dz)-s.position[k][3];assert(r>=worst-1e-4f);worst=std::max(worst,r);}
      assert(s.fogDistance>=def.fogDistance);}
    { NorthlightLocalLights::Light light={{-9533,40,71},{1,.8f,.4f},1,12,123,1,1};NorthlightPointShadow::RefreshSchedule sch;
      sch.commit(1000,light,7);
      for(uint32_t dt=0;dt<120;++dt)for(size_t replays:{0u,255u,256u,900u}){
          assert(sch.due(1000+dt,light,7,replays,false)==oldDue(sch,1000+dt,replays));
          assert(sch.due(1000+dt,light,7,replays,false,d.pointShadowRefreshMs)==oldDue(sch,1000+dt,replays));
          assert(sch.due(1000+dt,light,7,replays,false,33)==(dt>=33));
          assert(sch.due(1000+dt,light,8,replays,false,33)&&sch.due(1000+dt,light,7,replays,true,33));}
    }
    { /* Far-cascade reuse: interval 1 never skips or records; a failed render is never reused. */
      const float m1[16]={1,2,3},m2[16]={4,5,6};FarShadowReuse r;
      for(unsigned pass=1;pass<50;++pass){assert(!r.canSkip(1,pass,true,false));r.begin(1);r.commit(1,pass,m1);assert(!r.valid);}
      r.begin(3);r.commit(3,10,m1);assert(r.valid&&r.matrix[0]==1);
      assert(r.canSkip(3,11,true,false)&&r.canSkip(3,12,true,false)&&!r.canSkip(3,13,true,false)); /* at most N-1 reuses */
      assert(!r.canSkip(3,11,false,false)&&!r.canSkip(3,11,true,true)&&!r.canSkip(1,11,true,false));
      r.begin(3); /* render starts writing, then fails before commit */
      for(unsigned pass=13;pass<20;++pass)assert(!r.canSkip(3,pass,true,false));
      r.commit(3,20,m2);assert(r.canSkip(3,21,true,false)&&r.matrix[0]==4);
      r.invalidate();assert(!r.canSkip(3,21,true,false));
      r.commit(2,UINT32_MAX,m1);assert(r.canSkip(2,0,true,false)&&!r.canSkip(2,1,true,false)); /* pass counter wrap */ }
    { /* FarShadowInterval 16 (new maximum): a render at least every 16 passes, every cache re-render
         (reason) forces one, a failed render is never reused, across the pass-counter wrap. */
      for(unsigned interval:{4u,15u,16u})for(unsigned start:{1u,UINT32_MAX-200u}){
        FarShadowReuse r;unsigned since=0,renders=0,skips=0,maxRun=0;float m[16]={};
        for(unsigned i=0;i<2000;++i){const unsigned pass=start+i;const bool reusable=i%97!=50,fails=i%211==7;
            if(r.canSkip(interval,pass,reusable,false)){assert(reusable&&r.valid&&since<interval-1);++since;++skips;maxRun=std::max(maxRun,since);
                assert(r.matrix[0]==float(pass-since));continue;}
            r.begin(interval);since=0;
            if(fails){assert(!r.valid);continue;} /* the next pass must render again */
            m[0]=float(pass);r.commit(interval,pass,m);++renders;}
        assert(maxRun==interval-1&&renders>=2000/interval&&skips>0);}
      FarShadowReuse r;const float m[16]={7};r.commit(16,5,m);assert(r.canSkip(16,20,true,false)&&!r.canSkip(16,21,true,false));
      }
    { /* NearShadowInterval: 1 on Quality (the old path), 2 on Balanced and Performance (0.3.169). */
      assert(d.nearShadowInterval==1&&preset(Preset::Balanced).nearShadowInterval==2&&preset(Preset::Performance).nearShadowInterval==2);
      assert(parse("[Quality]\nNearShadowInterval=16\n").nearShadowInterval==16&&parse("[Quality]\nNearShadowInterval=0\n")==d);
      /* Near reuse is the same complete-map contract as far: interval 1 never skips or records. */
      ShadowMapReuse r;const float m[16]={3};
      for(unsigned pass=1;pass<50;++pass){assert(!r.canSkip(d.nearShadowInterval,pass,true,false));r.begin(1);r.commit(1,pass,m);assert(!r.valid&&!r.fresh(1,pass+1));}
      r.begin(2);r.commit(2,9,m);assert(r.fresh(2,10)&&!r.fresh(2,11)&&r.canSkip(2,10,true,false)&&!r.canSkip(2,10,false,false)&&!r.canSkip(2,10,true,true));
      /* Actions: the default path never reuses or defers; a fresh frame never defers. */
      for(unsigned pass=1;pass<40;++pass)for(int bits=0;bits<8;++bits){ShadowMapReuse x;if(bits&4){x.commit(3,pass-1,m);}
          assert(cascadeAction(x,1,pass,bits&1,bits&2,true)==CascadeAction::Render&&cascadeAction(x,1,pass,bits&1,bits&2,false)==CascadeAction::Render);
          assert(cascadeAction(x,3,pass,bits&1,bits&2,true)!=CascadeAction::Defer);}
    }
    { /* Capture-skip decision table. Shadows on with any interval 1 (Quality: near 1): never skip. */
      ShadowMapReuse nearMaps[2],farMaps[2];const float m[16]={1};
      for(int i=0;i<2;++i){nearMaps[i].commit(16,10,m);farMaps[i].commit(16,10,m);}
      for(unsigned bits=0;bits<512;++bits){CaptureInputs in;in.shadows=true;in.actorDue=bits&1;in.demand=bits&2;in.diagnostic=bits&4;in.pointDue=bits&8;
          in.sourceActive[0]=bits&16;in.sourceActive[1]=bits&32;in.nextPass=11+(bits>>6);
          assert(!skipModelCapture(preset(Preset::Quality),in,nearMaps,farMaps)&&!captureSkipPossible(preset(Preset::Quality),true));
          for(Preset p:{Preset::Balanced,Preset::Performance})assert(captureSkipPossible(preset(p),true));
          Settings one=d;one.farShadowInterval=16;assert(!skipModelCapture(one,in,nearMaps,farMaps));
          one=d;one.farShadowInterval=1;one.nearShadowInterval=16;assert(!skipModelCapture(one,in,nearMaps,farMaps));
          /* Both 16: every active source must reuse both maps at the next pass; GI, demand, diagnostics and a due lamp refresh capture. */
          Settings both=d;both.nearShadowInterval=both.farShadowInterval=16;
          assert(skipModelCapture(both,in,nearMaps,farMaps)==!(in.actorDue||in.demand||in.diagnostic||in.pointDue||in.nextPass-10>=16));
          ShadowMapReuse stale[2];stale[0]=nearMaps[0];stale[1]=nearMaps[1];stale[bits&1].invalidate();
          if(in.sourceActive[bits&1])assert(!skipModelCapture(both,in,stale,farMaps)&&!skipModelCapture(both,in,nearMaps,stale));
          /* Shadows off (F9, any preset): capture only for GI actor packets, a demand or a diagnostic. */
          in.shadows=false;
          for(Preset p:{Preset::Quality,Preset::Balanced,Preset::Performance})assert(captureSkipPossible(preset(p),false)&&skipModelCapture(preset(p),in,nearMaps,farMaps)==!(in.actorDue||in.demand||in.diagnostic));}
    }
    { /* Frame protocol model: the render-side use of the decision (world_renderer.h render(),
         world_point_rendering.inl renderPointShadow) over random scenes. Proves: default
         settings capture every frame with the old far schedule; no map or cube is committed
         from a frame without replays; every reused/deferred map was committed on a capture
         frame at most interval (+1 deferred) passes ago; a deferral always makes the next
         frame capture; GI actor frames always capture; a frame without replays draws a
         cascade/cube without replays only after an in-frame invalidation (reset/resize). */
      struct Map {ShadowMapReuse reuse;bool fromFresh=false;};
      struct Stats {unsigned frames=0,skipped=0,renders=0,reuses=0,defers=0,pointDefers=0,pointBare=0,bare=0,pulls=0;};
      auto run=[&](const Settings& q,unsigned seed,bool inFrameInvalidation,Stats& st){
        std::mt19937 g(seed);Map maps[2][2];bool demand=false,shadows=true,lastDeferred=false;unsigned passes=0;uint32_t now=1000,lastActor=0;
        NorthlightPointShadow::RefreshSchedule point;NorthlightLocalLights::Light lamp={{0,0,0},{1,1,1},1,10,1,1,1};bool lampOn=false;size_t replayCount=300;
        bool active[2]={true,false};
        for(unsigned frame=0;frame<20000;++frame){now+=16+g()%3;++st.frames;
            if(g()%500==0){shadows=!shadows;for(auto& s:maps)for(auto& m:s)m.reuse.invalidate();} /* setEffects: invalidateShadowCache */
            if(g()%300==0)for(auto& s:maps)for(auto& m:s)m.reuse.invalidate();                  /* device reset between frames */
            if(g()%200==0)active[1]=!active[1];
            if(g()%150==0)lampOn=!lampOn;
            if(g()%400==0){lamp.sourceId=1+g()%3;point.invalidate();}
            if(g()%50==0)replayCount=g()%2?100:300;
            /* decision at the frame's first model draw */
            const bool actorDue=!lastActor||now-lastActor>=200;
            CaptureInputs in;in.shadows=shadows;in.demand=demand;in.actorDue=actorDue;in.diagnostic=false;in.nextPass=passes+1;
            in.sourceActive[0]=active[0];in.sourceActive[1]=active[1];
            in.pointDue=shadows&&lampOn&&point.due(now,lamp,1,replayCount,false,q.pointShadowRefreshMs);
            ShadowMapReuse nearR[2]={maps[0][0].reuse,maps[1][0].reuse},farR[2]={maps[0][1].reuse,maps[1][1].reuse};
            const bool skip=captureSkipPossible(q,shadows)&&skipModelCapture(q,in,nearR,farR),fresh=!skip;
            if(fresh)demand=false;else ++st.skipped;
            if(lastDeferred)assert(fresh); /* a deferral is followed by a capture frame */
            if(actorDue)assert(fresh);
            if(fresh&&actorDue)lastActor=now;
            if(q==Settings{}&&shadows)assert(fresh);
            lastDeferred=false;
            if(inFrameInvalidation&&g()%700==0)for(auto& s:maps)for(auto& m:s)m.reuse.invalidate(); /* resize inside render() */
            if(!shadows)continue;
            ++passes;const uint32_t renderAt=now+g()%12; /* render runs later in the frame */
            for(int source=0;source<2;++source){if(!active[source])continue;bool nearRendered=false;
                for(int cascade=0;cascade<2;++cascade){auto& map=maps[source][cascade];const unsigned interval=cascade?q.farShadowInterval:q.nearShadowInterval;
                    const bool pull=cascade==1&&pullFar(q.nearShadowInterval,interval,passes,map.reuse,nearRendered);
                    if(pull){assert(fresh&&q.nearShadowInterval>1);++st.pulls;}
                    const bool reusable=g()%20!=0;const auto action=cascadeAction(map.reuse,interval,passes,reusable&&!pull,false,fresh);
                    if(q==Settings{}){ /* old 0.3.140 far logic, near always rendered (Quality: near 1) */
                        const bool oldSkip=cascade==1&&interval>1&&reusable&&map.reuse.valid&&passes-map.reuse.pass<interval;
                        assert((action==CascadeAction::Reuse)==oldSkip&&action!=CascadeAction::Defer);}
                    if(action!=CascadeAction::Render){assert(map.reuse.valid&&map.fromFresh);
                        if(action==CascadeAction::Reuse){assert(passes-map.reuse.pass<interval);++st.reuses;}
                        else{assert(!fresh&&passes-map.reuse.pass<=interval);demand=true;lastDeferred=true;++st.defers;}
                        continue;}
                    ++st.renders;map.reuse.begin(interval);if(cascade==0)nearRendered=fresh&&interval>1;
                    if(g()%400==0)continue; /* device failure part-way: never committed */
                    const float m[16]={float(passes)};
                    if(fresh){map.reuse.commit(interval,passes,m);map.fromFresh=true;}
                    else{assert(inFrameInvalidation);++st.bare;demand=true;lastDeferred=true;}}}
            if(lampOn){const bool rebuild=false;
                if(!point.due(renderAt,lamp,1,replayCount,rebuild,q.pointShadowRefreshMs))continue;
                if(!fresh&&point.complete){demand=true;lastDeferred=true;++st.pointDefers;continue;}
                point.invalidate();
                if(!fresh){++st.pointBare;demand=true;lastDeferred=true;continue;} /* drawn without replays, not committed */
                point.commit(renderAt,lamp,1);}
        }
      };
      /* 0.3.158 ActorShadows: the same protocol as render() now wires it. The capture decision sees
         shadows only with ActorShadows=1 (actorShadowWork), the point schedule counts no replays with
         0, and schedules/commits take complete=fresh||!ActorShadows. gi=false: no GI actor capture
         (GIDynamicProbes=0). With ActorShadows=1 and gi it reduces to run() above. */
      auto runActor=[&](const Settings& q,unsigned seed,bool inFrameInvalidation,bool gi,Stats& st){
        const bool actor=q.actorShadows!=0;
        std::mt19937 g(seed);Map maps[2][2];bool demand=false,shadows=true,lastDeferred=false;unsigned passes=0;uint32_t now=1000,lastActor=0;
        NorthlightPointShadow::RefreshSchedule point;NorthlightLocalLights::Light lamp={{0,0,0},{1,1,1},1,10,1,1,1};bool lampOn=false;size_t replayCount=300;
        bool active[2]={true,false};
        for(unsigned frame=0;frame<20000;++frame){now+=16+g()%3;++st.frames;
            if(g()%500==0){shadows=!shadows;for(auto& s:maps)for(auto& m:s)m.reuse.invalidate();}
            if(g()%300==0)for(auto& s:maps)for(auto& m:s)m.reuse.invalidate();
            if(g()%200==0)active[1]=!active[1];
            if(g()%150==0)lampOn=!lampOn;
            if(g()%400==0){lamp.sourceId=1+g()%3;point.invalidate();}
            if(g()%50==0)replayCount=g()%2?100:300;
            const size_t pointReplays=actor?replayCount:0; /* renderPointShadow(fresh,withReplays=false): pointReplayCount=0 */
            const bool actorDue=gi&&(!lastActor||now-lastActor>=200),work=actorShadowWork(q,shadows);
            CaptureInputs in;in.shadows=work;in.demand=demand;in.actorDue=actorDue;in.diagnostic=false;in.nextPass=passes+1;
            in.sourceActive[0]=active[0];in.sourceActive[1]=active[1];
            in.pointDue=work&&lampOn&&point.due(now,lamp,1,pointReplays,false,q.pointShadowRefreshMs);
            ShadowMapReuse nearR[2]={maps[0][0].reuse,maps[1][0].reuse},farR[2]={maps[0][1].reuse,maps[1][1].reuse};
            const bool skip=captureSkipPossible(q,work)&&skipModelCapture(q,in,nearR,farR),fresh=!skip,complete=fresh||!actor;
            if(!actor)assert(!demand&&fresh==actorDue); /* ActorShadows=0: GI capture frames only, never a demand */
            if(fresh)demand=false;else ++st.skipped;
            if(lastDeferred)assert(fresh);
            if(actorDue)assert(fresh);
            if(fresh&&actorDue)lastActor=now;
            if(q==Settings{}&&shadows)assert(fresh);
            lastDeferred=false;
            if(inFrameInvalidation&&g()%700==0)for(auto& s:maps)for(auto& m:s)m.reuse.invalidate();
            if(!shadows)continue;
            ++passes;const uint32_t renderAt=now+g()%12;
            for(int source=0;source<2;++source){if(!active[source])continue;bool nearRendered=false;
                for(int cascade=0;cascade<2;++cascade){auto& map=maps[source][cascade];const unsigned interval=cascade?q.farShadowInterval:q.nearShadowInterval;
                    const bool pull=cascade==1&&pullFar(q.nearShadowInterval,interval,passes,map.reuse,nearRendered);
                    if(pull){assert(fresh&&q.nearShadowInterval>1&&actor);++st.pulls;}
                    const bool reusable=g()%20!=0;const auto action=cascadeAction(map.reuse,interval,passes,reusable&&!pull,false,complete);
                    if(q==Settings{}){
                        const bool oldSkip=cascade==1&&interval>1&&reusable&&map.reuse.valid&&passes-map.reuse.pass<interval;
                        assert((action==CascadeAction::Reuse)==oldSkip&&action!=CascadeAction::Defer);}
                    if(action!=CascadeAction::Render){assert(map.reuse.valid&&map.fromFresh);
                        if(action==CascadeAction::Reuse){assert(passes-map.reuse.pass<interval);++st.reuses;}
                        else{assert(!complete&&passes-map.reuse.pass<=interval);demand=true;lastDeferred=true;++st.defers;}
                        continue;}
                    ++st.renders;map.reuse.begin(interval);if(cascade==0)nearRendered=fresh&&interval>1&&actor;
                    if(g()%400==0)continue;
                    const float m[16]={float(passes)};
                    if(complete){map.reuse.commit(interval,passes,m);map.fromFresh=true;}
                    else{assert(inFrameInvalidation);++st.bare;demand=true;lastDeferred=true;}}}
            if(lampOn){const bool rebuild=false;
                if(!point.due(renderAt,lamp,1,pointReplays,rebuild,q.pointShadowRefreshMs))continue;
                if(!complete&&point.complete){demand=true;lastDeferred=true;++st.pointDefers;continue;}
                point.invalidate();
                if(!complete){++st.pointBare;demand=true;lastDeferred=true;continue;}
                point.commit(renderAt,lamp,1);}
        }
      };
      auto same=[](const Stats& a,const Stats& b){return a.frames==b.frames&&a.skipped==b.skipped&&a.renders==b.renders&&a.reuses==b.reuses&&a.defers==b.defers&&
          a.pointDefers==b.pointDefers&&a.pointBare==b.pointBare&&a.bare==b.bare&&a.pulls==b.pulls;};
      for(unsigned seed=1;seed<=24;++seed){
        Stats q,b,p,big,off;run(d,seed,false,q);run(preset(Preset::Balanced),seed,false,b);run(preset(Preset::Performance),seed,false,p);
        Settings s=preset(Preset::Performance);s.nearShadowInterval=2;s.farShadowInterval=8;run(s,seed,false,big);
        run(preset(Preset::Performance),seed,true,off);
        assert(q.skipped>0&&q.bare==0&&b.bare==0&&q.pulls==0&&p.pulls>0); /* Quality skips only shadows-off frames (asserted per frame above) */
        assert(b.skipped>q.skipped&&b.pointBare==0); /* Balanced (0.3.169): near 2 / far 5 skips capture like Performance */
        assert(p.skipped>0&&big.skipped>0&&p.bare==0&&big.bare==0&&p.pointBare==0&&big.pointBare==0);
        /* ActorShadows=1 (every preset, big intervals, in-frame invalidation): exactly the run above. */
        {Settings s=preset(Preset::Performance);s.nearShadowInterval=2;s.farShadowInterval=8;
         const std::pair<Settings,bool> cases[]={{d,false},{preset(Preset::Balanced),false},{preset(Preset::Performance),false},{s,false},{preset(Preset::Performance),true}};
         const Stats* old[]={&q,&b,&p,&big,&off};unsigned i=0;
         for(const auto& c:cases){assert(c.first.actorShadows==1);Stats a;runActor(c.first,seed,c.second,true,a);assert(same(a,*old[i]));++i;}}
        /* ActorShadows=0: capture only on GI frames (none without GI), no deferral, demand, pull or bare map/cube. */
        for(Preset preset_:{Preset::Quality,Preset::Balanced,Preset::Performance})for(bool inFrame:{false,true})for(bool gi:{true,false}){
            Settings s=preset(preset_);s.actorShadows=0;Stats a;runActor(s,seed,inFrame,gi,a);
            assert(a.defers==0&&a.pointDefers==0&&a.bare==0&&a.pointBare==0&&a.pulls==0&&a.renders>0);
            if(gi)assert(a.skipped>0&&a.skipped<a.frames&&(a.frames-a.skipped)*10<a.frames&&(a.frames-a.skipped)*14>a.frames); /* one capture per ~200 ms (~12 frames) */
            else assert(a.skipped==a.frames);
            if(seed==1&&!inFrame&&preset_==Preset::Performance&&gi)std::printf("capture-skip model: ActorShadows=0 performance captured=%u/%u (GI only) renders=%u reuses=%u\n",a.frames-a.skipped,a.frames,a.renders,a.reuses);}
        if(seed==1)std::printf("capture-skip model: quality skipped=%u/%u performance skipped=%u reuses=%u defers=%u pointDefers=%u pointBare=%u pulls=%u near2/far8 skipped=%u defers=%u pulls=%u inFrameInvalidation bare=%u\n",
            q.skipped,q.frames,p.skipped,p.reuses,p.defers,p.pointDefers,p.pointBare,p.pulls,big.skipped,big.defers,big.pulls,off.bare);}
    }
    assert(bigEndianUtf16("\xFE\xFF")&&!bigEndianUtf16("\xFF\xFE")&&!bigEndianUtf16("["));
    // 0.3.172: PersistentCasters and PersistentRigidProps are retired (0 was the default in every preset; rigid_memory.h
    // replaces the rigid props). Any value, any case: the line is ignored with exactly one "retired" note (not "unknown"),
    // the settings equal the defaults, no key or slot is left.
    for(const char* key:{"PersistentCasters","PersistentRigidProps"}){const std::string k=key;
        for(const std::string& line:{k+"=1",k+"=0",lower(k)+"=7",k+"=abc"}){std::vector<std::string> p;
            const auto s=parse(("[Quality]\n"+line+"\n").c_str(),nullptr,p);
            assert(s==d&&!std::memcmp(s.origin,d.origin,sizeof d.origin)&&p.size()==1&&p[0]=="northlight-quality.ini line 2: "+k+" retired in 0.3.172 (ignored)");}
        std::vector<std::string> p;assert(parse(("[Quality]\nPreset=Performance\n"+k+"=1\n").c_str(),nullptr,p)==preset(Preset::Performance)&&p.size()==1);
        for(const auto& x:Keys)assert(std::string(x.name)!=k);for(const auto& x:ActorShadowForced)assert(std::string(x.name)!=k);
        assert(describe(d).find(k)==std::string::npos);}
    {std::vector<std::string> p;
        p.clear();parse("[Quality]\nNoSuchKey=1\n",nullptr,p);assert(p.size()==1&&p[0].find("unknown key NoSuchKey ignored")!=std::string::npos);}
    // 0.3.151 spike spreading: 6 faces / 1 slice (the 0.3.150 paths) in the code default and every preset, own origin slots.
    assert(sizeof(Settings::origin)==sizeof(Keys)/sizeof(Keys[0])&&d.pointShadowFacesPerFrame==6&&d.staticCacheSlices==1);
    for(auto p:{Preset::Balanced,Preset::Performance})assert(preset(p).pointShadowFacesPerFrame==6&&preset(p).staticCacheSlices==1);
    {auto on=parse("[Quality]\nPointShadowFacesPerFrame=2\nStaticCacheSlices=4\n");assert(on.pointShadowFacesPerFrame==2&&on.staticCacheSlices==4&&on!=d);
        unsigned i=0;for(const auto& k:Keys){const std::string n=k.name;assert(on.origin[i]==(n=="PointShadowFacesPerFrame"||n=="StaticCacheSlices"?'f':'d'));++i;}
        assert(parse("[Quality]\nPointShadowFacesPerFrame=1\nStaticCacheSlices=1\n").pointShadowFacesPerFrame==1&&parse("[Quality]\nPointShadowFacesPerFrame=6\nStaticCacheSlices=1\n")==d);
        std::vector<std::string> p;auto bad=parse("[Quality]\nPointShadowFacesPerFrame=0\nPointShadowFacesPerFrame=7\nStaticCacheSlices=0\nStaticCacheSlices=5\n",nullptr,p);assert(bad==d&&p.size()==4);
        p.clear();assert(parse(nullptr,"[ShadowExperiment]\nStaticCacheSlices=2\n",p)==d&&p.size()==1);
        assert(describe(d).find(" PointShadowFacesPerFrame=6(default) StaticCacheSlices=1(default)")!=std::string::npos);}
    // 0.3.167 GIDistance: 76 (20-cell window) in the code default, 52 (14-cell window, the 0.3.151
    // layout) in Balanced and Performance, own origin slot; yards snap down to d=4N-4 with N even, 36..84 -> 10..22.
    assert(d.giDistance==76&&giProbeGrid(d)==20&&preset(Preset::Performance).giDistance==52&&preset(Preset::Balanced).giDistance==52);
    assert(giProbeGrid(parse("[Quality]\nGIDistance=52\n"))==14&&giProbeGrid(preset(Preset::Balanced))==14&&giProbeGrid(preset(Preset::Performance))==14);
    for(unsigned y=36;y<=84;++y){auto g=parse(("[Quality]\nGIDistance="+std::to_string(y)+"\n").c_str());const unsigned n=giProbeGrid(g);
        assert(g.giDistance==y&&n%2==0&&n>=10&&n<=22&&4*n-4<=y&&y<4*n-4+8&&(y==76)==(g==d));}
    assert(giProbeGrid(parse("[Quality]\nGIDistance=60\n"))==16&&giProbeGrid(parse("[Quality]\nGIDistance=84\n"))==22&&giProbeGrid(parse("[Quality]\nGIDistance=43\n"))==10);
    {auto on=parse("[Quality]\nGIDistance=68\n");unsigned i=0;for(const auto& k:Keys){assert(on.origin[i]==(std::string(k.name)=="GIDistance"?'f':'d'));++i;}
        std::vector<std::string> p;assert(parse("[Quality]\nGIDistance=35\nGIDistance=85\n",nullptr,p)==d&&p.size()==2);
        p.clear();assert(parse(nullptr,"[ShadowExperiment]\nGIDistance=60\n",p)==d&&p.size()==1);
        assert(describe(d).find(" GIDistance=76(default)")!=std::string::npos&&describe(on).find(" GIDistance=68(file)")!=std::string::npos);}
    // 0.3.153 GIProbeAhead: 0 (window on the eye) in the code default and every preset, 0..48, own origin slot.
    assert(d.giProbeAhead==0&&preset(Preset::Balanced).giProbeAhead==0&&preset(Preset::Performance).giProbeAhead==0);
    {auto on=parse("[Quality]\nGIProbeAhead=20\n");assert(on.giProbeAhead==20&&on!=d&&parse("[Quality]\nGIProbeAhead=0\n")==d);
        unsigned i=0;for(const auto& k:Keys){assert(on.origin[i]==(std::string(k.name)=="GIProbeAhead"?'f':'d'));++i;}
        std::vector<std::string> p;assert(parse("[Quality]\nGIProbeAhead=49\n",nullptr,p)==d&&p.size()==1);
        assert(describe(d).find(" GIDistance=76(default) GIProbeAhead=0(default)")!=std::string::npos&&describe(on).find(" GIProbeAhead=20(file)")!=std::string::npos);}
    // Horizon haze (picture only): 50/75/6/1 in the code default and every preset, own origin slots;
    // HorizonHaze=0 is the 0.3.153 image (horizon_haze.h uploads optical depth 0).
    assert(d.horizonHaze==50&&d.horizonHazeStart==75&&d.horizonHazeBand==6&&d.horizonHazeTerrain==1);
    for(auto p:{Preset::Balanced,Preset::Performance}){const auto q=preset(p);
        assert(q.horizonHaze==50&&q.horizonHazeStart==75&&q.horizonHazeBand==6&&q.horizonHazeTerrain==1);}
    {auto on=parse("[Quality]\nHorizonHaze=0\nHorizonHazeStart=90\nHorizonHazeBand=3\nHorizonHazeTerrain=0\n");
        assert(on.horizonHaze==0&&on.horizonHazeStart==90&&on.horizonHazeBand==3&&on.horizonHazeTerrain==0&&on!=d);
        unsigned i=0;for(const auto& k:Keys){const std::string n=k.name;assert(on.origin[i]==(n.rfind("HorizonHaze",0)==0?'f':'d'));++i;}
        assert(parse("[Quality]\nHorizonHaze=50\nHorizonHazeStart=75\nHorizonHazeBand=6\nHorizonHazeTerrain=1\n")==d);
        std::vector<std::string> p;
        assert(parse("[Quality]\nHorizonHaze=101\nHorizonHazeStart=49\nHorizonHazeStart=96\nHorizonHazeBand=1\nHorizonHazeBand=16\nHorizonHazeTerrain=2\n",nullptr,p)==d&&p.size()==6);
        p.clear();assert(parse(nullptr,"[ShadowExperiment]\nHorizonHaze=0\n",p)==d&&p.size()==1);
        assert(describe(d).find(" GIProbeAhead=0(default) HorizonHaze=50(default) HorizonHazeStart=75(default) HorizonHazeBand=6(default) HorizonHazeTerrain=1(default)")!=std::string::npos);}
    // 0.3.158 ActorShadows: 1 (actor and static shadows, the 0.3.157 paths) in the code default and every
    // preset, 0..1, own last origin slot; 0 forces the replay-derived keys off (effective()).
    assert(d.actorShadows==1&&preset(Preset::Balanced).actorShadows==1&&preset(Preset::Performance).actorShadows==1);
    assert(std::string(Keys[30].name)=="ActorShadows"&&Keys[30].field==&Settings::actorShadows&&Keys[30].low==0&&Keys[30].high==1);
    {auto off=parse("[Quality]\nActorShadows=0\n");assert(off.actorShadows==0&&off!=d&&parse("[Quality]\nActorShadows=1\n")==d);
        unsigned i=0;for(const auto& k:Keys){assert(off.origin[i]==(std::string(k.name)=="ActorShadows"?'f':'d'));++i;}
        assert(parse("[Quality]\nPreset=Performance\nActorShadows=0\n").actorShadows==0&&parse("[Quality]\nPreset=Performance\nActorShadows=0\n").origin[30]=='f');
        std::vector<std::string> p;assert(parse("[Quality]\nActorShadows=2\n",nullptr,p)==d&&p.size()==1);
        p.clear();assert(parse(nullptr,"[ShadowExperiment]\nActorShadows=0\n",p)==d&&p.size()==1);
        assert(describe(d).find(" HorizonHazeTerrain=1(default) ActorShadows=1(default)")!=std::string::npos&&describe(off).find(" ActorShadows=0(file)")!=std::string::npos);}
    // 0.3.187 FrameDrawGates: 1 (the per-frame draw gates) in the code default and every preset, 0..1, own
    // last origin slot; 0 restores the 0.3.184 per-draw hook work. ActorShadows=0 does not force it.
    assert(d.frameDrawGates==1&&preset(Preset::Balanced).frameDrawGates==1&&preset(Preset::Performance).frameDrawGates==1);
    assert(std::string(Keys[31].name)=="FrameDrawGates"&&Keys[31].field==&Settings::frameDrawGates&&Keys[31].low==0&&Keys[31].high==1);
    {auto off=parse("[Quality]\nFrameDrawGates=0\n");assert(off.frameDrawGates==0&&off!=d&&parse("[Quality]\nFrameDrawGates=1\n")==d);
        unsigned i=0;for(const auto& k:Keys){assert(off.origin[i]==(std::string(k.name)=="FrameDrawGates"?'f':'d'));++i;}
        std::vector<std::string> p;assert(parse("[Quality]\nFrameDrawGates=2\n",nullptr,p)==d&&p.size()==1);
        assert(effective(off).frameDrawGates==0&&effective(parse("[Quality]\nActorShadows=0\n")).frameDrawGates==1);
        assert(describe(d).find(" ActorShadows=1(default) FrameDrawGates=1(default)")!=std::string::npos&&describe(off).find(" FrameDrawGates=0(file)")!=std::string::npos);}
    // 0.3.190 ShadowPivotCorrection: 1 (the pivot distance follows zoom/collision snaps and the captured self) in the
    // code default and every preset, 0..1, own last origin slot; 0 is the old orbit-only distance. ActorShadows=0 does not force it.
    assert(d.shadowPivotCorrection==1&&preset(Preset::Balanced).shadowPivotCorrection==1&&preset(Preset::Performance).shadowPivotCorrection==1);
    assert(std::string(Keys[32].name)=="ShadowPivotCorrection"&&Keys[32].field==&Settings::shadowPivotCorrection&&Keys[32].low==0&&Keys[32].high==1);
    {auto off=parse("[Quality]\nShadowPivotCorrection=0\n");assert(off.shadowPivotCorrection==0&&off!=d&&parse("[Quality]\nShadowPivotCorrection=1\n")==d);
        unsigned i=0;for(const auto& k:Keys){assert(off.origin[i]==(std::string(k.name)=="ShadowPivotCorrection"?'f':'d'));++i;}
        std::vector<std::string> p;assert(parse("[Quality]\nShadowPivotCorrection=2\n",nullptr,p)==d&&p.size()==1);
        assert(effective(off).shadowPivotCorrection==0&&effective(parse("[Quality]\nActorShadows=0\n")).shadowPivotCorrection==1);
        assert(describe(d).find(" FrameDrawGates=1(default) ShadowPivotCorrection=1(default)")!=std::string::npos&&describe(off).find(" ShadowPivotCorrection=0(file)")!=std::string::npos);}
    // 0.3.192 CommandStream: 1 (the replay-thread command stream) in the code default and every preset, 0..1, own last
    // origin slot; a creation-time key, so ActorShadows=0 does not force it. 0 is the direct path.
    assert(d.commandStream==1&&preset(Preset::Balanced).commandStream==1&&preset(Preset::Performance).commandStream==1);
    assert(std::string(Keys[33].name)=="CommandStream"&&Keys[33].field==&Settings::commandStream&&Keys[33].low==0&&Keys[33].high==1&&sizeof(Keys)/sizeof(Keys[0])==34);
    {auto off=parse("[Quality]\nCommandStream=0\n");assert(off.commandStream==0&&off!=d&&parse("[Quality]\nCommandStream=1\n")==d);
        unsigned i=0;for(const auto& k:Keys){assert(off.origin[i]==(std::string(k.name)=="CommandStream"?'f':'d'));++i;}
        std::vector<std::string> p;assert(parse("[Quality]\nCommandStream=2\n",nullptr,p)==d&&p.size()==1);
        assert(effective(off).commandStream==0&&effective(parse("[Quality]\nActorShadows=0\n")).commandStream==1);
        assert(parse("[Quality]\nPreset=Performance\n").commandStream==1);
        assert(describe(d).find(" ShadowPivotCorrection=1(default) CommandStream=1(default)")!=std::string::npos&&describe(off).find(" CommandStream=0(file)")!=std::string::npos);}
    { /* effective(): the identity with ActorShadows=1 (any value of every key), with 0 only the two replay keys drop. */
      std::mt19937 er(158);
      for(int round=0;round<4000;++round){Settings s;s.preset=Preset(er()%3);unsigned i=0;
          for(const auto& k:Keys){s.*k.field=k.low+unsigned(er()%(k.high-k.low+1));s.origin[i++]="dplf"[er()%4];}
          const Settings e=effective(s);
          if(s.actorShadows){assert(e==s&&!std::memcmp(e.origin,s.origin,sizeof s.origin)&&e.preset==s.preset&&forcedOff(s)=="none");continue;}
          assert(e.shadowFateDiagnostics==0&&e.diagReplayProbe==0&&!shadowFate(e)&&!replayProbe(e));
          for(const auto& k:Keys){const std::string n=k.name;
              if(n!="ShadowFateDiagnostics"&&n!="DiagReplayProbe")assert(e.*k.field==s.*k.field);}
          assert(!std::memcmp(e.origin,s.origin,sizeof s.origin)&&e.preset==s.preset&&effective(e)==e);}
      assert(effective(d)==d&&forcedOff(d)=="none");
      auto off=parse("[Quality]\nActorShadows=0\nPersistentRigidProps=1\nDiagReplayProbe=1\n");
      assert(forcedOff(off)=="DiagReplayProbe"&&forcedOff(effective(off))=="none");
      off=parse("[Quality]\nActorShadows=0\nPersistentCasters=1\nPersistentRigidProps=1\nShadowFateDiagnostics=1\nDiagReplayProbe=1\n");
      assert(forcedOff(off)=="ShadowFateDiagnostics DiagReplayProbe");
      off=parse("[Quality]\nActorShadows=1\nShadowFateDiagnostics=1\n");assert(effective(off)==off&&forcedOff(off)=="none");
      /* actorShadowWork: the replay work request, for capture decisions. */
      Settings zero=d;zero.actorShadows=0;
      assert(actorShadowWork(d,true)&&!actorShadowWork(d,false)&&!actorShadowWork(zero,true)&&!actorShadowWork(zero,false));
      for(Preset pr:{Preset::Quality,Preset::Balanced,Preset::Performance}){Settings z=preset(pr);z.actorShadows=0;
          assert(captureSkipPossible(z,actorShadowWork(z,true))&&captureSkipPossible(preset(pr),actorShadowWork(preset(pr),true))==captureSkipPossible(preset(pr),true));}}
    std::printf("quality: %s\n",describe(d).c_str());
    std::printf("quality: defaults==0.3.136, presets, overrides, legacy precedence and fallbacks, BOM/CRLF, range edges, shipped ini, identical default code paths passed\n");
}
