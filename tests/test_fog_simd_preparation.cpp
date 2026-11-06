#pragma STDC FENV_ACCESS ON
#include "fog_reference/cloud-generation-before.h"
#include "fog_reference/global-height-before.h"
#include <cassert>
#include <cfenv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
namespace FC=NorthlightFogClouds;
namespace RF=NorthlightRegionalFog;
int main(int argc,char**){
 unsigned lanes=0,volumes=0,bounds=0;std::mt19937 rng(625493);
 auto sameBounds=[&](const RF::Field& field){
  std::feclearexcept(FE_ALL_EXCEPT);const auto a=FrozenFogBounds::globalHeightBounds(field);const int before=std::fetestexcept(FE_ALL_EXCEPT);
  std::feclearexcept(FE_ALL_EXCEPT);const auto b=RF::globalHeightBounds(field);const int after=std::fetestexcept(FE_ALL_EXCEPT);
  assert(!std::memcmp(a.data(),b.data(),sizeof a)&&before==after);++bounds;
 };
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
  assert(std::fesetround(mode)==0);
#if defined(__SSE2__)
  std::vector<uint8_t> table(FC::N*FC::N*FC::N);
  for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x)table[(o==0?0:o==1?64:576)+x+P*(y+P*z)]=uint8_t(FC::detail::hash(x,y,z,42+o*0x9e37u)%12);
  FC::detail::preparePerlinCubes(table.data());
  for(unsigned o=0,P=4;o<3;++o,P*=2){FC::detail::PerlinAxis axis[FC::N];for(unsigned i=0;i<FC::N;++i)axis[i]=FC::detail::perlinAxis(float(i),float(P)/FC::N);
   for(unsigned z=0;z<FC::N;++z)for(unsigned y=0;y<FC::N;++y)for(unsigned x=0;x<FC::N;x+=4){
    float batch[4];_mm_storeu_ps(batch,FC::detail::perlinFourPrepared(P,o,&axis[x],axis[y],axis[z],table.data()+19072));
    for(unsigned i=0;i<4;++i){const float old=FC::detail::perlinPrepared<true>(P,42,o,axis[x+i],axis[y],axis[z],table.data()+19072);assert(!std::memcmp(&old,batch+i,4));++lanes;}
   }
  }
#endif
  for(unsigned seed:{0u,1u,42u,0xffffffffu}){const auto a=FrozenCloudGeneration::generate(seed),b=FC::generate(seed);assert(a==b);++volumes;}
  for(unsigned scenario=0;scenario<20;++scenario){RF::Field f;
   for(unsigned i=0;i<RF::N*RF::N;++i){
    const float ground=scenario<4?(i%2?-0.f:0.f):float(int(rng()%200000)-100000)/32;
    f.texels[i]={ground,.01f,.02f,scenario==0?0.f:scenario==1?5.f:float(rng()%7)*.625f};
   }
   sameBounds(f);
   for(unsigned at:{0u,1u,2u,3u,4u,63u,64u,4095u})for(unsigned component=0;component<4;++component){
    auto altered=f;uint32_t words[4];std::memcpy(words,&altered.texels[at],16);
    for(uint32_t bits:{0x7fc12345u,0x7f800001u,0x7f800000u,0xff800000u,1u,0x80000001u,0x7f7fffffu}){words[component]=bits;std::memcpy(&altered.texels[at],words,16);sameBounds(altered);}
   }
  }
 }
 assert(std::fesetround(FE_TONEAREST)==0);
#if defined(__SSE2__)
 const unsigned control=_mm_getcsr();
 for(unsigned mask:{0u,1u<<6,1u<<15,(1u<<6)|(1u<<15)}){_mm_setcsr(control|mask);RF::Field f;for(unsigned i=0;i<RF::N*RF::N;++i){uint32_t bits=i%2?1u:0x80000001u;float ground;std::memcpy(&ground,&bits,4);f.texels[i]={ground,0,0,5};}sameBounds(f);}
 _mm_setcsr(control);
#endif
 std::printf("FOG_SIMD_PREPARATION_PASS lanes=%u volumes=%u exactBytes=%u bounds=%u scalarFallback=%u\n",lanes,volumes,volumes*FC::N*FC::N*FC::N,bounds,lanes==0);
 if(argc==1)return 0;
 double times[2][8];unsigned retained[2]={};volatile unsigned byte=0;
 for(unsigned block=0;block<20;++block){const unsigned variant=(block/2)%2?1-block%2:block%2;const auto start=std::chrono::steady_clock::now();auto v=variant?FC::generate(42):FrozenCloudGeneration::generate(42);byte=v[block];if(block>=4)times[variant][retained[variant]++]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
 for(auto& t:times)std::sort(t,t+8);(void)byte;std::printf("CLOUD_GENERATE_ABBA old_ms=%.6f new_ms=%.6f ratio=%.5f\n",(times[0][3]+times[0][4])*.5,(times[1][3]+times[1][4])*.5,(times[1][3]+times[1][4])/(times[0][3]+times[0][4]));
 RF::Field field;for(unsigned i=0;i<RF::N*RF::N;++i)field.texels[i]={float(int(i%271)-135),.01f,.02f,i%7?5.f:0.f};retained[0]=retained[1]=0;volatile float sink=0;
 for(unsigned block=0;block<20;++block){const unsigned variant=(block/2)%2?1-block%2:block%2;const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<1000;++i){auto v=variant?RF::globalHeightBounds(field):FrozenFogBounds::globalHeightBounds(field);sink=v[i%3];}if(block>=4)times[variant][retained[variant]++]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/1000;}
 for(auto& t:times)std::sort(t,t+8);(void)sink;std::printf("FOG_BOUNDS_ABBA old_us=%.6f new_us=%.6f ratio=%.5f\n",(times[0][3]+times[0][4])*.5,(times[1][3]+times[1][4])*.5,(times[1][3]+times[1][4])/(times[0][3]+times[0][4]));
}
