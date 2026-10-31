// Manual differential regression against the preserved pre-optimization header.
#include "spell_fog.h"
#define NorthlightSpellFog ReferenceSpellFog
#include "fog_reference/fit3-old.h"
#undef NorthlightSpellFog
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace New=NorthlightSpellFog;
namespace Old=ReferenceSpellFog;
static uint32_t seed=1531;
static uint32_t next(){return seed=seed*1664525u+1013904223u;}
static void sameFit(const New::Fit& a,const Old::Fit& b){
 assert(a.valid==b.valid&&a.ok==b.ok&&a.bare==b.bare&&a.bareSkip==b.bareSkip);
 assert(!std::memcmp(a.key,b.key,sizeof a.key)&&!std::memcmp(a.fit,b.fit,sizeof a.fit));
}
static void same(const New::Frame& a,const Old::Frame& b){
 assert(a.active==b.active&&a.revision==b.revision&&a.stamp==b.stamp);
 assert(!std::memcmp(a.tau,b.tau,sizeof a.tau)&&!std::memcmp(a.scatter,b.scatter,sizeof a.scatter));
 for(unsigned i=0;i<2;++i)sameFit(a.flat[i],b.flat[i]);
 for(unsigned i=0;i<New::kRamps;++i)sameFit(a.ramp[i],b.ramp[i]);
}
static constexpr float games[][3]={{0,1,1},{-.00078308536f,1,1.872f},{-.01f,1,1},{-.02f,2,4},{-.5f,1,.1f},{.01f,.5f,1},{0,.5f,1}};
int main(){
 unsigned fits=0,sharedFits=0,updates=0;volatile float sink=0;
 New::FitWork shared;
 for(float base:{-INFINITY,-2.f,-0.f,0.f,.5f,1.f,1.0001f,INFINITY,NAN})
  for(float power:{-INFINITY,-2.f,0.f,.6f,1.f,2.f,32.f,INFINITY,NAN}){
   const float expected=std::min(1.f,std::pow(std::max(base,0.f),power));
   const float actual=New::clampedPower(base,power);
   assert(!std::memcmp(&expected,&actual,sizeof expected));
  }
 // Direct fits: changing optical depth profiles, ramps, flat constants, bare
 // shaders, failed fits and thick fog. Identical parameters, not a tolerance.
 for(unsigned i=0;i<400;++i){
  New::Frame a;Old::Frame b;a.active=b.active=i%17!=0;
  float tau=0;
  for(unsigned k=0;k<New::kSamples;++k){tau+=float(next()%1000)/float(i%3?10000:100);a.tau[k]=b.tau[k]=tau;}
  for(const auto& g:games)for(bool bare:{i%2!=0,i%2==0}){
   float af[3]={-999,-999,-999},bf[3]={-999,-999,-999};bool as=false,bs=false;
   const bool ao=New::emitFog(a,g,af,bare,&as),bo=Old::emitFog(b,g,bf,bare,&bs);
   assert(ao==bo&&as==bs&&!std::memcmp(af,bf,sizeof af));++fits;
   float cached[3]={-999,-999,-999};bool skipped=false;
   const bool ok=New::emitFog(a,g,cached,bare,&skipped,&shared);
   assert(ok==bo&&skipped==bs&&!std::memcmp(cached,bf,sizeof cached));++sharedFits;
  }
 }
 // Directed cutoff boundaries: equal candidate errors, clear and saturated
 // profiles, subnormal values, and invalid targets preserve old selection.
 for(float density:{0.f,1e-40f,.004f,.2f,10.f,INFINITY,NAN}){
  New::Frame a;Old::Frame b;a.active=b.active=true;
  for(unsigned k=0;k<New::kSamples;++k)a.tau[k]=b.tau[k]=density*New::kDistance[k];
  for(const auto& g:games)for(bool bare:{false,true}){
   float af[3]={-999,-999,-999},bf[3]={-999,-999,-999};bool as=false,bs=false;
   const bool ao=New::emitFog(a,g,af,bare,&as),bo=Old::emitFog(b,g,bf,bare,&bs);
   assert(ao==bo&&as==bs&&!std::memcmp(af,bf,sizeof af));++fits;
  }
 }
 // The complete hold path, including new hints, unchanged frames, smoothing,
 // inactive transitions, invalid input reset and the revision threshold.
 New::Hold a;Old::Hold b;
 for(unsigned i=0;i<500;++i){
  New::Frame ar;Old::Frame br;ar.active=br.active=i%13!=0;
  for(unsigned k=0;k<New::kSamples;++k)ar.tau[k]=br.tau[k]=New::kDistance[k]*(.002f+float(next()%1000)/100000);
  for(unsigned k=0;k<3;++k)ar.scatter[k]=br.scatter[k]=float(next()%256)/256;
  if(i%7==0){const auto& game=games[i%5];a.addHint(game,i%2);b.addHint(game,i%2);}
  if(i%11==0)for(bool bare:{i%2!=0,i%2==0}){
   const auto& game=games[1+i%4];a.addHint(game,bare);b.addHint(game,bare);
  }
  if(i%41==0)ar.tau[0]=br.tau[0]=NAN;
  const float dt=i%3?.008f:0;const bool wanted=i%29!=0;
  a.update(ar,wanted,dt);b.update(br,wanted,dt);same(a.out,b.out);assert(a.resets==b.resets);++updates;
 }
 // Same static profile repeated while its transmittance changes enough to
 // trigger refits: reproduces the work in Hold::update on a moving camera.
 double ms[2][7];
 for(unsigned r=0;r<7;++r)for(unsigned mode=0;mode<2;++mode){
  New::Hold nh;Old::Hold oh;
  for(unsigned h=0;h<4;++h){nh.addHint(games[1+h%3],h%2);oh.addHint(games[1+h%3],h%2);}
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<120;++i){
   const float density=.003f+.0001f*float(i%20);
   if(mode){New::Frame f;f.active=true;for(unsigned k=0;k<New::kSamples;++k)f.tau[k]=New::kDistance[k]*density;nh.update(f,true,.016f);sink=nh.out.flat[0].fit[0];}
   else{Old::Frame f;f.active=true;for(unsigned k=0;k<Old::kSamples;++k)f.tau[k]=Old::kDistance[k]*density;oh.update(f,true,.016f);sink=oh.out.flat[0].fit[0];}
  }
  ms[mode][r]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120;
 }
 std::sort(ms[0],ms[0]+7);std::sort(ms[1],ms[1]+7);(void)sink;
 std::printf("{\"pass\":true,\"fits_exact\":%u,\"shared_fits_exact\":%u,\"hold_updates_exact\":%u,\"old_ms\":%.6f,\"new_ms\":%.6f,\"ratio\":%.4f}\n",fits,sharedFits,updates,ms[0][3],ms[1][3],ms[1][3]/ms[0][3]);
}
