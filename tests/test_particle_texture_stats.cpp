// northlight-test: requires=cxx
#include "particle_texture_stats.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "texture_reset_cases.h"
#include <cstdio>
#include <limits>
using namespace NorthlightParticleTextures;
static const void* key(unsigned n){return reinterpret_cast<const void*>(std::uintptr_t(n));}
static void close(float a,float b){assert(std::fabs(a-b)<.00001f);}
int main(){
    Cache c;View v;float rgba[4]{};const std::uint8_t atlas[]={0,0,255,255,255,0,0,0};
    c.upload(key(1),key(10),2,1,0,21,atlas,sizeof atlas,8,true,true);assert(c.bytes()==0&&!c.counts.uploads&&!c.lookup(key(1),v));
    enabled.store(true);
    assert(!c.lookup(key(1),v));
    c.upload(key(1),key(10),2,1,0,21,atlas,sizeof atlas,8,true,true);assert(c.counts.retained==1&&!c.counts.decoded); // kept as uploaded, not decoded
    assert(c.lookup(key(1),v)&&c.counts.decoded==1);assert(c.lookup(key(1),v)&&c.counts.decoded==1); // decoded once, at the first lookup
    const auto life=v.lifetime,revision=v.revision,hash=v.rgbaHash;
    assert(v.sample(.25f,.5f,false,3,3,rgba));close(rgba[0],1);close(rgba[2],0);close(rgba[3],1);
    assert(v.sample(.75f,.5f,false,3,3,rgba));close(rgba[0],0);close(rgba[2],1);close(rgba[3],0); // Atlas cell stays transparent and blue, never an average white.
    assert(v.sample(-.25f,.5f,false,1,3,rgba));close(rgba[2],1);
    assert(v.sample(-.25f,.5f,false,3,3,rgba));close(rgba[0],1);
    assert(v.sample(1.25f,.5f,false,2,3,rgba));close(rgba[2],1);
    assert(v.sample(.5f,.5f,false,3,3,rgba));close(rgba[0],.5f);close(rgba[2],.5f);close(rgba[3],.5f);
    assert(v.sample(.75f,.5f,false,3,3,rgba,false));close(rgba[2],1);close(rgba[3],0);
    assert(!v.sample(.25f,.5f,false,4,3,rgba));assert(!v.sample(NAN,.5f,false,1,3,rgba));
    const std::uint8_t gray[]={128,128,128,128};c.upload(key(1),key(10),1,1,1,21,gray,4,4,true,true);
    assert(c.lookup(key(1),v)&&v.rgbaHash==hash&&v.revision>revision); // Other full mips leave the selected mip intact.
    c.upload(key(1),key(10),1,1,0,21,gray,4,4,true,true);assert(c.lookup(key(1),v)&&v.lifetime==life);
    assert(v.sample(.5f,.5f,true,3,3,rgba));close(rgba[0],.2158605f);close(rgba[3],128.f/255);
    c.upload(key(1),key(10),1,1,0,21,gray,4,4,false,true);assert(!c.lookup(key(1),v)&&c.counts.partial==1);
    c.upload(key(1),key(10),1,1,0,21,gray,4,4,true,true);assert(c.lookup(key(1),v)&&v.lifetime==life);
    c.upload(key(1),key(10),1,1,3,21,gray,4,4,true,false);assert(!c.lookup(key(1),v)); // Failed writes on any level invalidate.
    c.upload(key(1),key(20),1,1,0,21,gray,4,4,true,true);assert(c.lookup(key(1),v)&&v.lifetime!=life);
    c.erase(key(1),key(10));assert(c.lookup(key(1),v));c.erase(key(1),key(20));assert(!c.lookup(key(1),v));
    c.upload(key(1),key(20),1,1,0,21,gray,3,4,true,true);assert(!c.lookup(key(1),v));
    c.upload(key(1),key(20),1,1,0,50,gray,4,4,true,true);assert(!c.lookup(key(1),v));
    const std::uint8_t bc1[]={0,248,0,0,0,0,0,0};
    c.upload(key(2),key(20),4,4,0,0x31545844,bc1,8,8,true,true);assert(c.lookup(key(2),v));assert(v.sample(.5f,.5f,false,3,3,rgba));close(rgba[0],1);close(rgba[1],0);close(rgba[3],1);
    const std::uint8_t bc3[]={0,255,0,0,0,0,0,0,0,248,0,0,0,0,0,0};
    c.upload(key(3),key(20),4,4,0,0x35545844,bc3,16,16,true,true);assert(c.lookup(key(3),v));assert(v.sample(.5f,.5f,false,3,3,rgba));close(rgba[0],1);close(rgba[3],0);
    c.clear();assert(c.bytes()==0&&c.size()==0);
    std::vector<std::uint8_t> large(32*32*4,255);const unsigned uploads=unsigned(Cache::PixelBudget/large.size())+600;
    for(unsigned i=1;i<=uploads;++i){c.upload(key(i),key(i+100000),32,32,0,21,large.data(),large.size(),128,true,true);assert(c.bytes()<=Cache::PixelBudget);assert(c.size()<=Cache::MaxEntries);}
    assert(c.counts.evictions>0&&c.size()<=Cache::PixelBudget/large.size()&&c.size()>=(Cache::PixelBudget-Cache::PixelBudget/8)/large.size());
    assert(!c.lookup(key(1),v)&&c.lookup(key(uploads),v));
    c.lookup(key(1),v);assert(c.lastMiss()==Evicted); // an evicted texture is reported as such
    c.upload(key(uploads+1),key(uploads+1),64,32,0,21,large.data(),large.size(),256,true,true);assert(!c.lookup(key(uploads+1),v)&&c.lastMiss()==Large);
    c.invalidate(key(uploads));assert(!c.lookup(key(uploads),v));
    enabled.store(false);assert(!c.lookup(key(uploads-1),v));c.upload(key(uploads-1),key(uploads-1+100000),32,32,0,21,large.data(),large.size(),128,true,true);enabled.store(true);assert(!c.lookup(key(uploads-1),v));
    c.clear();assert(!c.bytes());
    TextureResetCases::run();
    std::puts("particle textures: decode on first lookup, actual atlas UV/alpha, wrap/mirror/clamp, point/bilinear, sRGB alpha parity, native BC decode, lifetime/revision, mip order, partial/failed/GPU invalidation, bounded eviction, pool-aware reset/retry/recreation PASS");
}
