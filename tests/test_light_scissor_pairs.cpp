#include "local_light_scissor.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
using namespace NorthlightLocalLightScissor;
static bool same(Rect a,Rect b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
int main(){std::mt19937 random(72841);unsigned cases=0;
 for(unsigned iteration=0;iteration<12000;++iteration){
  float iv[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},p[3]={.7f,1.1f,1};
  iv[12]=float(int(random()%40000)-20000);iv[13]=float(int(random()%40000)-20000);iv[14]=float(int(random()%40000)-20000);
  auto v=view(iv,p,.1f,3.5f,iteration%2?137:128,iteration%2?79:96);if(iteration%29==0)v.valid=false;
  std::array<std::array<float,4>,136> positions;const unsigned count=iteration%137;
  for(unsigned i=0;i<count;++i){for(unsigned k=0;k<3;++k)positions[i][k]=iv[12+k]+float(int(random()%1000)-500);positions[i][3]=float(random()%200);if(iteration%31==0&&i==count/2)positions[i][0]=std::numeric_limits<float>::quiet_NaN();}
  Rect fog[34]{},direct[17]{};pairedBatchRects<4>(v,positions,count,fog,direct);
  for(unsigned first=0;first<count;first+=4){std::array<std::array<float,4>,4> b{};unsigned n=std::min(4u,count-first);std::copy_n(positions.begin()+first,n,b.begin());assert(same(fog[first/4],batchRect(v,b,n)));}
  for(unsigned first=0;first<count;first+=8){std::array<std::array<float,4>,8> b{};unsigned n=std::min(8u,count-first);std::copy_n(positions.begin()+first,n,b.begin());assert(same(direct[first/8],batchRect(v,b,n)));}++cases;
 }
 std::printf("LIGHT_SCISSOR_PAIRS_PASS cases=%u exactRects=1 timings=0\n",cases);
}
