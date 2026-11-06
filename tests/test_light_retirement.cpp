#include "local_light_selection.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <random>
using namespace NorthlightLocalLightSelection;
static void linear(Tracker& t,std::uint64_t id){for(unsigned i=0;i<t.count;++i)if(t.entries[i].id==id){std::move(t.entries.begin()+i+1,t.entries.begin()+t.count,t.entries.begin()+i);--t.count;return;}}
static void equal(const Tracker& a,const Tracker& b){assert(a.count==b.count&&a.fading==b.fading&&a.offscreen==b.offscreen);for(unsigned i=0;i<a.count;++i){const auto& x=a.entries[i];const auto& y=b.entries[i];assert(x.id==y.id&&x.shown==y.shown&&x.selected==y.selected&&!std::memcmp(&x.light,&y.light,sizeof x.light));}}
int main(){std::mt19937_64 random(625437);unsigned cases=0;
 for(unsigned count=0;count<=Slots;++count)for(unsigned batch=0;batch<=16;++batch)for(unsigned round=0;round<8;++round){
  Tracker seed;seed.count=count;seed.fading=7;seed.offscreen=3;
  for(unsigned i=0;i<count;++i){auto& e=seed.entries[i];e.id=round==0?i/3:round==1?UINT64_MAX-i/3:random()%97;e.shown=float(i)/Slots;e.selected=i%2;e.light={};e.light.sourceId=i;e.light.position[0]=float(i);e.light.flags=i;}
  std::sort(seed.entries.begin(),seed.entries.begin()+count,[](const auto& a,const auto& b){return a.id<b.id;});
  std::array<std::uint64_t,16> ids{};for(unsigned i=0;i<batch;++i)ids[i]=round==1?UINT64_MAX-random()%97:random()%97;
  auto reference=seed,one=seed,many=seed;
  for(unsigned i=0;i<batch;++i){linear(reference,ids[i]);one.forget(ids[i]);equal(reference,one);}
  std::sort(ids.begin(),ids.begin()+batch);many.forgetSorted(batch?ids.data():nullptr,batch);equal(reference,many);++cases;
 }
 std::printf("LIGHT_RETIREMENT_PASS cases=%u sortedStable=1 duplicateIds=1 exactRecords=1 timings=0\n",cases);
}
