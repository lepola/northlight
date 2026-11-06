#define NOMINMAX
#include <windows.h>
#undef near
#undef far
#include "actor_deformation.h"
#define NorthlightActorTexture CPUReferenceTexture
#include "fixtures/actor-decode-0.3.127/actor_texture.h"
#undef NorthlightActorTexture
#include "actor_texture.h"
#include "world_mesh_pages.h"
#include <cassert>
#include <cstdio>
#include <iterator>
#include <list>
#include <sstream>

static void arrays(){
    using namespace NorthlightGI;
    AdaptiveGeometryArray<unsigned> a;const size_t block=a.blockSize();
    std::vector<unsigned> reference(block*3+17);for(size_t i=0;i<reference.size();++i)reference[i]=unsigned(i*37+9);
    a.insert(a.end(),reference.begin(),reference.end());a.chunks(nullptr);
    auto equal=[&]{assert(a.size()==reference.size());for(size_t i=0;i<a.size();++i)assert(a[i]==reference[i]);};equal();
    auto copy=a;for(size_t i=0;i<a.size();++i)assert(copy[i]==a[i]);
    a.resize(block-1);reference.resize(block-1);a.resize(block*4+3);reference.resize(block*4+3);equal();
    a.assign(block*2+11,73);reference.assign(block*2+11,73);equal();
    a.assign(block+1,a[0]);reference.assign(block+1,73);equal();
    a.insert(a.end(),copy.begin()+block-2,copy.begin()+block+11);reference.insert(reference.end(),13,0);
    for(unsigned i=0;i<13;++i)reference[reference.size()-13+i]=copy[block-2+i];equal();
    std::list<unsigned> list={3,5,7};a.insert(a.end(),list.begin(),list.end());reference.insert(reference.end(),list.begin(),list.end());equal();
    std::istringstream input("11 13 17");a.insert(a.end(),std::istream_iterator<unsigned>(input),std::istream_iterator<unsigned>());reference.insert(reference.end(),{11,13,17});equal();
    struct Value {unsigned x=7,y=11;};TemporaryGeometryArray<Value> disk;
    const size_t page=65536/sizeof(Value);disk.resize(page*7+9);
    for(size_t i=0;i<disk.size();++i)assert(disk[i].x==7&&disk[i].y==11);
    for(size_t i=0;i<disk.size();++i)disk[i]={unsigned(i),unsigned(i^0x5a5a)};
    disk.resize(page-3);disk.resize(page*8+1);
    for(size_t i=0;i<disk.size();++i){const auto& v=static_cast<const TemporaryGeometryArray<Value>&>(disk)[i];assert(v.x==(i<page-3?i:7)&&v.y==(i<page-3?(i^0x5a5a):11));}
    disk.clear();disk.push_back({19,23});assert(disk[0].x==19);disk.flush();
    TemporaryGeometryFile file;unsigned value=1;file.append(&value,4);file.write(4,nullptr,0);file.read(4,nullptr,0);
    std::puts("CPU_ARRAY_PASS block boundaries, aliases, forward/input iterators, disk eviction, shrink/regrow, nonzero defaults");
}
static void halfValues(){
    for(unsigned bits=0;bits<65536;++bits){const unsigned exponent=(bits>>10)&31,mantissa=bits&1023;
        float expected=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?(mantissa?std::numeric_limits<float>::quiet_NaN():INFINITY):std::ldexp(float(mantissa+1024),int(exponent)-25);if(bits&32768)expected=-expected;
        const float actual=NorthlightActorDeformation::half(std::uint16_t(bits));assert(!std::memcmp(&expected,&actual,4));
    }
    std::puts("CPU_HALF_PASS all 65536 half values bit exact including signed zero, subnormals, infinities and NaNs");
}
static void packedTextures(){
    using namespace NorthlightActorTexture;std::vector<uint8_t> output;
    for(auto format:{Format::RGB565,Format::ARGB1555,Format::XRGB1555,Format::ARGB4444})
        for(unsigned pad:{0u,13u})for(unsigned base=0;base<65536;base+=16384){
            const size_t pitch=256+pad;std::vector<uint8_t> input(pitch*128+1,0xcd);auto* bytes=input.data()+1;
            for(unsigned i=0;i<16384;++i){const uint16_t word=uint16_t(base+i);std::memcpy(bytes+(i/128)*pitch+(i%128)*2,&word,2);}
            const auto before=input;assert(decode(bytes,pitch*128,128,128,pitch,format,output));
            for(unsigned i=0;i<16384;++i){const uint16_t word=uint16_t(base+i);const auto expected=format==Format::RGB565?rgb565(word):format==Format::ARGB4444?argb4444(word):argb1555(word,format==Format::ARGB1555);assert(!std::memcmp(output.data()+i*4,expected.data(),4));}assert(input==before);
        }
    for(unsigned width=1;width<=128;++width)for(auto format:{Format::RGB565,Format::ARGB1555,Format::XRGB1555,Format::ARGB4444}){
        const size_t pitch=width*2+5;std::vector<uint8_t> input(pitch*3+1,0xa7);auto* bytes=input.data()+1;
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<width;++x){uint16_t word=uint16_t((y*width+x)*513);std::memcpy(bytes+y*pitch+x*2,&word,2);}
        assert(decode(bytes,pitch*3,width,3,pitch,format,output));
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<width;++x){uint16_t word;std::memcpy(&word,bytes+y*pitch+x*2,2);const auto expected=format==Format::RGB565?rgb565(word):format==Format::ARGB4444?argb4444(word):argb1555(word,format==Format::ARGB1555);assert(!std::memcmp(output.data()+(y*width+x)*4,expected.data(),4));}
    }
    std::puts("CPU_PACKED_TEXTURE_PASS every 16-bit value, all four formats, unaligned/padded/flat rows and every SIMD tail");
}
static void indexMap(){
    NorthlightWorldMeshPages::IndexMap map;
    for(unsigned epoch=0;epoch<20;++epoch){map.clear();for(unsigned i=0;i<16000;++i){const unsigned key=i*32768+epoch;assert(map.find(key)==map.Missing);assert(map.insert(key,i).second);assert(map.find(key)==i);assert(!map.insert(key,99).second);}assert(map.size()==16000);}
    std::puts("CPU_INDEX_MAP_PASS repeated misses/inserts/hits, collisions and epoch clears");
}
static void compressedTextures(){
    using namespace NorthlightActorTexture;uint32_t random=12345;std::vector<uint8_t> current,reference;
    for(auto format:{Format::BC1,Format::BC2,Format::BC3})for(unsigned width:{1u,3u,4u,5u,31u,128u})for(unsigned height:{1u,3u,4u,9u,128u})for(unsigned mode=0;mode<6;++mode){
        const unsigned blockBytes=format==Format::BC1?8:16;const size_t pitch=((width+3)/4)*blockBytes+7;
        std::vector<uint8_t> input(pitch*((height+3)/4)+1);auto* bytes=input.data()+1;
        for(auto& v:input){random=random*1664525u+1013904223u;v=uint8_t(random>>24);}
        for(unsigned y=0;y<(height+3)/4;++y)for(unsigned x=0;x<(width+3)/4;++x){auto* block=bytes+y*pitch+x*blockBytes;auto* color=block+(format==Format::BC1?0:8);
            if(mode<4){const uint32_t selector=mode*0x55555555u;std::memcpy(color+4,&selector,4);}
            if(mode==4){const uint32_t endpointsOnly=0x11445051;std::memcpy(color+4,&endpointsOnly,4);}
            if(mode<5&&format==Format::BC3)std::memset(block+2,0,6);
        }
        assert(decode(bytes,input.size()-1,width,height,pitch,format,current));
        assert(CPUReferenceTexture::decode(bytes,input.size()-1,width,height,pitch,CPUReferenceTexture::Format(format),reference));assert(current==reference);
        const auto sums=[&](unsigned x,unsigned y,const std::array<unsigned,4>& sum){std::array<unsigned,4> expected{};for(unsigned dy=y;dy<std::min(y+4,height);++dy)for(unsigned dx=x;dx<std::min(x+4,width);++dx)for(unsigned c=0;c<4;++c)expected[c]+=reference[(size_t(dy)*width+dx)*4+c];assert(sum==expected);};
        if(format==Format::BC1)decodeCompressed<Format::BC1,true>(bytes,width,height,pitch,sums);
        else if(format==Format::BC2)decodeCompressed<Format::BC2,true>(bytes,width,height,pitch,sums);
        else decodeCompressed<Format::BC3,true>(bytes,width,height,pitch,sums);
    }
    std::puts("CPU_BC_PASS archived decoder oracle: uniform/mixed endpoint selectors, zero/random alpha, clipped blocks and block sums");
}
int main(){arrays();halfValues();packedTextures();compressedTextures();indexMap();}
