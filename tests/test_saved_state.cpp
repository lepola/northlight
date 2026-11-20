// Native mock of the exact production pool and SavedState implementation.
// No graphics API/device is created.
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
using HRESULT=int;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
constexpr int D3DSBT_ALL=1;
struct D3DVIEWPORT9 { unsigned X=0,Y=0,Width=0,Height=0;float MinZ=0,MaxZ=1; };
struct IDirect3DSurface9 {
    int refs=1;
    void AddRef(){++refs;}
    void Release(){assert(refs>1);--refs;}
};
struct IDirect3DStateBlock9;
struct IDirect3DDevice9 {
    int refs=1,creates=0,captures=0,applies=0,alive=0,state=17;
    bool failCreate=false,failCapture=false,failViewport=false,failApply=false;
    int failChange=-1;
    std::array<IDirect3DSurface9*,4> targets{};
    IDirect3DSurface9* depth=nullptr;
    IDirect3DSurface9* boundResource=nullptr;
    D3DVIEWPORT9 viewport{3,4,800,600,.1f,.9f};
    std::vector<int> changes;
    HRESULT CreateStateBlock(int,IDirect3DStateBlock9**);
    HRESULT GetRenderTarget(int i,IDirect3DSurface9** out){*out=targets[i];if(*out)(*out)->AddRef();return *out?0:-1;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** out){*out=depth;if(*out)(*out)->AddRef();return *out?0:-1;}
    HRESULT GetViewport(D3DVIEWPORT9* out){if(failViewport)return -1;*out=viewport;return 0;}
    HRESULT SetRenderTarget(int i,IDirect3DSurface9* value){targets[i]=value;changes.push_back(10+i);return failChange==10+i?-1:0;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* value){depth=value;changes.push_back(value?21:20);return failChange==(value?21:20)?-1:0;}
    HRESULT SetViewport(const D3DVIEWPORT9* v){viewport=*v;changes.push_back(30);return failChange==30?-1:0;}
};
struct IDirect3DStateBlock9 {
    IDirect3DDevice9* d;int value;IDirect3DSurface9* retained;
    explicit IDirect3DStateBlock9(IDirect3DDevice9* p):d(p),value(p->state),retained(p->boundResource){++d->refs;++d->alive;if(retained)retained->AddRef();}
    HRESULT Capture(){++d->captures;if(d->failCapture)return -1;value=d->state;if(retained)retained->Release();retained=d->boundResource;if(retained)retained->AddRef();return 0;}
    HRESULT Apply(){++d->applies;d->changes.push_back(40);if(d->failApply)return -1;d->state=value;d->boundResource=retained;return 0;}
    void Release(){if(retained)retained->Release();--d->refs;--d->alive;delete this;}
};
HRESULT IDirect3DDevice9::CreateStateBlock(int kind,IDirect3DStateBlock9** out){assert(kind==D3DSBT_ALL);++creates;if(failCreate){*out=nullptr;return -1;}*out=new IDirect3DStateBlock9(this);return 0;}
#include "saved_state.h"
struct Fixture {
    IDirect3DSurface9 surface[6];IDirect3DDevice9 d;
    Fixture(){for(int i=0;i<4;++i)d.targets[i]=surface+i;d.depth=surface+4;d.boundResource=surface+5;}
    ~Fixture(){assert(d.refs==1&&d.alive==0);for(auto& s:surface)assert(s.refs==1);}
};
static void mutate(Fixture& f,int state){f.d.state=state;f.d.targets={f.surface+5,nullptr,nullptr,nullptr};f.d.depth=nullptr;f.d.viewport={0,0,7,9,0,1};}
int main(){
    {Fixture f;{SavedState s(&f.d);assert(s.ok);mutate(f,99);}assert(f.d.state==17&&f.d.targets[3]==f.surface+3&&f.d.depth==f.surface+4);assert(f.d.viewport.X==3&&f.d.viewport.Width==800);assert(f.d.creates==1&&f.d.captures==0&&f.d.alive==0);
        const std::vector<int> expected={20,11,12,13,10,11,12,13,21,40,30};assert(f.d.changes==expected);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);
        for(int i=0;i<10000;++i){f.d.state=i;{SavedState s(&f.d,&pool);assert(s.ok);f.d.state=-7;}assert(f.d.state==i);}
        assert(f.d.creates==1&&f.d.captures==9999&&f.d.alive==1&&f.d.refs==2);pool.clear();assert(f.d.alive==0&&f.d.refs==1);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);std::vector<SavedState*> nesting;
        for(int i=0;i<7;++i){f.d.state=i;nesting.push_back(new SavedState(&f.d,&pool));assert(nesting.back()->ok);for(size_t j=0;j+1<nesting.size();++j)assert(nesting[j]->block!=nesting.back()->block);}
        for(int i=6;i>=0;--i){f.d.state=-1;delete nesting[i];assert(f.d.state==i);}
        assert(f.d.creates==7&&f.d.captures==0&&f.d.alive==4);pool.clear();assert(f.d.alive==0);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);{SavedState outer(&f.d,&pool);f.d.state=31;pool.clear();assert(f.d.alive==1);{SavedState inner(&f.d,&pool);f.d.state=91;}assert(f.d.state==31);assert(f.d.alive==2);}assert(f.d.state==17&&f.d.alive==1);pool.clear();assert(f.d.alive==0);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);{SavedState s(&f.d,&pool);assert(s.ok);}f.d.failCapture=true;{SavedState s(&f.d,&pool);assert(!s.ok&&s.block==nullptr);}assert(f.d.alive==0);f.d.failCapture=false;{SavedState s(&f.d,&pool);assert(s.ok);}assert(f.d.creates==2&&f.d.captures==1);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);f.d.failCreate=true;{SavedState s(&f.d,&pool);assert(!s.ok);}assert(f.d.applies==0&&f.d.alive==0);f.d.failCreate=false;f.d.failViewport=true;{SavedState s(&f.d,&pool);assert(!s.ok);}assert(f.d.applies==0&&f.d.changes.empty());f.d.failViewport=false;{SavedState s(&f.d,&pool);assert(s.ok);}assert(f.d.creates==2&&f.d.captures==1);}
    {Fixture f;f.d.targets[0]=nullptr;{SavedState s(&f.d);assert(!s.ok);}assert(f.d.changes.empty());}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);Fixture other;{SavedState s(&other.d,&pool);assert(s.ok);other.d.state=8;}assert(other.d.state==17&&f.d.creates==0&&other.d.alive==0);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);{SavedState s(&f.d,&pool);assert(s.ok);}assert(f.surface[5].refs==2);f.d.boundResource=f.surface+2;{SavedState s(&f.d,&pool);assert(s.ok);assert(f.surface[5].refs==1);}assert(f.surface[2].refs==2);pool.clear();assert(f.surface[2].refs==1&&f.d.refs==1);}
    {SavedState s(nullptr);assert(!s.ok);NorthlightStateBlockPool pool(nullptr);assert(!pool.acquire());pool.clear();}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);f.d.depth=nullptr;{SavedState s(&f.d,&pool);assert(s.ok);f.d.depth=f.surface+5;}assert(!f.d.depth);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);{SavedState s(&f.d,&pool);assert(s.ok);f.d.failApply=true;}assert(f.d.applies==1);pool.clear();assert(f.d.alive==0);}
    {Fixture f;NorthlightStateBlockPool pool(&f.d);{SavedState s(&f.d,&pool);mutate(f,99);
        assert(s.restore()&&s.restore());assert(f.d.state==17&&f.d.applies==1);}
        assert(f.d.applies==1);}
    for(int failure:{10,11,12,13,20,21,30,40}){Fixture f;NorthlightStateBlockPool pool(&f.d);
        {SavedState s(&f.d,&pool);mutate(f,99);f.d.failChange=failure;f.d.failApply=failure==40;
         assert(!s.restore()&&!s.restore());assert(f.d.applies==1&&f.d.changes.back()==30);}
        assert(f.d.applies==1);pool.clear();assert(f.d.alive==0);}
    std::cout<<"SavedState: restoration order, 10000 reuses (1 create / 9999 captures), 7 nested leases, active clear, failures, device identity, null depth, resource release passed\n";
}
