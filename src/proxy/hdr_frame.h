#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include "hdr_presentation.h"
#include "hdr_compiled_shaders.h"
#include "hdr_quad.h"
#include "saved_state.h"

namespace NorthlightHdr {
// Scene values use a relative linear white of 1. Display nits enter only in
// the last pass. The legacy scene and owned HDR radiance remain distinct until
// composition, so expansion cannot boost Northlight's sources or the UI.
class Frame {
    template<class T> static void release(T*& value){if(value){value->Release();value=nullptr;}}
    struct Target {
        IDirect3DTexture9* texture=nullptr;IDirect3DSurface9* surface=nullptr;
        void reset(){release(surface);release(texture);}
        HRESULT create(IDirect3DDevice9* device,unsigned width,unsigned height){
            if(texture)return S_OK;
            HRESULT hr=device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&texture,nullptr);
            if(SUCCEEDED(hr))hr=texture->GetSurfaceLevel(0,&surface);
            if(FAILED(hr))reset();return hr;
        }
    } linear,meter64,meter8,legacyStatistics,worldStatistics,history[2],bloomTargets[2];
    IDirect3DPixelShader9 *decode=nullptr,*meter=nullptr,*reduce=nullptr,*adapt=nullptr,*bloom=nullptr,*bloomVertical=nullptr,*output=nullptr,*constant=nullptr,*copy=nullptr,*legacyComposite=nullptr,*outputOnly=nullptr,*legacyEncode=nullptr;
    unsigned width=0,height=0,historyIndex=0;bool historyReady=false,worldWasValid=false;
    float restoredExposure=0;bool restoredReady=false;

    static HRESULT pass(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,
                        unsigned width,unsigned height,IDirect3DTexture9* first,IDirect3DTexture9* second=nullptr,IDirect3DTexture9* third=nullptr,const float* parameters=nullptr,bool prepared=false){
        HRESULT hr=prepared?quadTarget(device,target,width,height,3):quadState(device,target,width,height,3);if(FAILED(hr))return hr;
        device->SetPixelShader(shader);device->SetTexture(0,first);device->SetTexture(1,second);device->SetTexture(2,third);
        if(parameters)device->SetPixelShaderConstantF(2,parameters,1);
        return drawQuad(device,width,height);
    }
    HRESULT measure(IDirect3DDevice9* device,IDirect3DTexture9* source,bool encoded,Target& result){
        float parameters[]={0,0,encoded?1.f:0.f,0};
        HRESULT hr=pass(device,meter64.surface,meter,64,64,source,nullptr,nullptr,parameters);if(FAILED(hr))return hr;
        parameters[0]=parameters[1]=1.f/64;
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        hr=pass(device,meter8.surface,reduce,8,8,meter64.texture,nullptr,nullptr,parameters,true);if(FAILED(hr))return hr;
        parameters[0]=parameters[1]=1.f/8;
        hr=pass(device,result.surface,reduce,1,1,meter8.texture,nullptr,nullptr,parameters,true);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        return hr;
    }
public:
    Frame()=default;Frame(const Frame&)=delete;Frame& operator=(const Frame&)=delete;
    ~Frame(){reset();}
    void reset(){
        linear.reset();meter64.reset();meter8.reset();legacyStatistics.reset();worldStatistics.reset();for(auto& target:history)target.reset();for(auto& target:bloomTargets)target.reset();
        release(decode);release(meter);release(reduce);release(adapt);release(bloom);release(bloomVertical);release(output);release(constant);release(copy);release(legacyComposite);release(outputOnly);release(legacyEncode);
        width=height=historyIndex=0;historyReady=worldWasValid=false;
    }
    void invalidateExposure(){historyReady=false;worldWasValid=false;restoredReady=false;}
    IDirect3DTexture9* texture()const{return linear.texture;}
    IDirect3DSurface9* surface()const{return linear.surface;}
    IDirect3DSurface9* exposureSurface()const{return history[historyIndex].surface;}
    std::uint64_t payloadBytes()const{
        std::uint64_t bytes=linear.texture?std::uint64_t(width)*height*8:0;
        for(const auto* target:{&meter64,&meter8,&legacyStatistics,&worldStatistics,&history[0],&history[1]})if(target->texture){const unsigned size=target==&meter64?64:target==&meter8?8:1;bytes+=size*size*8;}
        for(const auto& target:bloomTargets)if(target.texture)bytes+=std::uint64_t(std::max(1u,width/2))*std::max(1u,height/2)*8;return bytes;
    }
    HRESULT ensure(IDirect3DDevice9* device,unsigned w,unsigned h){
        if(!w||!h)return D3DERR_INVALIDCALL;
        if(w!=width||h!=height){linear.reset();for(auto& target:bloomTargets)target.reset();width=w;height=h;}
        struct Shader{const std::uint32_t* code;IDirect3DPixelShader9** shader;};
        const Shader shaders[]={{kHdrDecodeWorld,&decode},{kHdrMeter,&meter},{kHdrReduceLinear,&reduce},{kHdrAdapt,&adapt},{kHdrBloomHorizontal,&bloom},{kHdrBloomVertical,&bloomVertical},{kHdrWorldLinear,&output},{kHdrWorldLinearOnly,&outputOnly},{kHdrLegacyEncode,&legacyEncode},{kHdrCalibration,&constant},{kHdrCopy,&copy},{kHdrLegacyComposite,&legacyComposite}};
        for(const auto& shader:shaders)if(!*shader.shader){HRESULT hr=device->CreatePixelShader(reinterpret_cast<const DWORD*>(shader.code),shader.shader);if(FAILED(hr))return hr;}
        HRESULT hr=linear.create(device,w,h);if(FAILED(hr))return hr;
        for(auto* target:{&meter64,&meter8,&legacyStatistics,&worldStatistics,&history[0],&history[1]}){
            const unsigned size=target==&meter64?64:target==&meter8?8:1;
            const bool fresh=!target->texture;hr=target->create(device,size,size);if(FAILED(hr))return hr;
            if(fresh&&size==1){
                hr=quadState(device,target->surface,1,1,3);if(FAILED(hr))return hr;
                float value[]={restoredExposure,0,0,1};device->SetPixelShaderConstantF(0,value,1);device->SetPixelShader(constant);
                hr=drawQuad(device,1,1);if(FAILED(hr))return hr;
            }
        }
        if(restoredReady){historyReady=worldWasValid=true;restoredReady=false;}
        return S_OK;
    }
    // Called before a real game Reset; never in the normal exposure feedback
    // loop. Only one texel is read to preserve adaptation across resource loss.
    void saveAndReset(IDirect3DDevice9* device){
        if(historyReady&&exposureSurface()){
            IDirect3DSurface9* staging=nullptr;
            if(SUCCEEDED(device->CreateOffscreenPlainSurface(1,1,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr))){
                if(SUCCEEDED(device->GetRenderTargetData(exposureSurface(),staging))){D3DLOCKED_RECT lock{};
                    if(SUCCEEDED(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY))){
                        std::uint16_t bits;std::memcpy(&bits,lock.pBits,2);unsigned exponent=(bits>>10)&31,mantissa=bits&1023;
                        float value=exponent==0?std::ldexp(float(mantissa),-24):exponent<31?std::ldexp(float(1024+mantissa),int(exponent)-25):0;
                        restoredExposure=bits&32768?-value:value;restoredReady=true;staging->UnlockRect();
                    }
                }
                staging->Release();
            }
        }
        reset();
    }
    HRESULT prepareLegacy(IDirect3DDevice9* device,IDirect3DTexture9* source,IDirect3DTexture9* depth,
                          IDirect3DSurface9* capturedSky,unsigned w,unsigned h,float minDepth,float maxDepth,
                          const Settings& settings,NorthlightStateBlockPool* pool=nullptr,bool materialize=true){
        SavedState saved(device,pool);if(!saved.ok)return E_FAIL;
        HRESULT hr=ensure(device,w,h);if(FAILED(hr)||!materialize)return hr;
        if(settings.legacyExpansion>0){hr=measure(device,source,true,legacyStatistics);if(FAILED(hr))return hr;}
        const bool preserveSky=capturedSky&&depth&&maxDepth>minDepth;
        if(preserveSky){for(unsigned i=0;i<3;++i)device->SetTexture(i,nullptr);hr=device->StretchRect(capturedSky,nullptr,linear.surface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;}
        const float parameters[]={minDepth,maxDepth>minDepth?1/(maxDepth-minDepth):1,preserveSky?1.f:0.f,settings.legacyExpansion};
        return pass(device,linear.surface,decode,w,h,source,depth,legacyStatistics.texture,parameters,settings.legacyExpansion>0);
    }
    HRESULT copyBase(IDirect3DDevice9* device,IDirect3DSurface9* target,NorthlightStateBlockPool* pool=nullptr){
        SavedState saved(device,pool);if(!saved.ok||!linear.texture)return E_FAIL;
        return pass(device,target,copy,width,height,linear.texture);
    }
    // The caller binds stencil/name masks after the ordinary effect state.
    HRESULT bindLegacyComposite(IDirect3DDevice9* device,IDirect3DTexture9* encoded,IDirect3DTexture9* depth,IDirect3DTexture9* ao,
                                float minDepth,float maxDepth,float nearZ,float farZ){
        if(!legacyComposite||!linear.texture)return E_FAIL;
        const float parameters[]={minDepth,1/std::max(maxDepth-minDepth,.000001f),nearZ,farZ};
        device->SetTexture(0,linear.texture);device->SetTexture(1,depth);device->SetTexture(2,ao);device->SetTexture(3,encoded);
        device->SetSamplerState(3,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(3,D3DSAMP_MAGFILTER,D3DTEXF_POINT);device->SetSamplerState(3,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(3,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(3,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(3,D3DSAMP_SRGBTEXTURE,FALSE);
        HRESULT hr=device->SetPixelShaderConstantF(2,parameters,1);return SUCCEEDED(hr)?device->SetPixelShader(legacyComposite):hr;
    }
    HRESULT mirrorLegacy(IDirect3DDevice9* device,IDirect3DSurface9* target,NorthlightStateBlockPool* pool=nullptr){
        SavedState saved(device,pool);if(!saved.ok||!linear.texture||!legacyEncode)return E_FAIL;
        return pass(device,target,legacyEncode,width,height,linear.texture);
    }
    HRESULT finish(IDirect3DDevice9* device,IDirect3DSurface9* composed,IDirect3DSurface9* destination,const Settings& settings,
                   float dt,bool validWorld,bool sceneCut,float reference=.18f,NorthlightStateBlockPool* pool=nullptr,
                   IDirect3DTexture9* firstBloom=nullptr,IDirect3DSurface9* firstBloomSurface=nullptr,
                   IDirect3DTexture9* secondBloom=nullptr,IDirect3DSurface9* secondBloomSurface=nullptr,IDirect3DSurface9* legacyDestination=nullptr){
        SavedState saved(device,pool);if(!saved.ok||!linear.texture)return E_FAIL;
        HRESULT hr=S_OK;bool prepared=false;
        if(composed){for(unsigned i=0;i<3;++i)device->SetTexture(i,nullptr);hr=device->StretchRect(composed,nullptr,linear.surface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;}
        if(settings.autoExposure&&validWorld){
            hr=measure(device,linear.texture,false,worldStatistics);if(FAILED(hr))return hr;
            float timing[]={std::isfinite(dt)?std::clamp(dt,0.f,60.f):0,settings.lightT90,settings.darkT90,reference};
            float limits[]={settings.minEV,settings.maxEV,!historyReady||!worldWasValid||sceneCut?1.f:0.f,0};
            device->SetPixelShaderConstantF(3,timing,1);device->SetPixelShaderConstantF(4,limits,1);
            const unsigned next=1-historyIndex;
            hr=pass(device,history[next].surface,adapt,1,1,worldStatistics.texture,history[historyIndex].texture,nullptr,nullptr,true);if(FAILED(hr))return hr;
            prepared=true;
            historyIndex=next;historyReady=true;
        }
        worldWasValid=validWorld;
        IDirect3DTexture9* bloomTexture=nullptr;
        if(settings.bloom>0){
            const unsigned bw=std::max(1u,width/2),bh=std::max(1u,height/2);
            if(!firstBloom||!firstBloomSurface||!secondBloom||!secondBloomSurface){
                for(auto& target:bloomTargets){hr=target.create(device,bw,bh);if(FAILED(hr))return hr;}
                firstBloom=bloomTargets[0].texture;firstBloomSurface=bloomTargets[0].surface;
                secondBloom=bloomTargets[1].texture;secondBloomSurface=bloomTargets[1].surface;
            }
            float parameters[]={2.f/width,0,0,0};
            hr=pass(device,firstBloomSurface,bloom,bw,bh,linear.texture,nullptr,nullptr,parameters,prepared);if(FAILED(hr))return hr;
            parameters[0]=0;parameters[1]=1.f/bh;parameters[2]=1;
            device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            hr=pass(device,secondBloomSurface,bloomVertical,bw,bh,firstBloom,nullptr,nullptr,parameters,true);if(FAILED(hr))return hr;
            device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            prepared=true;
            bloomTexture=secondBloom;
        }
        hr=prepared?quadTarget(device,destination,width,height,3):quadState(device,destination,width,height,3);if(FAILED(hr))return hr;
        if(legacyDestination){hr=device->SetRenderTarget(1,legacyDestination);if(FAILED(hr))return hr;device->SetRenderState(D3DRS_COLORWRITEENABLE1,15);}
        const float peak=std::max(settings.maxNits,80.f),white=std::min(settings.paperWhite,peak)/80;
        float scale[]={white,white,white,peak/80},grade[]={settings.contrast,std::exp2(settings.exposure),settings.autoExposure?1.f:0.f,settings.bloom};
        const float span=scale[3]-white,shoulder[]={span>.000001f?-1.44269504089f/span:0,span,0,0};
        device->SetPixelShaderConstantF(2,shoulder,1);
        device->SetPixelShaderConstantF(0,scale,1);device->SetPixelShaderConstantF(1,grade,1);
        device->SetPixelShader(legacyDestination?output:outputOnly);device->SetTexture(0,linear.texture);device->SetTexture(1,history[historyIndex].texture);device->SetTexture(2,bloomTexture);
        device->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(2,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        return drawQuad(device,width,height);
    }
};
}
