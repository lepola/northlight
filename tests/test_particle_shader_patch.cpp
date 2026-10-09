// Runs src/proxy/particle_shader_patch.h over a batch of programs for test_particle_shader_patch.py
// (cross-check with the Python patch()) and exercises the variant cache over a mock device.
// Stream, in and out: per program a u32 kind, a u32 word count, then the words; the answer is a u32
// reason and a u32 word count (0 when rejected), then the words. No D3D, game or Wine.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
typedef unsigned UINT;typedef long HRESULT;
#define FAILED(h) ((h)<0)
#include "particle_shader_patch.h"

namespace PP=NorthlightParticleShaderPatch;
struct MockShader {
    std::vector<std::uint32_t> code;bool noFunction=false;int refs=1;
    HRESULT GetFunction(void* out,UINT* size){
        if(noFunction)return -1;
        if(!out){*size=UINT(code.size()*4);return 0;}
        if(*size<code.size()*4)return -1;
        std::memcpy(out,code.data(),code.size()*4);return 0;
    }
    unsigned long Release(){return unsigned(--refs);}
};
struct MockDevice {
    std::vector<std::unique_ptr<MockShader>> created;bool failCreate=false;
    HRESULT CreatePixelShader(const std::uint32_t* code,MockShader** out){
        if(failCreate)return -1;
        std::size_t n=0;while(code[n]!=0xffffu)++n;++n; /* up to and including END */
        created.emplace_back(new MockShader());created.back()->code.assign(code,code+n);*out=created.back().get();return 0;
    }
};
typedef PP::Cache<MockDevice,MockShader> MockCache;
// The cache over the first accepted program and the first rejected one: built once per (shader, kind), a rejection is remembered, forget/clear release.
static int cacheSelfTest(const std::vector<std::uint32_t>& good,const std::vector<std::uint32_t>& bad){
    MockDevice dev;MockShader a,b,c;a.code=good;b.code=bad;c.code=good;c.noFunction=true;
    MockCache cache;
    auto r0=cache.get(&dev,&a,0);
    if(r0.reason!=PP::Ok||!r0.shader||!r0.log||dev.created.size()!=1||cache.size()!=1)return 1;
    auto r0b=cache.get(&dev,&a,0);
    if(r0b.shader!=r0.shader||r0b.log||dev.created.size()!=1)return 2; /* the second draw: the same shader, no log, no second build */
    auto r1=cache.get(&dev,&a,2);
    if(r1.reason!=PP::Ok||r1.shader==r0.shader||!r1.log||dev.created.size()!=2||cache.size()!=1||cache.variants()!=2)return 3;
    auto rb=cache.get(&dev,&b,0);
    if(rb.reason==PP::Ok||rb.shader||!rb.log||dev.created.size()!=2)return 4;
    auto rbb=cache.get(&dev,&b,0);
    if(rbb.reason!=rb.reason||rbb.log||dev.created.size()!=2)return 5; /* the rejection is cached */
    auto rc=cache.get(&dev,&c,0);
    if(rc.reason!=PP::NoBytecode||rc.shader)return 6;
    MockShader d;d.code=good; /* the same bytecode at another address: built again, logged once per hash and kind */
    auto rd=cache.get(&dev,&d,0);
    if(rd.reason!=PP::Ok||rd.log||dev.created.size()!=3||rd.hash!=r0.hash)return 7;
    cache.forget(&a); /* the game freed a and registered a new shader at its address */
    if(cache.size()!=3||dev.created[0]->refs!=0||dev.created[1]->refs!=0||dev.created[2]->refs!=1)return 8;
    dev.failCreate=true;MockShader e;e.code=good;
    auto re=cache.get(&dev,&e,1);
    if(re.reason!=PP::CreateFailed||re.shader)return 9;
    dev.failCreate=false;
    cache.clear();
    if(cache.size()!=0||dev.created[2]->refs!=0)return 10;
    auto rk=cache.get(&dev,&a,3);
    if(rk.reason!=PP::UnknownKind||rk.shader||cache.size()!=0)return 11;
    std::vector<std::unique_ptr<MockShader>> many;
    for(unsigned i=0;i<MockCache::kMaxShaders+4;++i){many.emplace_back(new MockShader());many.back()->code=good;auto rr=cache.get(&dev,many.back().get(),0);if(i>=MockCache::kMaxShaders&&rr.reason!=PP::CacheFull)return 12;}
    return 0;
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    FILE* in=std::fopen(argv[1],"rb");FILE* out=std::fopen(argv[2],"wb");if(!in||!out)return 3;
    unsigned programs=0,accepted=0;std::uint32_t kind=0,count=0;
    std::vector<std::uint32_t> firstGood,firstBad;
    while(std::fread(&kind,4,1,in)==1&&std::fread(&count,4,1,in)==1){
        std::vector<std::uint32_t> words(count),patched;
        if(count&&std::fread(words.data(),4,count,in)!=count)return 4;
        const PP::Reason reason=PP::patch(words.data(),words.size(),kind,patched);
        const std::uint32_t n=reason==PP::Ok?std::uint32_t(patched.size()):0,rc=reason;
        std::fwrite(&rc,4,1,out);std::fwrite(&n,4,1,out);if(n)std::fwrite(patched.data(),4,n,out);
        ++programs;if(reason==PP::Ok){++accepted;if(firstGood.empty()&&kind==0)firstGood=words;}else if(firstBad.empty()&&reason!=PP::Empty&&reason!=PP::UnknownKind)firstBad=words;
    }
    std::fclose(in);std::fclose(out);
    const int cache=firstGood.empty()||firstBad.empty()?9999:cacheSelfTest(firstGood,firstBad);
    std::printf("particle_shader_patch: %u programs, %u accepted, cache self test %d\n",programs,accepted,cache);
    return cache;
}
