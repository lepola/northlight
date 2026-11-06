// SPDX-License-Identifier: MIT
#include "ui_draw_diagnostic.h"
#include <cassert>
struct UiCpuOnlyBuffer {
    unsigned refs=1,locks=0,unlocks=0;
    ULONG AddRef(){return ++refs;}ULONG Release(){return --refs;}
    HRESULT Lock(UINT,UINT,void**,DWORD){++locks;return E_FAIL;}
    HRESULT Unlock(){++unlocks;return S_OK;}
};

static void uiDrawScenarios(IDirect3DDevice9* d,IDirect3DDevice9* backend,NorthlightStream::StreamDevice* stream,const std::string& prefix){
    namespace U=NorthlightUiDrawDiagnostic;using A=NorthlightSpellLayer::DrawArgs;
    U::Window window;assert(!window.active(1));window.arm("test",10);assert(window.claim(reinterpret_cast<void*>(1),11));assert(!window.claim(reinterpret_cast<void*>(1),11));
    assert(window.claim(reinterpret_cast<void*>(2),11)&&window.claim(reinterpret_cast<void*>(3),12)&&window.claim(reinterpret_cast<void*>(4),12));assert(!window.claim(reinterpret_cast<void*>(5),12));
    window.arm("test",20);assert(!window.active(23));window.arm("test",UINT_MAX);assert(window.active(0)&&!window.active(2));
    window.arm("test",20);for(unsigned n=0;n<U::MaxProbes;++n)assert(window.probe(21));assert(!window.probe(21));
    unsigned begin=0,size=0;assert(U::range(1,3,24,24,48,480,256,begin,size)&&begin==72&&size==72);
    assert(!U::range(0,4096,24,24,0,UINT_MAX,U::MaxBytes,begin,size));assert(!U::range(UINT_MAX,UINT_MAX,4096,24,0,UINT_MAX,U::MaxBytes,begin,size));
    assert(!U::range(1,3,24,24,48,100,256,begin,size));assert(U::vertexCount(4,UINT_MAX)==std::uint64_t(UINT_MAX)*3);
    U::Bytes invalid;assert(!U::copyUp(reinterpret_cast<void*>(1),0,16,invalid)&&invalid.data.empty());
    UiCpuOnlyBuffer cpu;NorthlightReplayCopies::Slot slot;std::atomic<unsigned> locks{0};
    NorthlightReplayCopies::attach(slot,&cpu,&cpu,&cpu,128,&locks);assert(NorthlightReplayCopies::beginFill(slot,128));
    for(unsigned n=0;n<128;++n)slot.data[n]=static_cast<unsigned char>(n);NorthlightReplayCopies::endFill(slot,true);NorthlightReplayCopies::unpin(slot);
    U::Bytes cached;assert(U::read(&cpu,8,16,cached)&&cached.data.front()==8&&cached.data.back()==23&&cpu.refs==1&&!slot.pins&&!cpu.locks);
    assert(std::string(cached.source)=="valid-replay-CPU-copy");NorthlightReplayCopies::invalidate(slot,true);
    U::Bytes miss;assert(!U::read(&cpu,8,16,miss)&&miss.data.empty()&&cpu.locks==1&&!cpu.unlocks);NorthlightReplayCopies::detach(slot);
    struct Vertex {float x,y,z,rhw,u,v;};std::array<Vertex,20> vertices{};
    for(unsigned n=0;n<vertices.size();++n)vertices[n]={float(n),float(n+1),0,1,float(n)/32,float(n+1)/32};
    const unsigned short indices[]={99,98,3,5,4},upIndices[]={2,4,3};
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    check(d->CreateVertexBuffer(sizeof vertices,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr),"UIDRAW create VB");check(d->CreateIndexBuffer(sizeof indices,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr),"UIDRAW create IB");
    void* data=nullptr;check(vb->Lock(0,sizeof vertices,&data,0),"UIDRAW write VB");std::memcpy(data,vertices.data(),sizeof vertices);check(vb->Unlock(),"UIDRAW unlock VB");
    check(ib->Lock(0,sizeof indices,&data,0),"UIDRAW write IB");std::memcpy(data,indices,sizeof indices);check(ib->Unlock(),"UIDRAW unlock IB");
    check(d->SetStreamSource(0,vb,48,sizeof(Vertex)),"UIDRAW stream");check(d->SetStreamSourceFreq(0,1),"UIDRAW frequency");check(d->SetIndices(ib),"UIDRAW indices");
    check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1),"UIDRAW FVF");check(d->SetVertexShader(nullptr),"UIDRAW VS");check(d->SetPixelShader(nullptr),"UIDRAW PS");
    const DWORD vsCode[]={0xfffe0200,0x0200001f,0x80000000,0x900f0000,0x02000001,0xc00f0000,0x90e40000,0x0000ffff};
    const DWORD psCode[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    check(d->CreateVertexShader(vsCode,&vs),"UIDRAW create VS");check(d->CreatePixelShader(psCode,&ps),"UIDRAW create PS");
    check(d->SetVertexShader(vs),"UIDRAW bind VS");check(d->SetPixelShader(ps),"UIDRAW bind PS");
    const float color[]={.125f,.25f,.5f,1};check(d->SetVertexShaderConstantF(0,color,1),"UIDRAW VS constant");check(d->SetPixelShaderConstantF(0,color,1),"UIDRAW PS constant");
    check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT),"UIDRAW sampler");sync(stream);
    auto capture=[&](){
        DeviceMirror mirror;mirror.samplerKnown[0][D3DSAMP_MINFILTER]=true;mirror.samplers[0][D3DSAMP_MINFILTER]=D3DTEXF_LINEAR;
        mirror.vsFloatKnown[0]=mirror.psFloatKnown[0]=true;std::memcpy(mirror.psFloat[0],color,sizeof color);
        U::Ref<IDirect3DVertexBuffer9> before;UINT offset=0,stride=0;check(backend->GetStreamSource(0,before.out(),&offset,&stride),"UIDRAW before stream");
        mirror.streams[0]={before.p,offset,stride,true};mirror.vertexShaderKnown=mirror.pixelShaderKnown=true;
        const A args[]={A::indexed(D3DPT_TRIANGLELIST,-2,3,3,2,1),A::primitive(D3DPT_TRIANGLELIST,3,1),A::indexedUP(D3DPT_TRIANGLELIST,2,3,1,upIndices,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex)),A::primitiveUP(D3DPT_TRIANGLELIST,1,vertices.data(),sizeof(Vertex))};
        for(unsigned n=0;n<4;++n)assert(U::capture(backend,mirror,args[n],prefix+"-kind"+std::to_string(n),100,nullptr));
        auto malformed=args[0];malformed.primCount=UINT_MAX;assert(U::capture(backend,mirror,malformed,prefix+"-oversize",100,nullptr));
        U::Ref<IDirect3DVertexBuffer9> after;UINT afterOffset=0,afterStride=0;check(backend->GetStreamSource(0,after.out(),&afterOffset,&afterStride),"UIDRAW after stream");
        assert(before.p==after.p&&offset==afterOffset&&stride==afterStride);DWORD sampler=0;check(backend->GetSamplerState(0,D3DSAMP_MINFILTER,&sampler),"UIDRAW after sampler");assert(sampler==D3DTEXF_POINT);
        assert(mirror.samplers[0][D3DSAMP_MINFILTER]==D3DTEXF_LINEAR&&mirror.samplerKnown[0][D3DSAMP_MINFILTER]&&!mirror.samplerKnown[0][D3DSAMP_MAGFILTER]);
    };
    if(stream){assert(NorthlightStream::runTask(*NorthlightStream::activeCore.load(),[&](NorthlightStream::StreamCore&){capture();},NorthlightStream::Cmd::SyncInit));}else capture();
    check(d->SetStreamSource(0,nullptr,0,0),"UIDRAW clear stream");check(d->SetIndices(nullptr),"UIDRAW clear indices");vb->Release();ib->Release();
    check(d->SetVertexShader(nullptr),"UIDRAW clear VS");check(d->SetPixelShader(nullptr),"UIDRAW clear PS");vs->Release();ps->Release();
    std::printf("UIDRAW scenarios mode=%s pass=1 bounds=1 UP=1 mirrorMismatch=1 stateUnchanged=1\n",stream?"stream":"direct");
}
