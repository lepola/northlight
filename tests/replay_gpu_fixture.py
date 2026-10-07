"""Shared native fixture for the replay GPU cache tests (test_replay_gpu_maintenance.py,
test_replay_gpu_diagnostics.py): fake D3D device/buffers with allocation, lock and unlock
fault injection, plus the Mesh/Bindings/sized/verify/gpuBind helpers. Not a test.

Moved verbatim from test_replay_gpu_capacity_fix.py (0.3.122), which was deleted:
its 2048-entry scan-memo policy was replaced by the 0.3.124 LRU list."""
import ast
from pathlib import Path
HERE=Path(__file__).resolve().parent
def literal(path,name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub').replace('struct IDirect3DDevice9{','struct IDirect3DDevice9{\nvirtual HRESULT CreateVertexBuffer(UINT,DWORD,UINT,unsigned,IDirect3DVertexBuffer9**,void*)=0;\nvirtual HRESULT CreateIndexBuffer(UINT,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9**,void*)=0;')
fixture=literal(HERE/'test_replay_gpu_cache.py','body').split('int main(){')[0]
fixture=fixture.replace('struct Buffer:T{','struct Buffer final:T{').replace('bool fail=false;','bool fail=false,unlockFail=false;').replace('HRESULT Unlock()override{return D3D_OK;}','HRESULT Unlock()override{return unlockFail?E_POINTER:D3D_OK;}').replace('unsigned calls=0,failAt=0;bool lockFail=false;','unsigned calls=0,failAt=0,failLockAt=0,failUnlockAt=0;bool lockFail=false;').replace('p->fail=lockFail;','p->fail=lockFail||calls==failLockAt;p->unlockFail=calls==failUnlockAt;')
# Mesh/Owner aliases, MiB, Bindings, sized(), verify() and gpuBind(); prepend fixture.
helpers=r'''
using Mesh=NorthlightDrawSnapshot::Mesh;
using Owner=std::shared_ptr<const Mesh>;
constexpr size_t MiB=1024u*1024u;
struct Bindings {
 IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;
 void clear(){for(auto& p:vb){if(p)p->Release();p=nullptr;}if(ib)ib->Release();ib=nullptr;}
 ~Bindings(){clear();}
};
static Owner sized(size_t n,unsigned salt=0){auto m=std::make_shared<Mesh>();m->streams[0].stride=4;m->streams[0].bytes.resize(n);for(size_t i=0;i<n;++i)m->streams[0].bytes[i]=static_cast<unsigned char>((i*17+salt)%251);return m;}
static void verify(const Owner& m,const Bindings& b,bool bound){
 for(unsigned i=0;i<4;++i){const auto& bytes=m->streams[i].bytes;
  if(bound){if(bytes.empty()){assert(!b.vb[i]);continue;}auto* buffer=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(b.vb[i]);assert(buffer&&buffer->data==bytes);}
  else{std::vector<unsigned char> fallback(bytes.size());if(!bytes.empty())std::memcpy(fallback.data(),bytes.data(),bytes.size());assert(fallback==bytes);}
 }
 if(bound){if(m->indices.empty())assert(!b.ib);else{auto* buffer=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(b.ib);assert(buffer&&buffer->data.size()==m->indices.size()*4&&!std::memcmp(buffer->data.data(),m->indices.data(),buffer->data.size()));}}
 else{auto fallback=m->indices;assert(fallback==m->indices);}
}
template<class C>static bool gpuBind(C& c,Device& d,const Owner& m,Bindings& b,bool check=true){const bool good=c.bind(&d,m,b.vb,b.ib,[](size_t){return true;});if(check)verify(m,b,good);return good;}
'''
