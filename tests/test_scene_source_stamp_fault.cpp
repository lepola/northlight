#include "scene_source_cache.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <thread>
using namespace NorthlightGI;
static void u32(FILE* f,uint32_t n){assert(std::fwrite(&n,4,1,f)==1);}
static void scalar(FILE* f,float n){assert(std::fwrite(&n,4,1,f)==1);}
static void write(const std::filesystem::path& path,float x){
    FILE* f=std::fopen(path.string().c_str(),"wb");assert(f);
    assert(std::fwrite("FGS2",4,1,f)==1);for(uint32_t n:{2u,3u,1u,1u})u32(f,n);
    for(auto p:{Vec3{x,0,0},Vec3{x+1,0,0},Vec3{x,1,0}}){
        for(float n:{p.x,p.y,p.z,0.f,0.f,1.f,0.f,0.f})scalar(f,n);
    }
    for(uint32_t n:{0u,1u,2u,0u})u32(f,n);
    for(float n:{.4f,.5f,.6f})scalar(f,n);u32(f,1);u32(f,1);scalar(f,.5f);u32(f,4);u32(f,0xffffffffu);
    std::fclose(f);
}
#include <cstdlib>
#include <new>
static int failAt=-1,allocations=0,failMode=0;void* operator new(std::size_t n){if(failAt>=0&&(failMode?allocations++>=failAt:allocations++==failAt))throw std::bad_alloc();if(void*p=std::malloc(n))return p;throw std::bad_alloc();}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,std::size_t)noexcept{std::free(p);}
int main(int argc,char**argv){assert(argc==2);namespace fs=std::filesystem;fs::path root=fs::absolute(argv[1]);fs::create_directories(root);auto path=root/"a-long-file-name-for-lease-failure-check.fgs";write(path,1);for(int mode=0;mode<2;++mode)for(int f=0;f<32;++f){SceneSourceCache cache;std::string e;auto held=cache.get(path.string(),e);assert(held&&held->sourceIdentity);auto token=held->sourceIdentity;auto name=path.string();cache.trimPayloads();bool threw=false;std::shared_ptr<const WorldScene> result;allocations=0;failMode=mode;failAt=f;try{result=cache.get(name,e);}catch(...){threw=true;}failAt=-1;assert(held->vertices[0].position.x==1);if(result)assert(result->vertices[0].position.x==1&&result->sourceIdentity==token);assert(cache.liveBytes()<=8u*1024u*1024u);e.clear();auto recovered=cache.get(name,e);assert(recovered&&recovered->sourceIdentity==token&&e.empty());cache.reset();assert(cache.liveBytes()>0);held.reset();result.reset();recovered.reset();assert(cache.liveBytes()==0&&cache.decodedLiveBytes()==0);(void)threw;}
    // The extra snapshot allocation must not turn a missing source into an
    // exception when the original path conversion succeeded under persistent OOM.
    {SceneSourceCache cache;const auto missing=(root/"missing-long-file-name-for-source-cache.fgs").string();
     allocations=0;failMode=1;failAt=2;bool threw=false;uint64_t id=0;
     try{id=cache.findIdentity(missing);}catch(...){threw=true;}failAt=-1;assert(!threw&&!id);}
    std::puts("PASS source stamp allocation failures, lease recovery and missing path");}
