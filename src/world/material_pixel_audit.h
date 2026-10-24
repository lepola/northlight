#pragma once
#include "material_profile.h"
#include "world_compiled_shaders.h"

// Diagnostic grid only; never reads back the full material targets or waits for
// the GPU. An occlusion query counts fragments surviving MaterialAudit's clip.
class NorthlightMaterialPixelAudit {
    static constexpr unsigned W=128,H=72,Ring=3,Counts=9;
    struct Slot {IDirect3DQuery9* query[Counts]={};uint64_t frame=0;bool pending=false;} slots[Ring];
    IDirect3DDevice9* d;IDirect3DPixelShader9* shader=nullptr;IDirect3DSurface9* target=nullptr;
    bool failed=false;
    void disable(){reset();failed=true;logf("MATERIAL_PIXELS unavailable=1 rendering_retained=1");}
public:
    unsigned last[Counts]={};uint64_t lastFrame=0;
    explicit NorthlightMaterialPixelAudit(IDirect3DDevice9* device):d(device){}
    ~NorthlightMaterialPixelAudit(){reset();}
    void reset(){for(auto& s:slots){for(auto*& q:s.query)drop(q);s.pending=false;}drop(shader);drop(target);failed=false;}
    void poll(){
        for(auto& s:slots){if(!s.pending)continue;DWORD value[Counts]={};bool ready=true;
            for(unsigned i=0;i<Counts;++i){HRESULT hr=s.query[i]->GetData(value+i,sizeof(DWORD),0);if(FAILED(hr)){disable();return;}if(hr==S_FALSE){ready=false;break;}}
            if(!ready)continue;s.pending=false;lastFrame=s.frame;for(unsigned i=0;i<Counts;++i)last[i]=value[i];
            logf("MATERIAL_PIXELS frame=%llu grid=%ux%u valid=%lu normalChanged=%lu cavity=%lu selfShadow=%lu rawCaptured=%lu depthMismatch=%lu sceneNormalRange=%lu sceneNormalFull=%lu sceneOcclusionRange=%lu sampled_grid=1",(unsigned long long)s.frame,W,H,(unsigned long)value[0],(unsigned long)value[1],(unsigned long)value[2],(unsigned long)value[3],(unsigned long)value[4],(unsigned long)value[5],(unsigned long)value[6],(unsigned long)value[7],(unsigned long)value[8]);
        }
    }
    template<class Quad> void sample(NorthlightMaterialProfile* profile,NorthlightStateBlockPool* pool,Quad quad){
        if(!profile||!profile->recording()||failed)return;
        Slot* slot=nullptr;for(auto& s:slots)if(!s.pending){slot=&s;break;}if(!slot)return;
        NorthlightMaterialProfile::Scope timing(profile,NorthlightMaterialProfile::VisibilityAudit,0,true);timing.work(uint64_t(W)*H*Counts);
        if(!shader&&FAILED(d->CreatePixelShader(kMaterialAuditShader,&shader))){disable();return;}
        if(!target&&FAILED(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr))){disable();return;}
        for(auto*& q:slot->query)if(!q&&FAILED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&q))){disable();return;}
        SavedState saved(d,pool);if(!saved.ok)return;
        float control[4]={};if(FAILED(d->GetPixelShaderConstantF(73,control,1)))return;
        if(FAILED(d->SetDepthStencilSurface(nullptr))||FAILED(d->SetRenderTarget(0,target))||FAILED(d->SetPixelShader(shader)))return;
        for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,D3DRS_SCISSORTESTENABLE})if(FAILED(d->SetRenderState(key,FALSE)))return;
        for(unsigned i=0;i<Counts;++i){control[3]=float(i);
            if(FAILED(d->SetPixelShaderConstantF(73,control,1))||FAILED(slot->query[i]->Issue(D3DISSUE_BEGIN))){disable();return;}
            const HRESULT drawn=quad(W,H),ended=slot->query[i]->Issue(D3DISSUE_END);
            if(FAILED(drawn)||FAILED(ended)){disable();return;}
        }
        slot->frame=profile->frameNumber();slot->pending=true;
    }
};
