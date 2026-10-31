#define NorthlightSpellFog OldFog
#include "fog_reference/fit-wave2-old.h"
#undef NorthlightSpellFog
#include "spell_fog.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <random>
static void sameFit(const OldFog::Fit& a,const NorthlightSpellFog::Fit& b){assert(a.valid==b.valid&&a.ok==b.ok&&a.bare==b.bare&&a.bareSkip==b.bareSkip&&!std::memcmp(a.key,b.key,sizeof a.key)&&!std::memcmp(a.fit,b.fit,sizeof a.fit));}
static void same(const OldFog::Hold& a,const NorthlightSpellFog::Hold& b){
    assert(a.out.active==b.out.active&&a.out.revision==b.out.revision&&a.out.stamp==b.out.stamp);
    assert(!std::memcmp(a.out.tau,b.out.tau,sizeof a.out.tau)&&!std::memcmp(a.out.scatter,b.out.scatter,sizeof a.out.scatter));
    for(unsigned i=0;i<2;++i)sameFit(a.out.flat[i],b.out.flat[i]);for(unsigned i=0;i<OldFog::kRamps;++i)sameFit(a.out.ramp[i],b.out.ramp[i]);
    assert(!std::memcmp(a.fitTau,b.fitTau,sizeof a.fitTau)&&!std::memcmp(a.fitScatter,b.fitScatter,sizeof a.fitScatter)&&a.fitActive==b.fitActive);
    assert(!std::memcmp(a.hints,b.hints,sizeof a.hints)&&a.hintN==b.hintN&&a.hintNext==b.hintNext&&a.hintsNew==b.hintsNew&&a.resets==b.resets);
}
int main(){std::mt19937 rng(83121);static_assert(sizeof(OldFog::Hold)==sizeof(NorthlightSpellFog::Hold));
    for(unsigned k=0;k<100000;++k){OldFog::Hold a{};NorthlightSpellFog::Hold b{};OldFog::Frame f{};NorthlightSpellFog::Frame g{};
        f.active=rng()%2;for(float& t:f.tau)t=float(rng()%100)/1000.f;for(float& t:f.scatter)t=float(rng()%100)/100.f;
        for(unsigned i=0;i<6;++i){a.out.tau[i]=rng()%3?0.f:float(rng()%100)/1000.f;a.fitTau[i]=rng()%2?a.out.tau[i]:0.f;}
        a.out.active=rng()%2;a.fitActive=rng()%2;a.hintsNew=rng()%2;for(unsigned i=0;i<3;++i){a.out.scatter[i]=rng()%3?0.f:.2f;a.fitScatter[i]=rng()%2?a.out.scatter[i]:0.f;}
        std::memcpy(&b,&a,sizeof a);std::memcpy(&g,&f,sizeof f);const float dt=k%61==0?std::numeric_limits<float>::quiet_NaN():float(rng()%101)/1000.f;const bool wanted=rng()%2;
        a.update(f,wanted,dt);b.update(g,wanted,dt);same(a,b);
    }
    for(float dt:{0.f,.016f,-1.f,INFINITY,std::numeric_limits<float>::quiet_NaN()}){OldFog::Hold a{};NorthlightSpellFog::Hold b{};OldFog::Frame f{};NorthlightSpellFog::Frame g{};a.update(f,false,dt);b.update(g,false,dt);same(a,b);}
    {OldFog::Hold a{};NorthlightSpellFog::Hold b{};OldFog::Frame f{};NorthlightSpellFog::Frame g{};for(unsigned i=0;i<6;++i)f.tau[i]=g.tau[i]=.1f;f.active=g.active=true;float hint[3]={-.001f,1.f,1.f};a.addHint(hint,false);b.addHint(hint,false);for(unsigned i=0;i<50;++i){bool wanted=i<25;a.update(f,wanted,.016f);b.update(g,wanted,.016f);same(a,b);}}
}
