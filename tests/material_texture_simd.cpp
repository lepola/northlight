#include "particle_texture_stats.h"
#include "northlight_mem.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <new>
static bool failNew=false;
void* operator new(std::size_t n){if(failNew){failNew=false;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
static void verify(unsigned width,unsigned height,unsigned format,unsigned pad){using namespace NorthlightMaterialTextures;const size_t pitch=size_t(width)*4+pad;std::vector<unsigned char> storage(pitch*height+1);auto* src=storage.data()+1;for(size_t i=0;i<pitch*height;++i)src[i]=unsigned((i*37+i/11+width)%256);const auto original=storage;Cache c;void* key=(void*)16;void* owner=(void*)32;c.upload(key,owner,width,height,0,format,src,pitch*height,pitch,true,true);View out;assert(c.lookup(key,out));const unsigned tw=std::min(width,128u),th=std::min(height,128u);assert(out.width==tw&&out.height==th);for(unsigned y=0;y<th;++y)for(unsigned x=0;x<tw;++x){unsigned sum[4]={},n=0;for(unsigned sy=y*height/th;sy<(y+1)*height/th;++sy)for(unsigned sx=x*width/tw;sx<(x+1)*width/tw;++sx){const auto*p=src+size_t(sy)*pitch+sx*4;for(unsigned k=0;k<4;++k)sum[k]+=p[k];++n;}const auto*q=out.rgba+(size_t(y)*tw+x)*4;assert(q[0]==(sum[2]+n/2)/n&&q[1]==(sum[1]+n/2)/n&&q[2]==(sum[0]+n/2)/n&&q[3]==(format==22?255:(sum[3]+n/2)/n));}assert(storage==original);const auto version=c.version(key);c.upload(key,owner,width,height,0,format,src,pitch*height,pitch,true,true);assert(c.version(key)==version);c.upload(key,owner,width,height,0,format,src,pitch*height,pitch-1,true,false);assert(c.version(key)!=version&&!c.lookup(key,out));
 c.upload(key,owner,width,height,0,format,src,pitch*height,pitch,true,true);assert(c.lookup(key,out));c.upload(key,owner,width,height,0,format,src,size_t(width)*4-1,pitch,true,true);assert(!c.lookup(key,out));
 if(width>128||height>128){c.upload(key,owner,width,height,0,format,src,pitch*height,pitch,true,true);assert(c.lookup(key,out));failNew=true;c.upload(key,owner,width,height,0,format,src,pitch*height,pitch,true,true);assert(!failNew&&!c.lookup(key,out));}}
int main(){for(unsigned n=2;n<=64;++n){const auto m=std::uint32_t(((UINT64_C(1)<<32)+n-1)/n);for(unsigned sum=0;sum<=255*n;++sum)assert(((std::uint64_t(sum+n/2)*m)>>32)==(sum+n/2)/n);}const unsigned sizes[][2]={{1,1},{128,128},{256,256},{512,512},{1024,1024},{512,128},{128,512},{1024,768},{513,127},{768,512}};unsigned cases=0;for(auto& size:sizes)for(unsigned fmt:{21u,22u})for(unsigned pad:{0u,11u}){verify(size[0],size[1],fmt,pad);++cases;}printf("MATERIAL_TEXTURE_SIMD_PASS cases=%u exact_RGBA alpha_padding unaligned repeat_revision failure_invalidation allocation_failure short_input exhaustive_quotients source_unchanged\n",cases);}
