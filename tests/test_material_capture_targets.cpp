#define main prior_material_bindings_main
#include "test_material_batch_bindings.cpp"
#undef main
static bool skipTargetFaults=false;
struct TargetMeter:MaterialBindingDevice {
    bool stable=false;unsigned failure=0;std::vector<D3DVIEWPORT9> expected;std::vector<bool> writes;
    unsigned vertexCalls=0,indexCalls=0;
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT n,IDirect3DVertexBuffer9* value,UINT offset,UINT stride)override{++vertexCalls;return MaterialBindingDevice::SetStreamSource(n,value,offset,stride);}
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* value)override{++indexCalls;return MaterialBindingDevice::SetIndices(value);}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{*caps={};caps->MaxVertexShaderConst=256;caps->PrimitiveMiscCaps=stable?D3DPMISCCAPS_INDEPENDENTWRITEMASKS:0;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE key,DWORD value)override{
        if(key==D3DRS_COLORWRITEENABLE1&&failure==1&&!faultUsed&&draws.size()==2){faultUsed=true;return E_FAIL;}
        return MaterialBindingDevice::SetRenderState(key,value);
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* target)override{
        if(n==1&&target&&failure==2&&!faultUsed){faultUsed=true;return E_FAIL;}
        return MaterialBindingDevice::SetRenderTarget(n,target);
    }
    void output(){
        const auto i=draws.size();assert(i<expected.size());
        if(std::memcmp(&state.viewport,&expected[i],sizeof state.viewport)){std::fputs("CAPTURE_TARGET_VIEWPORT_MISMATCH\n",stderr);std::abort();}
        const bool active=state.targets[1]&&(stable?state.render[D3DRS_COLORWRITEENABLE1]:state.render[D3DRS_COLORWRITEENABLE]);
        if(active!=writes[i]){std::fputs("CAPTURE_TARGET_WRITE_MISMATCH\n",stderr);std::abort();}
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)override{output();return record(false);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)override{output();return record(false);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)override{output();const auto result=record(true);if(perturbUP)state.render[D3DRS_COLORWRITEENABLE1]=0;return result;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)override{output();const auto result=record(true);if(perturbUP)state.render[D3DRS_COLORWRITEENABLE1]=0;return result;}
};
void MaterialCaptureStateTestAccess::captureTargets(){
    unsigned cases=0;
    for(unsigned stable=0;stable<2;++stable)for(unsigned shape=0;shape<2;++shape)for(unsigned pattern=0;pattern<3;++pattern)for(unsigned up=0;up<2;++up)for(unsigned failure=0;failure<3;++failure){
        if(skipTargetFaults&&failure)continue;
        TargetMeter d;d.stable=stable;d.resetAuxViewport=true;d.perturbUP=up;d.failure=failure;const auto before=d.state;
        unsigned binds=0;bool success=false;
        {
            NorthlightAutoBump b(&d);b.independentCaptureWrites=stable;b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();b.occlusionTarget=&d.auxiliary;b.occlusionTarget->AddRef();
            auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);
            if(!up){b.gpuScene->setFrame(1);IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;
                b.deferredGPU.bind(&d,mesh,vb,ib,[](size_t){return true;},at);b.gpuScene->setFrame(2);b.deferredGPU.bind(&d,mesh,vb,ib,[](size_t){return true;},at);b.deferredGPU.commitPending();for(auto*& v:vb)drop(v);drop(ib);}
            for(unsigned i=0;i<12;++i){auto packet=std::make_unique<NorthlightAutoBump::Deferred>();packet->mesh=mesh;packet->tag=19;packet->vertex=&d.vs;packet->vertex->AddRef();packet->pixel=&d.ps;packet->pixel->AddRef();packet->declaration=&d.decl;packet->declaration->AddRef();packet->vertexCount=0;packet->vertexFloatOnly=true;
                packet->viewport=shape?D3DVIEWPORT9{3,5,32,48,.2f,.8f}:D3DVIEWPORT9{0,0,64,64,0,1};packet->occlusion=pattern==1||(pattern==2&&(i&1));
                std::fill(packet->states,packet->states+14,0);packet->states[4]=packet->states[13]=15;std::fill(packet->raster,packet->raster+NorthlightAutoBump::DeferredRasterCount,0);std::fill(packet->pixelInt,packet->pixelInt+64,0);std::fill(packet->pixelBool,packet->pixelBool+16,FALSE);
                d.expected.push_back(packet->viewport);d.writes.push_back(packet->occlusion);b.deferred.push_back(std::move(packet));}
            success=b.finishCaptures();binds=d.auxiliaryCalls;
            assert(b.deferred.empty()&&d.state==before);
            if(!success)std::fprintf(stderr,"TARGET_FAILURE stable=%u shape=%u pattern=%u up=%u failure=%u triggered=%u draws=%zu\n",stable,shape,pattern,up,failure,unsigned(d.faultUsed),d.draws.size());
            if(pattern&&(failure==2||(stable&&(pattern==2||up)&&failure==1)))assert(!success&&d.faultUsed);else assert(success&&d.draws.size()==12);
            if(stable&&!up&&pattern&&success)assert(binds<=5); // setup, one attachment, SavedState restore
        }
        assert(d.state==before&&d.capture.refs==1&&d.auxiliary.refs==1&&d.privateDepth.refs==1);
        std::printf("CAPTURE_TARGET_CASE stable=%u shape=%u pattern=%u up=%u failure=%u success=%u draws=%zu bindings=%u vertexCalls=%u indexCalls=%u restored=1\n",stable,shape,pattern,up,failure,unsigned(success),d.draws.size(),binds,d.vertexCalls,d.indexCalls);++cases;
    }
    std::printf("CAPTURE_TARGETS_PASS cases=%u exactCapturedViewport=1 effectiveWrites=1 independentMaskFallback=1 failedCalls=1 UPInvalidation=1 restoration=1\n",cases);
}
int main(int argc,char**){skipTargetFaults=argc>1;std::setvbuf(stdout,nullptr,_IONBF,0);MaterialCaptureStateTestAccess::captureTargets();}
