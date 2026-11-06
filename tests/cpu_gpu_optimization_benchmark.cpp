#define NOMINMAX
#include "weather_lens.h"
#include "dither_noise.h"
#include "position_bounds_scan.h"
#include "material_byte_ops.h"
#include "static_shadow_dedup.h"
#define NorthlightWeatherLens ReferenceWeatherLens
#include "weather_lens_reference.h"
#undef NorthlightWeatherLens
#define NorthlightDitherNoise ReferenceDither
#include "dither_noise_reference.h"
#undef NorthlightDitherNoise
#define NorthlightPositionBounds ReferenceBounds
#include "position_bounds_scan_reference.h"
#undef NorthlightPositionBounds
#define NorthlightMaterialBytes ReferenceBytes
#include "material_byte_ops_reference.h"
#undef NorthlightMaterialBytes
#define StaticShadowDedup ReferenceShadow
#include "static_shadow_dedup_reference.h"
#undef StaticShadowDedup
#include <chrono>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <cassert>
static volatile unsigned sink=0;
template<class F> static double timed(F function){double times[5];for(double& t:times){const auto begin=std::chrono::steady_clock::now();function();t=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}std::sort(times,times+5);return times[2];}
template<class Old,class Next> static void compare(const char* name,Old old,Next next){const double a=timed(old),b=timed(next);std::printf("CPU_BENCH name=%s before_ms=%.6f after_ms=%.6f ratio=%.4f\n",name,a,b,b/a);}
template<class State> static State rain(){State s;s.count=192;for(unsigned i=0;i<s.count;++i){auto& d=s.drops[i];d.x=float(i%16)*.08f;d.y=float(i/16)*.08f;d.previousX=d.x-.001f;d.previousY=d.y-.002f;d.water=.00001f;d.vx=.01f;d.vy=.04f;d.seed=float(i)/192;d.age=.5f;d.primeVisual();}return s;}
int main(){
    compare("dither_ranks",[]{auto r=ReferenceDither::ranks();sink+=r[17];},[]{auto r=NorthlightDitherNoise::ranks();sink+=r[17];});
    NorthlightDitherNoise::offsets();
    compare("dither_recreate",[]{auto r=ReferenceDither::offsets();sink+=r[17]>0;},[]{auto r=NorthlightDitherNoise::offsets();sink+=r[17]>0;});
    auto oldRain=rain<ReferenceWeatherLens::State>();auto newRain=rain<NorthlightWeatherLens::State>();
    compare("rain_merge",[&]{for(unsigned i=0;i<100;++i){auto s=oldRain;s.merge();sink+=s.count;}},[&]{for(unsigned i=0;i<100;++i){auto s=newRain;s.merge();sink+=s.count;}});
    for(unsigned stride:{12u,16u,32u}){
        std::vector<unsigned char> bytes(12000*stride);for(unsigned i=0;i<12000;++i){float p[]={float(i%191),-float(i%317),float(i%13)};std::memcpy(bytes.data()+i*stride,p,12);}
        char label[64];std::snprintf(label,sizeof label,"position_scan_stride%u",stride);
        compare(label,[&]{float lo[3],hi[3];for(unsigned i=0;i<120;++i){assert(ReferenceBounds::scanPositions(bytes.data(),12000,stride,lo,hi));sink+=unsigned(hi[0]);}},[&]{float lo[3],hi[3];for(unsigned i=0;i<120;++i){assert(NorthlightPositionBounds::scanPositions(bytes.data(),12000,stride,lo,hi));sink+=unsigned(hi[0]);}});
    }
    std::vector<unsigned char> pixels(512*512*4,255);
    compare("alpha_opaque",[&]{for(unsigned i=0;i<500;++i)sink+=ReferenceBytes::minimumAlpha(pixels.data(),pixels.size());},[&]{for(unsigned i=0;i<500;++i)sink+=NorthlightMaterialBytes::minimumAlpha(pixels.data(),pixels.size());});
    pixels[3]=0;
    compare("alpha_zero",[&]{for(unsigned i=0;i<500;++i)sink+=ReferenceBytes::minimumAlpha(pixels.data(),pixels.size());},[&]{for(unsigned i=0;i<500;++i)sink+=NorthlightMaterialBytes::minimumAlpha(pixels.data(),pixels.size());});
    std::vector<StaticShadowDedup::Triangle> triangles(8192);uint32_t rng=31;
    for(auto& t:triangles)for(auto& p:t){rng=rng*1664525u+1013904223u;p={float(int(rng%1000)-500),float(rng%113),float(rng%71)};}
    compare("canonical_triangles",[&]{for(unsigned r=0;r<32;++r)for(auto& t:triangles){StaticShadowDedup::Canonical value;assert(ReferenceShadow::canonical(t,value));sink^=value[0];}},[&]{for(unsigned r=0;r<32;++r)for(auto& t:triangles){StaticShadowDedup::Canonical value;assert(StaticShadowDedup::canonical(t,value));sink^=value[0];}});
    std::vector<NorthlightGI::Vec3> vertices(8194);std::vector<uint32_t> indices;
    for(unsigned i=0;i<vertices.size();++i){vertices[i]={float(i),float(i%17),float(i%11)};indices.push_back(i);}
    std::vector<StaticShadowDedup::Triangle> output;
    compare("strip_triangles",[&]{for(unsigned r=0;r<200;++r){assert(ReferenceShadow::makeTriangles(vertices,indices,true,8192,true,output));sink+=unsigned(output.size());}},[&]{for(unsigned r=0;r<200;++r){assert(StaticShadowDedup::makeTriangles(vertices,indices,true,8192,true,output));sink+=unsigned(output.size());}});
    std::printf("CPU_BENCH_PASS sink=%u\n",sink);
}
