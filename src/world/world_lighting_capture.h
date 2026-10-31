#pragma once
#include "utf8_path.h"
#include <d3d9.h>
#include "local_light_selection.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Explicit, bounded normal-view diagnostics. Nothing is retained beyond this
// render call; a missing export is never replaced with an invented texture.
namespace NorthlightLightingCapture {
class Capture {
    template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}};
    IDirect3DDevice9* device_=nullptr;
    const std::string* directory_=nullptr;
    FILE* journal_=nullptr;
    unsigned capture_=0,frame_=0,rendererFrame_=0,passes_=0;
    bool complete_=true,finished_=false;
    struct Input {IDirect3DBaseTexture9* identity=nullptr;unsigned snapshot=0;bool known=false;};
    std::array<std::array<Input,16>,4> groups_{};
    std::array<IDirect3DPixelShader9*,8> shaders_{};unsigned shaderCount_=0;

    bool path(const char* name,const char* extension,char (&output)[2048])noexcept{
        const int n=std::snprintf(output,sizeof output,"%s/capture-%u-%s.%s",directory_->c_str(),capture_,name,extension);
        if(n<0||size_t(n)>=sizeof output){complete_=false;return false;}return true;
    }
    void unavailable(const char* item,const char* reason)noexcept{
        complete_=false;if(journal_)std::fprintf(journal_,"{\"event\":\"unavailable\",\"pass\":%u,\"item\":\"%s\",\"reason\":\"%s\"}\n",passes_,item,reason);
    }
    bool call(const char* operation,HRESULT hr,bool notFoundAllowed=false)noexcept{
        if(journal_)std::fprintf(journal_,"{\"event\":\"api\",\"pass\":%u,\"operation\":\"%s\",\"HRESULT\":\"%08lx\"}\n",passes_,operation,(unsigned long)hr);
        if(FAILED(hr)&&!(notFoundAllowed&&hr==D3DERR_NOTFOUND))complete_=false;return SUCCEEDED(hr);
    }
    bool bytes(const char* name,const char* extension,const void* data,size_t size)noexcept{
        char output[2048];bool ok=false;
        if(path(name,extension,output))if(FILE* f=NorthlightFiles::open(output,"wb")){
            ok=std::fwrite(data,1,size,f)==size;if(std::fclose(f))ok=false;
        }
        if(journal_)std::fprintf(journal_,"{\"event\":\"file\",\"name\":\"%s.%s\",\"bytes\":%zu,\"saved\":%s}\n",name,extension,size,ok?"true":"false");
        if(!ok)complete_=false;return ok;
    }
    bool surface(const char* name,IDirect3DSurface9* source,bool resolveAllowed=true)noexcept{
        if(!source){unavailable(name,"missing-surface");return false;}
        D3DSURFACE_DESC desc{};if(!call("GetDesc",source->GetDesc(&desc)))return false;
        const unsigned stride=desc.Format==D3DFMT_A16B16G16R16F||desc.Format==D3DFMT_G32R32F?8:
            desc.Format==D3DFMT_R32F||desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8?4:0;
        std::fprintf(journal_,"{\"event\":\"image\",\"name\":\"%s\",\"width\":%u,\"height\":%u,\"format\":%u,\"usage\":%lu,\"pool\":%u,\"samples\":%u,\"sample_quality\":%u}\n",name,desc.Width,desc.Height,unsigned(desc.Format),(unsigned long)desc.Usage,unsigned(desc.Pool),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));
        if(!stride||!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*stride>64u*1024u*1024u){unavailable(name,"unsupported-format-or-size");return false;}
        if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            // Keep the application's scene boundary. Backends that reject this
            // resolve at the capture point produce an incomplete journal.
            if(!resolveAllowed){unavailable(name,"multisample-resolve-failed");return false;}
            Ref<IDirect3DSurface9> resolved;
            if(!call("CreateRenderTarget-color-resolve",device_->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved.p,nullptr))||!resolved.p){unavailable(name,"resolve-target-unavailable");return false;}
            if(!call("StretchRect-color-resolve",device_->StretchRect(source,nullptr,resolved.p,nullptr,D3DTEXF_NONE)))return false;
            std::fprintf(journal_,"{\"event\":\"color-resolve\",\"name\":\"%s\",\"source_samples\":%u,\"method\":\"StretchRect-NONE\",\"per_sample_image\":false}\n",name,unsigned(desc.MultiSampleType));
            return surface(name,resolved.p,false);
        }
        Ref<IDirect3DSurface9> staging;
        if(desc.Pool==D3DPOOL_MANAGED||desc.Pool==D3DPOOL_SYSTEMMEM){staging.p=source;source->AddRef();}
        else{
            if(!(desc.Usage&D3DUSAGE_RENDERTARGET)){unavailable(name,"non-lockable-input-without-render-target-usage");return false;}
            if(!call("CreateOffscreenPlainSurface",device_->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.p,nullptr))||!staging.p){unavailable(name,"staging-unavailable");return false;}
            if(!call("GetRenderTargetData",device_->GetRenderTargetData(source,staging.p)))return false;
        }
        D3DLOCKED_RECT lock{};if(!call("LockRect-readonly",staging.p->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;
        bool ok=false;char output[2048];
        if(lock.pBits&&lock.Pitch>=INT(desc.Width*stride)&&path(name,"fgr",output)){
            if(FILE* f=NorthlightFiles::open(output,"wb")){
                const uint32_t header[]={0x31524746,desc.Width,desc.Height,unsigned(desc.Format),stride};
                ok=std::fwrite(header,sizeof header,1,f)==1;
                for(unsigned y=0;y<desc.Height&&ok;++y)ok=std::fwrite(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,stride,desc.Width,f)==desc.Width;
                if(std::fclose(f))ok=false;
            }
        }
        const bool unlocked=call("UnlockRect-readonly",staging.p->UnlockRect());
        std::fprintf(journal_,"{\"event\":\"file\",\"name\":\"%s.fgr\",\"bytes\":%llu,\"saved\":%s}\n",name,(unsigned long long)(20+uint64_t(desc.Width)*desc.Height*stride),ok?"true":"false");
        if(!ok)complete_=false;return ok&&unlocked;
    }
    bool depth(const char* name,IDirect3DTexture9* source,const D3DSURFACE_DESC& desc)noexcept{
        if(!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*4>64u*1024u*1024u){unavailable(name,"depth-size-limit");return false;}
        // ps_3_0; dcl_texcoord v0.xy; dcl_2d s0; texld r0,v0,s0;
        // mov oC0,r0. No depth inversion, filtering or half-resolution input.
        static const DWORD code[]={0xffff0300,0x0200001f,0x80000005,0x90030000,
            0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0x90e40000,0xa0e40800,
            0x02000001,0x800f0800,0x80e40000,0x0000ffff};
        Ref<IDirect3DSurface9> target;Ref<IDirect3DPixelShader9> shader;
        if(!call("CreateRenderTarget-depth-copy",device_->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr))||!target.p){unavailable(name,"depth-target-unavailable");return false;}
        if(!call("CreatePixelShader-depth-copy",device_->CreatePixelShader(code,&shader.p))||!shader.p){unavailable(name,"depth-shader-unavailable");return false;}
        Ref<IDirect3DStateBlock9> state;Ref<IDirect3DSurface9> targets[4],oldDepth;
        D3DVIEWPORT9 viewport{};float vertex[256*4]{},pixel[224*4]{};D3DCAPS9 caps{};
        bool saved=call("CreateStateBlock-depth-copy",device_->CreateStateBlock(D3DSBT_ALL,&state.p))&&state.p;
        for(unsigned i=0;i<4;++i){const HRESULT hr=device_->GetRenderTarget(i,&targets[i].p);call("GetRenderTarget-depth-copy",hr,i!=0);saved=(SUCCEEDED(hr)||(i&&hr==D3DERR_NOTFOUND))&&saved;}
        {const HRESULT hr=device_->GetDepthStencilSurface(&oldDepth.p);call("GetDepthStencilSurface-depth-copy",hr,true);saved=(SUCCEEDED(hr)||hr==D3DERR_NOTFOUND)&&saved;}
        saved=call("GetViewport-depth-copy",device_->GetViewport(&viewport))&&saved;
        saved=call("GetDeviceCaps-depth-copy",device_->GetDeviceCaps(&caps))&&saved;
        const UINT vertexCount=std::min<UINT>(caps.MaxVertexShaderConst,256);
        saved=vertexCount&&call("GetVertexShaderConstantF-depth-copy",device_->GetVertexShaderConstantF(0,vertex,vertexCount))&&saved;
        saved=call("GetPixelShaderConstantF-depth-copy",device_->GetPixelShaderConstantF(0,pixel,224))&&saved;
        if(!saved||!targets[0].p){unavailable(name,"state-save-failed");return false;}
        bool changed=false,ok=true;
        auto set=[&](const char* operation,HRESULT hr){changed=true;const bool success=call(operation,hr);ok=success&&ok;};
        set("SetDepthStencilSurface-depth-copy",device_->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)set("SetRenderTarget-depth-copy",device_->SetRenderTarget(i,nullptr));
        set("SetRenderTarget-depth-copy",device_->SetRenderTarget(0,target.p));
        set("SetVertexShader-depth-copy",device_->SetVertexShader(nullptr));set("SetFVF-depth-copy",device_->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        set("SetStreamSourceFreq-depth-copy",device_->SetStreamSourceFreq(0,1));
        set("SetTextureStageState-depth-copy",device_->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));
        set("SetTextureStageState-depth-copy",device_->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        set("SetPixelShader-depth-copy",device_->SetPixelShader(shader.p));set("SetTexture-depth-copy",device_->SetTexture(0,source));
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,D3DTEXF_POINT));
        for(auto key:{D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,0));
        for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})set("SetSamplerState-depth-copy",device_->SetSamplerState(0,key,D3DTADDRESS_CLAMP));
        for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_DITHERENABLE,D3DRS_WRAP0})set("SetRenderState-depth-copy",device_->SetRenderState(key,FALSE));
        set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_COLORWRITEENABLE,15));set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID));set("SetRenderState-depth-copy",device_->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));
        D3DVIEWPORT9 copyViewport={0,0,desc.Width,desc.Height,0,1};set("SetViewport-depth-copy",device_->SetViewport(&copyViewport));
        struct Vertex{float x,y,z,w,u,v;};const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(desc.Width)-.5f,-.5f,0,1,1,0},{-.5f,float(desc.Height)-.5f,0,1,0,1},{float(desc.Width)-.5f,float(desc.Height)-.5f,0,1,1,1}};
        if(ok)ok=call("DrawPrimitiveUP-depth-copy",device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
        if(ok)ok=surface(name,target.p);
        if(changed){
            auto restore=[&](const char* operation,HRESULT hr){const bool success=call(operation,hr);ok=success&&ok;};
            restore("restore-SetDepthStencilSurface",device_->SetDepthStencilSurface(nullptr));
            for(unsigned i=1;i<4;++i)restore("restore-SetRenderTarget",device_->SetRenderTarget(i,nullptr));
            restore("restore-SetRenderTarget",device_->SetRenderTarget(0,targets[0].p));
            for(unsigned i=1;i<4;++i)restore("restore-SetRenderTarget",device_->SetRenderTarget(i,targets[i].p));
            restore("restore-SetDepthStencilSurface",device_->SetDepthStencilSurface(oldDepth.p));restore("restore-StateBlockApply",state.p->Apply());
            // Native fixed-function vertex submission may clear registers not
            // reliably restored by ALL on every supported D3D9 implementation.
            restore("restore-VertexConstants",device_->SetVertexShaderConstantF(0,vertex,vertexCount));restore("restore-PixelConstants",device_->SetPixelShaderConstantF(0,pixel,224));
            restore("restore-Viewport",device_->SetViewport(&viewport));
        }
        return ok;
    }
    void shader(IDirect3DPixelShader9* source)noexcept{
        for(unsigned i=0;i<shaderCount_;++i)if(shaders_[i]==source){std::fprintf(journal_,"{\"event\":\"shader-binding\",\"pass\":%u,\"shader\":%u}\n",passes_,i);return;}
        if(!source||shaderCount_==shaders_.size()){unavailable("pixel-shader","missing-or-shader-limit");return;}
        UINT size=0;if(!call("GetFunction-size",source->GetFunction(nullptr,&size)))return;
        if(!size||size>64u*1024u||size%4){unavailable("pixel-shader","unsupported-bytecode-size");return;}
        try{std::vector<unsigned char> code(size);if(!call("GetFunction-bytes",source->GetFunction(code.data(),&size)))return;
            if(size!=code.size()){unavailable("pixel-shader","bytecode-size-changed");return;}
            char name[80];std::snprintf(name,sizeof name,"lighting-shader-%u",shaderCount_);
            if(bytes(name,"bin",code.data(),code.size())){std::fprintf(journal_,"{\"event\":\"shader-binding\",\"pass\":%u,\"shader\":%u}\n",passes_,shaderCount_);shaders_[shaderCount_++]=source;}
        }catch(...){unavailable("pixel-shader","allocation-failed");}
    }
public:
    Capture(IDirect3DDevice9* device,const std::string& directory,unsigned capture,unsigned frame,unsigned rendererFrame,unsigned width,unsigned height)noexcept
        :device_(device),directory_(&directory),capture_(capture),frame_(frame),rendererFrame_(rendererFrame){
        char output[2048];if(device_&&!directory.empty()&&path("lighting-inputs","jsonl.tmp",output))journal_=NorthlightFiles::open(output,"wb");
        logf("WORLD LIGHT_CAPTURE begin capture=%u worldFrame=%u rendererFrame=%u rendererFrameKnown=%u normalView=1 diagnostic=1 fpsEligible=0 saved=%u",capture_,frame_,rendererFrame_,unsigned(rendererFrame_!=0),unsigned(journal_!=nullptr));
        if(journal_)std::fprintf(journal_,"{\"event\":\"begin\",\"schema\":1,\"capture\":%u,\"world_frame\":%u,\"renderer_frame\":%u,\"renderer_frame_known\":%s,\"width\":%u,\"height\":%u,\"normal_view\":true,\"scope\":\"local-light-contribution-inputs\",\"constant_register_count\":116,\"per_source_bank_missing\":false,\"depth_reconstructed\":false,\"source_physical_ownership_proven\":false,\"material_replay_complete\":false,\"whole_scene_replay\":false,\"replay_execution_verified\":false,\"per_sample_color_exported\":false,\"stencil_image_exported\":false,\"scene_boundary_changed\":false,\"fps_eligible\":false}\n",capture_,frame_,rendererFrame_,rendererFrame_?"true":"false",width,height);
    }
    ~Capture(){if(journal_)finish(false);}
    Capture(const Capture&)=delete;
    Capture& operator=(const Capture&)=delete;
    bool active()const noexcept{return journal_!=nullptr&&!finished_;}
    void image(const char* name,IDirect3DSurface9* value)noexcept{if(active())surface(name,value);}
    void sources(const NorthlightLocalLightSelection::Selection& selected,bool fogEnabled)noexcept{
        if(!active())return;
        if(selected.count>selected.sources.size()){unavailable("selection","source-limit");return;}
        std::fprintf(journal_,"{\"event\":\"selection\",\"count\":%u,\"fog_enabled\":%s,\"fog_distance\":%.9g}\n",selected.count,fogEnabled?"true":"false",selected.fogDistance);
        for(unsigned i=0;i<selected.count;++i){const auto& l=selected.sources[i];
            std::fprintf(journal_,"{\"event\":\"source\",\"slot\":%u,\"source_id\":\"%llu\",\"kind\":%u,\"flags\":%u,\"position\":[%.9g,%.9g,%.9g],\"rgb\":[%.9g,%.9g,%.9g],\"range\":[%.9g,%.9g],\"sigma\":%.9g,\"physical_ownership_proven\":false}\n",i,(unsigned long long)l.sourceId,l.kind,l.flags,l.position[0],l.position[1],l.position[2],l.diffuse[0],l.diffuse[1],l.diffuse[2],l.attenuationStart,l.attenuationEnd,selected.fog[i][0]);}
    }
    void inputs(unsigned group,unsigned mask,unsigned immutableMask=0)noexcept{
        if(!active())return;if(group>=groups_.size()){unavailable("inputs","group-limit");return;}
        for(unsigned slot=0;slot<16;++slot)if(mask&(1u<<slot)){
            Ref<IDirect3DBaseTexture9> source;if(!call("GetTexture-input",device_->GetTexture(slot,&source.p)))continue;
            auto& input=groups_[group][slot];const bool existing=input.known&&input.identity==source.p&&(immutableMask&(1u<<slot));
            if(!existing){input.identity=source.p;input.known=true;input.snapshot=group;}
            if(immutableMask&(1u<<slot))for(unsigned previous=0;previous<group;++previous){const auto& old=groups_[previous][slot];if(old.known&&old.identity==source.p){input.snapshot=old.snapshot;break;}}
            char name[80];if(input.snapshot==0&&slot==1)std::snprintf(name,sizeof name,"depth");else std::snprintf(name,sizeof name,"lighting-input-g%u-s%u",input.snapshot,slot);
            std::fprintf(journal_,"{\"event\":\"input-binding\",\"group\":%u,\"slot\":%u,\"null\":%s,\"snapshot_group\":%u,\"file_base\":\"%s\"}\n",group,slot,source.p?"false":"true",input.snapshot,source.p?name:"");
            if(!source.p||input.snapshot!=group||existing)continue;
            if(source.p->GetType()!=D3DRTYPE_TEXTURE){unavailable("input","unsupported-texture-type");continue;}
            auto* texture=static_cast<IDirect3DTexture9*>(source.p);D3DSURFACE_DESC desc{};
            if(!call("GetLevelDesc-input",texture->GetLevelDesc(0,&desc)))continue;
            if(texture->GetLevelCount()!=1){unavailable("input","mip-chain-not-exported");continue;}
            const bool sampledDepth=desc.Format==D3DFORMAT(MAKEFOURCC('I','N','T','Z'))||desc.Format==D3DFORMAT(MAKEFOURCC('D','F','2','4'));
            if(sampledDepth){std::fprintf(journal_,"{\"event\":\"sampled-depth\",\"group\":%u,\"slot\":%u,\"source_format\":%u,\"export_format\":%u,\"depth_reconstructed\":false}\n",group,slot,unsigned(desc.Format),unsigned(D3DFMT_R32F));depth(name,texture,desc);}
            else{Ref<IDirect3DSurface9> level;if(call("GetSurfaceLevel-input",texture->GetSurfaceLevel(0,&level.p)))surface(name,level.p);}
        }
    }
    void skipped(const char* entry,unsigned first,unsigned count,const char* reason)noexcept{
        if(active())std::fprintf(journal_,"{\"event\":\"skipped\",\"entry\":\"%s\",\"first\":%u,\"count\":%u,\"reason\":\"%s\",\"bank_present\":false}\n",entry,first,count,reason);
    }
    void pass(const char* entry,unsigned group,unsigned mask,unsigned first,unsigned count,bool submitted,HRESULT result)noexcept{
        if(!active())return;if(++passes_>64){unavailable("pass","pass-limit");return;}
        std::fprintf(journal_,"{\"event\":\"pass\",\"index\":%u,\"world_frame\":%u,\"renderer_frame\":%u,\"entry\":\"%s\",\"input_group\":%u,\"first\":%u,\"count\":%u,\"submitted\":%s,\"draw_HRESULT\":\"%08lx\",\"vertex_contract\":\"world-quad-XYZRHW-TEX1\"}\n",passes_,frame_,rendererFrame_,entry,group,first,count,submitted?"true":"false",(unsigned long)result);
        if(FAILED(result))complete_=false;
        float bank[116][4];if(call("GetPixelShaderConstantF-pass",device_->GetPixelShaderConstantF(0,bank[0],116))){char name[80];std::snprintf(name,sizeof name,"lighting-pass-%u-constants",passes_);bytes(name,"f32",bank,sizeof bank);}
        Ref<IDirect3DPixelShader9> ps;if(call("GetPixelShader-pass",device_->GetPixelShader(&ps.p)))shader(ps.p);
        Ref<IDirect3DVertexShader9> vs;if(call("GetVertexShader-pass",device_->GetVertexShader(&vs.p))&&vs.p)unavailable("vertex-contract","unexpected-programmable-vertex-shader");
        DWORD fvf=0;if(call("GetFVF-pass",device_->GetFVF(&fvf))&&fvf!=(D3DFVF_XYZRHW|D3DFVF_TEX1))unavailable("vertex-contract","unexpected-FVF");
        UINT frequency=0;if(call("GetStreamSourceFreq-pass",device_->GetStreamSourceFreq(0,&frequency))&&frequency!=1)unavailable("vertex-contract","unexpected-stream-frequency");
        D3DVIEWPORT9 viewport{};RECT rect{};if(call("GetViewport-pass",device_->GetViewport(&viewport)))std::fprintf(journal_,"{\"event\":\"viewport\",\"pass\":%u,\"value\":[%lu,%lu,%lu,%lu,%.9g,%.9g]}\n",passes_,(unsigned long)viewport.X,(unsigned long)viewport.Y,(unsigned long)viewport.Width,(unsigned long)viewport.Height,viewport.MinZ,viewport.MaxZ);
        if(call("GetScissorRect-pass",device_->GetScissorRect(&rect)))std::fprintf(journal_,"{\"event\":\"scissor\",\"pass\":%u,\"value\":[%ld,%ld,%ld,%ld]}\n",passes_,rect.left,rect.top,rect.right,rect.bottom);
        for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_BLENDFACTOR,D3DRS_COLORWRITEENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,D3DRS_ALPHAREF,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_CULLMODE,D3DRS_FILLMODE,D3DRS_CLIPPLANEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_WRAP0}){DWORD value=0;if(call("GetRenderState-pass",device_->GetRenderState(state,&value)))std::fprintf(journal_,"{\"event\":\"render-state\",\"pass\":%u,\"state\":%u,\"value\":%lu}\n",passes_,unsigned(state),(unsigned long)value);}
        for(auto state:{D3DTSS_TEXCOORDINDEX,D3DTSS_TEXTURETRANSFORMFLAGS}){DWORD value=0;if(call("GetTextureStageState-pass",device_->GetTextureStageState(0,state,&value)))std::fprintf(journal_,"{\"event\":\"texture-stage-state\",\"pass\":%u,\"slot\":0,\"state\":%u,\"value\":%lu}\n",passes_,unsigned(state),(unsigned long)value);}
        for(unsigned slot=0;slot<16;++slot)if(mask&(1u<<slot)){
            Ref<IDirect3DBaseTexture9> texture;if(call("GetTexture-pass",device_->GetTexture(slot,&texture.p))){if(group>=groups_.size()||!groups_[group][slot].known||groups_[group][slot].identity!=texture.p)unavailable("input","binding-changed-since-snapshot");}
            for(unsigned state=1;state<=13;++state){DWORD value=0;if(call("GetSamplerState-pass",device_->GetSamplerState(slot,D3DSAMPLERSTATETYPE(state),&value)))std::fprintf(journal_,"{\"event\":\"sampler-state\",\"pass\":%u,\"slot\":%u,\"state\":%u,\"value\":%lu}\n",passes_,slot,state,(unsigned long)value);}
        }
        for(unsigned slot=0;slot<4;++slot){Ref<IDirect3DSurface9> target;const HRESULT hr=device_->GetRenderTarget(slot,&target.p);call("GetRenderTarget-pass",hr,slot!=0);
            std::fprintf(journal_,"{\"event\":\"target-binding\",\"pass\":%u,\"slot\":%u,\"present\":%s}\n",passes_,slot,target.p?"true":"false");if(!slot&&!target.p)unavailable("target","missing-target-zero");
            if(SUCCEEDED(hr)&&target.p){D3DSURFACE_DESC desc{};if(call("GetDesc-target-pass",target.p->GetDesc(&desc)))std::fprintf(journal_,"{\"event\":\"target\",\"pass\":%u,\"slot\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"samples\":%u,\"sample_quality\":%u}\n",passes_,slot,desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));}}
        Ref<IDirect3DSurface9> ds;const HRESULT dsHr=device_->GetDepthStencilSurface(&ds.p);call("GetDepthStencilSurface-pass",dsHr,true);
        std::fprintf(journal_,"{\"event\":\"depth-stencil-binding\",\"pass\":%u,\"present\":%s,\"stencil_image_exported\":false}\n",passes_,ds.p?"true":"false");
        if(ds.p){D3DSURFACE_DESC desc{};if(call("GetDesc-depth-stencil-pass",ds.p->GetDesc(&desc)))std::fprintf(journal_,"{\"event\":\"depth-stencil\",\"pass\":%u,\"width\":%u,\"height\":%u,\"format\":%u,\"samples\":%u,\"sample_quality\":%u}\n",passes_,desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),unsigned(desc.MultiSampleQuality));}
    }
    void finish(bool renderCompleted)noexcept{
        if(!active())return;finished_=true;complete_=complete_&&renderCompleted&&std::ferror(journal_)==0;
        std::fprintf(journal_,"{\"event\":\"end\",\"render_completed\":%s,\"export_complete\":%s,\"passes\":%u,\"authentic_source_replay\":false,\"whole_scene_replay\":false,\"fps_eligible\":false}\n",renderCompleted?"true":"false",complete_?"true":"false",passes_);
        if(std::fclose(journal_))complete_=false;journal_=nullptr;
        char from[2048],to[2048];if(path("lighting-inputs","jsonl.tmp",from)&&path(complete_?"lighting-inputs":"lighting-inputs-incomplete","jsonl",to)&&std::rename(from,to))complete_=false;
        logf("WORLD LIGHT_CAPTURE end capture=%u worldFrame=%u rendererFrame=%u rendererFrameKnown=%u diagnostic=1 fpsEligible=0 renderCompleted=%u exportComplete=%u passes=%u authenticSourceReplay=0 wholeSceneReplay=0",capture_,frame_,rendererFrame_,unsigned(rendererFrame_!=0),unsigned(renderCompleted),unsigned(complete_),passes_);
    }
};
}
