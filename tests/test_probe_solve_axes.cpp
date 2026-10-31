#pragma STDC FENV_ACCESS ON
#include "world_probe_progress.h"
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <random>
using namespace NorthlightGI;
static std::vector<unsigned> reference(Vec3 camera){struct Item{unsigned index;float distance;};const auto& l=probeLayout();const unsigned count=l.count();std::vector<Item> items(count);const Vec3 origin=probeWindowOrigin(camera);const unsigned g=l.n,base=g/2-1,baseZ=l.nz/2-1;unsigned i=0;for(unsigned z=0;z<l.nz;++z)for(unsigned y=0;y<g;++y)for(unsigned x=0;x<g;++x,++i){Vec3 d={(origin.x+float(x)*8)-camera.x,(origin.y+float(y)*8)-camera.y,(origin.z+float(z)*8)-camera.z};const bool corner=x>=base&&x<=base+1&&y>=base&&y<=base+1&&z>=baseZ&&z<=baseZ+1;items[i]={i|(corner?0u:0x80000000u),d.x*d.x+d.y*d.y+d.z*d.z};}std::sort(items.begin(),items.end(),[](const Item&a,const Item&b){if((a.index^b.index)&0x80000000u)return a.index<b.index;if(a.distance!=b.distance)return a.distance<b.distance;return a.index<b.index;});std::vector<unsigned> order(count);for(unsigned i=0;i<count;++i)order[i]=items[i].index&0x7fffffffu;return order;}
int main(){std::mt19937 random(987412);unsigned cases=0;for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);for(unsigned n=2;n<=22;n+=2)for(unsigned nz:{2u,14u,22u}){ProbeLayout l{n,nz,std::max(n,nz)+2};assert(configureProbeLayout(l));for(unsigned i=0;i<32;++i){const auto value=[&](){return i<8?float(i)-4.f:float(int(random()%2000000)-1000000)/8.f;};Vec3 c{value(),value(),value()};assert(probeSolveOrder(c)==reference(c));++cases;}}}assert(std::fesetround(FE_TONEAREST)==0);assert(configureProbeLayout({14,14,16}));std::printf("PROBE_SOLVE_AXES_PASS cases=%u exactOrder=1 roundingModes=4 timings=0\n",cases);}
