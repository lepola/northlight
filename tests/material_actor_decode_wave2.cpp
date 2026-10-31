#include "actor_texture.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace NorthlightActorTexture;
int main(){std::mt19937 rng(43201);for(unsigned w=1;w<=128;++w)for(unsigned h:{1u,3u,17u,128u})for(auto format:{Format::BGRA8,Format::BGRX8})for(unsigned pad:{0u,3u,13u}){
 const size_t pitch=w*4+pad,bytes=pitch*h;std::vector<unsigned char> source(bytes+2);for(auto& v:source)v=std::uint8_t(rng());const auto original=source;std::vector<unsigned char> out;assert(decode(source.data()+1,bytes,w,h,pitch,format,out));assert(out.size()==size_t(w)*h*4&&source==original);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto* in=source.data()+1+y*pitch+x*4;const auto* p=out.data()+(size_t(y)*w+x)*4;assert(p[0]==in[2]&&p[1]==in[1]&&p[2]==in[0]&&p[3]==(format==Format::BGRA8?in[3]:255));}
 auto alias=source;std::vector<unsigned char> expected=out;assert(decode(alias.data()+1,bytes,w,h,pitch,format,alias));assert(alias==expected);
 assert(!decode(source.data()+1,bytes-1,w,h,pitch,format,out)&&out.empty());assert(!decode(source.data()+1,bytes,w,h,w*4-1,format,out)&&out.empty());
 }
 for(unsigned width:{1u,3u,4u,13u,128u})for(auto f:{Format::RGB565,Format::ARGB1555,Format::XRGB1555,Format::ARGB4444,Format::BC1,Format::BC2,Format::BC3}){unsigned height=7;size_t pitch=rowBytes(width,f)+3;std::vector<unsigned char> in(pitch*rowCount(height,f),0);std::vector<unsigned char> out;assert(decode(in.data(),in.size(),width,height,pitch,f,out));assert(out.size()==size_t(width)*height*4);}
 std::puts("BGRA/BGRX decode exact pixels, tails, pitches, alias and scalar fallback passed");}
