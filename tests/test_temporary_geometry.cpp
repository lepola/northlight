#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>
#include "memory_footprint.h"
static int ioFailure=-1,allocationFailure=-1,ioCalls=0,openFiles=0;
static bool failIO(){++ioCalls;if(ioFailure<0)return false;if(ioFailure--==0){ioFailure=-1;return true;}return false;}
void* operator new(size_t size){if(allocationFailure>=0&&allocationFailure--==0){allocationFailure=-1;throw std::bad_alloc();}if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
namespace std {
static FILE* geometryTestTmpfile(){if(failIO())return nullptr;auto* file=::tmpfile();if(file)++openFiles;return file;}
static int geometryTestFclose(FILE* file){--openFiles;return ::fclose(file);}
static int geometryTestFseek(FILE* file,long offset,int origin){return failIO()? -1 : ::fseek(file,offset,origin);}
static size_t geometryTestFread(void* data,size_t size,size_t count,FILE* file){return failIO()? 0 : ::fread(data,size,count,file);}
static size_t geometryTestFwrite(const void* data,size_t size,size_t count,FILE* file){return failIO()? 0 : ::fwrite(data,size,count,file);}
static int geometryTestFflush(FILE* file){return failIO()? EOF : ::fflush(file);}
}
#define tmpfile geometryTestTmpfile
#define fclose geometryTestFclose
#define fseek geometryTestFseek
#define fread geometryTestFread
#define fwrite geometryTestFwrite
#define fflush geometryTestFflush
#include "temporary_geometry.h"
#undef tmpfile
#undef fclose
#undef fseek
#undef fread
#undef fwrite
#undef fflush
#ifdef _WIN32
static int nativeFailure=-1,nativeLive=0;
static void* geometryTestAlloc(void* address,size_t bytes,DWORD type,DWORD protect){if(nativeFailure>=0&&nativeFailure--==0)return nullptr;void* data=::VirtualAlloc(address,bytes,type,protect);if(data)++nativeLive;return data;}
static BOOL geometryTestFree(void* address,size_t bytes,DWORD type){const auto ok=::VirtualFree(address,bytes,type);if(ok)--nativeLive;return ok;}
#define VirtualAlloc geometryTestAlloc
#define VirtualFree geometryTestFree
#endif
#include "world_mesh_pages.h"
#ifdef _WIN32
#undef VirtualAlloc
#undef VirtualFree
#endif
using namespace NorthlightGI;
static bool candidate(std::vector<uint32_t>& published){
    try{
        TemporaryGeometryArray<uint32_t> values;
        constexpr unsigned Count=100000;
        for(unsigned i=0;i<Count;++i)values.push_back(i*17+3);
        for(unsigned i=0;i<Count;i+=97)values[i]^=0x12345678;
        for(unsigned i=0;i<Count;++i)assert(values[i]==((i*17+3)^(i%97?0:0x12345678)));
        values.flush();assert(values.bytes()<=300000);
        std::vector<uint32_t> next={values[0],values[50000],values[Count-1]};published.swap(next);return true;
    }catch(const std::exception&){return false;}
}
int main(){
#ifdef _WIN32
    unsigned nativeRefused=0;
    for(int failure=-1;failure<3;++failure){nativeFailure=failure;
        try{AdaptiveGeometryArray<uint32_t> values;values.chunks(nullptr);values.resize(values.blockSize()*3);for(size_t i=0;i<values.size();i+=991)values[i]=uint32_t(i);auto copy=values;for(size_t i=0;i<copy.size();i+=991)assert(copy[i]==i);values.free();}
        catch(const std::bad_alloc&){++nativeRefused;}
        nativeFailure=-1;assert(!nativeLive);
    }
    assert(nativeRefused==3);std::puts("NATIVE_GEOMETRY_BLOCK_PASS allocationRefusals=3 copy=exact immediateCleanup=1");
#endif
    std::vector<uint32_t> previous={7,11,19},published=previous;
    assert(candidate(published)&&published!=previous&&!openFiles);const auto operations=ioCalls;
    unsigned refused=0;
    for(int failure=0;failure<operations;++failure){published=previous;ioFailure=failure;const bool ok=candidate(published);ioFailure=-1;if(!ok){++refused;assert(published==previous);}assert(!openFiles);}
    unsigned allocationRefused=0;
    for(int failure=0;failure<12;++failure){published=previous;allocationFailure=failure;const bool ok=candidate(published);allocationFailure=-1;if(!ok){++allocationRefused;assert(published==previous);}assert(!openFiles);}
    assert(refused&&allocationRefused);
    {TemporaryGeometryFile file;uint32_t value=123;const auto offset=file.append(&value,sizeof value);file.flush();uint32_t read=0;file.read(offset,&read,sizeof read);assert(read==value);
     for(unsigned mode=0;mode<2;++mode){bool failed=false;try{if(mode)file.write(UINT64_MAX,&value,4);else file.read(1,&read,4);}catch(const std::runtime_error&){failed=true;}assert(failed);}}
    {
        NorthlightWorldMeshPages::Plan plan;NorthlightWorldMeshPages::Page page;
        const uint32_t expected[]={17,29,41,53};page.key.vertexBytes=sizeof expected;page.key.indexBytes=0;page.spooled=true;
        plan.temporary=std::make_shared<TemporaryGeometryFile>();page.fileOffset=plan.temporary->append(expected,sizeof expected);plan.temporary->flush();
        uint32_t actual[4]={};assert(plan.copy(page,true,0,sizeof actual,actual));assert(!std::memcmp(expected,actual,sizeof actual));
        assert(!plan.copy(page,true,1,sizeof actual,actual));assert(!plan.copy(page,true,0,1,nullptr));
        for(int failure=0;failure<2;++failure){ioFailure=failure;assert(!plan.copy(page,true,0,sizeof actual,actual));ioFailure=-1;}
        page.fileOffset=UINT64_MAX;assert(!plan.copy(page,true,0,1,actual));
        page.spooled=false;assert(!plan.copy(page,true,0,1,actual));
    }
    unsigned sealIORefused=0;
    for(int failure=-1;failure<8;++failure){
        NorthlightGI::WorldScene source;source.vertices.resize(3);source.triangles.push_back({0,1,2,0});source.materials.resize(1);
        NorthlightWorldMesh::WorldMeshUploadPlan input;input.source=&source;input.vertexCount=3;input.triangleCount=1;input.vertexBytes=96;input.indexBytes=12;input.indices={0,1,2};input.materials.resize(1);input.materials[0].width=input.materials[0].height=2;input.materials[0].bgra={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        NorthlightWorldMesh::Batch batch;batch.count=1;input.batches.push_back(batch);
        const auto expected=input.materials[0].bgra;std::string error;auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();pages->temporary=std::make_shared<TemporaryGeometryFile>();NorthlightWorldMeshPages::Page page;page.spooled=true;page.key.vertexBytes=96;page.key.indexBytes=6;
        page.fileOffset=pages->temporary->append(source.vertices.data(),96);const uint16_t indices[]={0,1,2};pages->temporary->append(indices,6);pages->temporary->flush();pages->pages.push_back(page);pages->vertexBytes=96;pages->indexBytes=6;
        NorthlightWorldMeshPages::Batch paged;static_cast<NorthlightWorldMesh::Batch&>(paged)=batch;paged.vertexCount=3;pages->batches.push_back(paged);
        ioFailure=failure;const bool sealed=NorthlightWorldMeshPages::seal(input,pages,error);ioFailure=-1;
        if(!sealed){++sealIORefused;assert(!input.pagesSealed&&!input.payload.live()&&input.source==&source&&input.materials[0].bgra==expected);continue;}
        auto payload=input.payload.share();const auto& material=payload->materials[0];uint8_t bytes[16]={};
        assert(material.spooled&&material.bgra.empty()&&pages->copyMaterial(material,0,16,bytes)&&!std::memcmp(bytes,expected.data(),16));
        for(int failRead=0;failRead<2;++failRead){ioFailure=failRead;assert(!pages->copyMaterial(material,0,16,bytes));ioFailure=-1;}
        assert(!pages->copyMaterial(material,1,16,bytes)&&!pages->copyMaterial(material,0,16,nullptr));
    }
    assert(sealIORefused&&!openFiles);std::printf("SPOOLED_MATERIAL_PASS sealIoRefusals=%u exactBytes=1 rollback=1 checkedReads=1\n",sealIORefused);
    assert(!openFiles);std::printf("TEMPORARY_GEOMETRY_PASS ioSites=%d ioRefusals=%u allocationRefusals=%u previousGenerationPreserved=1 boundedCacheKiB=256 cleanup=1\n",operations,refused,allocationRefused);
}
