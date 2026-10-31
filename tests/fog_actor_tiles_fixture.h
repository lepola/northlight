#pragma once
#include "world_compiled_shaders.h"

// Stand-only: actual two-stage GPU reduction, checked against a direct CPU
// minimum over fine texels. Call before the stand's final pass setup.
static IDirect3DTexture9* fogActorTilesFixture(IDirect3DDevice9* d,const Image& image,bool wide){
    assert(image.width==1024&&image.height==1024&&image.format==D3DFMT_R32F);
    IDirect3DTexture9 *fine=texture(d,image),*mid=nullptr,*result=nullptr;
    IDirect3DSurface9 *oldTarget=nullptr,*oldDepth=nullptr,*midSurface=nullptr,*resultSurface=nullptr,*staging=nullptr;
    IDirect3DBaseTexture9* oldInput=nullptr;D3DVIEWPORT9 oldViewport{};
    ok(d->GetRenderTarget(0,&oldTarget));d->GetDepthStencilSurface(&oldDepth);ok(d->GetViewport(&oldViewport));ok(d->GetTexture(12,&oldInput));
    ok(d->CreateTexture(256,256,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&mid,nullptr));
    ok(d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&result,nullptr));
    ok(mid->GetSurfaceLevel(0,&midSurface));ok(result->GetSurfaceLevel(0,&resultSurface));
    IDirect3DPixelShader9 *first=nullptr,*last=nullptr;
    ok(d->CreatePixelShader(kActorTilesShader,&first));
    ok(d->CreatePixelShader(wide&&!std::getenv("NORTHLIGHT_BAD_FOG_TILES")?kActorTilesWideShader:kActorTilesShader,&last));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(12,state,D3DTEXF_POINT));
    ok(d->SetSamplerState(12,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(12,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(12,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    for(unsigned stage=0;stage<2;++stage){
        const unsigned size=stage?64:256;
        ok(d->SetRenderTarget(0,stage?resultSurface:midSurface));ok(d->SetTexture(12,stage?mid:fine));ok(d->SetPixelShader(stage?last:first));
        const float info[]={1,1.f/float(stage?256:1024),12,0};ok(d->SetPixelShaderConstantF(72,info,1));
        D3DVIEWPORT9 viewport={0,0,size,size,0,1};ok(d->SetViewport(&viewport));
        const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(size)-.5f,-.5f,0,1,1,0},{-.5f,float(size)-.5f,0,1,0,1},{float(size)-.5f,float(size)-.5f,0,1,1,1}};
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));ok(d->EndScene());
    }
    if(wide&&std::getenv("NORTHLIGHT_TILE_BENCH")){
        IDirect3DQuery9 *event=nullptr,*begin=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{63.5f,-.5f,0,1,1,0},{-.5f,63.5f,0,1,0,1},{63.5f,63.5f,0,1,1,1}};
        auto draw=[&](unsigned n){ok(d->BeginScene());for(unsigned i=0;i<n;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));ok(d->EndScene());};
        std::vector<double> timings[2];
        for(unsigned block=0;block<16;++block){
            const unsigned mode=(block/2)%2?1-block%2:block%2;ok(d->SetPixelShader(mode?last:first));draw(2);wait(event);
            ok(disjoint->Issue(D3DISSUE_BEGIN));ok(begin->Issue(D3DISSUE_END));draw(128);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
            UINT64 a=0,b=0,f=0;BOOL bad=FALSE;ok(begin->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(freq->GetData(&f,sizeof f,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&f);
            if(block>=4)timings[mode].push_back(double(b-a)/f*1000/128);
        }
        for(auto& t:timings)std::sort(t.begin(),t.end());
        std::printf("TILE_GPU_FINAL_MS narrow=%.9f wide=%.9f\n",(timings[0][2]+timings[0][3])*.5,(timings[1][2]+timings[1][3])*.5);
        ok(d->SetPixelShader(last));draw(1);
        for(auto* query:{event,begin,end,freq,disjoint})query->Release();
    }
    ok(d->CreateOffscreenPlainSurface(64,64,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(resultSurface,staging));
    D3DLOCKED_RECT locked{};ok(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
    const int low=wide?-8:0,high=wide?24:16;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){
        float expected=1,actual;for(int j=low;j<high;++j)for(int i=low;i<high;++i){
            const unsigned row=unsigned(std::clamp(y*16+j,0,1023)),col=unsigned(std::clamp(x*16+i,0,1023));float z;
            std::memcpy(&z,image.pixels.data()+4*(row*1024+col),4);expected=std::min(expected,z);
        }
        std::memcpy(&actual,static_cast<char*>(locked.pBits)+y*locked.Pitch+x*4,4);assert(actual==expected&&"FOG_TILE_REDUCTION_MISMATCH");
    }
    ok(staging->UnlockRect());std::printf("FOG_TILE_REDUCTION_PASS wide=%u texels=4096\n",unsigned(wide));
    ok(d->SetTexture(12,oldInput));ok(d->SetRenderTarget(0,oldTarget));ok(d->SetDepthStencilSurface(oldDepth));ok(d->SetViewport(&oldViewport));ok(d->SetPixelShader(nullptr));
    if(oldInput)oldInput->Release();if(oldDepth)oldDepth->Release();oldTarget->Release();staging->Release();resultSurface->Release();midSurface->Release();mid->Release();fine->Release();first->Release();last->Release();
    return result;
}
