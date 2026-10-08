// The fake Target of the stream tests: a deterministic D3D9 device (state storage, resources with real memory, locks with a
// pitch of its own) that appends every mutating call, its arguments and the bytes it was given to gTrace (via the generated
// Fake* classes and the overrides below). Included by test_command_stream.cpp after cs_generated.inc.
#pragma once
struct TargetKnobs {
    std::atomic<bool> hold{false};            // BeginScene blocks while set: keeps the replay thread busy so commands queue up
    std::atomic<int> presents{0};
    std::atomic<bool> failSwapChain{false},failQueries{false},noRaw{false},holdQueries{false};   // holdQueries: a polled query stays S_FALSE
    std::atomic<bool> noDigest{false};        // 0.3.200 (frame skip): STATE lines carry no ids, so a run without some draws numbers the objects the same
    //   // noRaw: the resolver proves no raw pointer (a proxy then replays through the Device)
    bool failCube=true;                       // CreateCubeTexture / CreateVolumeTexture fail (the dead-create path)
};
static TargetKnobs gKnobs;
static std::atomic<int> gLiveTargets{0},gDeviceDeletes{0};
struct TTexture;static TTexture* gLastTexture=nullptr;   // the Target's newest texture, for tests that look at its memory
static std::string hexOf(const unsigned char* p,std::size_t n){return fb(p,n);}

template<class B> struct Counted:B {
    std::atomic<int> refs{1};
    Counted(){gLiveTargets.fetch_add(1);}
    ~Counted() override{gLiveTargets.fetch_sub(1);forgetPtr(this);}
    ULONG AddRef() override{return ULONG(++refs);}
    ULONG Release() override{const int n=--refs;if(!n)delete this;return ULONG(n);}
};

static void traceDigest();   // appends the Target's effective state (defined after TargetDevice)
struct TargetDevice;static TargetDevice* gActiveTarget=nullptr;
struct TSurface;struct TTexture;
struct TSurface:Counted<FakeSurface> {
    unsigned w=0,h=0,fmt=22,usage=0,pool=0;std::vector<unsigned char> own;unsigned char* mem=nullptr;unsigned pitch=0;TTexture* owner=nullptr;unsigned level=0;
    unsigned bw=1,bytes=4;   // block width/height and bytes per block (unknown formats: 4 bytes a pixel)
    RECT locked{};bool isLocked=false;DWORD lockFlags=0;
    TSurface(unsigned W,unsigned H,unsigned Fmt,unsigned Usage,unsigned Pool):w(W),h(H),fmt(Fmt),usage(Usage),pool(Pool){
        const auto fi=D3::formatInfo(Fmt);if(fi.ok){bw=fi.bw;bytes=fi.bytes;}   // the real layout of the format (DXT block rows, 1/2/4/8/16 bytes a pixel)
        pitch=((W+bw-1)/bw)*bytes+16;own.assign(std::size_t(pitch)*((H+bw-1)/bw),0);mem=own.data();}
    std::size_t at(LONG y,LONG x)const{return std::size_t(y/LONG(bw))*pitch+std::size_t(x/LONG(bw))*bytes;}
    // The bytes the lock covered, row by row, for the trace: block rows of the locked rect (or the pixels of an uncompressed one).
    std::string lockedBytes()const{
        std::string t;const LONG rows=(locked.bottom-locked.top+LONG(bw)-1)/LONG(bw);const std::size_t rb=std::size_t((locked.right-locked.left+LONG(bw)-1)/LONG(bw))*bytes;
        for(LONG y=0;y<rows;++y)t+=hexOf(mem+at(locked.top,locked.left)+std::size_t(y)*pitch,rb);return t;}
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DSurface9)||id==__uuidof(IDirect3DResource9)){*out=static_cast<IDirect3DSurface9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG AddRef() override;ULONG Release() override;
    HRESULT GetDesc(D3DSURFACE_DESC* d) override{d->Format=(D3DFORMAT)fmt;d->Type=(D3DRESOURCETYPE)1;d->Usage=usage;d->Pool=(D3DPOOL)pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)0;d->MultiSampleQuality=0;d->Width=w;d->Height=h;return D3D_OK;}
    HRESULT LockRect(D3DLOCKED_RECT* lr,const RECT* r,DWORD f) override{
        locked=r?*r:RECT{0,0,LONG(w),LONG(h)};isLocked=true;lockFlags=f;lr->Pitch=INT(pitch);lr->pBits=mem+at(locked.top,locked.left);return D3D_OK;}
    HRESULT UnlockRect() override{
        std::string s="Surface::Unlock "+std::to_string(lockFlags&~D3::kLockReadOnly)+" ";
        if(!(lockFlags&D3::kLockReadOnly))s+=lockedBytes();
        gTrace.push_back(s+" rect "+std::to_string(locked.left)+","+std::to_string(locked.top)+","+std::to_string(locked.right)+","+std::to_string(locked.bottom)+((lockFlags&D3::kLockReadOnly)?" ro":""));
        if(!(lockFlags&D3::kLockReadOnly))traceDigest();isLocked=false;return D3D_OK;}
    HRESULT GetContainer(REFIID,void**) override;
};
struct TTexture:Counted<FakeTexture> {
    unsigned w,h,levels,usage,fmt,pool;std::vector<TSurface*> surf;
    TTexture(unsigned W,unsigned H,unsigned L,unsigned U,unsigned F,unsigned P):w(W),h(H),usage(U),fmt(F),pool(P){
        levels=L?L:D3::fullChain(W,H,1);for(unsigned i=0;i<levels;++i){auto* s=new TSurface(D3::mipDim(W,i),D3::mipDim(H,i),F,U,P);s->owner=this;s->level=i;surf.push_back(s);}
    }
    ~TTexture() override{for(auto* s:surf){s->owner=nullptr;s->refs.store(0);delete s;}}
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DTexture9)||id==__uuidof(IDirect3DBaseTexture9)||id==__uuidof(IDirect3DResource9)){*out=static_cast<IDirect3DTexture9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    D3DRESOURCETYPE GetType() override{return (D3DRESOURCETYPE)3;}
    DWORD GetLevelCount() override{return levels;}
    HRESULT GetLevelDesc(UINT l,D3DSURFACE_DESC* d) override{return surf[l]->GetDesc(d);}
    HRESULT GetSurfaceLevel(UINT l,IDirect3DSurface9** pp) override{if(l>=levels)return D3DERR_INVALIDCALL;surf[l]->AddRef();*pp=surf[l];return D3D_OK;}
    HRESULT LockRect(UINT l,D3DLOCKED_RECT* lr,const RECT* r,DWORD f) override{return surf[l]->LockRect(lr,r,f);}
    HRESULT UnlockRect(UINT l) override{
        auto* s=surf[l];std::string t="Texture::Unlock level "+std::to_string(l)+" "+std::to_string(s->lockFlags&~D3::kLockReadOnly)+" ";
        if(!(s->lockFlags&D3::kLockReadOnly))t+=s->lockedBytes();
        gTrace.push_back(t+" rect "+std::to_string(s->locked.left)+","+std::to_string(s->locked.top)+","+std::to_string(s->locked.right)+","+std::to_string(s->locked.bottom)+((s->lockFlags&D3::kLockReadOnly)?" ro":""));
        if(!(s->lockFlags&D3::kLockReadOnly))traceDigest();s->isLocked=false;return D3D_OK;}
};
inline ULONG TSurface::AddRef(){return owner?owner->AddRef():Counted<FakeSurface>::AddRef();}   // a level's references are its texture's
inline ULONG TSurface::Release(){return owner?owner->Release():Counted<FakeSurface>::Release();}
inline HRESULT TSurface::GetContainer(REFIID id,void** pp){if(!owner)return E_NOINTERFACE;return owner->QueryInterface(id,pp);}

template<class Base,Kind K> struct TBuffer:Counted<Base> {
    using IFace=typename std::conditional<K==Kind::VertexBuffer,IDirect3DVertexBuffer9,IDirect3DIndexBuffer9>::type;
    std::vector<unsigned char> mem;UINT length=0;DWORD usage=0,pool=0;unsigned fmt=0,fvf=0;UINT lockOff=0,lockSize=0;DWORD lockFlags=0;bool isLocked=false;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DResource9)||id==__uuidof(IFace)){*out=static_cast<Base*>(this);this->AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT Lock(UINT off,UINT size,void** pp,DWORD f) override{
        if(isLocked||off>length)return D3DERR_INVALIDCALL;if(!size)size=length-off;if(off+size>length)return D3DERR_INVALIDCALL;
        lockOff=off;lockSize=size;lockFlags=f;isLocked=true;*pp=mem.data()+off;return D3D_OK;}
    HRESULT Unlock() override{
        gTrace.push_back(std::string(K==Kind::VertexBuffer?"VB":"IB")+"::Unlock "+std::to_string(lockOff)+" "+std::to_string(lockSize)+" "+std::to_string(lockFlags&~D3::kLockReadOnly)+" "+((lockFlags&D3::kLockReadOnly)?std::string("ro"):hexOf(mem.data()+lockOff,lockSize)));
        if(!(lockFlags&D3::kLockReadOnly))traceDigest();isLocked=false;return D3D_OK;}
};
struct TVertexBuffer:TBuffer<FakeVertexBuffer,Kind::VertexBuffer> {
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC* d) override{d->Format=(D3DFORMAT)100;d->Type=(D3DRESOURCETYPE)6;d->Usage=usage;d->Pool=(D3DPOOL)pool;d->Size=length;d->FVF=fvf;return D3D_OK;}
};
struct TIndexBuffer:TBuffer<FakeIndexBuffer,Kind::IndexBuffer> {
    HRESULT GetDesc(D3DINDEXBUFFER_DESC* d) override{d->Format=(D3DFORMAT)fmt;d->Type=(D3DRESOURCETYPE)7;d->Usage=usage;d->Pool=(D3DPOOL)pool;d->Size=length;return D3D_OK;}
};
struct TVertexShader:Counted<FakeVertexShader> {
    std::vector<DWORD> code;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DVertexShader9)){*out=static_cast<IDirect3DVertexShader9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT GetFunction(void* d,UINT* n) override{*n=UINT(code.size()*4);if(d)std::memcpy(d,code.data(),code.size()*4);return D3D_OK;}
};
struct TPixelShader:Counted<FakePixelShader> {
    std::vector<DWORD> code;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DPixelShader9)){*out=static_cast<IDirect3DPixelShader9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT GetFunction(void* d,UINT* n) override{*n=UINT(code.size()*4);if(d)std::memcpy(d,code.data(),code.size()*4);return D3D_OK;}
};
struct TDecl:Counted<FakeVertexDeclaration> {
    std::vector<D3DVERTEXELEMENT9> el;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DVertexDeclaration9)){*out=static_cast<IDirect3DVertexDeclaration9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* d,UINT* n) override{*n=UINT(el.size());if(d)std::memcpy(d,el.data(),el.size()*sizeof(D3DVERTEXELEMENT9));return D3D_OK;}
};
struct TStateBlock:Counted<FakeStateBlock> {
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DStateBlock9)){*out=static_cast<IDirect3DStateBlock9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
};
struct TQuery:Counted<FakeQuery> {
    unsigned type=9;int polls=0;DWORD issued=0;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DQuery9)){*out=static_cast<IDirect3DQuery9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    D3DQUERYTYPE GetType() override{return (D3DQUERYTYPE)type;}
    DWORD GetDataSize() override{return queryDataSize(type);}
    HRESULT GetData(void* d,DWORD size,DWORD f) override{
        if(!(f&D3::kGetDataFlush)&&(++polls<3||gKnobs.holdQueries.load()))return S_FALSE;   // the result takes a few polls
        if(d&&size>=4){const DWORD v=0xABCD0000u+issued;std::memcpy(d,&v,4);}return D3D_OK;}
};
struct TSwapChain:Counted<FakeSwapChain> {
    D3DPRESENT_PARAMETERS pp{};std::vector<TSurface*> back;
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DSwapChain9)){*out=static_cast<IDirect3DSwapChain9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS* p) override{*p=pp;return D3D_OK;}
    HRESULT GetBackBuffer(UINT i,D3DBACKBUFFER_TYPE,IDirect3DSurface9** pp2) override{if(i>=back.size())return D3DERR_INVALIDCALL;back[i]->AddRef();*pp2=back[i];return D3D_OK;}
    void rebuild(){for(auto* b:back)b->Release();back.clear();for(UINT i=0;i<(pp.BackBufferCount?pp.BackBufferCount:1);++i)back.push_back(new TSurface(pp.BackBufferWidth,pp.BackBufferHeight,22,D3::kUsageRT,0));}
    ~TSwapChain() override{for(auto* b:back)b->Release();}
};

// The ExtensionDevice's role in the fake: RAW (unwrapped) pointers in, the backend state, and the trace. In the real code a Device-level
// exposed object wraps a raw backend object; here raw = exposed shifted by a constant, so a wrong or missing unwrap shows as another id.
template<class T> static T* unwrapFake(T* e){return e?reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(e)+kRawShift):nullptr;}
template<class T> static T* wrapFake(T* r){return r?reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(r)-kRawShift):nullptr;}
struct TargetExt:FakeDevice {
    DWORD rs[256],samp[21][16],tss[8][33];D3DVIEWPORT9 vp{};RECT scissor{};D3DMATRIX xf[512]{};DWORD fvfv=0;float vsF[256][4]{};
    TSurface* rt0=nullptr;TSurface* ds=nullptr;IDirect3DBaseTexture9* tex[21]{};IDirect3DVertexBuffer9* sv[16]{};IDirect3DIndexBuffer9* idx=nullptr;
    IDirect3DVertexShader9* vsv=nullptr;IDirect3DPixelShader9* psv=nullptr;bool inBlock=false;
    D3DMATERIAL9 mat{};float clip[32][4]{};WINBOOL lightEn[16]{};float npatch=0;WINBOOL swvp=0;UINT palette=0;
    TargetExt(){
        for(unsigned i=0;i<256;++i)rs[i]=1000+i;for(unsigned i=0;i<21;++i)for(unsigned t=0;t<16;++t)samp[i][t]=2000+i*16+t;
        for(unsigned s=0;s<8;++s)for(unsigned t=0;t<33;++t)tss[s][t]=3000+s*33+t;
        vp.X=0;vp.Y=0;vp.Width=640;vp.Height=480;vp.MinZ=0;vp.MaxZ=1;scissor=RECT{0,0,640,480};
    }
    ~TargetExt() override{
        if(rt0)rt0->Release();if(ds)ds->Release();
        for(auto& t:tex)if(t)t->Release();for(auto& v:sv)if(v)v->Release();if(idx)idx->Release();if(vsv)vsv->Release();if(psv)psv->Release();
    }
    template<class T> static void rep(T*& slot,T* np){if(np)np->AddRef();if(slot)slot->Release();slot=np;}
    HRESULT SetRenderState(D3DRENDERSTATETYPE s,DWORD v) override{FakeDevice::SetRenderState(s,v);if(inBlock)return D3D_OK;if(unsigned(s)<256)rs[s]=v;return D3D_OK;}
    HRESULT GetRenderState(D3DRENDERSTATETYPE s,DWORD* v) override{if(unsigned(s)>=256||!v)return D3DERR_INVALIDCALL;*v=rs[s];return D3D_OK;}
    HRESULT SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v) override{FakeDevice::SetSamplerState(s,t,v);if(inBlock)return D3D_OK;unsigned i;if(StreamState::sampIndex(s,i)&&unsigned(t)<16)samp[i][t]=v;return D3D_OK;}
    HRESULT GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v) override{unsigned i;if(!StreamState::sampIndex(s,i)||unsigned(t)>=16||!v)return D3DERR_INVALIDCALL;*v=samp[i][t];return D3D_OK;}
    HRESULT SetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD v) override{FakeDevice::SetTextureStageState(s,t,v);if(inBlock)return D3D_OK;if(s<8&&unsigned(t)<33)tss[s][t]=v;return D3D_OK;}
    HRESULT GetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD* v) override{if(s>=8||unsigned(t)>=33||!v)return D3DERR_INVALIDCALL;*v=tss[s][t];return D3D_OK;}
    HRESULT SetViewport(const D3DVIEWPORT9* v) override{FakeDevice::SetViewport(v);if(inBlock)return D3D_OK;if(v)vp=*v;return D3D_OK;}
    HRESULT GetViewport(D3DVIEWPORT9* v) override{*v=vp;return D3D_OK;}
    HRESULT SetScissorRect(const RECT* r) override{FakeDevice::SetScissorRect(r);if(inBlock)return D3D_OK;if(r)scissor=*r;return D3D_OK;}
    HRESULT GetScissorRect(RECT* r) override{*r=scissor;return D3D_OK;}
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s,const D3DMATRIX* m) override{FakeDevice::SetTransform(s,m);if(inBlock)return D3D_OK;if(unsigned(s)<512&&m)xf[s]=*m;return D3D_OK;}
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE s,D3DMATRIX* m) override{if(unsigned(s)>=512||!m)return D3DERR_INVALIDCALL;*m=xf[s];return D3D_OK;}
    HRESULT SetFVF(DWORD f) override{FakeDevice::SetFVF(f);if(inBlock)return D3D_OK;fvfv=f;return D3D_OK;}
    HRESULT GetFVF(DWORD* f) override{*f=fvfv;return D3D_OK;}
    HRESULT SetVertexShaderConstantF(UINT r,const float* d,UINT n) override{FakeDevice::SetVertexShaderConstantF(r,d,n);if(inBlock)return D3D_OK;for(UINT i=0;i<n&&r+i<256;++i)std::memcpy(vsF[r+i],d+4*i,16);return D3D_OK;}
    HRESULT GetVertexShaderConstantF(UINT r,float* d,UINT n) override{if(r+n>256)return D3DERR_INVALIDCALL;for(UINT i=0;i<n;++i)std::memcpy(d+4*i,vsF[r+i],16);return D3D_OK;}
    HRESULT SetRenderTarget(DWORD i,IDirect3DSurface9* s) override{FakeDevice::SetRenderTarget(i,s);if(inBlock)return D3D_OK;if(i==0)rep(rt0,static_cast<TSurface*>(wrapFake(s)));return D3D_OK;}
    HRESULT GetRenderTarget(DWORD i,IDirect3DSurface9** pp) override{if(i!=0||!rt0)return D3DERR_NOTFOUND;rt0->AddRef();*pp=rt0;return D3D_OK;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* s) override{FakeDevice::SetDepthStencilSurface(s);if(inBlock)return D3D_OK;rep(ds,static_cast<TSurface*>(wrapFake(s)));return D3D_OK;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** pp) override{if(!ds)return D3DERR_NOTFOUND;ds->AddRef();*pp=ds;return D3D_OK;}
    HRESULT SetTexture(DWORD s,IDirect3DBaseTexture9* t) override{FakeDevice::SetTexture(s,t);if(inBlock)return D3D_OK;unsigned i;if(StreamState::sampIndex(s,i))rep(tex[i],wrapFake(t));return D3D_OK;}
    HRESULT GetTexture(DWORD s,IDirect3DBaseTexture9** pp) override{unsigned i;if(!StreamState::sampIndex(s,i))return D3DERR_INVALIDCALL;if(tex[i])tex[i]->AddRef();*pp=tex[i];return D3D_OK;}
    HRESULT SetStreamSource(UINT i,IDirect3DVertexBuffer9* v,UINT o,UINT st) override{FakeDevice::SetStreamSource(i,v,o,st);if(inBlock)return D3D_OK;if(i<16)rep(sv[i],wrapFake(v));return D3D_OK;}
    HRESULT GetStreamSource(UINT i,IDirect3DVertexBuffer9** pp,UINT* o,UINT* st) override{if(i>=16)return D3DERR_INVALIDCALL;if(sv[i])sv[i]->AddRef();*pp=sv[i];*o=0;*st=0;return D3D_OK;}
    HRESULT SetIndices(IDirect3DIndexBuffer9* v) override{FakeDevice::SetIndices(v);if(inBlock)return D3D_OK;rep(idx,wrapFake(v));return D3D_OK;}
    HRESULT GetIndices(IDirect3DIndexBuffer9** pp) override{if(idx)idx->AddRef();*pp=idx;return D3D_OK;}
    HRESULT SetVertexShader(IDirect3DVertexShader9* v) override{FakeDevice::SetVertexShader(v);if(inBlock)return D3D_OK;rep(vsv,wrapFake(v));return D3D_OK;}
    HRESULT GetVertexShader(IDirect3DVertexShader9** pp) override{if(vsv)vsv->AddRef();*pp=vsv;return D3D_OK;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* v) override{FakeDevice::SetPixelShader(v);if(inBlock)return D3D_OK;rep(psv,wrapFake(v));return D3D_OK;}
    HRESULT GetPixelShader(IDirect3DPixelShader9** pp) override{if(psv)psv->AddRef();*pp=psv;return D3D_OK;}
    HRESULT SetMaterial(const D3DMATERIAL9* m) override{FakeDevice::SetMaterial(m);if(!inBlock&&m)mat=*m;return D3D_OK;}
    HRESULT SetClipPlane(DWORD i,const float* p) override{FakeDevice::SetClipPlane(i,p);if(!inBlock&&i<32&&p)std::memcpy(clip[i],p,16);return D3D_OK;}
    HRESULT LightEnable(DWORD i,WINBOOL e) override{FakeDevice::LightEnable(i,e);if(!inBlock&&i<16)lightEn[i]=e;return D3D_OK;}
    HRESULT SetNPatchMode(float n) override{FakeDevice::SetNPatchMode(n);if(!inBlock)npatch=n;return D3D_OK;}
    HRESULT SetSoftwareVertexProcessing(WINBOOL b) override{FakeDevice::SetSoftwareVertexProcessing(b);if(!inBlock)swvp=b;return D3D_OK;}
    HRESULT SetCurrentTexturePalette(UINT n) override{FakeDevice::SetCurrentTexturePalette(n);if(!inBlock)palette=n;return D3D_OK;}
    // the points where the Target's state matters: every draw, clear, copy and present logs a digest of it
    std::string digest(){   // one hash per component, so a mismatch names what differs
        if(gKnobs.noDigest.load())return "STATE";
        std::string out="STATE";std::uint64_t h=0;
        auto part=[&](const char* name,auto fill){h=1469598103934665603ull;fill([&](const void* p,std::size_t n){for(std::size_t i=0;i<n;++i)h=(h^static_cast<const unsigned char*>(p)[i])*1099511628211ull;});out+=std::string(" ")+name+"="+std::to_string(h%100000);};
        part("rs",[&](auto m){m(rs,sizeof rs);});part("samp",[&](auto m){m(samp,sizeof samp);});part("tss",[&](auto m){m(tss,sizeof tss);});
        part("vp",[&](auto m){m(&vp,sizeof vp);m(&scissor,sizeof scissor);});part("xf",[&](auto m){m(xf[2].m,64);m(xf[3].m,64);});part("vsF",[&](auto m){m(vsF,sizeof vsF);});
        part("misc",[&](auto m){m(&mat,sizeof mat);m(clip,sizeof clip);m(lightEn,sizeof lightEn);m(&npatch,4);m(&swvp,4);m(&palette,4);m(&fvfv,4);});
        std::string list;
        part("ids",[&](auto m){auto id=[&](const void* p){const int v=p?std::atoi(ptrId(reinterpret_cast<std::uintptr_t>(p)+kRawShift).c_str()+1):0;m(&v,sizeof v);list+=std::to_string(v)+",";};
            for(auto* t:tex)id(t);for(auto* v:sv)id(v);id(idx);id(vsv);id(psv);id(rt0);id(ds);});   // the raw namespace the Set trace lines use
        return out+" ["+list+"]";
    }
};

// The Device fake: like the real Device it forwards the state calls to the extension device after unwrapping; creates, draws, Present,
// Reset and the rest are its own.
struct TargetDevice:Counted<FakeDevice> {
    TargetExt ext;TSwapChain* sc=nullptr;int resets=0;
    const void* lastUpData=nullptr;const void* lastUpIdentity=nullptr;
    TargetDevice(){
        gActiveTarget=this;
        sc=new TSwapChain;sc->pp.BackBufferWidth=640;sc->pp.BackBufferHeight=480;sc->pp.BackBufferFormat=(D3DFORMAT)22;sc->pp.BackBufferCount=2;sc->rebuild();
        ext.rt0=sc->back[0];ext.rt0->AddRef();ext.ds=new TSurface(640,480,75,D3::kUsageDS,0);
    }
    ~TargetDevice() override{gDeviceDeletes.fetch_add(1);if(gActiveTarget==this)gActiveTarget=nullptr;sc->Release();}
    std::string digest(){return ext.digest();}
    // -- state: Device -> unwrap -> ext (a Get reads what ext holds) --
    HRESULT SetRenderState(D3DRENDERSTATETYPE s,DWORD v) override{return ext.SetRenderState(s,v);}
    HRESULT GetRenderState(D3DRENDERSTATETYPE s,DWORD* v) override{return ext.GetRenderState(s,v);}
    HRESULT SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v) override{return ext.SetSamplerState(s,t,v);}
    HRESULT GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v) override{return ext.GetSamplerState(s,t,v);}
    HRESULT SetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD v) override{return ext.SetTextureStageState(s,t,v);}
    HRESULT GetTextureStageState(DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD* v) override{return ext.GetTextureStageState(s,t,v);}
    HRESULT SetViewport(const D3DVIEWPORT9* v) override{return ext.SetViewport(v);}
    HRESULT GetViewport(D3DVIEWPORT9* v) override{return ext.GetViewport(v);}
    HRESULT SetScissorRect(const RECT* r) override{return ext.SetScissorRect(r);}
    HRESULT GetScissorRect(RECT* r) override{return ext.GetScissorRect(r);}
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE s,const D3DMATRIX* m) override{return ext.SetTransform(s,m);}
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE s,D3DMATRIX* m) override{return ext.GetTransform(s,m);}
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE s,const D3DMATRIX* m) override{return ext.MultiplyTransform(s,m);}
    HRESULT SetFVF(DWORD f) override{return ext.SetFVF(f);}
    HRESULT GetFVF(DWORD* f) override{return ext.GetFVF(f);}
    HRESULT SetVertexShaderConstantF(UINT r,const float* d,UINT n) override{return ext.SetVertexShaderConstantF(r,d,n);}
    HRESULT GetVertexShaderConstantF(UINT r,float* d,UINT n) override{return ext.GetVertexShaderConstantF(r,d,n);}
    HRESULT SetVertexShaderConstantI(UINT r,const int* d,UINT n) override{return ext.SetVertexShaderConstantI(r,d,n);}
    HRESULT SetVertexShaderConstantB(UINT r,const WINBOOL* d,UINT n) override{return ext.SetVertexShaderConstantB(r,d,n);}
    HRESULT SetPixelShaderConstantF(UINT r,const float* d,UINT n) override{return ext.SetPixelShaderConstantF(r,d,n);}
    HRESULT SetPixelShaderConstantI(UINT r,const int* d,UINT n) override{return ext.SetPixelShaderConstantI(r,d,n);}
    HRESULT SetPixelShaderConstantB(UINT r,const WINBOOL* d,UINT n) override{return ext.SetPixelShaderConstantB(r,d,n);}
    HRESULT SetRenderTarget(DWORD i,IDirect3DSurface9* s) override{return ext.SetRenderTarget(i,unwrapFake(s));}
    HRESULT GetRenderTarget(DWORD i,IDirect3DSurface9** pp) override{return ext.GetRenderTarget(i,pp);}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* s) override{return ext.SetDepthStencilSurface(unwrapFake(s));}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** pp) override{return ext.GetDepthStencilSurface(pp);}
    HRESULT SetTexture(DWORD s,IDirect3DBaseTexture9* t) override{return ext.SetTexture(s,unwrapFake(t));}
    HRESULT GetTexture(DWORD s,IDirect3DBaseTexture9** pp) override{return ext.GetTexture(s,pp);}
    HRESULT SetStreamSource(UINT i,IDirect3DVertexBuffer9* v,UINT o,UINT st) override{return ext.SetStreamSource(i,unwrapFake(v),o,st);}
    HRESULT GetStreamSource(UINT i,IDirect3DVertexBuffer9** pp,UINT* o,UINT* st) override{return ext.GetStreamSource(i,pp,o,st);}
    HRESULT SetStreamSourceFreq(UINT i,UINT f) override{return ext.SetStreamSourceFreq(i,f);}
    HRESULT SetIndices(IDirect3DIndexBuffer9* v) override{return ext.SetIndices(unwrapFake(v));}
    HRESULT GetIndices(IDirect3DIndexBuffer9** pp) override{return ext.GetIndices(pp);}
    HRESULT SetVertexShader(IDirect3DVertexShader9* v) override{return ext.SetVertexShader(unwrapFake(v));}
    HRESULT GetVertexShader(IDirect3DVertexShader9** pp) override{return ext.GetVertexShader(pp);}
    HRESULT SetPixelShader(IDirect3DPixelShader9* v) override{return ext.SetPixelShader(unwrapFake(v));}
    HRESULT GetPixelShader(IDirect3DPixelShader9** pp) override{return ext.GetPixelShader(pp);}
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* v) override{return ext.SetVertexDeclaration(unwrapFake(v));}
    HRESULT SetMaterial(const D3DMATERIAL9* m) override{return ext.SetMaterial(m);}
    HRESULT SetLight(DWORD i,const D3DLIGHT9* l) override{return ext.SetLight(i,l);}
    HRESULT SetClipPlane(DWORD i,const float* p) override{return ext.SetClipPlane(i,p);}
    HRESULT LightEnable(DWORD i,WINBOOL e) override{return ext.LightEnable(i,e);}
    HRESULT SetNPatchMode(float n) override{return ext.SetNPatchMode(n);}
    HRESULT SetSoftwareVertexProcessing(WINBOOL b) override{return ext.SetSoftwareVertexProcessing(b);}
    HRESULT SetCurrentTexturePalette(UINT n) override{return ext.SetCurrentTexturePalette(n);}
    HRESULT SetPaletteEntries(UINT n,const PALETTEENTRY* e) override{return ext.SetPaletteEntries(n,e);}
    HRESULT BeginScene() override{FakeDevice::BeginScene();while(gKnobs.hold.load())std::this_thread::sleep_for(std::chrono::microseconds(100));gTrace.push_back(digest());return D3D_OK;}
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE t,UINT a,UINT b) override{FakeDevice::DrawPrimitive(t,a,b);gTrace.push_back(digest());return D3D_OK;}
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT a,UINT b,UINT c,UINT d,UINT e) override{FakeDevice::DrawIndexedPrimitive(t,a,b,c,d,e);gTrace.push_back(digest());return D3D_OK;}
    HRESULT Clear(DWORD n,const D3DRECT* r,DWORD f,D3DCOLOR c,float z,DWORD st) override{FakeDevice::Clear(n,r,f,c,z,st);gTrace.push_back(digest());return D3D_OK;}
    HRESULT StretchRect(IDirect3DSurface9* a,const RECT* b,IDirect3DSurface9* c,const RECT* d,D3DTEXTUREFILTERTYPE f) override{FakeDevice::StretchRect(a,b,c,d,f);gTrace.push_back(digest());return D3D_OK;}
    HRESULT ColorFill(IDirect3DSurface9* a,const RECT* b,D3DCOLOR c) override{FakeDevice::ColorFill(a,b,c);gTrace.push_back(digest());return D3D_OK;}
    HRESULT UpdateTexture(IDirect3DBaseTexture9* a,IDirect3DBaseTexture9* b) override{FakeDevice::UpdateTexture(a,b);gTrace.push_back(digest());return D3D_OK;}
    // -- custom methods --
    HRESULT QueryInterface(REFIID id,void** out) override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DDevice9)){*out=static_cast<IDirect3DDevice9*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    HRESULT GetSwapChain(UINT i,IDirect3DSwapChain9** pp) override{if(i||gKnobs.failSwapChain.load())return D3DERR_INVALIDCALL;sc->AddRef();*pp=sc;return D3D_OK;}
    HRESULT GetBackBuffer(UINT,UINT i,D3DBACKBUFFER_TYPE t,IDirect3DSurface9** pp) override{return sc->GetBackBuffer(i,t,pp);}
    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override{p->AdapterOrdinal=0;p->DeviceType=1;p->hFocusWindow=nullptr;p->BehaviorFlags=0x40;return D3D_OK;}
    UINT GetAvailableTextureMem() override{return 1234;}
    HRESULT Present(const RECT* s,const RECT* d,HWND,const RGNDATA* dirty) override{
        gTrace.push_back(std::string("Device::Present ")+(s?"src":"nosrc")+" "+(d?"dst":"nodst")+" "+(dirty?"dirty":"nodirty"));gTrace.push_back(digest());
        const int n=gKnobs.presents.fetch_add(1);return n%5==4?S_FALSE:D3D_OK;}
    HRESULT Reset(D3DPRESENT_PARAMETERS* p) override{
        for(auto* b:sc->back)if(b->refs.load()>1+(b==ext.rt0?1:0))return D3DERR_INVALIDCALL;   // a held back buffer fails Reset, as in D3D9
        if(ext.rt0){ext.rt0->Release();ext.rt0=nullptr;}
        gTrace.push_back("Device::Reset "+std::to_string(p->BackBufferWidth)+"x"+std::to_string(p->BackBufferHeight));
        sc->pp.BackBufferWidth=p->BackBufferWidth;sc->pp.BackBufferHeight=p->BackBufferHeight;sc->pp.Windowed=p->Windowed;sc->rebuild();ext.rt0=sc->back[0];ext.rt0->AddRef();
        for(unsigned i=0;i<256;++i)ext.rs[i]=1000+i;ext.vp.Width=p->BackBufferWidth;ext.vp.Height=p->BackBufferHeight;++resets;
        for(auto& t:ext.tex)if(t){t->Release();t=nullptr;}for(auto& v:ext.sv)if(v){v->Release();v=nullptr;}if(ext.idx){ext.idx->Release();ext.idx=nullptr;}return D3D_OK;}
    HRESULT CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** pp,HANDLE*) override{
        gTrace.push_back("Device::CreateTexture "+std::to_string(w)+" "+std::to_string(h)+" "+std::to_string(l)+" "+std::to_string(u)+" "+std::to_string(unsigned(f))+" "+std::to_string(unsigned(p)));
        if(!w||!h)return D3DERR_INVALIDCALL;auto* tx=new TTexture(w,h,l,u,unsigned(f),unsigned(p));gLastTexture=tx;*pp=tx;return D3D_OK;}
    HRESULT CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9** pp,HANDLE*) override{gTrace.push_back("Device::CreateCubeTexture");*pp=nullptr;return D3DERR_NOTAVAILABLE;}
    HRESULT CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9** pp,HANDLE*) override{gTrace.push_back("Device::CreateVolumeTexture");*pp=nullptr;return D3DERR_NOTAVAILABLE;}
    HRESULT CreateVertexBuffer(UINT len,DWORD u,DWORD fvf,D3DPOOL p,IDirect3DVertexBuffer9** pp,HANDLE*) override{
        gTrace.push_back("Device::CreateVertexBuffer "+std::to_string(len)+" "+std::to_string(u)+" "+std::to_string(fvf)+" "+std::to_string(unsigned(p)));
        auto* b=new TVertexBuffer;b->length=len;b->usage=u;b->fvf=fvf;b->pool=unsigned(p);b->mem.assign(len,0);*pp=b;return D3D_OK;}
    HRESULT CreateIndexBuffer(UINT len,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DIndexBuffer9** pp,HANDLE*) override{
        gTrace.push_back("Device::CreateIndexBuffer "+std::to_string(len)+" "+std::to_string(u)+" "+std::to_string(unsigned(f))+" "+std::to_string(unsigned(p)));
        auto* b=new TIndexBuffer;b->length=len;b->usage=u;b->fmt=unsigned(f);b->pool=unsigned(p);b->mem.assign(len,0);*pp=b;return D3D_OK;}
    HRESULT CreateRenderTarget(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE,DWORD,WINBOOL,IDirect3DSurface9** pp,HANDLE*) override{gTrace.push_back("Device::CreateRenderTarget "+std::to_string(w)+" "+std::to_string(h));*pp=new TSurface(w,h,unsigned(f),D3::kUsageRT,0);return D3D_OK;}
    HRESULT CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE,DWORD,WINBOOL,IDirect3DSurface9** pp,HANDLE*) override{gTrace.push_back("Device::CreateDepthStencilSurface "+std::to_string(w)+" "+std::to_string(h));*pp=new TSurface(w,h,unsigned(f),D3::kUsageDS,0);return D3D_OK;}
    HRESULT CreateOffscreenPlainSurface(UINT w,UINT h,D3DFORMAT f,D3DPOOL p,IDirect3DSurface9** pp,HANDLE*) override{gTrace.push_back("Device::CreateOffscreenPlainSurface "+std::to_string(w)+" "+std::to_string(h)+" "+std::to_string(unsigned(p)));*pp=new TSurface(w,h,unsigned(f),0,unsigned(p));return D3D_OK;}
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9* e,IDirect3DVertexDeclaration9** pp) override{
        auto* d=new TDecl;UINT n=0;while(e[n].Stream!=0xFF)++n;d->el.assign(e,e+n+1);gTrace.push_back("Device::CreateVertexDeclaration "+std::to_string(n+1)+" "+fb(e,(n+1)*sizeof(D3DVERTEXELEMENT9)));*pp=d;return D3D_OK;}
    HRESULT CreateVertexShader(const DWORD* c,IDirect3DVertexShader9** pp) override{auto* v=new TVertexShader;const auto n=shaderTokens(c);v->code.assign(c,c+n);gTrace.push_back("Device::CreateVertexShader "+fb(c,n*4));*pp=v;return D3D_OK;}
    HRESULT CreatePixelShader(const DWORD* c,IDirect3DPixelShader9** pp) override{auto* v=new TPixelShader;const auto n=shaderTokens(c);v->code.assign(c,c+n);gTrace.push_back("Device::CreatePixelShader "+fb(c,n*4));*pp=v;return D3D_OK;}
    HRESULT CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9** pp) override{
        if(!pp)return (unsigned(t)==9||unsigned(t)==8)?D3D_OK:D3DERR_NOTAVAILABLE;   // the support probe
        if(gKnobs.failQueries.load()){*pp=nullptr;return D3DERR_NOTAVAILABLE;}
        gTrace.push_back("Device::CreateQuery "+std::to_string(unsigned(t)));auto* q=new TQuery;q->type=unsigned(t);*pp=q;return D3D_OK;}
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** pp) override{gTrace.push_back("Device::CreateStateBlock "+std::to_string(unsigned(t)));*pp=new TStateBlock;return D3D_OK;}
    HRESULT BeginStateBlock() override{gTrace.push_back("Device::BeginStateBlock");ext.inBlock=true;return D3D_OK;}
    HRESULT EndStateBlock(IDirect3DStateBlock9** pp) override{gTrace.push_back("Device::EndStateBlock");ext.inBlock=false;*pp=new TStateBlock;return D3D_OK;}
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* d,UINT stride) override{
        lastUpData=d;lastUpIdentity=upIdentity;
        gTrace.push_back("Device::DrawPrimitiveUP "+std::to_string(unsigned(t))+" "+std::to_string(n)+" "+std::to_string(stride)+" "+fb(d,std::size_t(StreamDevice::primVerts(unsigned(t),n))*stride));gTrace.push_back(digest());return D3D_OK;}
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT mn,UINT nv,UINT pc,const void* ix,D3DFORMAT f,const void* d,UINT stride) override{
        lastUpData=d;lastUpIdentity=upIdentity;
        const std::size_t ib=std::size_t(StreamDevice::primVerts(unsigned(t),pc))*(unsigned(f)==102?4:2);
        gTrace.push_back("Device::DrawIndexedPrimitiveUP "+std::to_string(unsigned(t))+" "+std::to_string(mn)+" "+std::to_string(nv)+" "+std::to_string(pc)+" "+std::to_string(unsigned(f))+" "+std::to_string(stride)+" "+fb(ix,ib)+" "+fb(d,std::size_t(mn+nv)*stride));gTrace.push_back(digest());return D3D_OK;}
};

static void traceDigest(){if(gActiveTarget)gTrace.push_back(gActiveTarget->ext.digest());}
