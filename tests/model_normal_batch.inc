// Included by the native-model fixture: identical shaders, changing poses and
// source bytes, interleaved MSAA scene draws, then exact normal/depth comparison.
#include "device_mirror.h"
static unsigned normalSamplerBatches=0;
static void normalRange(IDirect3DVertexShader9* shader){
    UINT size=0;ok(shader->GetFunction(nullptr,&size));std::vector<uint32_t> code(size/4);ok(shader->GetFunction(code.data(),&size));
    using namespace NorthlightModelNormalRange;const Kind kind=classify(code.data(),code.size(),true);
    if(kind==None){std::puts("MODEL_NORMALS_RANGE_SKIP unsupported_native_program");return;}
    SavedState saved(d);assert(saved.ok);
    constexpr unsigned W=128,H=128;auto color=target(W,H,D3DFMT_A8R8G8B8,4);IDirect3DSurface9* depth=nullptr;
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color.s));ok(d->SetDepthStencilSurface(depth));viewport(color);
    for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(key,FALSE));
    // Keep a real sampled alpha dependency so far rejection must precede
    // texture/sampler reads, even with an initially cold state mirror.
    using NorthlightWaterShaderPatch::dst;using NorthlightWaterShaderPatch::src;
    const DWORD alphaCode[]={0xffff0300u,0x0200001fu,0x80000005u,dst(1,0),0x0200001fu,0x90000000u,dst(10,0),
        0x03000042u,dst(0,0),src(1,0),src(10,0),0x02000001u,dst(8,0),src(0,0),65535u};
    NorthlightDrawSnapshot::Ref<IDirect3DPixelShader9> alpha;ok(d->CreatePixelShader(alphaCode,alpha.out()));ok(d->SetPixelShader(alpha.p));
    ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,64));
    ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;ok(d->GetVertexDeclaration(declaration.out()));
    D3DVERTEXELEMENT9 layout[MAXD3DDECLLENGTH+1];UINT count=MAXD3DDECLLENGTH+1;ok(declaration->GetDeclaration(layout,&count));
    auto mesh=[](bool mixed){NorthlightDrawSnapshot::Mesh m;m.topology=D3DPT_TRIANGLELIST;m.primitiveCount=1;m.vertexCount=3;m.streams[0].stride=48;m.streams[0].bytes.resize(144);
        const float positions[3][3]={{-.3f,-.3f,0},{.3f,-.3f,0},{0,.3f,0}},normal[3]={0,0,1};
        for(unsigned i=0;i<3;++i){auto* v=m.streams[0].bytes.data()+i*48;std::memcpy(v,positions[i],12);v[12]=255;v[16]=mixed&&i==2;std::memcpy(v+20,normal,12);}
        return std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(m));};
    const auto uniform=mesh(false),mixed=mesh(true);const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,3,0,1,false};
    NorthlightActorDeformation::Program program;assert(NorthlightActorDeformation::compile(code.data(),code.size(),program,false,2));
    float inverse[16]={};for(unsigned i=0;i<4;++i)inverse[i*5]=1;
    CaptureCalls calls(d);DeviceMirror mirror;ExtensionDevice extension(&calls,&mirror);MirrorGuard held(mirror.gate);
    auto normals=std::make_unique<NorthlightModelNormals>(&extension);normals->setDeferredCapture(true);normals->setTrustedPoseSource(extension.trustedConstantSource());
    NorthlightModelNormalRange::Cache producerRange;
    unsigned cases=0,totalCulled=0;
    for(unsigned scenario=0;scenario<(kind==Single||kind==Envelope?3u:2u);++scenario){
        const bool nearDraw=scenario==1,mixedBones=kind==Single&&scenario==2,stale=kind==Envelope&&scenario==2;
        const auto& source=mixedBones?mixed:uniform;std::vector<unsigned char> reference;
        for(bool enabled:{false,true}){
            const auto samplerBatches=normalSamplerBatches;
            normals->setSamplerStateSource(enabled?static_cast<IDirect3DDevice9*>(&extension):static_cast<IDirect3DDevice9*>(&calls),[](void* context,DWORD stage,unsigned mask,DWORD* out){
                const bool known=static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(context))->knownSamplerStates(stage,mask,out);normalSamplerBatches+=known;return known;});
            normals->endFrame();producerRange.beginFrame();normals->testRange(enabled);ok(extension.Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
            float bank[256][4]={};bank[2][0]=bank[3][1]=bank[4][2]=bank[5][2]=1;bank[4][3]=-.5f;bank[28][3]=1;
            auto capture=[&](float z,bool anchor,unsigned unknown=256){
                // Disjoint screen halves and constant projected size: the near
                // anchor cannot conceal an incorrectly retained far candidate.
                for(unsigned bone=0;bone<75;++bone){const unsigned first=31+3*bone;bank[first][0]=bank[first+1][1]=z;bank[first+2][2]=1;bank[first][3]=z*(anchor?-.5f:.5f);bank[first+2][3]=z;}
                ok(extension.SetVertexShaderConstantF(0,bank[0],256));
                if(unknown<256)mirror.vsFloatKnown[unknown]=false;
                NorthlightPoseBounds::Handoff proof;
                if(kind==Single&&unknown>=256&&producerRange.outside(source,layout,count,bank[0],kind)){
                    NorthlightConstantEpoch::Stamp stamp;stamp.requestHeld=true;assert(extension.constantStamp(stamp));
                    proof.publishFar(stamp,source.get(),shader,declaration.p);
                    if(unknown==257)ok(extension.SetVertexShaderConstantF(31,bank[31],1));
                }
                if(kind==Envelope){NorthlightReplayBounds::Budget budget;NorthlightReplayBounds::Bounds bounds;
                    assert(NorthlightReplayBounds::calculate(program,*source,layout,count,bank[0],inverse,budget,bounds)==NorthlightReplayBounds::Status::Valid);
                    NorthlightConstantEpoch::Stamp stamp;stamp.requestHeld=true;assert(extension.constantStamp(stamp));proof.publish(bounds,bank[0],stamp,source.get(),shader,declaration.p);assert(proof.bounds.valid);
                    if(stale)ok(extension.SetVertexShaderConstantF(31,bank[31],1));
                }
                normals->capture(shader,[&]{return extension.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,source->streams[0].bytes.data(),48);},true,&geometry,nullptr,D3DFMT_UNKNOWN,nullptr,0,&source,declaration.p,(kind==Envelope||proof.outsideRange)?&proof:nullptr);
            };
            ok(extension.BeginScene());capture(2,true);capture(2,true);assert(normals->finishCaptures());ok(extension.EndScene());const auto anchorOnly=capturedPixels(normals->texture(),W,H);
            assert(enabled?normalSamplerBatches>samplerBatches:normalSamplerBatches==samplerBatches);
            normals->endFrame();ok(extension.Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
            ok(extension.BeginScene());capture(2,true);assert(normals->testPendingPackets()==1&&!normals->testRangeCulled());
            if(enabled&&scenario==0){
                D3DVIEWPORT9 full{};ok(extension.GetViewport(&full));auto partial=full;--partial.Width;
                ok(extension.SetViewport(&partial));capture(2,true);assert(normals->testPendingPackets()==1);
                ok(extension.SetViewport(&full));
                NorthlightDrawSnapshot::Ref<IDirect3DSurface9> other;
                ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,other.out(),nullptr));
                ok(extension.SetRenderTarget(0,other.p));capture(2,true);assert(normals->testPendingPackets()==1);
                ok(extension.SetRenderTarget(0,color.s));ok(extension.SetViewport(&full));
            }
            assert(calls.textureReads&&calls.samplerReads);
            std::memset(mirror.textureKnown,0,sizeof mirror.textureKnown);std::memset(mirror.samplerKnown,0,sizeof mirror.samplerKnown);
            const auto textureReads=calls.textureReads,samplerReads=calls.samplerReads;
            const auto rangeRegisters=normals->testRangeRegisters(),viewportReads=calls.viewportReads;
            capture(nearDraw?20:80,false);
            const unsigned culled=enabled&&!nearDraw&&!mixedBones&&!stale?1u:0u;
            if(culled)assert(calls.textureReads==textureReads&&calls.samplerReads==samplerReads);
            if(culled)assert(calls.viewportReads==viewportReads);
            if(culled&&kind==Single)assert(normals->testRangeRegisters()==rangeRegisters&&calls.viewportReads==viewportReads);
            if(culled&&kind==Rigid)assert(normals->testRangeRegisters()-rangeRegisters==7);
            assert(normals->testRangeCulled()==culled&&normals->testPendingPackets()==2-culled);totalCulled+=culled;
            assert(normals->finishCaptures());ok(extension.EndScene());assert(normals->texture());
            auto image=capturedPixels(normals->texture(),W,H);
            auto coverage=[](const auto& pixels){unsigned count=0;for(size_t at=6;at<pixels.size();at+=8){unsigned short alpha;std::memcpy(&alpha,pixels.data()+at,2);count+=half(alpha)>0;}return count;};
            const unsigned anchorPixels=coverage(anchorOnly);assert(anchorPixels);
            if(nearDraw)assert(image!=anchorOnly&&coverage(image)>anchorPixels);else assert(image==anchorOnly);
            if(enabled)assert(image==reference);else reference=std::move(image);
            if(enabled&&scenario==0&&kind!=Envelope)for(unsigned unknown:{2u,31u}){
                normals->endFrame();ok(extension.BeginScene());capture(80,false,unknown);
                assert(!normals->testPendingPackets()&&normals->testRangeCulled()==1);
                assert(normals->testRangeRegisters()>=256&&normals->finishCaptures());ok(extension.EndScene());
            }
            if(enabled&&scenario==0&&kind==Single){
                normals->endFrame();ok(extension.BeginScene());capture(80,false,257);
                assert(normals->testRangeCulled()==1&&normals->testRangeRegisters()==7&&normals->finishCaptures());ok(extension.EndScene());
            }
        }++cases;
    }
    assert(totalCulled>0);std::printf("MODEL_NORMALS_RANGE_PASS kind=%u cases=%u culled=%u pixels=exact near=retained mixed_or_stale=retained disjoint=1 nearAddsPixels=1 farMatchesAnchor=1\n",unsigned(kind),cases,totalCulled);
    normals.reset();depth->Release();color.read->Release();color.s->Release();color.t->Release();
}
static uint64_t normalSourceRevision=1;
static void normalBatch(IDirect3D9* api,CaptureCalls& calls,IDirect3DVertexShader9* shader,
                        unsigned vertexCount,unsigned indexCount,const std::vector<unsigned char>& source,bool gpuTiming,const std::string& directory){
    SavedState original(d);assert(original.ok);
    for(unsigned stage=1;stage<16;++stage)bind(stage,nullptr);
    for(unsigned stage=0;stage<4;++stage)ok(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+stage,nullptr));
    for(unsigned stage=1;stage<4;++stage)ok(d->SetRenderTarget(stage,nullptr));
    normalRange(shader);
    constexpr unsigned W=3840,H=2160,N=640;
    IDirect3DSurface9 *color=nullptr,*depth=nullptr;
    ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr));
    ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&color,nullptr));
    ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&depth,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,color));ok(d->SetDepthStencilSurface(depth));
    D3DVIEWPORT9 viewport{0,0,W,H,0,1};ok(d->SetViewport(&viewport));
    float constants[256][4];ok(d->GetVertexShaderConstantF(0,constants[0],256));
    // A detailed scene resists render-target compression, as the actual game
    // does. A black clear alone conceals the cost of interrupting its MSAA pass.
    IDirect3DTexture9* noise=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&noise,nullptr));
    D3DLOCKED_RECT lock{};ok(noise->LockRect(0,&lock,nullptr,0));unsigned random=12345;
    for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x){random=random*1664525+1013904223;reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=random|0xff000000;}
    ok(noise->UnlockRect(0));
    auto background=[&]{SavedState saved(d);assert(saved.ok);ok(d->SetPixelShader(nullptr));bind(0,noise);
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
        ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));Target t;t.w=W;t.h=H;quad(t);};
    auto bytes=source;auto* vertices=bytes.data()+12;auto* indices=vertices+vertexCount*48;
    const NorthlightDrawSnapshot::Draw geometry{D3DPT_TRIANGLELIST,0,0,vertexCount,0,indexCount/3,true};
    auto draw=[&]{return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,vertexCount,indexCount/3,indices,D3DFMT_INDEX32,vertices,48);};
    IDirect3DQuery9 *begin=nullptr,*end=nullptr,*frequency=nullptr;
    if(gpuTiming){ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&begin));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));}
    auto wait=[](IDirect3DQuery9* q){UINT64 value=0;const DWORD start=GetTickCount();HRESULT hr;
        while((hr=q->GetData(&value,sizeof value,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<10000);Sleep(0);}ok(hr);return value;};
    auto normals=std::make_unique<NorthlightModelNormals>(&calls);
    UINT shaderBytes=0;ok(shader->GetFunction(nullptr,&shaderBytes));std::vector<DWORD> nativeCode(shaderBytes/4);ok(shader->GetFunction(nativeCode.data(),&shaderBytes));
    std::vector<uint32_t> patchedCode;assert(NorthlightModelNormal::vertex(reinterpret_cast<const uint32_t*>(nativeCode.data()),nativeCode.size(),patchedCode));
    const auto reads=NorthlightShaderReads::scan(patchedCode.data(),patchedCode.size());assert(reads.exact);
    unsigned first=256,last=0;for(unsigned c=0;c<256;++c)if(reads.f[c>>6]&(1ull<<(c&63))){first=std::min(first,c);last=c+1;}
    const unsigned floatCount=last?last-first:0;assert(floatCount<=256);
    if(NorthlightModelNormalRange::classify(reinterpret_cast<const uint32_t*>(nativeCode.data()),nativeCode.size())==NorthlightModelNormalRange::Rigid)assert(floatCount<64);
    unsigned pendingNotices=0;
    std::vector<unsigned char> expected;
    std::vector<double> times[2],cpu[2];unsigned targetSets[2]={};
    // ABBA; two warm rounds and four retained pairs. Native and captured output
    // both use each draw's current constants and current private source bytes.
    for(unsigned round=0;round<6;++round)for(bool batch:{false,true,true,false}){
        normals->endFrame();pendingNotices=0;normals->setDeferredCapture(batch,+[](void* p){++*static_cast<unsigned*>(p);},&pendingNotices);
        ok(d->SetVertexShaderConstantF(0,constants[0],256));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));
        background();
        if(gpuTiming){ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}LARGE_INTEGER t0{},t1{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&t0);
        const unsigned sets=calls.targetSets,viewportReads=calls.viewportReads,registerReads=calls.vertexRegistersRead;
        ok(d->BeginScene());
        for(unsigned i=0;i<N;++i){
            auto pose=constants[31];const float old=pose[3];pose[3]=old+float(i%7)*.017f;
            ok(d->SetVertexShaderConstantF(31,pose,1));pose[3]=old;
            bytes=source;
            float x;std::memcpy(&x,vertices,4);x+=float(i%3)*.001f;std::memcpy(vertices,&x,4);
            const auto packets=normals->testPendingPackets();const unsigned notices=pendingNotices;
            normals->capture(shader,draw,true,&geometry,indices,D3DFMT_INDEX32,vertices,48);
            if(batch&&normals->testPendingPackets()>packets)assert(pendingNotices==notices&&!normals->pendingTextures());
            ok(draw());
            // Reusing an UP array after capture must never change its replay.
            std::fill(bytes.begin()+12,bytes.end(),0);
        }
        if(batch)assert(normals->testPendingPackets()&&!normals->pendingTextures());
        const auto beforeRegisters=calls.vertexRegistersWritten;
        assert(normals->finishCaptures());ok(d->EndScene());
        if(batch)assert(calls.vertexRegistersRead-registerReads==N*floatCount);
        if(batch)assert(calls.viewportReads-viewportReads>=N&&calls.viewportReads-viewportReads<N+N/8);
        if(batch)assert(calls.vertexRegistersWritten>beforeRegisters&&calls.vertexRegistersWritten-beforeRegisters<N*4);
        QueryPerformanceCounter(&t1);double gpuMs=0;
        if(gpuTiming){ok(end->Issue(D3DISSUE_END));ok(d->Present(nullptr,nullptr,nullptr,nullptr));const auto b=wait(begin),e=wait(end),f=wait(frequency);gpuMs=double(e-b)*1000/double(f);}
        if(round>=2){times[batch].push_back(gpuMs);cpu[batch].push_back(double(t1.QuadPart-t0.QuadPart)*1000/hz.QuadPart);}
        targetSets[batch]=calls.targetSets-sets;
        assert(normals->texture());
        if(round==0){auto image=capturedPixels(normals->texture(),W,H);if(!batch)expected=std::move(image);else assert(expected==image);}
    }
    for(auto& a:times)std::sort(a.begin(),a.end());for(auto& a:cpu)std::sort(a.begin(),a.end());
    assert(targetSets[1]<targetSets[0]/22); // Bound full-resolution MSAA interruptions, including full-batch transitions.
    std::printf("MODEL_NORMALS_BATCH_PASS pixels=exact draws=%u width=%u height=%u msaa=2 targetSets=%u/%u gpuMs=%.6f/%.6f cpuMs=%.6f/%.6f order=ABBA gpuTiming=%u\n",
        N,W,H,targetSets[0],targetSets[1],times[0][times[0].size()/2],times[1][times[1].size()/2],cpu[0][cpu[0].size()/2],cpu[1][cpu[1].size()/2],unsigned(gpuTiming));
    // Alternate narrow/wide constant users. Bone 74 forces the wide program
    // to consume high rows that a preceding rigid packet did not capture.
    auto* alternate=vs(directory+(floatCount<64?"model-vs3-skinned.bin":"model-vs3-rigid.bin"));
    for(bool batch:{false,true}){
        normals->endFrame();normals->setDeferredCapture(batch);bytes=source;
        for(unsigned i=0;i<vertexCount;++i){auto* v=vertices+i*48;v[12]=255;v[13]=v[14]=v[15]=0;for(unsigned k=0;k<4;++k)v[16+k]=74;}
        ok(d->SetVertexShaderConstantF(0,constants[0],256));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());
        for(unsigned i=0;i<4;++i){
            float pose[3][4];std::memcpy(pose,constants[31],sizeof pose);pose[0][3]+=float(i)*.04f;
            ok(d->SetVertexShaderConstantF(31,pose[0],3));pose[0][3]+=.3f;ok(d->SetVertexShaderConstantF(253,pose[0],3));
            auto* selected=i%2?alternate:shader;ok(d->SetVertexShader(selected));
            normals->capture(selected,draw,true,&geometry,indices,D3DFMT_INDEX32,vertices,48);ok(draw());
        }
        assert(normals->finishCaptures());ok(d->EndScene());assert(normals->texture());
        auto image=capturedPixels(normals->texture(),W,H);assert(std::any_of(image.begin(),image.end(),[](unsigned char b){return b!=0;}));
        if(!batch)expected=std::move(image);else assert(expected==image);
    }
    alternate->Release();normals->endFrame();ok(d->SetVertexShader(shader));ok(d->SetVertexShaderConstantF(0,constants[0],256));
    std::printf("MODEL_NORMALS_CONSTANTS_PASS first=%u count=%u captureReads=bounded opaqueNotices=zero shaderSwitches=exact highBone=74\n",last?first:0,floatCount);
    // All four proxy entry points, including source-buffer writes after capture.
    std::vector<unsigned char> expanded(indexCount*48);
    for(unsigned i=0;i<indexCount;++i){unsigned index;std::memcpy(&index,source.data()+12+vertexCount*48+4*i,4);std::memcpy(expanded.data()+i*48,source.data()+12+index*48,48);}
    IDirect3DVertexBuffer9* vertexBuffer=nullptr;IDirect3DIndexBuffer9* indexBuffer=nullptr;
    ok(d->CreateVertexBuffer(UINT(expanded.size()),0,0,D3DPOOL_MANAGED,&vertexBuffer,nullptr));
    ok(d->CreateIndexBuffer(indexCount*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&indexBuffer,nullptr));
    NorthlightDrawSnapshot::Frame peer{8u<<20,0};
    const auto identity=+[](void* p,bool)->uint64_t{return reinterpret_cast<uintptr_t>(p);};
    const auto version=+[](void*,bool)->uint64_t{return normalSourceRevision;};
    peer.setIdentityProvider(identity);peer.setVersionProvider(version);peer.setSnapshotCacheLimit(8u<<20);
    assert(normals->shareCaptureSources(peer,identity,version,nullptr));
    NorthlightGpuScene::NativeStore store;NorthlightGpuScene::NativeClient cache(store,NorthlightGpuScene::Consumer::Material);
    uint64_t cacheFrame=1;store.advance(cacheFrame);normals->setGeometryCache(&cache);
    normals->setRasterStateSource(&calls,[](void* context,const D3DRENDERSTATETYPE* keys,unsigned count,DWORD* out){
        auto& calls=*static_cast<CaptureCalls*>(static_cast<IDirect3DDevice9*>(context));++calls.bulkReads;
        if(calls.bulkUnknown){++calls.bulkMisses;return false;}
        for(unsigned i=0;i<count;++i)if(FAILED(d->GetRenderState(keys[i],out+i)))return false;
        return true;
    });
    for(unsigned mode=0;mode<4;++mode){
        calls.bulkUnknown=bool(mode&1);
        std::vector<unsigned char> reference;
        for(bool batch:{false,true}){
            normals->endFrame();normals->setDeferredCapture(batch);bytes=source;
            const bool indexed=mode<2,buffered=mode&1;
            const auto* data=indexed?vertices:expanded.data();const unsigned count=indexed?vertexCount:indexCount;
            void* memory=nullptr;ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memcpy(memory,data,count*48);ok(vertexBuffer->Unlock());
            ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memcpy(memory,indices,indexCount*4);ok(indexBuffer->Unlock());
            ok(d->SetStreamSource(0,vertexBuffer,0,48));ok(d->SetIndices(indexBuffer));
            auto submit=[&]{return buffered?(indexed?d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,count,0,indexCount/3):d->DrawPrimitive(D3DPT_TRIANGLELIST,0,indexCount/3)):
                indexed?draw():d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,indexCount/3,data,48);};
            const NorthlightDrawSnapshot::Draw args{D3DPT_TRIANGLELIST,0,0,count,0,indexCount/3,indexed};
            ++normalSourceRevision;peer.clearFrame();
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> handedDeclaration;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> handed;
            if(buffered||mode==0){ok(d->GetVertexDeclaration(handedDeclaration.out()));NorthlightDrawSnapshot::Mesh local;
                if(buffered)assert(peer.read(d,handedDeclaration.p,args,local,nullptr,false,&handed)&&handed);
                else {assert(peer.readUP(handedDeclaration.p,args,indices,D3DFMT_INDEX32,data,48,local));handed=peer.retain(std::move(local));}}
            const bool handoff=mode<2;
            ok(d->BeginScene());normals->capture(shader,submit,!buffered,&args,indexed?indices:nullptr,D3DFMT_INDEX32,buffered?nullptr:data,48,handoff?&handed:nullptr,handoff?handedDeclaration.p:nullptr);
            if(batch)assert(normals->testPendingPackets()&&!normals->pendingTextures());
            if(batch&&buffered){assert(normals->reusedSources()==1);if(mode==1)peer.clearSnapshotCache();}
            ++normalSourceRevision;
            ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memset(memory,0,count*48);ok(vertexBuffer->Unlock());
            ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memset(memory,0,indexCount*4);ok(indexBuffer->Unlock());
            assert(normals->finishCaptures());ok(d->EndScene());
            auto image=capturedPixels(normals->texture(),W,H);if(batch)assert(image==reference);else reference=std::move(image);
            if(batch&&buffered){
                normals->endFrame();ok(d->BeginScene());normals->capture(shader,submit,false,&args,nullptr,D3DFMT_UNKNOWN,nullptr,0,&handed,nullptr); // Mismatched handoff must use current buffers.
                assert(normals->reusedSources()==0&&normals->finishCaptures());ok(d->EndScene());
                auto empty=capturedPixels(normals->texture(),W,H);assert(std::all_of(empty.begin(),empty.end(),[](unsigned char x){return x==0;}));
                // No peer populated the current revision: the normal consumer
                // must retain it itself, then read zero buffer bytes next frame.
                assert(normals->readSourceBytes()>0);
                ++normalSourceRevision;
                ok(vertexBuffer->Lock(0,count*48,&memory,0));std::memcpy(memory,data,count*48);ok(vertexBuffer->Unlock());
                ok(indexBuffer->Lock(0,indexCount*4,&memory,0));std::memcpy(memory,indices,indexCount*4);ok(indexBuffer->Unlock());
                for(unsigned repeat=0;repeat<3;++repeat){
                    if(repeat==2){normals->reset();cache.clear();}else normals->endFrame();
                    store.advance(++cacheFrame);
                    ok(d->BeginScene());normals->capture(shader,submit,false,&args);
                    assert(normals->reusedSources()==unsigned(repeat==1));
                    assert((normals->readSourceBytes()==0)==(repeat==1));
                    assert(normals->finishCaptures());ok(d->EndScene());
                    assert(normals->cachedDraws()==unsigned(repeat==1));
                    assert(capturedPixels(normals->texture(),W,H)==reference);
                    assert(peer.sharedSourceBytes()<=peer.sharedSourceLimit());
                    if(repeat==1&&mode==1){
                        NorthlightDrawSnapshot::Mesh local;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> direct;
                        assert(peer.read(d,handedDeclaration.p,args,local,nullptr,false,&direct)&&direct);
                        float palette[4];ok(d->GetVertexShaderConstantF(31,palette,1));
                        constexpr unsigned crowd=1200;
                        std::vector<double> gpu[3],cpuCost[3];std::vector<unsigned char> referenceCrowd;unsigned crowdSets[3]={};
                        for(unsigned round=0;round<(gpuTiming?6u:1u);++round)for(unsigned strategy:{0u,1u,2u,2u,1u,0u}){
                            normals->endFrame();normals->setGeometryCache(strategy?&cache:nullptr);store.advance(++cacheFrame);
                            ok(d->SetVertexShaderConstantF(31,palette,1));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));background();
                            if(gpuTiming){ok(frequency->Issue(D3DISSUE_END));ok(begin->Issue(D3DISSUE_END));}
                            LARGE_INTEGER start{},finish{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&start);const auto sets=calls.targetSets;
                            ok(d->BeginScene());
                            for(unsigned i=0;i<crowd;++i){float pose[4];std::copy(palette,palette+4,pose);pose[3]+=float(i%7)*.017f;ok(d->SetVertexShaderConstantF(31,pose,1));
                                normals->capture(shader,submit,false,&args,nullptr,D3DFMT_UNKNOWN,nullptr,0,strategy==2?&direct:nullptr,strategy==2?handedDeclaration.p:nullptr);ok(submit());}
                            assert(normals->finishCaptures());ok(d->EndScene());QueryPerformanceCounter(&finish);double gpuMs=0;
                            if(gpuTiming){ok(end->Issue(D3DISSUE_END));ok(d->Present(nullptr,nullptr,nullptr,nullptr));const auto b=wait(begin),e=wait(end),f=wait(frequency);gpuMs=double(e-b)*1000/double(f);}
                            if(round>=2||!gpuTiming){gpu[strategy].push_back(gpuMs);cpuCost[strategy].push_back(double(finish.QuadPart-start.QuadPart)*1000/hz.QuadPart);}
                            crowdSets[strategy]=calls.targetSets-sets;
                            assert(normals->cachedDraws()==(strategy?crowd:0)&&cache.uploaded()==0&&normals->readSourceBytes()==0);
                            if(strategy)assert(crowdSets[strategy]==11); // One pass under the unchanged 8 MiB CPU budget.
                            if(round==0){auto image=capturedPixels(normals->texture(),W,H);if(referenceCrowd.empty())referenceCrowd=std::move(image);else assert(image==referenceCrowd);}
                        }
                        ok(d->SetVertexShaderConstantF(31,palette,1));normals->setGeometryCache(&cache);
                        for(unsigned i=0;i<3;++i){std::sort(gpu[i].begin(),gpu[i].end());std::sort(cpuCost[i].begin(),cpuCost[i].end());}
                        std::printf("MODEL_NORMALS_RESIDENCY_BENCH draws=%u pixels=exact uploadedBytes=0 strategies=UP/resident/handoff targetSets=%u/%u/%u gpuMs=%.6f/%.6f/%.6f cpuMs=%.6f/%.6f/%.6f gpuTiming=%u\n",crowd,
                            crowdSets[0],crowdSets[1],crowdSets[2],gpu[0][gpu[0].size()/2],gpu[1][gpu[1].size()/2],gpu[2][gpu[2].size()/2],cpuCost[0][cpuCost[0].size()/2],cpuCost[1][cpuCost[1].size()/2],cpuCost[2][cpuCost[2].size()/2],unsigned(gpuTiming));
                        normals->endFrame();ok(d->BeginScene());normals->capture(shader,submit,false,&args);
                        cache.clear(); // A queued resident draw owns its buffers even after eviction.
                        assert(normals->finishCaptures()&&normals->cachedDraws()==1);ok(d->EndScene());
                        assert(capturedPixels(normals->texture(),W,H)==reference);
                    }
                }
            }
        }
    }
    assert(calls.bulkReads>calls.bulkMisses&&calls.bulkMisses>0);calls.bulkUnknown=false;
    std::puts("MODEL_NORMALS_BATCH_STATE_PASS known=exact unknown=fallback viewportReads=one_per_capture target_identity=retained");
    vertexBuffer->Release();indexBuffer->Release();
    std::puts("MODEL_NORMALS_BATCH_SOURCE_PASS inputs=indexedUP/indexedBuffer/primitiveUP/primitiveBuffer mutation=exact shared=tracked eviction=exact stale=rejected independent=zero_reads trim=cold gpu_resident=exact handoff=exact");
    normals->endFrame();assert(!normals->texture()&&!normals->pendingTextures());normals.reset();
    if(begin)begin->Release();if(end)end->Release();if(frequency)frequency->Release();color->Release();depth->Release();noise->Release();
}
