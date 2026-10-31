#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <random>
int main(){
 std::mt19937 rng(2352);for(unsigned p=1;p<32;++p)for(int i=0;i<1000;++i){float x=float(int(rng()%10000)-5000)/100.f,y=float(int(rng()%10000)-5000)/100.f,z=float(int(rng()%10000)-5000)/100.f;auto seed=rng();float a=OldFogClouds::detail::worley(p,seed,x,y,z),b=NorthlightFogClouds::detail::worley(p,seed,x,y,z);assert(!std::memcmp(&a,&b,4));}
 uint8_t features[14400];std::memset(features,0xa5,sizeof features);NorthlightFogClouds::detail::prepareWorleyFeatures(features,42);
 for(unsigned axis=0;axis<3;++axis)for(unsigned row=0;row<100;++row)for(unsigned cell=10;cell<12;++cell){uint32_t bits;std::memcpy(&bits,features+4*(axis*1200+row*12+cell),4);assert(bits==0);}
 NorthlightFogClouds::detail::PerlinAxis axis[64];for(unsigned i=0;i<64;++i)axis[i]=NorthlightFogClouds::detail::perlinAxis(float(i),.125f);
 for(unsigned z=0;z<64;++z)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const auto X=axis[x],Y=axis[y],Z=axis[z];float a=OldFogClouds::detail::worley(8,42,float(x),float(y),float(z)),b=NorthlightFogClouds::detail::worleyPrepared(8,42,float(X.cell)+X.fraction,float(Y.cell)+Y.fraction,float(Z.cell)+Z.fraction,X.cell,Y.cell,Z.cell,nullptr);assert(!std::memcmp(&a,&b,4));float c=NorthlightFogClouds::detail::worleyPrepared<true>(8,42,float(X.cell)+X.fraction,float(Y.cell)+Y.fraction,float(Z.cell)+Z.fraction,X.cell,Y.cell,Z.cell,features);assert(!std::memcmp(&a,&c,4));}
}
