#include "material_bindings_reference.h"
#include <chrono>
struct SamplerBackend:ForwardIDirect3DDevice9 {
    DWORD values[16][16]={};int failed=-1;unsigned calls=0;
    SamplerBackend():ForwardIDirect3DDevice9(nullptr){for(unsigned s=0;s<16;++s)for(unsigned k=1;k<14;++k)values[s][k]=s*100+k;}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD* out)override{++calls;if(int(k)==failed)return E_FAIL;*out=values[s][k];return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE k,DWORD value)override{if(int(k)==failed)return E_FAIL;values[s][k]=value;return D3D_OK;}
};
static constexpr unsigned mask=((1u<<13)-1)^(1u<<(D3DSAMP_ADDRESSW-1))^(1u<<(D3DSAMP_DMAPOFFSET-1));
static bool readSampler(ExtensionDevice& d,unsigned stage,DWORD* out,bool batch,unsigned requested=mask){
    if(batch&&d.knownSamplerStates(stage,requested,out))return true;
    for(unsigned i=0;i<13;++i)if((requested&(1u<<i))&&FAILED(d.GetSamplerState(stage,D3DSAMPLERSTATETYPE(i+1),out+i)))return false;
    return true;
}
static void test(){
    SamplerBackend backend;DeviceMirror mirror;ExtensionDevice ext(&backend,&mirror);DWORD values[13],expected[13];
    for(unsigned stage=0;stage<16;++stage){std::fill(expected,expected+13,0xfeedface);std::fill(values,values+13,0xfeedface);
        assert(readSampler(ext,stage,expected,false));const auto calls=backend.calls;
        assert(ext.knownSamplerStates(stage,mask,values)&&backend.calls==calls&&!std::memcmp(values,expected,sizeof values));
        assert(!mirror.samplerKnown[stage][D3DSAMP_ADDRESSW]&&!mirror.samplerKnown[stage][D3DSAMP_DMAPOFFSET]);
        for(unsigned i=0;i<13;++i)if(mask&(1u<<i)){
            mirror.samplerKnown[stage][i+1]=false;std::fill(values,values+13,0xfeedface);
            assert(!ext.knownSamplerStates(stage,mask,values));for(auto v:values)assert(v==0xfeedface);
            backend.failed=int(i+1);assert(!readSampler(ext,stage,values,true));backend.failed=-1;
            assert(readSampler(ext,stage,values,true)&&!std::memcmp(values,expected,sizeof values));
        }
    }
    assert(SUCCEEDED(ext.SetSamplerState(3,D3DSAMP_MINFILTER,99)));assert(readSampler(ext,3,values,true));assert(values[D3DSAMP_MINFILTER-1]==99);
    backend.failed=D3DSAMP_MINFILTER;assert(FAILED(ext.SetSamplerState(3,D3DSAMP_MINFILTER,77)));assert(!ext.knownSamplerStates(3,mask,values));backend.failed=-1;assert(readSampler(ext,3,values,true)&&values[D3DSAMP_MINFILTER-1]==99);
    for(unsigned scenario=0;scenario<4;++scenario){assert(readSampler(ext,0,values,false));
        if(scenario==0)mirror.recording=true;if(scenario==1)mirror.enabled=false;if(scenario==2)mirror.rawDepth=1;if(scenario==3)mirror.invalidate();
        assert(!ext.knownSamplerStates(0,mask,values));mirror.recording=false;mirror.enabled=true;mirror.rawDepth=0;
    }
    assert(readSampler(ext,0,values,false));{ExtensionDevice::RawScope raw(ext);assert(!ext.knownSamplerStates(0,mask,values));}assert(!ext.knownSamplerStates(0,mask,values));
    assert(readSampler(ext,0,values,true));mirror.gate.exclusive.store(true);assert(ext.knownSamplerStates(0,mask,values));mirror.gate.exclusive.store(false);
    std::fill(values,values+13,0xfeedface);assert(!ext.knownSamplerStates(16,mask,values));assert(!ext.knownSamplerStates(0,1u<<13,values));assert(!ext.knownSamplerStates(0,mask,nullptr));for(auto v:values)assert(v==0xfeedface);
    std::puts("MATERIAL_SAMPLER_BATCH_PASS stages=16 states=11 exact_values atomic_failure unused_unknown setter_failure recording raw disabled invalidation exclusive invalid_arguments");
}
static void allStates(){
    constexpr unsigned full=(1u<<13)-1;SamplerBackend backend;DeviceMirror mirror;ExtensionDevice ext(&backend,&mirror);DWORD expected[13],out[13];
    for(unsigned stage=0;stage<16;++stage){assert(readSampler(ext,stage,expected,false,full));const auto calls=backend.calls;
        assert(readSampler(ext,stage,out,true,full)&&backend.calls==calls&&!std::memcmp(expected,out,sizeof out));
        for(unsigned i=0;i<13;++i){mirror.samplerKnown[stage][i+1]=false;std::fill(out,out+13,0xfeedface);
            assert(!ext.knownSamplerStates(stage,full,out));for(auto value:out)assert(value==0xfeedface);
            backend.failed=int(i+1);assert(!readSampler(ext,stage,out,true,full));backend.failed=-1;
            assert(readSampler(ext,stage,out,true,full)&&!std::memcmp(expected,out,sizeof out));}}
    std::puts("MODEL_NORMAL_SAMPLERS_PASS stages=16 states=13 exact_output atomic_failure all_unknown_lanes");
}
int main(int argc,char**){test();allStates();if(argc<2)return 0;const unsigned requested=argc>2?(1u<<13)-1:mask;
    SamplerBackend backend;DeviceMirror mirror;ExtensionDevice ext(&backend,&mirror);DWORD out[13];
    for(unsigned s=0;s<16;++s)assert(readSampler(ext,s,out,false,requested));
    for(bool unknown:{false,true}){double times[2][7];unsigned calls[2]={};
        for(unsigned round=0;round<7;++round)for(unsigned turn=0;turn<2;++turn){const unsigned batch=(round+turn)%2;auto first=backend.calls;const auto start=std::chrono::steady_clock::now();
            for(unsigned frame=0;frame<500;++frame){MirrorGuard outer(mirror.gate,MirrorSite::Device);
                for(unsigned draw=0;draw<286;++draw){const unsigned stage=draw%16;if(unknown)mirror.samplerKnown[stage][D3DSAMP_MINFILTER]=false;assert(readSampler(ext,stage,out,batch,requested));}}
            times[batch][round]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/500;calls[batch]+=backend.calls-first;}
        std::sort(times[0],times[0]+7);std::sort(times[1],times[1]+7);
        std::printf("SAMPLER_BATCH_AB draws=286 unknown=%u oldMs=%.6f newMs=%.6f oldBackendCalls=%u newBackendCalls=%u\n",unsigned(unknown),times[0][3],times[1][3],calls[0],calls[1]);
    }
}
