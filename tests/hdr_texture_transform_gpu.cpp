// COUNT2 must preserve fixed-function UV transforms on both D3D9 backends.
#undef NDEBUG
#define main hdr_frame_unused
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_color.h"
#include "forwarders.h"
static constexpr unsigned W=64,H=64;
static void fill(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float (&color)[4]){
    ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(shader));ok(d->SetPixelShaderConstantF(0,color,1));
    ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,W,H));ok(d->EndScene());
}
template<class Draw> static double timed(IDirect3DDevice9* d,Draw draw){
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    for(unsigned i=0;i<16;++i)draw();ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
    for(unsigned i=0;i<32;++i)draw();ok(end->Issue(D3DISSUE_END));UINT64 a=0,b=0,hz=0;HRESULT hr;
    while((hr=end->GetData(&b,sizeof b,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);ok(start->GetData(&a,sizeof a,0));ok(frequency->GetData(&hz,sizeof hz,0));
    start->Release();end->Release();frequency->Release();assert(hz&&b>a);return double(b-a)*1000/double(hz)/32;
}

static unsigned layerPoolOracle(IDirect3DDevice9* raw,IDirect3DSurface9* target,IDirect3DSurface9* readback){
    struct Meter:ForwardIDirect3DDevice9{
        unsigned creates=0;explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
        HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out)override{++creates;return real->CreateStateBlock(type,out);}
    } d(raw);
    NorthlightStateBlockPool pool(&d);NorthlightHdr::Color color;unsigned cases=0;
    std::vector<std::uint16_t> reference(W*H*4);
    for(unsigned draw=0;draw<24;++draw){
        for(bool pooled:{false,true}){
            ok(NorthlightHdr::quadState(raw,target,W,H));ok(raw->Clear(0,nullptr,D3DCLEAR_TARGET,0xff5070a0,1,0));
            ok(raw->SetPixelShader(nullptr));ok(raw->SetTexture(0,nullptr));ok(raw->SetTexture(1,nullptr));
            // Unsupported ADD deliberately routes through the compatibility layer.
            ok(raw->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));ok(raw->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_ADD));
            ok(raw->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));ok(raw->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE));
            ok(raw->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(raw->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
            ok(raw->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
            ok(raw->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(raw->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(raw->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            const float bank[4]={float(draw),2,3,4};ok(raw->SetPixelShaderConstantF(0,bank,1));
            struct V{float x,y,z,w;DWORD c;};const DWORD c=D3DCOLOR_ARGB(32+draw*8,20+draw,50,80);const V q[]={{0,0,0,1,c},{W,0,0,1,c},{0,H,0,1,c},{W,H,0,1,c}};
            ok(raw->BeginScene());ok(color.draw(&d,160,[&]{return raw->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));},0,pooled?&pool:nullptr));ok(raw->EndScene());
            float restored[4];ok(raw->GetPixelShaderConstantF(0,restored,1));assert(!std::memcmp(bank,restored,sizeof bank));
            DWORD op=0;ok(raw->GetTextureStageState(0,D3DTSS_COLOROP,&op));assert(op==D3DTOP_ADD);
            ok(raw->GetRenderTargetData(target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<H;++y){const auto* pixels=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch);if(!pooled)std::copy(pixels,pixels+W*4,reference.data()+y*W*4);else assert(!std::memcmp(pixels,reference.data()+y*W*4,W*8));}
            ok(readback->UnlockRect());
        }++cases;
    }
    assert(color.layers==48&&d.creates==25);pool.clear();
    std::printf("HDR_LAYER_POOL_ORACLE cases=%u rgba=exact freshCreates=24 pooledCreates=1 changedConstantsRestored=true\n",cases);return cases;
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* constant=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    IDirect3DSurface9 *resolved=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    layerPoolOracle(d,resolved,staging);
    Image pattern{8,8,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(256)};
    for(unsigned i=0;i<64;++i){DWORD pixel=D3DCOLOR_ARGB(96+i*2,20+(i*37)%220,30+(i*53)%210,40+(i*17)%200);std::memcpy(pattern.pixels.data()+i*4,&pixel,4);}
    auto* first=texture(d,pattern);std::reverse(pattern.pixels.begin(),pattern.pixels.end());auto* second=texture(d,pattern);
    D3DMATRIX identity{};for(unsigned i=0;i<4;++i)identity.m[i][i]=1;
    ok(d->SetTransform(D3DTS_WORLD,&identity));ok(d->SetTransform(D3DTS_VIEW,&identity));ok(d->SetTransform(D3DTS_PROJECTION,&identity));
    unsigned cases=0;float maximum=0;
    for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES})for(bool rhw:{false,true})
    for(bool identityUV:{false,true})for(unsigned coordinates:{0u,1u})for(unsigned transforms=1;transforms<4;++transforms)for(unsigned opacity:{64u,160u,255u}){
        IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,samples,0,FALSE,&target,nullptr));
        std::vector<std::uint16_t> reference(W*H*4),fast(W*H*4);NorthlightHdr::Color transfer;
        const bool fastExpected=rhw;
        for(unsigned mode=0;mode<(fastExpected?3u:2u);++mode){const bool direct=mode!=0;
            const float background[]={.3f,.7f,1.4f,.8f};fill(d,target,constant,background);
            ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(nullptr));
            for(unsigned stage=0;stage<2;++stage){
                ok(d->SetTexture(stage,stage?second:first));
                for(auto state:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})ok(d->SetTextureStageState(stage,state,D3DTOP_MODULATE));
                for(auto state:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})ok(d->SetTextureStageState(stage,state,D3DTA_TEXTURE));
                for(auto state:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})ok(d->SetTextureStageState(stage,state,D3DTA_CURRENT));
                ok(d->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,stage?coordinates:0));ok(d->SetTextureStageState(stage,D3DTSS_RESULTARG,D3DTA_CURRENT));
                ok(d->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,mode!=2&&(transforms&(1u<<stage))?D3DTTFF_COUNT2:D3DTTFF_DISABLE));
                D3DMATRIX matrix=identity;matrix.m[0][0]=0;matrix.m[1][0]=stage?-.6f:.7f;matrix.m[0][1]=stage?.8f:-.6f;matrix.m[1][1]=0;matrix.m[2][0]=stage?.8f:.1f;matrix.m[2][1]=stage?.1f:.7f;
                ok(d->SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+stage),identityUV?&identity:&matrix));
            }
            ok(d->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
            const DWORD color=D3DCOLOR_ARGB(opacity,200,180,240);
            struct R{float x,y,z,w;DWORD c;float u,v,a,b;};const R screen[]={{-.5f,-.5f,0,1,color,0,0,1,0},{W-.5f,-.5f,0,1,color,1,0,0,0},{-.5f,H-.5f,0,1,color,0,1,1,1},{W-.5f,H-.5f,0,1,color,1,1,0,1}};
            struct V{float x,y,z;DWORD c;float u,v,a,b;};const V model[]={{-1,1,0,color,0,0,1,0},{1,1,0,color,1,0,0,0},{-1,-1,0,color,0,1,1,1},{1,-1,0,color,1,1,0,1}};
            ok(d->SetFVF((rhw?D3DFVF_XYZRHW:D3DFVF_XYZ)|D3DFVF_DIFFUSE|D3DFVF_TEX2));
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,rhw?static_cast<const void*>(screen):model,rhw?sizeof(R):sizeof(V));};
            if(direct)ok(transfer.draw(d,160,draw));else ok(transfer.legacyLayer(d,160,draw));
            for(unsigned stage=0;stage<2;++stage){DWORD flags=0;ok(d->GetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,&flags));assert(flags==(mode!=2&&(transforms&(1u<<stage))?D3DTTFF_COUNT2:D3DTTFF_DISABLE));}
            ok(d->EndScene());ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float error=0;
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const auto* pixel=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)+x*4;
                for(unsigned c=0;c<4;++c){const unsigned i=(y*W+x)*4+c;if(!direct)reference[i]=pixel[c];else if(c<3)error=std::max(error,std::fabs(half(pixel[c])-half(reference[i])));
                    if(mode==1)fast[i]=pixel[c];else if(mode==2)assert(fast[i]==pixel[c]);}}
            ok(staging->UnlockRect());if(direct){maximum=std::max(maximum,error);std::printf("rhw=%u identity=%u samples=%u transforms=%u alpha=%u error=%g layers=%llu direct=%llu\n",unsigned(rhw),unsigned(identityUV),unsigned(samples),transforms,opacity,error,transfer.layers,transfer.multiTextureDirect);assert(error<(fastExpected?.008f:.00001f));}
        }
        assert(transfer.layers==(fastExpected?1u:2u)&&transfer.multiTextureDirect==(fastExpected?2u:0u));target->Release();++cases;
    }
    std::printf("HDR_TEXTURE_TRANSFORM_PASS cases=%u maxRgbError=%g\n",cases,maximum);
    if(argc==3){
        constexpr unsigned width=3840,height=2160;IDirect3DSurface9* target=nullptr;
        ok(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&target,nullptr));
        NorthlightHdr::Color transfer;
        for(bool full:{false,true}){
            auto frame=[&](bool direct){
                ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,width,height));ok(d->SetPixelShader(nullptr));
                ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2));
                for(unsigned stage=0;stage<2;++stage){ok(d->SetTexture(stage,stage?second:first));ok(d->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,stage));ok(d->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2));}
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
                const float w=full?float(width):64,h=full?float(height):64;
                struct V{float x,y,z,w;DWORD c;float u,v,a,b;};const V q[]={{-.5f,-.5f,0,1,0xa0c8b4f0,0,0,1,0},{w-.5f,-.5f,0,1,0xa0c8b4f0,1,0,0,0},{-.5f,h-.5f,0,1,0xa0c8b4f0,0,1,1,1},{w-.5f,h-.5f,0,1,0xa0c8b4f0,1,1,0,1}};
                for(unsigned i=0;i<2;++i){auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};if(direct)ok(transfer.draw(d,160,draw));else ok(transfer.legacyLayer(d,160,draw));}
                ok(d->EndScene());
            };
            double old[5],now[5];for(unsigned i=0;i<5;++i){if(i%2){now[i]=timed(d,[&]{frame(true);});old[i]=timed(d,[&]{frame(false);});}else{old[i]=timed(d,[&]{frame(false);});now[i]=timed(d,[&]{frame(true);});}}
            std::sort(old,old+5);std::sort(now,now+5);std::printf("HDR_TRANSFORM_BENCH width=%u height=%u msaa=2 draws=2 full=%u legacyMs=%.6f directMs=%.6f scope=isolated_pass_not_game_fps\n",width,height,unsigned(full),old[2],now[2]);
        }
        target->Release();
    }
    d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);first->Release();second->Release();constant->Release();resolved->Release();staging->Release();d->Release();api->Release();DestroyWindow(window);return 0;
}
