#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include "forwarders.h"
static void logf(const char*,...){}
#include "position_bounds.h"
struct Buffer:ForwardIDirect3DVertexBuffer9 {
    unsigned refs=1,locks=0;uint64_t identity=1,revision=1;bool fail=false,badUnlock=false,mutate=false;
    std::vector<unsigned char> data=std::vector<unsigned char>(4096);
    Buffer():ForwardIDirect3DVertexBuffer9(nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* out)override{*out={};out->Size=UINT(data.size());out->Usage=D3DUSAGE_DYNAMIC;out->Pool=D3DPOOL_DEFAULT;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT begin,UINT bytes,void** out,DWORD)override{++locks;if(fail)return E_FAIL;assert(uint64_t(begin)+bytes<=data.size());*out=data.data()+begin;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Unlock()override{if(mutate)++revision;return badUnlock?E_FAIL:D3D_OK;}
    void position(unsigned i,float x,float y,float z,UINT offset=20,UINT stride=48){float p[]={x,y,z};std::memcpy(data.data()+offset+size_t(i)*stride,p,12);}
};
struct Device:ForwardIDirect3DDevice9 {
    Buffer vb;UINT offset=16,stride=48,frequency=1;unsigned indexReads=0;
    Device():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT s,IDirect3DVertexBuffer9** out,UINT* o,UINT* st)override{assert(s==0);*out=&vb;vb.AddRef();*o=offset;*st=stride;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT s,UINT* out)override{assert(s==0);*out=frequency;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9**)override{++indexReads;return E_FAIL;}
};
static void metadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* output,unsigned count){
    for(unsigned i=0;i<count;++i){assert(!requests[i].index);auto& b=*static_cast<Buffer*>(requests[i].buffer);output[i]={b.identity,b.revision,UINT(b.data.size()),D3DUSAGE_DYNAMIC,D3DFMT_UNKNOWN,true};}
}
int main(){
    Device d;NorthlightPositionBounds::Cache cache;cache.tracking(nullptr,nullptr,metadata);
    NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,-3,7,3,999,1,true};
    for(unsigned i=0;i<80;++i)d.vb.position(i,float(i),-float(i),60+float(i));
    float low[3],high[3];auto read=[&]{return cache.read(&d,draw,4,low,high);};
    assert(read()&&low[0]==4&&high[0]==6&&low[2]==64&&high[2]==66&&cache.bytesRead()==36);
    assert(cache.maintenance().coldScans==1&&!cache.maintenance().revisionScans&&!cache.maintenance().evictions&&!cache.maintenance().budgetRejected);
    const auto locks=d.vb.locks;for(unsigned i=0;i<100;++i)assert(read());assert(d.vb.locks==locks&&cache.cacheHits()==100);
    // Changed index ranges/topology refer to subsets of the declared range:
    // their conservative proof requires neither indices nor another read.
    draw.start=0;draw.topology=D3DPT_TRIANGLESTRIP;draw.primitives=2;assert(read()&&cache.cacheHits()==101);
    d.vb.position(5,5,-5,2);++d.vb.revision;assert(read()&&low[2]==2&&high[2]==66&&d.vb.locks==locks+1);
    assert(cache.maintenance().revisionScans==1&&cache.maintenance().coldScans==1);
    draw.minimum=8;assert(read()&&low[0]==5&&high[0]==7);draw.minimum=7;
    draw.base=-2;assert(read()&&low[0]==5);draw.base=-3;
    d.offset+=48;assert(read()&&low[0]==5);d.offset-=48;
    draw.vertices=4;assert(read()&&high[0]==7);draw.vertices=3;
    ++d.vb.identity;d.vb.position(4,4,-4,1);assert(read()&&low[2]==1); // Same COM address, new lifetime.
    cache.beginFrame();assert(read()&&cache.bytesRead()==0&&cache.cacheHits()==1);
    assert(!cache.maintenance().coldScans&&!cache.maintenance().revisionScans&&!cache.maintenance().evictions&&!cache.maintenance().budgetRejected);
    ++d.vb.revision;d.vb.mutate=true;assert(!read());d.vb.mutate=false;assert(read());
    ++d.vb.revision;d.vb.position(4,4,-4,std::numeric_limits<float>::quiet_NaN());assert(!read());
    d.vb.position(4,4,-4,60);++d.vb.revision;d.vb.fail=true;assert(!read());d.vb.fail=false;d.vb.badUnlock=true;assert(!read());d.vb.badUnlock=false;assert(read());
    ++d.vb.revision;d.vb.revision=0;assert(!read());d.vb.revision=100;
    d.frequency=2;assert(!read());d.frequency=1;d.stride=4;assert(!read());d.stride=48;
    draw.base=-8;assert(!read());draw.base=-3;draw.vertices=UINT(-1);assert(!read());draw.vertices=3;
    d.offset=UINT(-1);assert(!read());d.offset=16;
    cache.clear();for(unsigned i=0;i<5000;++i){++d.vb.identity;assert(read());}assert(cache.count()==4096&&d.vb.refs==1&&!d.indexReads);
    assert(cache.maintenance().coldScans==5000&&cache.maintenance().evictions==904&&!cache.maintenance().revisionScans);
    // Nonindexed bounds include exactly the vertex count implied by topology.
    draw={D3DPT_TRIANGLELIST,0,0,0,4,1,false};assert(read()&&low[0]==4&&high[0]==6);
    draw.topology=D3DPT_TRIANGLESTRIP;draw.primitives=2;assert(read()&&high[0]==7);
    d.vb.data.resize(4u<<20);draw={D3DPT_TRIANGLELIST,0,0,60000,0,1,true};
    for(unsigned i=0;i<60000;++i)d.vb.position(i,1,2,3);
    cache.clear();for(unsigned i=0;i<2;++i){++d.vb.revision;assert(read());}++d.vb.revision;assert(!read()&&cache.bytesRead()==1440000&&cache.maintenance().budgetRejected==1);cache.beginFrame();assert(read()&&!cache.maintenance().budgetRejected);
    cache.tracking(nullptr,nullptr,nullptr);assert(!read()&&cache.count()==0);
    std::puts("POSITION_BOUNDS_PASS conservative_declared_range no_indices no_mesh_copy current_revision address_reuse mutation nonfinite budget cap reset failures");
}
