#define NOMINMAX
#include <d3d9.h>
#include "auto_normal_memory.h"
#include <cassert>
#include <cstdio>
int main(){
    using NorthlightAutoNormalMemory::mipBytes;
    using NorthlightAutoNormalMemory::budget;
    assert(budget(715ull<<20,96ull<<20)==(32ull<<20));
    assert(budget(895ull<<20,96ull<<20)==(32ull<<20));
    assert(budget(950ull<<20,32ull<<20)==(32ull<<20));
    assert(budget(950ull<<20,96ull<<20)==(96ull<<20));
    assert(budget(1024ull<<20,32ull<<20)==(96ull<<20));
    assert(mipBytes(D3DFMT_DXT1,1,1,1)==8);
    assert(mipBytes(D3DFMT_DXT5,5,7,1)==64);
    assert(mipBytes(D3DFMT_DXT1,4,4,0)==24);
    assert(mipBytes(D3DFMT_DXT5,512,512,0)==349552);
    assert(mipBytes(D3DFMT_A8R8G8B8,512,512,0)==1398100);
    assert(mipBytes(D3DFMT_A8R8G8B8,512,512,1)==1048576);
    assert(mipBytes(D3DFMT_A8R8G8B8,3,5,0)==72);
    assert(mipBytes(D3DFMT_A8R8G8B8,0,1,0)==0);
    assert(mipBytes(D3DFMT_A32B32G32R32F,2,2,1)==64);
    const auto dxt1=mipBytes(D3DFMT_DXT1,512,512,0)+mipBytes(D3DFMT_A8R8G8B8,512,512,0);
    const auto dxt5=mipBytes(D3DFMT_DXT5,512,512,0)+mipBytes(D3DFMT_A8R8G8B8,512,512,0);
    assert((32u<<20)/dxt1==21&&(32u<<20)/dxt5==19);
    // Full-size RGBA sources still fit exactly the original conservative capacity.
    assert((32u<<20)/(2*mipBytes(D3DFMT_A8R8G8B8,512,512,0))==12);
    std::puts("AUTO_NORMAL_MEMORY_PASS compressed_mips block_rounding partial_chains bounded_capacity");
}
