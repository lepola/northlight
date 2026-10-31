#include "regional_fog.h"
#include <cassert>
#include <cstdio>
#include <random>
int main(){using namespace NorthlightRegionalFog;Field field;assert(globalHeightBounds(field)[2]==0);std::mt19937 random(934);auto unit=[&](){return (random()%1000000)/1000000.f;};
 for(auto& t:field.texels)t={unit()*2000-1000,unit(),unit(),random()%4?unit()*8+.1f:0};const auto bounds=globalHeightBounds(field);assert(bounds[2]>0);unsigned samples=0;
 for(unsigned n=0;n<1000000;++n){unsigned x=random()%64,y=random()%64;const float fx=unit(),fy=unit();float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy},sum=0,ground=0,tag=0;for(unsigned i=0;i<4;++i){const auto& t=field.texels[std::min(y+i/2,63u)*64+std::min(x+i%2,63u)];const float w=t.height>0?weights[i]:0;sum+=w;ground+=w*t.ground;tag+=w*t.height;}if(sum>0){ground/=std::max(sum,1e-6f);tag/=std::max(sum,1e-6f);assert(ground>=bounds[0]-.001f&&ground<=bounds[1]+.001f&&tag<=bounds[2]+.001f);}++samples;}
 for(float bad:{NAN,INFINITY,-INFINITY}){field.texels[800].ground=bad;assert(globalHeightBounds(field)[2]==0);}field.texels[800].ground=0;
 unsigned rays=0,skipped=0;
 for(unsigned n=0;n<100000;++n){const unsigned count=n%2?48:12;const float lo=unit()*2000-1000,hi=lo+unit()*128+.5f,cz=unit()*4000-2000,dz=unit()*2-1,surface=unit()*128,step=(128.f/count)/std::max(.57735026f,std::abs(dz)),offset=unit();const float inv=(dz<0?-1.f:1.f)/std::max(std::abs(dz),1e-8f),a=(lo-cz)*inv,b=(hi-cz)*inv,entry=std::max(0.f,std::min(a,b)),exit=std::max(a,b);const float march=std::min(surface,exit+step);const int first=std::max(0,int(std::floor(std::max(0.f,entry-step)/step+offset)));for(unsigned i=0;i<count+1;++i){const float start=std::max(0.f,(i-offset)*step);if(start>=surface)break;const float end=std::min((i+1-offset)*step,surface),mid=(start+end)*.5f,height=cz+dz*mid;const bool omitted=exit<entry||int(i)<first||start>=march;if(omitted){assert(height<lo+.0001f||height>hi-.0001f);++skipped;}}++rays;}
 assert(skipped);std::printf("FOG_GLOBAL_BOUNDS_PASS samples=%u rays=%u skipped=%u unknownFailsOpen=1\n",samples,rays,skipped);
}
