#define NOMINMAX
#include "position_bounds_scan.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

__attribute__((noinline)) static bool legacy(const void* data,size_t count,size_t stride,float* low,float* high){
    float a[3],b[3];for(unsigned k=0;k<3;++k){a[k]=FLT_MAX;b[k]=-FLT_MAX;}
    bool valid=true;
    for(size_t i=0;valid&&i<count;++i){float p[3];std::memcpy(p,static_cast<const unsigned char*>(data)+i*stride,12);
        for(unsigned k=0;k<3;++k){if(!std::isfinite(p[k])){valid=false;break;}a[k]=std::min(a[k],p[k]);b[k]=std::max(b[k],p[k]);}}
    std::memcpy(low,a,12);std::memcpy(high,b,12);return valid;
}
__attribute__((noinline)) static bool current(const void* data,size_t count,size_t stride,float* low,float* high){
    return NorthlightPositionBounds::scanPositions(data,count,stride,low,high);
}
static unsigned checks=0;
static void same(const void* data,size_t count,size_t stride){
    float low[3],high[3],a[3],b[3];
    const bool expected=legacy(data,count,stride,low,high),actual=current(data,count,stride,a,b);
    assert(actual==expected);if(actual){assert(!std::memcmp(low,a,12));assert(!std::memcmp(high,b,12));}++checks;
}
template<class F> static double bench(F scan,const std::vector<float>& vertices){
    float low[3],high[3];volatile unsigned valid=0;const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<200;++i){asm volatile("" : : "r"(vertices.data()) : "memory");valid+=scan(vertices.data(),vertices.size()/8,32,low,high);}
    assert(valid==200);
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(200*(vertices.size()/8));
}
int main(int argc,char**argv){
    same(nullptr,0,12);
    uint32_t seed=20261026;
    for(unsigned offset=0;offset<16;++offset)for(unsigned stride:{12u,13u,16u,24u,32u,48u,4096u})for(unsigned count:{1u,2u,3u,5u,17u,145u}){
        const auto bytes=offset+size_t(count-1)*stride+12;auto data=std::make_unique<unsigned char[]>(bytes);
        for(unsigned trial=0;trial<12;++trial){
            for(unsigned i=0;i<count;++i){uint32_t words[3];for(auto& word:words){seed=seed*1664525u+1013904223u;word=seed;}
                std::memcpy(data.get()+offset+size_t(i)*stride,words,12);}
            same(data.get()+offset,count,stride);
        }
    }
    // Every component, extremum position, sign of zero, subnormal, Inf and NaN.
    const uint32_t values[]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x00800000u,0x7f7fffffu,0xff7fffffu,
        0x7f800000u,0xff800000u,0x7fc01234u,0x7f801234u,0xffc01234u};
    for(uint32_t value:values)for(unsigned component=0;component<3;++component)for(unsigned vertex=0;vertex<5;++vertex){
        uint32_t words[15]={};words[vertex*3+component]=value;same(words,5,12);
    }
    for(uint32_t first:{0u,0x80000000u}){uint32_t words[]={first,first,first,first^0x80000000u,first^0x80000000u,first^0x80000000u};same(words,2,12);}
#if defined(__SSE2__)
    const auto originalControl=_mm_getcsr();
    for(unsigned flags:{0u,0x40u,0x8000u,0x8040u}){
        _mm_setcsr((originalControl&~0x8040u)|flags);
        for(uint32_t value:values)for(unsigned component=0;component<3;++component)for(unsigned vertex=0;vertex<5;++vertex){
            uint32_t words[15]={};words[vertex*3+component]=value;same(words,5,12);
        }
    }
    _mm_setcsr(originalControl);
#endif
#ifdef _WIN32
    SYSTEM_INFO system{};GetSystemInfo(&system);const size_t page=system.dwPageSize;
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*6,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages);
    DWORD previous=0;for(unsigned n:{1u,3u,5u})assert(VirtualProtect(pages+n*page,page,PAGE_NOACCESS,&previous));
    // Neither the final 12-byte vertex nor the 12-byte output arrays may overrun.
    for(unsigned count:{1u,2u,3u,17u})for(unsigned stride:{12u,13u,48u}){
        auto* data=pages+page-((count-1)*stride+12);float p[]={1,2,3};
        for(unsigned i=0;i<count;++i)std::memcpy(data+size_t(i)*stride,p,12);
        auto* low=reinterpret_cast<float*>(pages+page*3-12);auto* high=reinterpret_cast<float*>(pages+page*5-12);
        assert(current(data,count,stride,low,high)&&!std::memcmp(low,p,12)&&!std::memcmp(high,p,12));++checks;
    }
    assert(VirtualFree(pages,0,MEM_RELEASE));
#endif
    assert(argc==2);FILE* file=std::fopen(argv[1],"rb");assert(file);unsigned header[5];
    assert(std::fread(header,4,5,file)==5&&header[0]==0x32534746&&header[1]==2&&header[2]>0);
    std::vector<float> vertices(size_t(header[2])*8);assert(std::fread(vertices.data(),4,vertices.size(),file)==vertices.size());std::fclose(file);
    same(vertices.data(),header[2],32);
    std::vector<double> oldTimes,newTimes;
    for(unsigned round=0;round<9;++round){
        if(round&1){newTimes.push_back(bench(current,vertices));oldTimes.push_back(bench(legacy,vertices));}
        else{oldTimes.push_back(bench(legacy,vertices));newTimes.push_back(bench(current,vertices));}
    }
    std::sort(oldTimes.begin(),oldTimes.end());std::sort(newTimes.begin(),newTimes.end());
    std::printf("{\"pass\":true,\"exact_cases\":%u,\"MPQ_vertices\":%u,\"legacy_ns_per_vertex\":%.6f,\"current_ns_per_vertex\":%.6f}\n",checks,header[2],oldTimes[4],newTimes[4]);
}
