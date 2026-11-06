// northlight-test: requires=cxx
#include "equipment_lights.h"
#include "equipment_light_proof.h"
#include "local_light_selection.h"
#include "quality_settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include <sstream>
#include <cstdio>
using namespace NorthlightEquipmentLights;
static std::array<float,16> identity(){std::array<float,16> m{};m[0]=m[5]=m[10]=m[15]=1;return m;}
int main(){
    float camera[3]={100,200,300},bank[44]={};auto inv=identity();inv[12]=100;inv[13]=200;inv[14]=300;
    bank[0]=.2f;bank[1]=.4f;bank[2]=.8f;bank[16]=2;bank[17]=-3;bank[18]=4;bank[32]=1;bank[36]=.2f;bank[40]=.01f;
    Native n;assert(decode(bank,0,inv.data(),camera,n));assert(n.position[0]==102&&n.position[1]==197&&n.position[2]==304);
    assert(!decode(bank,4,inv.data(),camera,n));float old=bank[18];bank[18]=1000;assert(!decode(bank,0,inv.data(),camera,n));bank[18]=old;
    old=bank[1];bank[1]=NAN;assert(!decode(bank,0,inv.data(),camera,n));bank[1]=old;
    // Native admission uses the shared visibility sphere, including its fitted
    // radius, rather than the old equipment-only 48-yard cutoff.
    {auto far=std::array<float,44>{};std::copy_n(bank,44,far.begin());far[16]=MaxDistance;far[17]=far[18]=0;
     assert(decode(far.data(),0,inv.data(),camera,n));far[16]=MaxDistance+.01f;assert(!decode(far.data(),0,inv.data(),camera,n));
     far[16]=100;assert(decode(far.data(),0,inv.data(),camera,n));
     for(float bad:{NAN,INFINITY,-INFINITY}){far[16]=bad;assert(!decode(far.data(),0,inv.data(),camera,n));}
     far[16]=100;auto invalid=inv;invalid[0]=NAN;assert(!decode(far.data(),0,invalid.data(),camera,n));}
    assert(decode(bank,0,inv.data(),camera,n));auto l=light(n,1,1);assert(NorthlightLocalLights::valid(l));assert(l.diffuse[2]>l.diffuse[1]&&l.diffuse[1]>l.diffuse[0]);assert(l.attenuationEnd<=12);
    assert(std::fabs(l.diffuse[0]/l.diffuse[2]-.25f)<1e-6f); // Actual animated RGB hue, no item/texture name heuristic.
    // 12340 CM2Light defaults are denominator (0,.7,.03). At one yard
    // the compact fit preserves the native source energy (below its cap),
    // rather than applying the MPQ intensity for a second time.
    Native nativeDefault{};nativeDefault.color[0]=.35f;nativeDefault.color[1]=.14f;nativeDefault.color[2]=.07f;nativeDefault.attenuation[1]=.7f;nativeDefault.attenuation[2]=.03f;
    const auto fittedDefault=light(nativeDefault,3,1);assert(std::fabs(fittedDefault.diffuse[0]-.35f/.73f)<1e-6f);
    assert(std::fabs(fittedDefault.diffuse[1]/fittedDefault.diffuse[0]-.4f)<1e-6f);
    std::vector<NorthlightLocalLights::Light> fixed{l};assert(staticDuplicate(n,fixed));fixed[0].position[0]+=1;assert(!staticDuplicate(n,fixed));
    Tracker t;assert(t.admit(bank));assert(!t.admit(bank));for(unsigned i=1;i<MemoBanks;++i){bank[0]=float(i);assert(t.admit(bank));}bank[0]=100;assert(t.admit(bank));assert(t.banks==MemoBanks+1);
    // A/B/A and unused constants used to fill the bank budget again, starving
    // later character draws although no new native source had been seen.
    Tracker memo;bank[0]=.2f;assert(memo.admit(bank,1,false));bank[0]=.3f;assert(memo.admit(bank,1,false));bank[0]=.2f;assert(!memo.admit(bank,1,false));
    bank[3]=17;bank[4]=99;bank[19]=-11;bank[33]=7;assert(!memo.admit(bank,1,false));assert(memo.banks==2&&memo.bankDuplicates==2);
    std::array<float,44> ordered{};for(unsigned i=0;i<2;++i)for(unsigned k=0;k<3;++k){ordered[4*i+k]=float(i+k+1);ordered[4*(4+i)+k]=float(i+k+10);ordered[4*(8+k)+i]=float(i+k+20);}
    assert(memo.admit(ordered.data(),2));auto swapped=ordered;
    for(unsigned k=0;k<3;++k){std::swap(swapped[k],swapped[4+k]);std::swap(swapped[16+k],swapped[20+k]);std::swap(swapped[4*(8+k)],swapped[4*(8+k)+1]);}
    assert(!memo.admit(swapped.data(),2));
    CaptureBudget budget;
    for(unsigned i=0;i<400;++i)assert(budget.take(false,true));
    assert(budget.cached==400&&!budget.reads&&!budget.readLimited);
    for(unsigned i=0;i<200;++i)budget.take(false,false);
    assert(budget.backgroundReads==BackgroundReads&&budget.reads==BackgroundReads);
    for(unsigned i=0;i<200;++i)budget.take(true,false);
    assert(budget.reads==MaxReadsPerFrame&&budget.actorReads==MaxReadsPerFrame-BackgroundReads);
    assert(budget.take(false,true)); // Native read exhaustion never blocks known current state.
    budget.nextFrame();assert(!budget.reads&&!budget.candidates&&!budget.cached);
    t.nextFrame();assert(t.admit(bank));std::vector<NorthlightLocalLights::Light> none;
    t.observe(n,1000,none);t.observe(n,1000,none);assert(t.count==1);t.publish(1000);t.publish(1050);assert(t.outputCount==1&&t.output[0].diffuse[2]>0);
    const auto id=t.output[0].sourceId;n.position[0]+=.1f;n.color[2]=.6f;n.color[1]=.3f;n.color[0]=.15f;t.observe(n,1060,none);t.publish(1060);assert(t.count==1&&t.output[0].sourceId==id);
    t.publish(1160);t.publish(1260);t.publish(1360);assert(!t.count&&!t.outputCount); // No permanent lights after model/visibility disappears.
    t.reset();t.observe(n,0xfffffff0u,none);t.publish(0xfffffff0u);t.publish(0x20u);assert(t.count==1);t.publish(0x200u);t.publish(0x300u);assert(!t.count); // DWORD wrap.
    for(unsigned i=0;i<Capacity+4;++i){n.position[0]=float(i*2);t.observe(n,2000,none);}assert(t.count==Capacity&&t.dropped==4);
    t.reset();t.observe(n,3000,{light(n,2,1)});assert(!t.count&&t.duplicates==1);
    // A closer new source wins a full budget; an almost equal newcomer does
    // not churn the incumbent, and a stale source is immediately replaceable.
    t.reset();for(unsigned i=0;i<Capacity;++i){n.position[0]=camera[0]+20+2*i;t.observe(n,3000,none,camera);}
    n.position[0]=camera[0]+1;t.observe(n,3010,none,camera);bool near=false;for(unsigned i=0;i<t.count;++i)near|=t.entries[i].source.position[0]==n.position[0];assert(near&&t.count==Capacity);
    // Hundreds of first-frame candidates, both draw orders and equal-distance
    // ties: the nearest shared-cap set wins, including a lamp submitted last.
    const Native beforeMany=n;
    for(const auto& eye:std::array<std::array<float,3>,2>{{{{0,0,0}},{{1,3,2}}}}){
        std::vector<std::array<float,3>> places;
        for(int x=-3;x<=3;++x)for(int y=-3;y<=3;++y)for(int z=-3;z<=3;++z)places.push_back({x*2.f,y*2.f,z*2.f});
        const auto ranked=[&](const auto& a,const auto& b){const float da=distance2(a.data(),eye.data()),db=distance2(b.data(),eye.data());return da!=db?da<db:a<b;};
        auto expected=places;std::sort(expected.begin(),expected.end(),ranked);expected.resize(Capacity);std::sort(expected.begin(),expected.end());
        for(bool reverse:{false,true}){Tracker many;if(reverse)std::reverse(places.begin(),places.end());
            for(const auto& p:places){std::copy(p.begin(),p.end(),n.position);many.observe(n,5000,none,eye.data());}
            many.publish(5000);many.publish(5050);assert(many.outputCount==Capacity);
            std::vector<std::array<float,3>> got;for(unsigned i=0;i<many.outputCount;++i){const auto& p=many.output[i].position;got.push_back({p[0],p[1],p[2]});}
            std::sort(got.begin(),got.end());assert(got==expected);
        }
    }
    n=beforeMany;t.reset();
    // Static publication after capture must remove the previous dynamic light
    // AND the selection's already visible/fading entry, in this same frame.
    n.position[0]=camera[0]+2;Tracker publication;publication.observe(n,4000,none);publication.publish(4000);publication.publish(4050);
    NorthlightLocalLightSelection::Tracker pubSelect;pubSelect.update(none,camera,8,.016f,false,nullptr,publication.output.data(),publication.outputCount);
    const auto staticSource=light(n,77,1);std::vector<NorthlightLocalLights::Light> appeared{staticSource};
    publication.publish(4060,&appeared);assert(!publication.count&&publication.suppressedCount==1);
    for(unsigned i=0;i<publication.suppressedCount;++i)pubSelect.forget(publication.suppressed[i]);
    auto appearedOut=pubSelect.update(appeared,camera,8,.016f,true);assert(appearedOut.count==1);
    // When FGL removes the source, its native replacement must also remove the
    // old static selection tail instead of adding two versions for .4 seconds.
    publication.observe(n,4070,none);publication.publish(4070);publication.publish(4120);
    replaceFadingDuplicates(pubSelect,publication.output.data(),publication.outputCount);
    auto disappearedOut=pubSelect.update(none,camera,8,.016f,true,nullptr,publication.output.data(),publication.outputCount);assert(disappearedOut.count==1);
    publication.publish(4500);assert(!publication.count&&!publication.outputCount&&publication.suppressedCount==1);
    for(unsigned i=0;i<publication.suppressedCount;++i)pubSelect.forget(publication.suppressed[i]);
    assert(pubSelect.update(none,camera,8,.016f,true).count==0); // No second selection fade after native expiry.
    // Additional lights share the original global limit and deterministic sort.
    fixed={l};Tracker e;e.observe(n,4000,none);e.publish(4000);e.publish(4050);
    NorthlightLocalLightSelection::Tracker select;auto out=select.update(fixed,camera,8,.016f,false,nullptr,e.output.data(),e.outputCount);assert(out.count==2);
    NorthlightQuality::Settings settings;assert(settings.equipmentLights);std::vector<std::string> problems;
    assert(NorthlightQuality::preset(NorthlightQuality::Preset::Quality).equipmentLights==1);
    assert(!NorthlightQuality::preset(NorthlightQuality::Preset::Balanced).equipmentLights&&!NorthlightQuality::preset(NorthlightQuality::Preset::Performance).equipmentLights);
    NorthlightQuality::apply({{"EquipmentLights","0",1}},settings,"test",'f',problems,true);assert(!settings.equipmentLights);
    NorthlightQuality::apply({{"EquipmentLights","1",1}},settings,"test",'f',problems,true);assert(settings.equipmentLights==1&&problems.empty());
    assert(!NorthlightEquipmentLightProof::analyze(nullptr,0));
    {Tracker proof;proof.count=1;auto& entry=proof.entries[0];entry.id=42;entry.nativeId=0xfea0000000000001ull;entry.fixtureTag=2;entry.flags=NorthlightLocalLights::CastShadow;
        assert(proof.fixture(0xfec000000000002aull)==&entry);
        assert(!proof.fixture(42)&&!proof.fixture(0xfea000000000002aull)&&!proof.fixture(0xfec000000000002bull));
        entry.fixtureTag=0;assert(!proof.fixture(0xfec000000000002aull));entry.fixtureTag=2;entry.flags=0;assert(!proof.fixture(0xfec000000000002aull));}
    std::puts("equipment lights: world transform, actual hue, invalid input, bank revisit/order/unused-slot dedup, cached late-bank admission, bounded native fallback, nearest admission, static appears/disappears, movement, fade/TTL, clock wrap, shared selection limit, Quality default and override PASS");
}
