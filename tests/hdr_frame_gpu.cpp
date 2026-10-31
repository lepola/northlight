#undef NDEBUG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_hdr_frame_unused
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "hdr_frame.h"
#include "hdr_color.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmicrosoft-exception-spec"
#include "forwarders.h"
#pragma clang diagnostic pop
#include "world_compiled_shaders.h"

static constexpr unsigned W=64,H=64;
static void fill(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float (&color)[4]){
    ok(NorthlightHdr::quadState(device,target,W,H));ok(device->SetPixelShader(shader));ok(device->SetPixelShaderConstantF(0,color,1));
    ok(device->BeginScene());ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());
}
static void sample(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DSurface9* staging,float (&color)[4],unsigned x=0,unsigned y=0){
    ok(device->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::uint16_t values[4];std::memcpy(values,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8,8);ok(staging->UnlockRect());
    for(unsigned i=0;i<4;++i)color[i]=half(values[i]);
}
static IDirect3DTexture9* legacy(IDirect3DDevice9* device,bool spot){
    Image image{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const float value=spot?(x==W/2&&y==H/2?1.f:.1f):1.f;
        const float pixel[]={value,value,value,1};std::memcpy(image.pixels.data()+(y*W+x)*16,pixel,16);
    }
    return texture(device,image);
}
static IDirect3DTexture9* solid(IDirect3DDevice9* device,float r,float g,float b,float a){
    Image image{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float rgba[]={r,g,b,a};std::memcpy(image.pixels.data(),rgba,16);return texture(device,image);
}
static unsigned decodeOracle(IDirect3DDevice9* d){
    constexpr unsigned w=256,h=4;
    float maximumError=0,transferError=0,alphaError=0;unsigned cases=0;
    for(auto format:{D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8}){
        Image encoded{w,h,format,4,std::vector<unsigned char>(w*h*4)};
        Image floats{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const unsigned a=format==D3DFMT_X8R8G8B8?255:(x+73*y)%256,r=x,g=255-x,b=(x*37+y*13)%256;
            const DWORD pixel=D3DCOLOR_ARGB(a,r,g,b);const float rgba[]={r/255.f,g/255.f,b/255.f,a/255.f};
            std::memcpy(encoded.pixels.data()+(y*w+x)*4,&pixel,4);std::memcpy(floats.pixels.data()+(y*w+x)*16,rgba,16);
        }
        auto* input=texture(d,encoded);auto* reference=texture(d,floats);
        IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;
        for(float expansion:{0.f,1.f,2.f}){
            settings.legacyExpansion=expansion;std::vector<std::uint16_t> expected(w*h*4);
            for(bool candidate:{false,true}){
                ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE));
                ok(d->BeginScene());ok(frame.prepareLegacy(d,candidate?input:reference,nullptr,nullptr,w,h,0,1,settings));ok(d->EndScene());
                DWORD restored=0;ok(d->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&restored));assert(restored==TRUE);
                ok(d->GetRenderTargetData(frame.surface(),staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){
                    const unsigned i=y*w*4+x;const auto bits=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x];
                    if(!candidate)expected[i]=bits;else{
                        const float actual=half(bits),baseline=half(expected[i]),error=std::fabs(actual-baseline);
                        assert(std::isfinite(actual));maximumError=std::max(maximumError,error);
                        float value;std::memcpy(&value,floats.pixels.data()+i*4,4);
                        if(x%4==3){assert(bits==expected[i]);alphaError=std::max(alphaError,std::fabs(actual-value));assert(std::fabs(actual-value)<.0005f);}
                        else assert(error<std::max(.001f,std::fabs(baseline)*.002f));
                        if(!expansion&&x%4!=3){
                            const double linear=value<=.04045?value/12.92:std::pow((value+.055)/1.055,2.4);
                            transferError=std::max(transferError,float(std::fabs(actual-linear)));
                            if(std::fabs(actual-linear)>=.001)std::fprintf(stderr,"decode format=%u x=%u y=%u input=%g actual=%g baseline=%g expected=%g\n",unsigned(format),x,y,value,actual,baseline,linear);
                            assert(std::fabs(actual-linear)<.001);
                        }
                    }
                }
                ok(staging->UnlockRect());
            }
            ++cases;
        }
        ok(d->SetTexture(0,nullptr));input->Release();reference->Release();staging->Release();
    }
    ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
    std::printf("HDR_DECODE_ORACLE cases=%u codes=256 formats=ARGB8/XRGB8 expansion=0/1/2 alpha=exact floatInputControl=true referenceError=%g independentTransferError=%g alphaRoundingError=%g\n",cases,maximumError,transferError,alphaError);return cases;
}
static unsigned batchStateOracle(IDirect3DDevice9* raw,IDirect3DSurface9* source,IDirect3DSurface9* destination,IDirect3DTexture9* encoded){
    struct Meter:ForwardIDirect3DDevice9{
        unsigned states=0,samplers=0,targets=0,failAt=0;
        explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
        HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s,DWORD v)override{++states;return real->SetRenderState(s,v);}
        HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE s,DWORD v)override{++samplers;return real->SetSamplerState(n,s,v);}
        HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{
            if(n==0&&++targets==failAt)return E_FAIL;return real->SetRenderTarget(n,p);
        }
    } d(raw);
    NorthlightHdr::Frame frame;NorthlightStateBlockPool pool(&d);NorthlightHdr::Settings settings;
    ok(d.BeginScene());ok(frame.prepareLegacy(&d,encoded,nullptr,nullptr,W,H,0,1,settings,&pool));ok(d.EndScene());
    unsigned cases=0;
    for(bool automatic:{false,true})for(bool bloom:{false,true})for(bool valid:{false,true}){
        settings.autoExposure=automatic;settings.bloom=bloom?1:0;
        // Start each public entry with hostile game state. Reuse is legal only
        // inside that entry, never across SavedState or a previous frame.
        ok(raw->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));RECT clip={3,5,9,11};ok(raw->SetScissorRect(&clip));
        ok(raw->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE));
        const unsigned passes=1+(automatic&&valid?4:0)+(bloom?2:0);
        for(unsigned failure=0;failure<=passes;++failure){
            d.states=d.samplers=d.targets=0;d.failAt=failure;
            ok(d.BeginScene());const HRESULT hr=frame.finish(&d,source,destination,settings,1.f/60,valid,true,.18f,&pool);ok(d.EndScene());
            assert(failure?FAILED(hr):SUCCEEDED(hr));
            if(!failure){assert(d.states==15&&d.samplers==20+(automatic&&valid?4:0)+(bloom?4:0));assert(d.targets==passes+1);}
            DWORD restored=0;ok(raw->GetRenderState(D3DRS_SCISSORTESTENABLE,&restored));assert(restored==TRUE);
            ok(raw->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&restored));assert(restored==TRUE);
            RECT actual{};ok(raw->GetScissorRect(&actual));assert(!std::memcmp(&actual,&clip,sizeof clip));
            ++cases;
        }
    }
    ok(raw->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(raw->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));
    std::printf("HDR_BATCH_STATE_ORACLE cases=%u setupPerEntry=1 targetFailureRecovery=true\n",cases);return cases;
}
static unsigned filterOracle(IDirect3DDevice9* d){
    unsigned cases=0;float reduceError=0,bloomError=0;
    for(unsigned mode=0;mode<3;++mode)for(unsigned size=0;size<3;++size)for(unsigned pattern=0;pattern<7;++pattern){
        const bool reduction=mode==1,horizontal=mode==2;
        if(reduction&&size==2)continue;
        const unsigned w=reduction?(size?8:64):(size==0?1:size==1?17:32),h=reduction?w:(size==0?1:size==1?13:32);
        const unsigned ow=reduction?w/8:w,oh=reduction?h/8:h;
        Image input{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            const float v=pattern==0?.18f:pattern==1?float((x+y)&1)*32:pattern==2?(x==w/2&&y==h/2?64.f:0):pattern==3?float((x*17+y*31)%101)*.07f:pattern==4?float(y)/h:pattern==5?float(int(x)-int(y))*.125f:1+float(int(x%7)-3)*.00001f;
            const float pixel[]={v,v*.3f,v*.07f,1};std::memcpy(input.pixels.data()+(y*w+x)*16,pixel,16);
        }
        auto* source=texture(d,input);IDirect3DSurface9 *target=nullptr,*readback=nullptr;
        ok(d->CreateRenderTarget(ow,oh,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateOffscreenPlainSurface(ow,oh,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        std::vector<float> reference(ow*oh*4);
        for(bool candidate:{false,true}){
            IDirect3DPixelShader9* shader=nullptr;
            const auto* code=reduction?(candidate?kHdrReduceLinear:kHdrReduce):(candidate?(horizontal?kHdrBloomHorizontal:kHdrBloomVertical):kHdrBloom);
            ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code),&shader));ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,ow,oh));
            const float parameters[]={reduction||horizontal?1.f/w:0,horizontal?0:1.f/h,horizontal?0.f:1.f,0};ok(d->SetPixelShaderConstantF(2,parameters,1));
            ok(d->SetPixelShader(shader));ok(d->SetTexture(0,source));
            if(candidate&&!horizontal){ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));}
            ok(NorthlightHdr::drawQuad(d,ow,oh));ok(d->EndScene());ok(d->GetRenderTargetData(target,readback));
            D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow*4;++x){
                const unsigned i=y*ow*4+x;const float value=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x];
                if(!candidate)reference[i]=value;else{
                    const float error=std::fabs(value-reference[i]);assert(std::isfinite(value));
                    if(reduction){reduceError=std::max(reduceError,error);assert(error<.00002f);}
                    else{bloomError=std::max(bloomError,error);assert(error<.00002f);}
                    if(x%4==3)assert(value==1);
                }
            }
            ok(readback->UnlockRect());shader->Release();
        }
        ok(d->SetTexture(0,nullptr));source->Release();target->Release();readback->Release();++cases;
    }
    std::printf("HDR_FILTER_ORACLE cases=%u reductionError=%g bloomError=%g kernels=constant/checker/impulse/noise/ramp/negative/threshold horizontal=1 vertical=1 edges=clamp\n",cases,reduceError,bloomError);return cases;
}
static unsigned uiStateOracle(IDirect3DDevice9* raw,IDirect3DSurface9* target,IDirect3DPixelShader9* constant){
    struct Meter:ForwardIDirect3DDevice9{
        unsigned blendReads=0,srgbWrites=0;bool failRead=false;
        explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
        HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s,DWORD* v)override{
            if(failRead&&s==D3DRS_SRGBWRITEENABLE)return E_FAIL;
            if(s==D3DRS_SRCBLEND||s==D3DRS_DESTBLEND)++blendReads;return real->GetRenderState(s,v);
        }
        HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s,DWORD v)override{srgbWrites+=s==D3DRS_SRGBWRITEENABLE;return real->SetRenderState(s,v);}
    } d(raw);
    NorthlightHdr::Color color;unsigned cases=0;
    for(bool fixed:{false,true})for(bool blend:{false,true})for(bool srgb:{false,true})for(bool failure:{false,true}){
        ok(NorthlightHdr::quadState(raw,target,W,H));ok(raw->SetPixelShader(fixed?nullptr:constant));
        ok(raw->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));ok(raw->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(raw->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
        ok(raw->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok(raw->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(raw->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
        ok(raw->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(raw->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
        ok(raw->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));ok(raw->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        ok(raw->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb));d.blendReads=d.srgbWrites=0;d.failRead=failure;
        unsigned submits=0;const HRESULT hr=color.draw(&d,160,[&]{++submits;return S_OK;});
        assert(failure?FAILED(hr):SUCCEEDED(hr));assert(submits==unsigned(!failure));
        assert(d.blendReads==(blend?2u:0u)&&d.srgbWrites==(failure?0u:srgb||fixed?2u:1u));
        DWORD restored=0;ok(raw->GetRenderState(D3DRS_SRGBWRITEENABLE,&restored));assert(restored==unsigned(srgb));++cases;
    }
    ok(raw->SetPixelShader(nullptr));ok(raw->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    std::printf("HDR_UI_STATE_ORACLE cases=%u disabledBlendReads=0 srgbRestored=true failedReadSubmits=0\n",cases);return cases;
}
static unsigned gradeOracle(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* target,IDirect3DSurface9* readback,IDirect3DPixelShader9* constant,IDirect3DTexture9* encoded){
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=false;settings.bloom=0;
    ok(d->BeginScene());ok(frame.prepareLegacy(d,encoded,nullptr,nullptr,W,H,0,1,settings));ok(d->EndScene());unsigned cases=0;
    for(float paper:{80.f,203.f,400.f})for(float v:{0.f,.125f,.5f,.99951171875f,1.f,1.0009765625f,4.f,32.f})for(float contrast:{.5f,.999f,1.f,1.001f,2.f})for(float ev:{-2.f,0.f,2.f})for(float nits:{80.f,203.f,203.0001f,400.f,1000.f,2000.f,10000.f}){
        const float input[]={v,v*.25f,v*.0625f,.75f};fill(d,source,constant,input);
        settings.paperWhite=paper;settings.contrast=contrast;settings.exposure=ev;settings.maxNits=nits;
        ok(d->BeginScene());ok(frame.finish(d,source,target,settings,0,true,false));ok(d->EndScene());float actual[4];sample(d,target,readback,actual);
        const double luminance=std::max(double(v)*(.2126+.25*.7152+.0625*.0722),.000001);
        const double white=std::min(paper,nits)/80.,peak=nits/80.;
        const double scale=std::pow(luminance/.18,contrast-1)*std::exp2(ev)*white;
        const double maximum=v*scale,span=peak-white;
        const double mapped=span>.000001?white+span*(1-std::exp(-std::max(maximum-white,0.)/span)):std::min(maximum,peak);
        for(unsigned c=0;c<3;++c){const double expected=input[c]*scale*(maximum>white?mapped/std::max(maximum,.000001):1);
            assert(std::isfinite(actual[c])&&std::fabs(actual[c]-expected)<std::max(.00003,std::fabs(expected)*.001));}
        assert(actual[3]==.75f);++cases;
    }
    std::printf("HDR_GRADE_ORACLE cases=%u contrast=0.5/0.999/1/1.001/2 peak=80/203/203.0001/400/1000/2000/10000 white=80/203/400 alpha=exact\n",cases);return cases;
}
static unsigned fixedUiOracle(IDirect3DDevice9* d,IDirect3DSurface9* destination,IDirect3DSurface9* staging,IDirect3DPixelShader9* constant){
    Image pattern{4,4,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(64)};
    for(unsigned i=0;i<16;++i){const DWORD pixel=D3DCOLOR_ARGB(128+i*8,70+i*10,220-i*8,120+i*5);std::memcpy(pattern.pixels.data()+i*4,&pixel,4);}
    auto* first=texture(d,pattern);std::reverse(pattern.pixels.begin(),pattern.pixels.end());auto* second=texture(d,pattern);
    struct V{float x,y,z,w;DWORD color;float u,v,u1,v1;};
    const DWORD ops[]={D3DTOP_SELECTARG1,D3DTOP_SELECTARG2,D3DTOP_MODULATE};
    unsigned cases=0;float maxError=0;
    for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_4_SAMPLES}){
        IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,samples,0,FALSE,&target,nullptr));
        for(unsigned coordinates=0;coordinates<2;++coordinates)for(DWORD rgb:ops)for(DWORD alpha:ops)for(unsigned opacity:{64u,160u,255u}){
            const V q[]={{-.5f,-.5f,0,1,D3DCOLOR_ARGB(opacity,200,180,240),0,0,1,0},{W-.5f,-.5f,0,1,D3DCOLOR_ARGB(opacity,200,180,240),1,0,0,0},
                         {-.5f,H-.5f,0,1,D3DCOLOR_ARGB(opacity,200,180,240),0,1,1,1},{W-.5f,H-.5f,0,1,D3DCOLOR_ARGB(opacity,200,180,240),1,1,0,1}};
            std::vector<std::uint16_t> reference(W*H*4);NorthlightHdr::Color transfer;
            for(bool direct:{false,true}){
                const float background[]={.3f,.7f,1.4f,.8f};fill(d,target,constant,background);
                ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(nullptr));
                ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2));
                for(unsigned stage=0;stage<2;++stage){
                    ok(d->SetTexture(stage,stage?second:first));
                    ok(d->SetTextureStageState(stage,D3DTSS_COLOROP,stage?rgb:D3DTOP_MODULATE));
                    ok(d->SetTextureStageState(stage,D3DTSS_ALPHAOP,stage?alpha:D3DTOP_MODULATE));
                    for(auto state:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})ok(d->SetTextureStageState(stage,state,D3DTA_TEXTURE));
                    for(auto state:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})ok(d->SetTextureStageState(stage,state,D3DTA_CURRENT));
                    ok(d->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,stage?coordinates:0));
                    ok(d->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
                    ok(d->SetTextureStageState(stage,D3DTSS_RESULTARG,D3DTA_CURRENT));
                    ok(d->SetSamplerState(stage,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
                }
                ok(d->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
                float constants[16],restored[16];for(unsigned i=0;i<16;++i)constants[i]=i*.125f;
                ok(d->SetPixelShaderConstantF(0,constants,4));
                auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
                if(direct)ok(transfer.draw(d,160,draw));else ok(transfer.legacyLayer(d,160,draw));
                ok(d->GetPixelShaderConstantF(0,restored,4));assert(!std::memcmp(constants,restored,sizeof constants));
                IDirect3DPixelShader9* shader=nullptr;ok(d->GetPixelShader(&shader));assert(!shader);
                ok(d->EndScene());ok(d->StretchRect(target,nullptr,destination,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(destination,staging));
                D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                    const auto* pixel=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)+x*4;
                    for(unsigned channel=0;channel<3;++channel){const unsigned i=(y*W+x)*4+channel;if(!direct)reference[i]=pixel[channel];else{
                        const float error=std::fabs(half(reference[i])-half(pixel[channel]));maxError=std::max(maxError,error);assert(error<.025f);
                    }}
                }
                ok(staging->UnlockRect());
            }
            assert(transfer.layers==1&&transfer.multiTextureDirect==1);++cases;
        }
        target->Release();
    }
    // Unsupported stages must keep the original fixed-function shader path.
    const D3DTEXTURESTAGESTATETYPE unsupported[]={D3DTSS_COLOROP,D3DTSS_COLORARG1,D3DTSS_RESULTARG,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTSS_TEXCOORDINDEX};
    const DWORD values[]={D3DTOP_ADD,D3DTA_TEXTURE|D3DTA_COMPLEMENT,D3DTA_TEMP,D3DTTFF_COUNT2,2};
    const DWORD previous[]={D3DTOP_MODULATE,D3DTA_TEXTURE,D3DTA_CURRENT,D3DTTFF_DISABLE,1};
    for(unsigned i=0;i<5;++i){
        NorthlightHdr::Color transfer;const float background[]={.3f,.7f,1.4f,.8f};fill(d,destination,constant,background);
        ok(d->BeginScene());ok(d->SetPixelShader(nullptr));ok(d->SetTexture(0,first));ok(d->SetTexture(1,second));
        ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_MODULATE));ok(d->SetTextureStageState(1,unsupported[i],values[i]));
        unsigned submits=0;ok(transfer.draw(d,160,[&]{++submits;return NorthlightHdr::drawQuad(d,W,H);}));ok(d->EndScene());
        assert(submits==1&&transfer.layers==1&&transfer.multiTextureDirect==0);ok(d->SetTextureStageState(1,unsupported[i],previous[i]));++cases;
    }
    ok(d->SetTexture(0,nullptr));ok(d->SetTexture(1,nullptr));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));first->Release();second->Release();
    std::printf("HDR_FIXED_UI_ORACLE cases=%u rgbError=%g tolerance=0.025 reference=UNORM_compatibility_layer samples=1/4 uvSets=0/1\n",cases,maxError);return cases;
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    IDirect3DSurface9 *source=nullptr,*destination=nullptr,*readback=nullptr,*scalar=nullptr;IDirect3DPixelShader9* constant=nullptr;
    ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&source,nullptr));
    ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&destination,nullptr));
    ok(device->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    ok(device->CreateOffscreenPlainSurface(1,1,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&scalar,nullptr));
    ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    auto* white=legacy(device,false);auto* spot=legacy(device,true);
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=false;settings.bloom=0;
    auto prepare=[&](IDirect3DTexture9* input){ok(device->BeginScene());ok(frame.prepareLegacy(device,input,nullptr,nullptr,W,H,0,1,settings));ok(device->EndScene());};
    auto finish=[&](float dt=1.f/60,bool cut=false){ok(device->BeginScene());ok(frame.finish(device,source,destination,settings,dt,true,cut));ok(device->EndScene());};
    auto exposure=[&]{float value[4];sample(device,frame.exposureSurface(),scalar,value);return value[0];};
    prepare(white);unsigned cases=decodeOracle(device)+batchStateOracle(device,source,destination,white)+filterOracle(device)+uiStateOracle(device,destination,constant)+gradeOracle(device,source,destination,readback,constant,white);
    float actual[4],reference[4];
    const float ordinary[]={1,1,1,1};fill(device,source,constant,ordinary);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]*80-203)<.406);++cases;
    const float grey[]={.18f,.18f,.18f,1};fill(device,source,constant,grey);finish();sample(device,destination,readback,reference,W/2,H/2);
    settings.maxNits=2000;finish();sample(device,destination,readback,actual,W/2,H/2);assert(actual[0]==reference[0]);++cases;
    settings.maxNits=1000;
    const float hdr[]={4,2,.5f,1};fill(device,source,constant,hdr);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(actual[0]*80>203&&actual[0]*80<1000&&std::fabs(actual[0]/actual[1]-2)<.004&&std::fabs(actual[0]/actual[2]-8)<.02);++cases;
    float lowerPeak=actual[0];settings.maxNits=2000;finish();sample(device,destination,readback,actual,W/2,H/2);assert(actual[0]>lowerPeak);++cases;settings.maxNits=1000;
    settings.legacyExpansion=2;prepare(white);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.001);++cases;
    prepare(spot);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(actual[0]>3.9f&&actual[0]<=4.01f);++cases;
    settings.maxNits=4000;prepare(spot);sample(device,frame.surface(),readback,reference,W/2,H/2);assert(reference[0]==actual[0]);++cases;
    settings.legacyExpansion=0;prepare(spot);sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.001);++cases;
    settings.maxNits=1000;settings.autoExposure=true;
    for(unsigned fps:{2u,30u,60u,144u}){
        frame.invalidateExposure();fill(device,source,constant,grey);finish(1.f/fps,true);assert(std::fabs(exposure())<.01);
        const float dark[]={.09f,.09f,.09f,1};fill(device,source,constant,dark);
        for(unsigned i=0;i<2*fps;++i)finish(1.f/fps);
        assert(std::fabs(exposure()-.9f)<.03);++cases;
        fill(device,source,constant,grey);finish(1.f/fps,true);
        const float bright[]={.72f,.72f,.72f,1};fill(device,source,constant,bright);
        for(unsigned i=0;i<fps/2;++i)finish(1.f/fps);
        assert(std::fabs(exposure()+1.8f)<.03);++cases;
    }
    fill(device,source,constant,grey);finish(1.f/60,true);float neutral=exposure();
    settings.bloom=1;settings.maxNits=2000;settings.uiWhite=320;finish(1.f/60,true);assert(exposure()==neutral);++cases;
    const float black[]={0,0,0,1};fill(device,source,constant,black);finish(1.f/60,true);assert(exposure()==1);++cases;
    const float extreme[]={64,64,64,1};fill(device,source,constant,extreme);finish(1.f/60,true);assert(exposure()==-2);++cases;
    settings.autoExposure=false;settings.bloom=0;settings.exposure=1;fill(device,source,constant,grey);finish();sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]*80-.36f*203)<.2);++cases;
    // Production world/material shaders must retain native Northlight energy above SDR white.
    auto* greySource=solid(device,.5f,.5f,.5f,1);auto* depth=solid(device,.5f,0,0,1);auto* light=solid(device,4,2,1,1);
    auto* baseline=solid(device,.5f,.5f,.5f,1);auto* neutralTexture=solid(device,0,0,0,1);auto* distance=solid(device,.1f*100/(100-.5f*(100-.1f)),0,0,1);
    settings.legacyExpansion=0;prepare(greySource);
    float bank[68][4]{};bank[0][0]=bank[0][1]=1.f/W;bank[0][2]=.1f;bank[0][3]=100;bank[1][0]=bank[1][1]=bank[1][2]=1;bank[2][0]=1;
    for(unsigned i=0;i<4;++i)bank[3+i][i]=1;bank[20][2]=bank[30][2]=1;bank[33][0]=W;bank[33][1]=H;bank[33][2]=W/2;bank[33][3]=H/2;
    IDirect3DTexture9* worldSource=greySource;
    auto worldPass=[&](const DWORD* code,IDirect3DTexture9* fog){
        IDirect3DPixelShader9* shader=nullptr;ok(device->CreatePixelShader(code,&shader));ok(device->BeginScene());ok(NorthlightHdr::quadState(device,source,W,H,16));
        for(unsigned i=0;i<16;++i)ok(device->SetTexture(i,neutralTexture));
        const unsigned slots[]={0,1,2,8,9,10,12,15};IDirect3DTexture9* values[]={worldSource,depth,frame.texture(),light,fog,neutralTexture,baseline,distance};
        for(unsigned i=0;i<8;++i)ok(device->SetTexture(slots[i],values[i]));
        ok(device->SetPixelShaderConstantF(0,bank[0],68));ok(device->SetPixelShader(shader));ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());shader->Release();sample(device,source,readback,actual,W/2,H/2);
    };
    worldPass(kWorldCompositeHdrShader,neutralTexture);assert(actual[0]>1.8f&&actual[1]>.9f);++cases;
    worldPass(kWorldUnlitCompositeHdrShader,neutralTexture);assert(std::fabs(actual[0]-.214041f)<.001f);++cases;
    auto* brightFog=solid(device,4,2,1,1);worldPass(kWorldUnlitCompositeHdrShader,brightFog);assert(actual[0]>4&&actual[1]>2);++cases;
    baseline->Release();baseline=solid(device,2,2,2,1);worldSource=spot;settings.legacyExpansion=0;prepare(spot);worldPass(kWorldCompositeHdrShader,neutralTexture);const float unexpandedLit=actual[0];
    settings.legacyExpansion=2;prepare(spot);worldPass(kWorldCompositeHdrShader,neutralTexture);assert(std::fabs(actual[0]-unexpandedLit-3)<.02f);++cases;settings.legacyExpansion=0;worldSource=greySource;
    // Material detail disabled: an already composed FP16 signal is unchanged.
    IDirect3DPixelShader9* material=nullptr;ok(device->CreatePixelShader(kMaterialCompositeHdrShader,&material));
    IDirect3DTexture9* materialInput=solid(device,4,2,.5f,1);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,source,W,H,16));
    ok(device->SetTexture(0,materialInput));ok(device->SetTexture(1,depth));ok(device->SetPixelShaderConstantF(0,bank[0],68));ok(device->SetPixelShader(material));ok(NorthlightHdr::drawQuad(device,W,H));ok(device->EndScene());sample(device,source,readback,actual,W/2,H/2);assert(actual[0]==4&&actual[1]==2);++cases;material->Release();materialInput->Release();
    // Common fixed-function UI shades its original geometry once, without a full-screen layer.
    NorthlightHdr::Color color;
    auto* worldVariant=color.replacement(device,constant,80,0);auto* uiVariant=color.replacement(device,constant,160,0);assert(worldVariant&&uiVariant);
    for(unsigned i=0;i<20;++i){assert(color.replacement(device,constant,80,0)==worldVariant);assert(color.replacement(device,constant,160,0)==uiVariant);}assert(color.patched==2);++cases;
    fill(device,destination,constant,grey);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));
    device->SetPixelShader(nullptr);device->SetTexture(0,white);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    unsigned fixedSubmits=0;ok(color.draw(device,160,[&]{++fixedSubmits;return NorthlightHdr::drawQuad(device,W,H);}));
    assert(fixedSubmits==1&&color.payloadBytes()==0);IDirect3DPixelShader9* restored=nullptr;ok(device->GetPixelShader(&restored));assert(!restored);
    ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]-2)<.005f);++cases;
    // WoW leaves MODULATE in stage 1 between UI draws. A null TEXTURE
    // COLORARG1 terminates that stage; bound textures/other arguments do not.
    for(unsigned scenario=0;scenario<3;++scenario){
        NorthlightHdr::Color fixedColor;fill(device,destination,constant,black);
        ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));
        ok(device->SetPixelShader(nullptr));ok(device->SetTexture(0,white));ok(device->SetTexture(1,scenario==1?white:nullptr));
        for(unsigned stage=0;stage<2;++stage){
            ok(device->SetTextureStageState(stage,D3DTSS_COLOROP,D3DTOP_MODULATE));
            ok(device->SetTextureStageState(stage,D3DTSS_COLORARG1,stage==1&&scenario==2?D3DTA_DIFFUSE:D3DTA_TEXTURE));
            ok(device->SetTextureStageState(stage,D3DTSS_COLORARG2,D3DTA_CURRENT));
            ok(device->SetTextureStageState(stage,D3DTSS_ALPHAOP,D3DTOP_MODULATE));
            ok(device->SetTextureStageState(stage,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
            ok(device->SetTextureStageState(stage,D3DTSS_ALPHAARG2,D3DTA_CURRENT));
            ok(device->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,0));
            ok(device->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        }
        ok(device->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok(fixedColor.draw(device,160,[&]{return NorthlightHdr::drawQuad(device,W,H);}));
        ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);
        assert(std::fabs(actual[0]-2)<.005f&&fixedColor.layers==(scenario==2?1u:0u));
        if(!scenario)assert(fixedColor.payloadBytes()==0);++cases;
    }
    ok(device->SetTexture(1,nullptr));ok(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    cases+=fixedUiOracle(device,destination,readback,constant);
    fill(device,destination,constant,hdr);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));device->SetPixelShader(constant);device->SetPixelShaderConstantF(0,ordinary,1);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
    ok(color.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]-5)<.01f&&actual[1]>2.9f&&color.payloadBytes()==0&&color.layers==0&&color.screenDirect==1);
    DWORD restoredBlend=0;ok(device->GetRenderState(D3DRS_SRCBLEND,&restoredBlend));assert(restoredBlend==D3DBLEND_INVDESTCOLOR);++cases;color.reset();
    // A legacy shader's result still has the original UNORM render-target
    // range, even when replayed directly into a floating-point destination.
    fill(device,destination,constant,black);ok(device->BeginScene());ok(NorthlightHdr::quadState(device,destination,W,H));
    device->SetPixelShader(constant);device->SetPixelShaderConstantF(0,hdr,1);
    ok(color.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));ok(device->EndScene());sample(device,destination,readback,actual,W/2,H/2);
    assert(std::fabs(actual[0]-1)<.005f&&std::fabs(actual[1]-1)<.005f&&std::fabs(actual[2]-.214041f)<.005f);++cases;color.reset();
    for(auto* value:{greySource,depth,light,baseline,neutralTexture,distance,brightFog})value->Release();
    // A real game Reset's resource loss preserves the last adapted exposure.
    settings.autoExposure=true;fill(device,source,constant,grey);finish(1.f/60,true);const float beforeReset=exposure();
    frame.saveAndReset(device);prepare(white);finish(0);assert(std::fabs(exposure()-beforeReset)<.002f);++cases;
    // Presentation writes a compatible SDR image as a second MRT, including under MSAA.
    IDirect3DSurface9 *legacyTarget=nullptr,*legacyRead=nullptr,*multisampled=nullptr;
    ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&legacyTarget,nullptr));ok(device->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&legacyRead,nullptr));
    settings.autoExposure=false;settings.exposure=0;settings.bloom=0;fill(device,source,constant,ordinary);
    ok(device->BeginScene());ok(frame.finish(device,source,destination,settings,0,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,legacyTarget));ok(device->EndScene());
    ok(device->GetRenderTargetData(legacyTarget,legacyRead));D3DLOCKED_RECT bytes{};ok(legacyRead->LockRect(&bytes,nullptr,D3DLOCK_READONLY));assert(*static_cast<DWORD*>(bytes.pBits)==0xffffffff);ok(legacyRead->UnlockRect());++cases;
    legacyTarget->Release();
    ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,&legacyTarget,nullptr));ok(device->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,&multisampled,nullptr));
    ok(device->BeginScene());ok(frame.finish(device,source,multisampled,settings,0,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,legacyTarget));ok(device->EndScene());
    ok(device->StretchRect(multisampled,nullptr,destination,nullptr,D3DTEXF_NONE));sample(device,destination,readback,actual,W/2,H/2);assert(std::fabs(actual[0]*80-203)<.406f);++cases;
    // Match the production sky bootstrap: resolve the current, nonblack
    // legacy framebuffer before replaying an alpha-blended sky layer.
    IDirect3DTexture9* skyCopy=nullptr;IDirect3DSurface9* skyCopySurface=nullptr;
    ok(device->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&skyCopy,nullptr));ok(skyCopy->GetSurfaceLevel(0,&skyCopySurface));
    ok(device->ColorFill(legacyTarget,nullptr,0xff336699));NorthlightHdr::Color skyColor;
    ok(device->BeginScene());ok(skyColor.convertWorld(device,legacyTarget,multisampled,skyCopy,skyCopySurface,W,H,80,80));ok(device->EndScene());
    const float skyLayer[]={4,2,.5f,.25f};
    auto skyDraw=[&](IDirect3DPixelShader9* shader,bool patch){
        ok(device->BeginScene());ok(NorthlightHdr::quadState(device,multisampled,W,H));device->SetPixelShader(shader);device->SetPixelShaderConstantF(0,skyLayer,1);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        if(patch)ok(skyColor.draw(device,80,[&]{return NorthlightHdr::drawQuad(device,W,H);}));else ok(NorthlightHdr::drawQuad(device,W,H));
        ok(device->EndScene());ok(device->StretchRect(multisampled,nullptr,destination,nullptr,D3DTEXF_NONE));sample(device,destination,readback,actual,W/2,H/2);
    };
    if(argc==3){const auto previous=read(argv[2]);std::vector<std::uint32_t> patched;
        assert(NorthlightHdrUi::patch(kHdrCalibration,sizeof kHdrCalibration/4,reinterpret_cast<const std::uint32_t*>(previous.data()),previous.size()/4,80,false,patched));
        IDirect3DPixelShader9* before=nullptr;ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&before));skyDraw(before,false);assert(actual[0]>6);
        std::printf("HDR_LEGACY_COUNTERFACTUAL red=%g UNORM_white=1 overshootReproduced=1\n",actual[0]);before->Release();
        ok(device->BeginScene());ok(skyColor.convertWorld(device,legacyTarget,multisampled,skyCopy,skyCopySurface,W,H,80,80));ok(device->EndScene());
    }
    skyDraw(constant,true);assert(std::fabs(actual[0]-(.25f+.75f*.0331048f))<.002f&&std::fabs(actual[1]-(.25f+.75f*.132868f))<.002f);++cases;
    Image splitDepth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float value=y<H/3?1.f:y<2*H/3?.9985f:.5f;std::memcpy(splitDepth.pixels.data()+(y*W+x)*4,&value,4);}
    auto* split=texture(device,splitDepth);const float skyRed=actual[0];
    ok(device->BeginScene());ok(frame.prepareLegacy(device,white,split,multisampled,W,H,0,.94f,settings));ok(device->EndScene());
    sample(device,frame.surface(),readback,actual,W/2,H/6);assert(std::fabs(actual[0]-skyRed)<.002f);
    // Actual captured horizon depth: beyond .94, but still opaque geometry.
    sample(device,frame.surface(),readback,actual,W/2,H/2);assert(std::fabs(actual[0]-1)<.002f);++cases;
    sample(device,frame.surface(),readback,actual,W/2,3*H/4);assert(std::fabs(actual[0]-1)<.002f);++cases;
    split->Release();skyCopySurface->Release();skyCopy->Release();skyColor.reset();
    // On-demand SDR copies match the former MRT mirror even with MSAA 4.
    fill(device,source,constant,grey);settings.exposure=2;settings.contrast=1.4f;settings.bloom=1;
    ok(device->BeginScene());ok(frame.finish(device,source,multisampled,settings,0,true,false));
    ok(frame.mirrorLegacy(device,legacyTarget));ok(device->EndScene());
    IDirect3DSurface9* legacyResolved=nullptr;ok(device->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&legacyResolved,nullptr));
    ok(device->StretchRect(legacyTarget,nullptr,legacyResolved,nullptr,D3DTEXF_NONE));ok(device->GetRenderTargetData(legacyResolved,legacyRead));ok(legacyRead->LockRect(&bytes,nullptr,D3DLOCK_READONLY));
    const DWORD mirrored=*static_cast<DWORD*>(bytes.pBits);ok(legacyRead->UnlockRect());assert((mirrored&0xffffff)==0x767676);++cases;
    legacyResolved->Release();
    legacyTarget->Release();legacyRead->Release();multisampled->Release();
    frame.reset();white->Release();spot->Release();constant->Release();source->Release();destination->Release();readback->Release();scalar->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::printf("HDR_FRAME_GPU_PASS cases=%u headroom=1 hue=1 diffuseStable=1 legacyExpansion=1 exposureT90=2/30/60/144fps exposureBounds=1 peakBloomUiIndependent=1 worldFogMaterialHeadroom=1 expansionDoesNotBoostLighting=1 fixedFunction=1\n",cases);
}
