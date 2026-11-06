#include "material_bindings_reference.h"
#include <array>
#include <string>
struct MaterialBindingState {
    std::array<IDirect3DSurface9*,4> targets{};IDirect3DSurface9* depth=nullptr;D3DVIEWPORT9 viewport{};
    IDirect3DVertexShader9* vertex=nullptr;IDirect3DPixelShader9* pixel=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
    std::array<IDirect3DVertexBuffer9*,4> streams{};IDirect3DIndexBuffer9* indices=nullptr;
    std::array<UINT,4> offsets{},strides{},frequency{{3,1,1,1}};
    std::array<float,1024> vf{};std::array<float,896> pf{};std::array<int,64> vi{},pi{};std::array<BOOL,16> vb{},pb{};
    std::array<DWORD,256> render{};std::array<DWORD,16*14> samplers{};std::array<IDirect3DBaseTexture9*,16> textures{};RECT scissor{};
    bool operator==(const MaterialBindingState& o)const{return targets==o.targets&&depth==o.depth&&!std::memcmp(&viewport,&o.viewport,sizeof viewport)&&vertex==o.vertex&&pixel==o.pixel&&declaration==o.declaration&&streams==o.streams&&indices==o.indices&&offsets==o.offsets&&strides==o.strides&&frequency==o.frequency&&vf==o.vf&&pf==o.pf&&vi==o.vi&&pi==o.pi&&vb==o.vb&&pb==o.pb&&render==o.render&&samplers==o.samplers&&textures==o.textures&&!std::memcmp(&scissor,&o.scissor,sizeof scissor);}
};
struct BindingVertexBuffer:ForwardIDirect3DVertexBuffer9 {
    unsigned refs=1;std::vector<unsigned char> bytes;explicit BindingVertexBuffer(UINT n):ForwardIDirect3DVertexBuffer9(nullptr),bytes(n){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** out,DWORD)override{assert(uint64_t(offset)+size<=bytes.size());*out=bytes.data()+offset;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Unlock()override{return D3D_OK;}
};
struct BindingIndexBuffer:ForwardIDirect3DIndexBuffer9 {
    unsigned refs=1;std::vector<unsigned char> bytes;explicit BindingIndexBuffer(UINT n):ForwardIDirect3DIndexBuffer9(nullptr),bytes(n){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Lock(UINT offset,UINT size,void** out,DWORD)override{assert(uint64_t(offset)+size<=bytes.size());*out=bytes.data()+offset;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Unlock()override{return D3D_OK;}
};
struct MaterialBindingDevice:CaptureBackend {
    MaterialBindingState state;CaptureSurface capture,auxiliary,privateDepth;
    bool resetAuxViewport=false,perturbUP=false,faultUsed=false;unsigned fault=0,viewportCalls=0,frequencyCalls=0,auxiliaryCalls=0;
    std::vector<uint64_t> draws;
    struct FloatCall {bool vertex;UINT first,count;size_t draw;bool failed;};
    struct FloatBanks {std::array<float,1024> vertex;std::array<float,896> pixel;};
    std::vector<FloatCall> floatCalls;
    std::vector<FloatBanks> expectedFloatBanks;
    struct Block:IDirect3DStateBlock9 {
        MaterialBindingDevice& d;MaterialBindingState captured;unsigned refs=1;
        explicit Block(MaterialBindingDevice& device):d(device),captured(device.state){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
        ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
        HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out)override{*out=&d;return D3D_OK;}
        HRESULT STDMETHODCALLTYPE Capture()override{captured=d.state;return D3D_OK;}
        HRESULT STDMETHODCALLTYPE Apply()override{auto targets=d.state.targets;auto depth=d.state.depth;d.state=captured;d.state.targets=targets;d.state.depth=depth;return D3D_OK;}
    };
    MaterialBindingDevice(){state.targets[0]=&surface;state.depth=&surface;state.viewport={0,0,64,64,.1f,.9f};state.vertex=&vs;state.pixel=&ps;state.declaration=&decl;state.scissor={1,2,40,50};for(unsigned i=0;i<state.vf.size();++i)state.vf[i]=float(i)*.01f;for(unsigned i=0;i<state.pf.size();++i)state.pf[i]=-float(i)*.02f;capture.samples=auxiliary.samples=privateDepth.samples=D3DMULTISAMPLE_NONE;}
    bool refuse(unsigned kind){if(fault==kind&&!faultUsed&&draws.empty()){faultUsed=true;return true;}return false;}
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out)override{*out=new Block(*this);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** out)override{*out=state.targets[n];if(*out)(*out)->AddRef();return *out?D3D_OK:D3DERR_NOTFOUND;}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* value)override{
        if(n==1){++auxiliaryCalls;if(fault==3&&!faultUsed&&draws.size()>=2&&draws.size()<6&&state.targets[1]!=value){faultUsed=true;return E_FAIL;}}
        state.targets[n]=value;if(n==0||(n==1&&resetAuxViewport))state.viewport={0,0,64,64,0,1};return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out)override{*out=state.depth;if(*out)(*out)->AddRef();return *out?D3D_OK:D3DERR_NOTFOUND;}
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* value)override{state.depth=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out)override{*out=state.viewport;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* value)override{++viewportCalls;if(refuse(2))return E_FAIL;state.viewport=*value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT n,UINT value)override{++frequencyCalls;if(refuse(1))return E_FAIL;state.frequency[n]=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT n,UINT* out)override{*out=state.frequency[n];return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT n,IDirect3DVertexBuffer9* value,UINT offset,UINT stride)override{state.streams[n]=value;state.offsets[n]=offset;state.strides[n]=stride;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* value)override{state.indices=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value)override{state.declaration=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* value)override{state.vertex=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* value)override{state.pixel=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{std::memcpy(out,state.vf.data()+first*4,count*16);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT first,float* out,UINT count)override{std::memcpy(out,state.pf.data()+first*4,count*16);return D3D_OK;}
    HRESULT floatConstants(bool vertex,UINT first,const float* in,UINT count){
        assert(first<=UINT(vertex?256:224)&&count<=UINT(vertex?256:224)-first);
        const bool failed=!faultUsed&&draws.size()==1&&fault==unsigned(vertex?10:11);
        floatCalls.push_back({vertex,first,count,draws.size(),failed});
        if(failed){faultUsed=true;return E_FAIL;}
        if(count)std::memcpy((vertex?state.vf.data():state.pf.data())+first*4,in,count*16);return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* in,UINT count)override{return floatConstants(true,first,in,count);}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT first,const float* in,UINT count)override{return floatConstants(false,first,in,count);}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT first,const int* in,UINT count)override{std::memcpy(state.vi.data()+first*4,in,count*16);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT first,const int* in,UINT count)override{std::memcpy(state.pi.data()+first*4,in,count*16);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT first,const BOOL* in,UINT count)override{std::memcpy(state.vb.data()+first,in,count*sizeof(BOOL));return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT first,const BOOL* in,UINT count)override{std::memcpy(state.pb.data()+first,in,count*sizeof(BOOL));return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE key,DWORD value)override{assert(unsigned(key)<state.render.size());state.render[key]=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE key,DWORD value)override{assert(n<16&&unsigned(key)<14);state.samplers[n*14+unsigned(key)]=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD n,IDirect3DBaseTexture9* value)override{assert(n<16);state.textures[n]=value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* value)override{state.scissor=*value;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Clear(DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)override{return D3D_OK;}
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT n,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9** out,HANDLE*)override{*out=new BindingVertexBuffer(n);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT n,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9** out,HANDLE*)override{*out=new BindingIndexBuffer(n);return D3D_OK;}
    HRESULT record(bool up){
        if(state.frequency[0]!=1){std::fputs("MATERIAL_BINDING_FREQUENCY_MISMATCH\n",stderr);std::abort();}
        assert(state.vertex==&vs&&state.pixel==&ps&&state.declaration==&decl);
        if(!expectedFloatBanks.empty()){
            assert(draws.size()<expectedFloatBanks.size());const auto& expected=expectedFloatBanks[draws.size()];
            if(std::memcmp(state.vf.data(),expected.vertex.data(),sizeof state.vf)||std::memcmp(state.pf.data(),expected.pixel.data(),sizeof state.pf)){
                std::fputs("MATERIAL_CONSTANT_BANK_MISMATCH\n",stderr);std::abort();
            }
        }
        uint64_t h=1469598103934665603ull;auto hash=[&](const void* data,size_t bytes){const auto* p=static_cast<const unsigned char*>(data);for(size_t i=0;i<bytes;++i)h=(h^p[i])*1099511628211ull;};
        hash(&state.viewport,sizeof state.viewport);hash(state.frequency.data(),sizeof state.frequency);hash(state.vf.data(),sizeof state.vf);hash(state.pf.data(),sizeof state.pf);hash(state.vi.data(),sizeof state.vi);hash(state.pi.data(),sizeof state.pi);hash(state.vb.data(),sizeof state.vb);hash(state.pb.data(),sizeof state.pb);hash(state.render.data(),sizeof state.render);hash(state.samplers.data(),sizeof state.samplers);hash(&state.scissor,sizeof state.scissor);
        const bool auxiliaryBound=state.targets[1]==&auxiliary;hash(&auxiliaryBound,sizeof auxiliaryBound);hash(&up,sizeof up);draws.push_back(h);
        if(up){state.streams[0]=nullptr;state.indices=nullptr;if(perturbUP){state.frequency[0]=7;state.viewport={1,1,32,32,.3f,.7f};state.targets[1]=nullptr;}}
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE,UINT,UINT)override{return record(false);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)override{assert(state.streams[0]&&state.indices);return record(false);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void* vertices,UINT stride)override{assert(vertices&&stride==12);return record(true);}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE,UINT,UINT,UINT,const void* indices,D3DFORMAT format,const void* vertices,UINT stride)override{assert(indices&&vertices&&format==D3DFMT_INDEX32&&stride==12);return record(true);}
};
void MaterialCaptureStateTestAccess::replayBindings(){
    unsigned cases=0;
    for(unsigned reset=0;reset<2;++reset)for(unsigned shape=0;shape<3;++shape)for(unsigned failure=0;failure<4;++failure){
        MaterialBindingDevice d;d.resetAuxViewport=reset;d.perturbUP=true;const auto initial=d.state;
        bool success=false;unsigned resident=0,up=0;
        {
            NorthlightAutoBump b(&d);b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();b.occlusionTarget=&d.auxiliary;b.occlusionTarget->AddRef();
            std::shared_ptr<const NorthlightDrawSnapshot::Mesh> meshes[4];
            for(unsigned i=0;i<4;++i){auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->indexed=(i%2)==0;mesh->topology=D3DPT_TRIANGLELIST;mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);if(mesh->indexed)mesh->indices={0,1,2};meshes[i]=std::move(mesh);}
            NorthlightReplayGPU::createBudgetMs()=1e9;
            for(unsigned phase=0;phase<2;++phase){b.deferredGPU.beginFrame();for(unsigned i=0;i<2;++i){IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;const bool bound=([&]{NorthlightReplayGPU::Placement at;const bool ready=b.deferredGPU.bind(&d,meshes[i],vb,ib,[](size_t){return true;},at);b.deferredGPU.commitPending();return ready||(b.deferredGPU.resident(meshes[i])&&b.deferredGPU.bind(&d,meshes[i],vb,ib,[](size_t){return true;},at));}());assert(bound==bool(phase));for(auto*& p:vb)drop(p);drop(ib);}}
            const unsigned mixed[]={0,0,2,1,3,1};
            for(unsigned i=0;i<6;++i){auto packet=std::make_unique<NorthlightAutoBump::Deferred>();packet->mesh=meshes[shape==1?mixed[i]:i%2];packet->vertex=&d.vs;packet->vertex->AddRef();packet->pixel=&d.ps;packet->pixel->AddRef();packet->declaration=&d.decl;packet->declaration->AddRef();packet->viewport={0,0,64,64,0,1};packet->occlusion=shape==0?true:((i/2)%2==0);packet->tag=19;packet->first=20;packet->pixelConstants.assign(20,float(i));std::fill(packet->vertexConstants,packet->vertexConstants+1024,float(i));std::fill(packet->vertexInt,packet->vertexInt+64,int(i));std::fill(packet->pixelInt,packet->pixelInt+64,int(i));std::fill(packet->vertexBool,packet->vertexBool+16,BOOL(i&1));std::fill(packet->pixelBool,packet->pixelBool+16,BOOL(i&1));std::fill(packet->states,packet->states+14,DWORD(i));std::fill(packet->raster,packet->raster+NorthlightAutoBump::DeferredRasterCount,DWORD(i));
                if(shape==2){packet->occlusion=(i&1)!=0;if(i==2||i==3)packet->viewport={0,0,64,64,.2f,.8f};if(i==4)packet->viewport={3,5,32,48,0,1};}
                b.deferred.push_back(std::move(packet));
            }
            d.fault=failure;success=b.finishCaptures();resident=b.residentDraws;up=b.uploadDraws;assert(b.deferred.empty());if(success)assert(d.draws.size()==6&&b.flushed==6);else assert(d.faultUsed&&b.failed);
            assert(d.state==initial);
        }
        assert(d.state==initial&&d.vs.refs==1&&d.ps.refs==1&&d.decl.refs==1&&d.surface.refs==1&&d.capture.refs==1&&d.auxiliary.refs==1&&d.privateDepth.refs==1);
        uint64_t hash=1469598103934665603ull;for(auto draw:d.draws)hash=(hash^draw)*1099511628211ull;
        std::printf("MATERIAL_BINDINGS_CASE reset=%u shape=%u failure=%u success=%u draws=%zu hash=%llu resident=%u up=%u viewportCalls=%u frequencyCalls=%u auxiliaryCalls=%u restored=1\n",reset,shape,failure,unsigned(success),d.draws.size(),(unsigned long long)hash,resident,up,d.viewportCalls,d.frequencyCalls,d.auxiliaryCalls);++cases;
    }
    std::printf("MATERIAL_BINDINGS_PASS cases=%u actualProductionFlush=1 mixedResidentUP=1 viewportReset=1 partialViewport=1 failures=1 restoration=1\n",cases);
}
void MaterialCaptureStateTestAccess::replayConstantRanges(){
    using namespace NorthlightWaterShaderPatch;
    auto range=[](std::vector<uint32_t> code,unsigned expectedFirst,unsigned expectedCount){unsigned first=999,count=999;NorthlightAutoBumpVertex::constantRange(code.data(),code.size(),first,count);assert(first==expectedFirst&&count==expectedCount);};
    const std::vector<uint32_t> prefix={0xfffe0300u,0x0200001fu,0x80000000u,dst(6,0)};
    for(unsigned index:{0u,31u,63u,64u,255u}){auto code=prefix;code.insert(code.end(),{0x02000001u,dst(6,0),src(2,index),65535});range(code,index,1);}
    {auto code=prefix;code.insert(code.end(),{0x03000002u,dst(6,0),src(2,31),src(2,33),65535});range(code,31,3);}
    {auto code=prefix;code.insert(code.end(),{0x03000001u,dst(6,0),src(2,31)|0x2000u,src(3,0),65535});range(code,0,256);}
    {auto code=prefix;code.insert(code.end(),{0x03000014u,dst(6,0),src(1,0),src(2,0),65535});range(code,0,256);}
    {auto code=prefix;code.insert(code.end(),{0x02000001u,dst(6,0),src(1,0),65535});range(code,0,1);code.pop_back();range(code,0,256);}
    std::puts("MATERIAL_CONSTANT_RANGE_PASS boundary0_31_63_64_255 relative_matrix_malformed_full_bank no_constants");
    unsigned cases=0;
    for(unsigned shape=0;shape<14;++shape)for(unsigned mixed=0;mixed<2;++mixed)for(unsigned failure=0;failure<(shape==0||shape==9?1u:3u);++failure){
        MaterialBindingDevice d;d.perturbUP=true;const auto initial=d.state;
        bool success=false;unsigned resident=0,up=0;
        {
            NorthlightAutoBump b(&d);b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();b.occlusionTarget=&d.auxiliary;b.occlusionTarget->AddRef();
            auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->topology=D3DPT_TRIANGLELIST;mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);
            std::shared_ptr<const NorthlightDrawSnapshot::Mesh> residentMesh=mesh;
            auto upMesh=std::make_shared<NorthlightDrawSnapshot::Mesh>(*mesh);
            NorthlightReplayGPU::createBudgetMs()=1e9;
            for(unsigned phase=0;phase<2;++phase){b.deferredGPU.beginFrame();IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;assert(([&]{NorthlightReplayGPU::Placement at;const bool ready=b.deferredGPU.bind(&d,residentMesh,vb,ib,[](size_t){return true;},at);b.deferredGPU.commitPending();return ready||(b.deferredGPU.resident(residentMesh)&&b.deferredGPU.bind(&d,residentMesh,vb,ib,[](size_t){return true;},at));}())==bool(phase));for(auto*& p:vb)drop(p);drop(ib);}
            std::array<float,1024> vertex{};std::vector<float> pixel(shape==9?0:20,0.f);UINT first=20;
            unsigned vertexFirst=shape==10||shape==13?31:shape==11?255:0,vertexCount=shape==10||shape==13?3:shape>=11?1:256;
            const auto initialFirst=vertexFirst,initialCount=vertexCount;
            UINT restoreFirst=256,restoreEnd=0,restorePixelFirst=224,restorePixelEnd=0;
            MaterialBindingDevice::FloatBanks expected{initial.vf,initial.pf};
            auto setBits=[](float& value,uint32_t bits){std::memcpy(&value,&bits,sizeof bits);};
            for(unsigned i=0;i<5;++i){
                if(i==1){
                    if(shape==1){vertex[0]=1;pixel[0]=2;}
                    if(shape==2){vertex[1023]=1;pixel[19]=2;}
                    if(shape==3){vertex[401]=1;pixel[9]=2;}
                    if(shape==4){vertex[12]=1;vertex[1007]=2;pixel[4]=1;pixel[19]=2;}
                    if(shape==5){setBits(vertex[401],0x7fc00001u);setBits(pixel[9],0x7fc00002u);}
                    if(shape==6){setBits(vertex[2],0x80000000u);setBits(pixel[19],0x80000000u);}
                    if(shape==7){vertex.fill(1);std::fill(pixel.begin(),pixel.end(),2);}
                    if(shape==8){vertex[501]=1;first=18;pixel.assign(24,2);}
                    if(shape>=10){vertex[vertexFirst*4]=1;setBits(vertex[(vertexFirst+vertexCount)*4-1],0x80000000u);pixel[9]=2;}
                }
                if(i==3){first=21;pixel.assign(16,3);}
                if(i==4){first=20;pixel.assign(24,4);}
                if(shape==13&&i>=3){vertexFirst=i==3?30:32;vertexCount=i==3?5:2;vertex[vertexFirst*4]=float(i);}
                auto packet=std::make_unique<NorthlightAutoBump::Deferred>();packet->mesh=mixed&&i%2?std::shared_ptr<const NorthlightDrawSnapshot::Mesh>(upMesh):residentMesh;
                packet->vertex=&d.vs;packet->vertex->AddRef();packet->pixel=&d.ps;packet->pixel->AddRef();packet->declaration=&d.decl;packet->declaration->AddRef();packet->viewport={0,0,64,64,0,1};packet->occlusion=true;packet->tag=19;packet->first=first;packet->pixelConstants=pixel;
                packet->vertexFirst=vertexFirst;packet->vertexCount=vertexCount;
                restoreFirst=std::min(restoreFirst,vertexFirst);restoreEnd=std::max(restoreEnd,vertexFirst+vertexCount);
                if(!pixel.empty()){restorePixelFirst=std::min(restorePixelFirst,first);restorePixelEnd=std::max(restorePixelEnd,first+UINT(pixel.size()/4));}
                std::memcpy(packet->vertexConstants,vertex.data()+vertexFirst*4,vertexCount*16);std::fill(packet->vertexInt,packet->vertexInt+64,0);std::fill(packet->pixelInt,packet->pixelInt+64,0);std::fill(packet->vertexBool,packet->vertexBool+16,FALSE);std::fill(packet->pixelBool,packet->pixelBool+16,FALSE);std::fill(packet->states,packet->states+14,0);std::fill(packet->raster,packet->raster+NorthlightAutoBump::DeferredRasterCount,0);
                std::memcpy(expected.vertex.data()+vertexFirst*4,vertex.data()+vertexFirst*4,vertexCount*16);if(!pixel.empty())std::memcpy(expected.pixel.data()+first*4,pixel.data(),pixel.size()*sizeof(float));d.expectedFloatBanks.push_back(expected);
                b.deferred.push_back(std::move(packet));
            }
            d.fault=failure?failure+9:0;success=b.finishCaptures();resident=b.residentDraws;up=b.uploadDraws;assert(b.deferred.empty());
            if(failure)assert(!success&&d.faultUsed&&b.failed&&d.draws.size()==1);else assert(success&&d.draws.size()==5&&b.flushed==5);
            assert(d.floatCalls.size()>=4&&d.floatCalls[0].vertex&&d.floatCalls[0].first==initialFirst&&d.floatCalls[0].count==initialCount);
            assert(!d.floatCalls[1].vertex&&d.floatCalls[1].first==20&&d.floatCalls[1].count==(shape==9?0u:5u));
            const auto& restoreVertex=d.floatCalls[d.floatCalls.size()-2];const auto& restorePixel=d.floatCalls.back();
            assert(restoreVertex.vertex&&restoreVertex.first==restoreFirst&&restoreVertex.count==restoreEnd-restoreFirst&&!restoreVertex.failed);
            assert(!restorePixel.vertex&&restorePixel.first==restorePixelFirst&&restorePixel.count==restorePixelEnd-restorePixelFirst&&!restorePixel.failed);
            assert(d.state==initial);
        }
        assert(d.state==initial&&d.vs.refs==1&&d.ps.refs==1&&d.decl.refs==1&&d.surface.refs==1&&d.capture.refs==1&&d.auxiliary.refs==1&&d.privateDepth.refs==1);
        unsigned vertexCalls=0,pixelCalls=0,vertexRegisters=0,pixelRegisters=0;uint64_t hash=1469598103934665603ull;std::string order;
        for(const auto& call:d.floatCalls){if(call.vertex){++vertexCalls;vertexRegisters+=call.count;}else{++pixelCalls;pixelRegisters+=call.count;}order+=call.vertex?'V':'P';order+=std::to_string(call.draw);order+=call.failed?'!':'.';}
        for(auto draw:d.draws)hash=(hash^draw)*1099511628211ull;
        std::printf("MATERIAL_CONSTANTS_CASE shape=%u mixed=%u failure=%u success=%u draws=%zu hash=%llu resident=%u up=%u vertexCalls=%u pixelCalls=%u vertexRegisters=%u pixelRegisters=%u callOrder=%s restored=1\n",shape,mixed,failure,unsigned(success),d.draws.size(),(unsigned long long)hash,resident,up,vertexCalls,pixelCalls,vertexRegisters,pixelRegisters,order.c_str());++cases;
    }
    std::printf("MATERIAL_CONSTANTS_PASS cases=%u actualProductionFlush=1 fullBankOracle=1 bitwiseFloat4=1 oneCallPerBank=1 zeroCount=1 shiftedPixelRange=1 mixedResidentUP=1 failures=1 restoration=1\n",cases);
}
int main(){MaterialCaptureStateTestAccess::replayBindings();MaterialCaptureStateTestAccess::replayConstantRanges();}
