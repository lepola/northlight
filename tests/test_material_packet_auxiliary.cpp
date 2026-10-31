#include "material_packet_bindings.h"

struct PacketDevice:MaterialBindingDevice {
    struct Expected {bool integer=false,boolean=false,pixelUsed=true;std::array<int,64> vi,pi;std::array<BOOL,16> vb,pb;};
    std::vector<Expected> expected;
    unsigned getInt=0,getBool=0,setInt=0,setBool=0,pixelGetInt=0,pixelGetBool=0,pixelSetInt=0,pixelSetBool=0;
    unsigned failedGetter=0,failedSetter=0;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD*,IDirect3DVertexShader9** out)override{*out=&vs;vs.AddRef();return D3D_OK;}
    float STDMETHODCALLTYPE GetNPatchMode()override{return 0;}
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* out)override{*out=state.scissor;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{*out=state.render[unsigned(key)];return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT first,int* out,UINT count)override{++getInt;if(failedGetter==1){failedGetter=0;return E_FAIL;}std::memcpy(out,state.vi.data()+first*4,count*16);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT first,BOOL* out,UINT count)override{++getBool;if(failedGetter==2){failedGetter=0;return E_FAIL;}std::memcpy(out,state.vb.data()+first,count*sizeof(BOOL));return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT first,int* out,UINT count)override{++pixelGetInt;std::memcpy(out,state.pi.data()+first*4,count*16);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT first,BOOL* out,UINT count)override{++pixelGetBool;std::memcpy(out,state.pb.data()+first,count*sizeof(BOOL));return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT first,const int* in,UINT count)override{++setInt;if(failedSetter==1&&draws.size()==1){failedSetter=0;return E_FAIL;}return MaterialBindingDevice::SetVertexShaderConstantI(first,in,count);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT first,const BOOL* in,UINT count)override{++setBool;if(failedSetter==2&&draws.size()==1){failedSetter=0;return E_FAIL;}return MaterialBindingDevice::SetVertexShaderConstantB(first,in,count);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT first,const int* in,UINT count)override{++pixelSetInt;return MaterialBindingDevice::SetPixelShaderConstantI(first,in,count);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT first,const BOOL* in,UINT count)override{++pixelSetBool;return MaterialBindingDevice::SetPixelShaderConstantB(first,in,count);}
    HRESULT submit(bool up){
        assert(draws.size()<expected.size());const auto& e=expected[draws.size()];
        if((e.integer&&state.vi!=e.vi)||(e.boolean&&state.vb!=e.vb)||(e.pixelUsed&&(state.pi!=e.pi||state.pb!=e.pb))){std::fputs("MATERIAL_AUXILIARY_BANK_MISMATCH\n",stderr);std::abort();}
        assert(state.vertex==&vs&&state.pixel==&ps&&state.declaration==&decl&&state.frequency[0]==1);
        uint64_t hash=1469598103934665603ull;auto bank=[&](const auto& values){for(auto value:values)hash=(hash^uint64_t(value))*1099511628211ull;};
        if(e.integer)bank(state.vi);if(e.boolean)bank(state.vb);if(e.pixelUsed){bank(state.pi);bank(state.pb);}draws.push_back(hash);
        if(up){state.streams[0]=nullptr;state.indices=nullptr;state.frequency[0]=7;}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)override{return submit(false);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)override{return submit(false);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)override{return submit(true);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)override{return submit(true);}
};

void MaterialCaptureStateTestAccess::packetAuxiliary(){
    using namespace NorthlightWaterShaderPatch;
    const std::vector<uint32_t> prefix{0xfffe0300,0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000000,dst(6,0)};
    auto program=[&](unsigned shape){auto code=prefix;
        if(shape==3){const uint32_t definitions[]={0x05000030,dst(7,3),1,2,3,4,0x0200002f,dst(14,4),1};code.insert(code.end(),std::begin(definitions),std::end(definitions));}
        if(shape==1||shape==3){const uint32_t op[]={0x02000001,dst(0,0),src(7,3)};code.insert(code.end(),std::begin(op),std::end(op));}
        if(shape==2||shape==3){const uint32_t op[]={0x02000001,dst(0,1),src(14,4)};code.insert(code.end(),std::begin(op),std::end(op));}
        if(shape==4){const uint32_t op[]={0x0200002e,dst(3,0),src(1,0),0x03000001,dst(0,0),src(2,12)|0x2000u,src(3,0)};code.insert(code.end(),std::begin(op),std::end(op));}
        if(shape==5)code.push_back(0x00000028); // Unknown flow retains every bank.
        const uint32_t end[]={0x02000001,dst(6,0),src(1,0),65535};code.insert(code.end(),std::begin(end),std::end(end));return code;};
    unsigned cases=0;
    for(unsigned mixed=0;mixed<2;++mixed)for(unsigned failure=0;failure<5;++failure){
        PacketDevice d;d.state.frequency[0]=1;
        bool success=false;unsigned queued=0;size_t packetSize=0,vertexSize=0;
        {
            NorthlightAutoBump b(&d);b.profileTag=19;b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();
            const unsigned sequence[]={0,1,0,2,0,3,4,5,1,0,2};
            for(unsigned i=0;i<std::size(sequence);++i){const unsigned shape=sequence[i];auto code=program(shape);
                const auto usage=NorthlightShaderConstants::analyze(code.data(),code.size(),2);
                assert((usage.analyzed&&!usage.integers.count&&!usage.booleans.count)==(shape==0||shape==3||shape==4));
                NorthlightAutoBump::Vertex vertex;vertex.kind=4;
                if(shape==5){vertex.patched=&d.vs;d.vs.AddRef();} // Failed analysis uses the conservative default.
                else{assert(b.compileVertex(&d.vs,vertex,reinterpret_cast<const DWORD*>(code.data()),code.size()));assert(vertex.floatOnly==(shape==0||shape==3||shape==4));}
                for(unsigned j=0;j<64;++j){d.state.vi[j]=int(i*100+j);d.state.pi[j]=-int(i*100+j);}
                for(unsigned j=0;j<16;++j){d.state.vb[j]=BOOL((i+j)&1);d.state.pb[j]=BOOL((i+j+1)&1);}
                PacketDevice::Expected expected{shape==1||shape==5,shape==2||shape==5,shape==1||shape==2||shape==4||shape==5,d.state.vi,d.state.pi,d.state.vb,d.state.pb};
                const float vertices[12]={0,0,0,0,1,0,0,0,0,1,0,0};
                NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
                NorthlightAutoBump::NativeCapture model;model.shader=&d.ps;model.first=model.constant=20;model.last=24;auto pixelCode=code;pixelCode[0]=0xffff0300;model.pixelFloatOnly=NorthlightNativeMaterialCapture::floatOnlyConstants(pixelCode.data(),pixelCode.size());assert(model.pixelFloatOnly==(shape==0||shape==3));
                NorthlightAutoBump::ReplayState saved(&d,&d.vs,d.state.viewport);NorthlightAutoNormalCache::Lease normal;float constants[20]={};
                const bool fail=(failure==1||failure==2)&&i==1;
                if(fail)d.failedGetter=failure;
                const bool recorded=b.recordCapture(args,normal,false,saved,model,vertex,constants,D3DTADDRESS_WRAP,D3DTADDRESS_WRAP);drop(vertex.patched);
                assert(recorded!=fail);if(recorded){++queued;d.expected.push_back(expected);
                    // Omitted storage must never certify a later used bank, even if its bytes match.
                    auto& packet=*b.deferred.back();if(packet.pixelFloatOnly){for(unsigned j=0;j<64;++j)packet.pixelInt[j]=-int((i+1)*100+j);for(unsigned j=0;j<16;++j)packet.pixelBool[j]=BOOL((i+1+j+1)&1);}if(packet.vertexFloatOnly){for(unsigned j=0;j<64;++j)packet.vertexInt[j]=int((i+1)*100+j);for(unsigned j=0;j<16;++j)packet.vertexBool[j]=BOOL((i+1+j)&1);}}
                assert(!d.failedGetter);
            }
            // Alternate actual resident and UP packets to invalidate replay assumptions.
            if(mixed){for(unsigned phase=0;phase<2;++phase){b.deferredGPU.beginFrame();for(size_t i=0;i<b.deferred.size();i+=2){IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::createBudgetMs()=1e9;(void)([&]{NorthlightReplayGPU::Placement at;const bool ready=b.deferredGPU.bind(&d,b.deferred[i]->mesh,vb,ib,[](size_t){return true;},at);b.deferredGPU.commitPending();return ready||(b.deferredGPU.resident(b.deferred[i]->mesh)&&b.deferredGPU.bind(&d,b.deferred[i]->mesh,vb,ib,[](size_t){return true;},at));}());for(auto*& v:vb)drop(v);drop(ib);}}}
            const auto initial=d.state;d.failedSetter=failure==3?1:failure==4?2:0;success=b.finishCaptures();
            assert(success==(failure<3)&&!d.failedSetter&&b.deferred.empty()&&d.state==initial);
            if(success){assert(d.draws.size()==queued);if(mixed)assert(b.residentDraws&&b.uploadDraws);}else assert(b.failed&&b.deferredDiscarded==queued);
            packetSize=sizeof(NorthlightAutoBump::Deferred);vertexSize=sizeof(NorthlightAutoBump::Vertex);
        }
        assert(d.vs.refs==1&&d.ps.refs==1&&d.decl.refs==1&&d.surface.refs==1&&d.capture.refs==1&&d.privateDepth.refs==1);
        uint64_t hash=1469598103934665603ull;for(auto draw:d.draws)hash=(hash^draw)*1099511628211ull;
        std::printf("MATERIAL_AUXILIARY_CASE mixed=%u failure=%u success=%u queued=%u draws=%zu hash=%llu getInt=%u getBool=%u setInt=%u setBool=%u pixelGetInt=%u pixelGetBool=%u pixelSetInt=%u pixelSetBool=%u packetSize=%zu vertexSize=%zu restored=1\n",mixed,failure,unsigned(success),queued,d.draws.size(),(unsigned long long)hash,d.getInt,d.getBool,d.setInt,d.setBool,d.pixelGetInt,d.pixelGetBool,d.pixelSetInt,d.pixelSetBool,packetSize,vertexSize);++cases;
    }
    std::printf("MATERIAL_AUXILIARY_PASS cases=%u productionCaptureFlush=1 shaderSwitch=1 definitions=1 relativeFloat=1 unknownFallback=1 failures=1 restoration=1\n",cases);
}
int main(){MaterialCaptureStateTestAccess::packetAuxiliary();}
