
#include "draw_snapshot.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightDrawSnapshot;
template<class Interface,class Desc>struct Buffer:Interface{
 unsigned refs=1,locks=0,unlocks=0;bool failLock=false,failUnlock=false;Desc desc;std::vector<std::uint8_t> bytes;
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDesc(Desc*out)override{*out=desc;out->Size=UINT(bytes.size());return 0;}
 HRESULT Lock(UINT offset,UINT size,void**out,DWORD flags)override{assert(flags==D3DLOCK_READONLY);if(failLock)return D3DERR_INVALIDCALL;assert(std::uint64_t(offset)+size<=bytes.size());++locks;*out=bytes.data()+offset;return 0;}
 HRESULT Unlock()override{++unlocks;return failUnlock?D3DERR_INVALIDCALL:0;}
};
struct Declaration:IDirect3DVertexDeclaration9{
 unsigned refs=1;std::vector<D3DVERTEXELEMENT9> e={{0,4,2,0,0,0},{1,0,4,0,1,0},{0xff,0,17,0,0,0}};
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDeclaration(D3DVERTEXELEMENT9*out,UINT*n)override{assert(*n>=e.size());*n=UINT(e.size());std::memcpy(out,e.data(),e.size()*sizeof(*out));return 0;}
};
struct Device:IDirect3DDevice9{
 Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> vb[4];Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> ib;Declaration decl;UINT offset[4]={8,4,0,0},stride[4]={24,8,0,0},freq[4]={1,1,1,1};
 Device(){for(unsigned s=0;s<2;++s){vb[s].desc.Usage=D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY;vb[s].bytes.resize(offset[s]+12*stride[s]);for(unsigned i=0;i<vb[s].bytes.size();++i)vb[s].bytes[i]=std::uint8_t(i);}indices({999,7,9,8});}
 void indices(std::initializer_list<std::uint32_t> in,bool wide=false){ib.desc.Format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;ib.desc.Usage=D3DUSAGE_DYNAMIC;ib.bytes.resize(in.size()*(wide?4:2));unsigned i=0;for(auto v:in){if(wide)std::memcpy(ib.bytes.data()+i*4,&v,4);else{std::uint16_t n=std::uint16_t(v);std::memcpy(ib.bytes.data()+i*2,&n,2);}++i;}}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return D3DERR_INVALIDCALL;}
 HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**out)override{*out=&decl;decl.AddRef();return 0;}
 HRESULT GetStreamSourceFreq(UINT s,UINT*out)override{*out=freq[s];return 0;}
 HRESULT GetStreamSource(UINT s,IDirect3DVertexBuffer9**out,UINT*o,UINT*st)override{*out=&vb[s];vb[s].AddRef();*o=offset[s];*st=stride[s];return 0;}
 HRESULT GetIndices(IDirect3DIndexBuffer9**out)override{*out=&ib;ib.AddRef();return 0;}
};

#include <map>
static std::map<void*,uint64_t> revisions;
static uint64_t identity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t version(void* p,bool){return revisions[p];}
static unsigned locks(Device& d){return d.vb[0].locks+d.vb[1].locks+d.ib.locks;}
int main(){
 Device d;d.indices({7,9,8});revisions[&d.vb[0]]=1;revisions[&d.vb[1]]=2;revisions[&d.ib]=3;
 Draw draw{D3DPT_TRIANGLELIST,0,0,12,0,1,true};Mesh scratch;Diagnostics why;std::shared_ptr<const Mesh> first,next;
 Frame probe;probe.setIdentityProvider(identity);probe.setVersionProvider(version);
 assert(probe.read(&d,&d.decl,draw,scratch,&why,false,&first));const auto charge=probe.bytesRead();assert(charge);
 Frame ordinary(charge,0);ordinary.setIdentityProvider(identity);ordinary.setVersionProvider(version);
 assert(ordinary.read(&d,&d.decl,draw,scratch,&why,false,&first));
 assert(!ordinary.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 Frame geometry(charge,0);geometry.setIdentityProvider(identity);geometry.setVersionProvider(version);geometry.setChargeResidentHits(false);
 assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&first));const auto before=locks(d);const auto oldBytes=first->streams[0].bytes;
 for(unsigned i=0;i<5000;++i){assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&next));assert(next==first);}
 assert(geometry.bytesRead()==charge&&geometry.trackedHits()==5000&&locks(d)==before);
 // A new generation is still charged and must wait for the next read-budget window.
 d.vb[0].bytes[d.offset[0]+7*d.stride[0]+1]^=0x55;revisions[&d.vb[0]]=4;
 assert(!geometry.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 geometry.clearFrame();assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&next));assert(next!=first&&next->streams[0].bytes!=oldBytes&&first->streams[0].bytes==oldBytes);
 // Unknown static versions still use normal revalidation, admission and read charge.
 revisions.clear();d.vb[0].desc.Usage=d.vb[1].desc.Usage=d.ib.desc.Usage=0;
 Frame untracked(charge,0);untracked.setIdentityProvider(identity);untracked.setVersionProvider(version);untracked.setChargeResidentHits(false);
 assert(untracked.read(&d,&d.decl,draw,scratch,&why,false,&next));
 assert(!untracked.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 puts("SNAPSHOT_GEOMETRY_POLICY_PASS default_budget tracked_5000_hits_zero_reads admission_uncharged generation_miss_budget immutable_old unknown_static_charged");
}
