#pragma once
#include <d3d9.h>
#include <unordered_map>
#include "model_normal_patch.h"
#include "native_material_capture_patch.h"
#include "draw_snapshot.h"
#include "shader_read_set.h"
#include "scene_native_store.h"
#include "vertex_declaration_cache.h"
#include "model_normal_range.h"
#include "pose_bounds_handoff.h"
#include "replay_constants.h"

// Independent of AutoBump and actor-shadow capture budgets. Replay only opaque
// native depth writers; the consumer rejects captures hidden by final depth.
class NorthlightModelNormals {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    struct Vertex {IDirect3DVertexShader9 *source=nullptr,*shader=nullptr;NorthlightShaderReads::ReadSet reads;unsigned first=0,count=256;NorthlightModelNormalRange::Kind rangeKind=NorthlightModelNormalRange::None;};
    struct Pixel {IDirect3DPixelShader9 *source=nullptr,*shader=nullptr;unsigned control=0,first=0,count=0;NorthlightShaderReads::ReadSet reads;};
    IDirect3DDevice9* d;
    NorthlightModelNormalRange::Cache rangeCache;
    void* poseContext=nullptr;NorthlightConstantEpoch::Reader poseReader=nullptr;NorthlightConstantEpoch::KnownSpan poseSpan=nullptr;unsigned poseBoundsTests=0,poseBoundsCulled=0;
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    bool rangeEnabled=true;
#endif
    void* rasterContext=nullptr;bool(*rasterReader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)=nullptr;
    void* samplerContext=nullptr;bool(*samplerReader)(void*,DWORD,unsigned,DWORD*)=nullptr;
    template<size_t N> bool renderStates(const D3DRENDERSTATETYPE (&keys)[N],DWORD (&out)[N]){
        if(rasterReader&&rasterReader(rasterContext,keys,unsigned(N),out))return true;
        for(unsigned i=0;i<N;++i)if(FAILED(d->GetRenderState(keys[i],out+i)))return false;
        return true;
    }
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels[2];
    IDirect3DTexture9* normal=nullptr;
    IDirect3DSurface9 *surface=nullptr,*depth=nullptr,*frameTarget=nullptr;
    IDirect3DStateBlock9* batchState=nullptr;
    UINT width=0,height=0;unsigned draws=0;bool cleared=false,bad=false;
    // Private immutable geometry permits one normal/depth pass instead of
    // interrupting the full-resolution MSAA scene for every opaque model.
    inline static constexpr D3DRENDERSTATETYPE rasterKeys[]={D3DRS_CULLMODE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,
        D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_CLIPPING,D3DRS_DITHERENABLE,D3DRS_SHADEMODE,
        D3DRS_FILLMODE,D3DRS_LASTPIXEL,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_WRAP0,D3DRS_WRAP1,D3DRS_WRAP2,
        D3DRS_WRAP3,D3DRS_WRAP4,D3DRS_WRAP6,D3DRS_WRAP8,D3DRS_WRAP9,D3DRS_WRAP10,D3DRS_WRAP11,
        D3DRS_WRAP12,D3DRS_WRAP13,D3DRS_WRAP14,D3DRS_WRAP15};
    struct Pending {
        Vertex* vertex=nullptr;Pixel* pixel=nullptr;
        IDirect3DVertexDeclaration9* declaration=nullptr;
        D3DVIEWPORT9 viewport{};DWORD raster[sizeof(rasterKeys)/sizeof(*rasterKeys)]={};
        float vs[256*4];std::vector<float> ps;int vi[16*4],pi[16*4];BOOL vb[16],pb[16];
        NorthlightDrawSnapshot::Mesh mesh;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> sharedMesh;
        IDirect3DVertexBuffer9* gpuVertex[4]={};IDirect3DIndexBuffer9* gpuIndex=nullptr;
        NorthlightReplayGPU::Placement placement;bool resident=false;
        const NorthlightDrawSnapshot::Mesh& geometry()const{return sharedMesh?*sharedMesh:mesh;}
        IDirect3DBaseTexture9* textures[16]={};DWORD samplers[16][13]={};
        void recycle(){resident=false;for(auto*& b:gpuVertex)release(b);release(gpuIndex);sharedMesh.reset();release(declaration);for(auto*& t:textures)release(t);}
        ~Pending(){recycle();}
    };
    NorthlightVertexDeclarations::Cache declarations;
    NorthlightDrawSnapshot::Frame snapshots{8u<<20,0};
    NorthlightGpuScene::NativeClient* geometryCache=nullptr;
    std::vector<std::unique_ptr<Pending>> pending,spare;
    // Recycled buffers and queued geometry share the same 8 MiB allowance.
    size_t pendingBytes=0,spareBytes=0;unsigned pendingTextureSlots=0;bool deferred=false,sourceSharing=false;
    static size_t packetBytes(const Pending& p){return sizeof(Pending)+p.ps.capacity()*sizeof(float)+p.mesh.capacityBytes()+(p.sharedMesh?p.sharedMesh->capacityBytes():0);}
    void(*pendingChanged)(void*)=nullptr;void* pendingContext=nullptr;
    unsigned queued=0,flushes=0,immediate=0,sourceReused=0,residentDraws=0,handoffs=0,rangeNarrow=0,rangeFull=0,rangeRegisters=0;size_t sourceReadBytes=0,upBytes=0;
    void clearPending(){const bool hadPending=!pending.empty();
        for(auto& p:pending){p->recycle();spareBytes+=packetBytes(*p);spare.push_back(std::move(p));}
        pending.clear();pendingBytes=0;pendingTextureSlots=0;snapshots.clearFrame();if(hadPending&&pendingChanged)pendingChanged(pendingContext);}
    template<class Admit> bool record(Vertex* vs,Pixel* ps,const float* control,const D3DVIEWPORT9& viewport,Admit& admit,const NorthlightDrawSnapshot::Draw* geometry,
                const void* indices,D3DFORMAT format,const void* vertices,UINT stride,
                const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured,IDirect3DVertexDeclaration9* capturedDeclaration,const NorthlightPoseBounds::Handoff* pose){
        if(!deferred||!vs->reads.exact||vs->reads.samplers||!ps->reads.exact||!geometry||pending.size()>=2048||d->GetNPatchMode()!=0)return false;
        try{
            std::unique_ptr<Pending> packet;
            if(spare.empty())packet=std::make_unique<Pending>();
            else {packet=std::move(spare.back());spare.pop_back();spareBytes-=packetBytes(*packet);}
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
            if(FAILED(d->GetVertexDeclaration(declaration.out()))||!declaration.p)return false;
            // The material producer hands off the immutable RAW source from
            // this same serialized draw, before geometry replacement/dedup.
            if(captured&&*captured&&capturedDeclaration==declaration.p&&(*captured)->topology==geometry->topology&&
               (*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed){packet->sharedMesh=*captured;++sourceReused;++handoffs;}
            else if(vertices){if(!snapshots.readUP(declaration.p,*geometry,indices,format,vertices,stride,packet->mesh))return false;}
            else {
                // Use the existing bounded source pool even when AutoBump did
                // not capture this draw. Current write revisions validate hits.
                const auto hits=snapshots.snapshotCacheHits();const auto read=snapshots.bytesRead();
                const bool captured=snapshots.read(d,declaration.p,*geometry,packet->mesh,nullptr,false,sourceSharing?&packet->sharedMesh:nullptr);
                sourceReadBytes+=snapshots.bytesRead()-read;sourceReused+=snapshots.snapshotCacheHits()-hits;
                if(!captured)return false;
            }
            const auto& mesh=packet->geometry();
            if(mesh.streams[0].bytes.empty())return false;
            for(unsigned i=1;i<4;++i)if(!mesh.streams[i].bytes.empty())return false;
            if(vs->rangeKind==NorthlightModelNormalRange::Envelope&&pose&&pose->bounds.valid&&poseReader&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){++poseBoundsTests;NorthlightConstantEpoch::Stamp current;current.requestHeld=true;
                if(poseReader(poseContext,current)&&pose->outside(packet->sharedMesh.get(),vs->source,declaration.p,current)){
                    ++poseBoundsCulled;packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            bool vsCaptured=false;
            if((vs->rangeKind==NorthlightModelNormalRange::Rigid||vs->rangeKind==NorthlightModelNormalRange::Single)&&packet->sharedMesh
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
               &&rangeEnabled
#endif
            ){
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;bool outside=false,known=false;
                if(declarations.get(declaration.p,elements,count)){
                    namespace E=NorthlightConstantEpoch;E::Stamp before;before.requestHeld=true;
                    known=poseSpan&&poseReader&&poseReader(poseContext,before)&&before.held;
                    if(known)outside=rangeCache.outsideRead(packet->sharedMesh,elements,count,[&](unsigned first,unsigned n,float* out){
                        E::Stamp copied;const bool valid=poseSpan(poseContext,first,n,out,copied)&&E::exact(before,copied);known&=valid;if(valid)rangeRegisters+=n;return valid;
                    },vs->rangeKind);
                    if(known)++rangeNarrow;
                    else {if(FAILED(d->GetVertexShaderConstantF(0,packet->vs,256)))return false;vsCaptured=true;++rangeFull;rangeRegisters+=256;
                        outside=rangeCache.outside(packet->sharedMesh,elements,count,packet->vs,vs->rangeKind);}
                }
                if(outside){
                    packet->recycle();spareBytes+=packetBytes(*packet);spare.push_back(std::move(packet));return true;}}
            if(!admit())return false;
            // Proven out-of-range draws need no target/texture/sampler capture.
            // Render-target inputs otherwise retain the immediate path;
            // ordinary inputs use the serialized texture-write guard.
            for(unsigned i=0;i<16;++i)if(ps->reads.samplers&(1u<<i)){
                NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;
                if(FAILED(d->GetTexture(i,texture.out())))return false;
                if(!samplerReader||!samplerReader(samplerContext,i,(1u<<13)-1,packet->samplers[i]))
                    for(unsigned s=0;s<13;++s)if(FAILED(d->GetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),packet->samplers[i]+s)))return false;
                if(!texture.p)continue;
                D3DSURFACE_DESC desc{};
                if(texture->GetType()!=D3DRTYPE_TEXTURE||
                   FAILED(static_cast<IDirect3DTexture9*>(texture.p)->GetLevelDesc(0,&desc))||
                   (desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)))return false;
                packet->textures[i]=texture.p;texture.p=nullptr;
            }
            // A resident GPU lease already owns immutable source bytes. Keep
            // only draw metadata, so the CPU budget does not split warm scenes.
            if(geometryCache&&packet->sharedMesh&&packet->sharedMesh->sourceIdentity&&
               geometryCache->bind(d,packet->sharedMesh,packet->gpuVertex,packet->gpuIndex,[](size_t){return false;},packet->placement,true)){
                NorthlightDrawSnapshot::Mesh resident;
                resident.topology=mesh.topology;resident.vertexCount=mesh.vertexCount;resident.primitiveCount=mesh.primitiveCount;
                resident.indexed=mesh.indexed;resident.streams[0].stride=mesh.streams[0].stride;
                packet->mesh=std::move(resident);packet->sharedMesh.reset();packet->resident=true;
            }
            packet->ps.resize(ps->count*4);
            const size_t bytes=packetBytes(*packet);
            while(!spare.empty()&&bytes+pendingBytes+spareBytes>(8u<<20)){spareBytes-=packetBytes(*spare.back());spare.pop_back();}
            if(bytes>(8u<<20)-pendingBytes)return false;
            DWORD planes[1]={};const D3DRENDERSTATETYPE planeKeys[]={D3DRS_CLIPPLANEENABLE};
            if(!renderStates(planeKeys,planes)||planes[0]||!renderStates(rasterKeys,packet->raster))return false;
            if((!vsCaptured&&vs->count&&FAILED(d->GetVertexShaderConstantF(vs->first,packet->vs+vs->first*4,vs->count)))||
               FAILED(d->GetPixelShaderConstantF(ps->first,packet->ps.data(),ps->count))||(vs->reads.i&&FAILED(d->GetVertexShaderConstantI(0,packet->vi,16)))||
               (ps->reads.i&&FAILED(d->GetPixelShaderConstantI(0,packet->pi,16)))||(vs->reads.b&&FAILED(d->GetVertexShaderConstantB(0,packet->vb,16)))||(ps->reads.b&&FAILED(d->GetPixelShaderConstantB(0,packet->pb,16))))return false;
            packet->viewport=viewport;packet->declaration=declaration.p;declaration.p=nullptr;
            packet->vertex=vs;packet->pixel=ps;
            std::copy(control,control+4,packet->ps.data()+(ps->control-ps->first)*4);
            unsigned textureSlots=0;for(unsigned i=0;i<16;++i)if(packet->textures[i])textureSlots|=1u<<i;
            const bool texturesWerePending=pendingTextureSlots!=0;
            pending.push_back(std::move(packet));pendingBytes+=bytes;++queued;pendingTextureSlots|=textureSlots;
            // Only the first sampled texture changes the write guard. Its
            // callback also ends the native coverage MRT batch.
            if(!texturesWerePending&&pendingTextureSlots&&pendingChanged)pendingChanged(pendingContext);return true;
        }catch(const std::bad_alloc&){return false;}
    }
    struct State {
        inline static constexpr D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,
            D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,
            D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_COLORWRITEENABLE,D3DRS_WRAP5,D3DRS_WRAP7};
        static constexpr unsigned Count=sizeof(keys)/sizeof(keys[0]);
        IDirect3DDevice9* d;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        IDirect3DSurface9 *rt[4]={},*ds=nullptr;IDirect3DVertexBuffer9* stream=nullptr;IDirect3DIndexBuffer9* index=nullptr;
        UINT offset=0,stride=0;D3DVIEWPORT9 viewport{};DWORD values[Count]={};float constant[4]={};unsigned control,renderChanged=0,targetChanged=0;bool changed=false,up;
        State(IDirect3DDevice9* device,unsigned c,bool user):d(device),control(c),up(user){}
        bool save(bool full=true){
            if(FAILED(d->GetViewport(&viewport))||FAILED(d->GetRenderTarget(0,&rt[0])))return false;
            return !full||saveRest();
        }
        bool saveRest(){
            if(FAILED(d->GetVertexShader(&vs))||FAILED(d->GetPixelShader(&ps))||
               FAILED(d->GetDepthStencilSurface(&ds))||FAILED(d->GetPixelShaderConstantF(control,constant,1)))return false;
            for(unsigned i=1;i<4;++i){auto hr=d->GetRenderTarget(i,&rt[i]);if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
            for(unsigned i=0;i<Count;++i)if(FAILED(d->GetRenderState(keys[i],values+i)))return false;
            return !up||(SUCCEEDED(d->GetStreamSource(0,&stream,&offset,&stride))&&SUCCEEDED(d->GetIndices(&index)));
        }
        bool restore(){
            if(!changed)return true;changed=false;
            auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
            ok=retry([&]{return d->SetDepthStencilSurface(nullptr);})&&ok;
            for(unsigned i=1;i<4;++i)if(targetChanged&(1u<<i))ok=retry([&]{return d->SetRenderTarget(i,nullptr);})&&ok;
            for(unsigned i=0;i<4;++i)if(!i||(targetChanged&(1u<<i)))ok=retry([&]{return d->SetRenderTarget(i,rt[i]);})&&ok;
            ok=retry([&]{return d->SetDepthStencilSurface(ds);})&&ok;
            ok=retry([&]{return d->SetViewport(&viewport);})&&ok;
            ok=retry([&]{return d->SetVertexShader(vs);})&&ok;ok=retry([&]{return d->SetPixelShader(ps);})&&ok;
            ok=retry([&]{return d->SetPixelShaderConstantF(control,constant,1);})&&ok;
            for(unsigned i=0;i<Count;++i)if(renderChanged&(1u<<i))ok=retry([&]{return d->SetRenderState(keys[i],values[i]);})&&ok;
            if(up){ok=retry([&]{return d->SetStreamSource(0,stream,offset,stride);})&&ok;ok=retry([&]{return d->SetIndices(index);})&&ok;}
            return ok;
        }
        ~State(){restore();release(vs);release(ps);for(auto*& p:rt)release(p);release(ds);release(stream);release(index);}
    };
    Vertex* vertex(IDirect3DVertexShader9* source){
        auto it=vertices.find(source);if(it!=vertices.end())return &it->second;if(vertices.size()>=512)return nullptr;
        Vertex v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightModelNormal::vertex(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched)){
                v.rangeKind=NorthlightModelNormalRange::classify(reinterpret_cast<const uint32_t*>(code.data()),code.size(),true);
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                if(v.reads.exact){
                    unsigned first=256,last=0;
                    for(unsigned c=0;c<256;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){first=std::min(first,c);last=c+1;}
                    v.first=last?first:0;v.count=last-v.first;
                }
                d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
            }
        }
        return &vertices.emplace(source,v).first->second;
    }
    Pixel* pixel(IDirect3DPixelShader9* source,bool alpha){
        auto& map=pixels[alpha];auto it=map.find(source);if(it!=map.end())return &it->second;if(map.size()>=512)return nullptr;
        Pixel v;v.source=source;source->AddRef();UINT bytes=0;
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&bytes%4==0){
            std::vector<DWORD> code(bytes/4);std::vector<std::uint32_t> patched;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,v.control,alpha,false,true)){
                v.reads=NorthlightShaderReads::scan(patched.data(),patched.size());
                v.reads.samplers=v.reads.sampledSamplers;
                unsigned last=v.control+1;v.first=v.control;
                for(unsigned c=0;c<224;++c)if(v.reads.f[c>>6]&(1ull<<(c&63))){v.first=std::min(v.first,c);last=std::max(last,c+1);}
                v.count=last-v.first;
                d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&v.shader);
            }
        }
        return &map.emplace(source,v).first->second;
    }
    bool targets(UINT w,UINT h){
        if(w!=width||h!=height){reset();width=w;height=h;}
        if(normal&&surface&&depth)return true;
        const auto bytes=uint64_t(w)*h*8;MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes*2+(128ull<<20))return false;
        if(FAILED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&normal,nullptr))||
           FAILED(normal->GetSurfaceLevel(0,&surface))||FAILED(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr))){reset();return false;}
        return true;
    }
public:
    explicit NorthlightModelNormals(IDirect3DDevice9* device):d(device){snapshots.setSnapshotCacheLimit(0);pending.reserve(128);spare.reserve(128);
        snapshots.setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& elements,UINT& count){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,elements,count);});
        snapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});
    }
    ~NorthlightModelNormals(){pendingChanged=nullptr;reset();for(auto& v:vertices){release(v.second.source);release(v.second.shader);}for(auto& map:pixels)for(auto& v:map){release(v.second.source);release(v.second.shader);}}
    void endFrame(){clearPending();rangeCache.beginFrame();poseBoundsTests=poseBoundsCulled=0;draws=queued=flushes=immediate=sourceReused=residentDraws=handoffs=rangeNarrow=rangeFull=rangeRegisters=0;sourceReadBytes=upBytes=0;cleared=bad=false;release(frameTarget);}
    void reset(){endFrame();rangeCache.clear();snapshots.clearSnapshotCache();declarations.clear();spare.clear();spareBytes=0;release(batchState);release(surface);release(normal);release(depth);width=height=0;}
    IDirect3DTexture9* texture()const{return draws&&!bad?normal:nullptr;}
    bool shareCaptureSources(NorthlightDrawSnapshot::Frame& source,NorthlightDrawSnapshot::Frame::IdentityFn identity,
                            NorthlightDrawSnapshot::Frame::VersionFn version,NorthlightCaptureMetadata::Read metadata){
        snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);
        snapshots.setChargeResidentHits(false);sourceSharing=snapshots.shareSnapshotPool(source);return sourceSharing;
    }
    void setRasterStateSource(void* context,bool(*reader)(void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*)){rasterContext=context;rasterReader=context==d?reader:nullptr;}
    void setSamplerStateSource(void* context,bool(*reader)(void*,DWORD,unsigned,DWORD*)){samplerContext=context;samplerReader=context==d?reader:nullptr;}
    void setTrustedPoseSource(NorthlightConstantEpoch::TrustedSource source){poseContext=source.context();poseReader=source&&source.context()==d?source.reader():nullptr;poseSpan=poseReader?source.span():nullptr;}
    void setGeometryCache(NorthlightGpuScene::NativeClient* cache){geometryCache=cache;}
    unsigned cachedDraws()const{return residentDraws;}
    unsigned reusedSources()const{return sourceReused;}
    size_t readSourceBytes()const{return sourceReadBytes;}
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
    size_t testPendingPackets()const{return pending.size();}
    void testRange(bool enabled){rangeEnabled=enabled;}
    unsigned testRangeCulled()const{return rangeCache.culled+poseBoundsCulled;}
    size_t testRangeBytes()const{return sizeof(rangeCache);}
    unsigned testRangeRegisters()const{return rangeRegisters;}
#endif
    bool pendingTextures()const{return pendingTextureSlots!=0;}
    bool samplesTexture(void* texture)const{if(!texture||!pendingTextureSlots)return false;for(const auto& p:pending)for(unsigned i=0;i<16;++i)if((pendingTextureSlots&(1u<<i))&&p->textures[i]==texture)return true;return false;}
    void beforeTextureWrite(void* texture){if(samplesTexture(texture))finishCaptures();}
    void setDeferredCapture(bool on,void(*changed)(void*)=nullptr,void* context=nullptr){
        if(!on)finishCaptures();deferred=on;pendingChanged=changed;pendingContext=context;
    }
    template<class Log> void report(unsigned frame,Log log)const{log("MODEL_NORMALS frame=%u draws=%u queued=%u flushes=%u immediate=%u pending=%zu failed=%u sourceReused=%u sourceReadBytes=%zu sourcePoolBytes=%zu sourcePoolLimitMiB=%zu residentDraws=%u upBytes=%zu handoffs=%u rangeTests=%u rangeCulled=%u rangeSingleTests=%u rangeSingleUniform=%u rangeSingleMixed=%u rangeDeferred=%u poseBoundsTests=%u poseBoundsCulled=%u rangeNarrow=%u rangeFull=%u rangeRegisters=%u",frame,draws,queued,flushes,immediate,pending.size(),unsigned(bad),sourceReused,sourceReadBytes,snapshots.sharedSourceBytes(),snapshots.sharedSourceLimit()>>20,residentDraws,upBytes,handoffs,rangeCache.tested,rangeCache.culled,rangeCache.singleTests,rangeCache.singleUniform,rangeCache.singleMixed,rangeCache.deferred,poseBoundsTests,poseBoundsCulled,rangeNarrow,rangeFull,rangeRegisters);}
    bool finishCaptures(){
        if(pending.empty())return !bad;
        bool ok=!bad;
        if(ok&&geometryCache){
            // Reuse the existing scene budget/promotion limits. Every packet
            // keeps buffer references across eviction; misses retain UP replay.
            for(auto& p:pending)if(p->sharedMesh&&p->sharedMesh->sourceIdentity)
                p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement);
            geometryCache->commitPending();
            for(auto& p:pending)if(!p->resident&&p->sharedMesh&&p->sharedMesh->sourceIdentity&&geometryCache->resident(p->sharedMesh))
                p->resident=geometryCache->bind(d,p->sharedMesh,p->gpuVertex,p->gpuIndex,[](size_t){return true;},p->placement);
        }
        {
            State saved(d,0,true);
            ok=saved.save()&&SUCCEEDED(batchState?batchState->Capture():d->CreateStateBlock(D3DSBT_ALL,&batchState))&&ok;
            if(ok){
                saved.changed=true;saved.targetChanged=14;saved.renderChanged=(1u<<State::Count)-1;
                ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
                for(unsigned i=1;i<4;++i)ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;
                ok=SUCCEEDED(d->SetDepthStencilSurface(depth))&&ok;
                for(unsigned i=0;i<16;++i)ok=SUCCEEDED(d->SetTexture(i,nullptr))&&ok;
                ok=SUCCEEDED(d->SetStreamSourceFreq(0,1))&&ok;
                ok=SUCCEEDED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0))&&ok;
                const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
                for(unsigned i=0;i<State::Count;++i)ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;
                ++flushes;
                const Pending* previous=nullptr;IDirect3DVertexBuffer9* boundVertex=nullptr;IDirect3DIndexBuffer9* boundIndex=nullptr;UINT boundStride=0;bool bufferKnown=false;
                for(const auto& p:pending){
                    if(!ok)break;
                    if(!previous||std::memcmp(&previous->viewport,&p->viewport,sizeof p->viewport))ok=SUCCEEDED(d->SetViewport(&p->viewport))&&ok;
                    if(!previous||previous->declaration!=p->declaration)ok=SUCCEEDED(d->SetVertexDeclaration(p->declaration))&&ok;
                    for(unsigned i=0;i<sizeof(rasterKeys)/sizeof(*rasterKeys);++i)if(!previous||previous->raster[i]!=p->raster[i])ok=SUCCEEDED(d->SetRenderState(rasterKeys[i],p->raster[i]))&&ok;
                    for(unsigned i=0;i<16;++i)if(p->pixel->reads.samplers&(1u<<i)){
                        const bool known=previous&&(previous->pixel->reads.samplers&(1u<<i));
                        if(!known||previous->textures[i]!=p->textures[i])ok=SUCCEEDED(d->SetTexture(i,p->textures[i]))&&ok;
                        for(unsigned s=0;s<13;++s)if(!known||previous->samplers[i][s]!=p->samplers[i][s])ok=SUCCEEDED(d->SetSamplerState(i,D3DSAMPLERSTATETYPE(s+1),p->samplers[i][s]))&&ok;
                    }
                    // Both packets must have captured the whole compared range;
                    // a shader switch may expose previously uninitialized lanes.
                    auto dirty=NorthlightReplayConstants::DirtySpan{p->vertex->first,p->vertex->count};
                    if(previous&&previous->vertex->first<=dirty.first&&previous->vertex->first+previous->vertex->count>=dirty.first+dirty.count){
                        const auto relative=NorthlightReplayConstants::Detail::knownFloatSpan(previous->vs+dirty.first*4,p->vs+dirty.first*4,dirty.count);
                        dirty.first+=relative.first;dirty.count=relative.count;
                    }
                    if(dirty.count)ok=SUCCEEDED(d->SetVertexShaderConstantF(dirty.first,p->vs+dirty.first*4,dirty.count))&&ok;
                    if(!previous||previous->pixel->first!=p->pixel->first||previous->pixel->count!=p->pixel->count||
                       std::memcmp(previous->ps.data(),p->ps.data(),p->pixel->count*16))
                        ok=SUCCEEDED(d->SetPixelShaderConstantF(p->pixel->first,p->ps.data(),p->pixel->count))&&ok;
                    if(p->vertex->reads.i)ok=SUCCEEDED(d->SetVertexShaderConstantI(0,p->vi,16))&&ok;
                    if(p->pixel->reads.i)ok=SUCCEEDED(d->SetPixelShaderConstantI(0,p->pi,16))&&ok;
                    if(p->vertex->reads.b)ok=SUCCEEDED(d->SetVertexShaderConstantB(0,p->vb,16))&&ok;
                    if(p->pixel->reads.b)ok=SUCCEEDED(d->SetPixelShaderConstantB(0,p->pb,16))&&ok;
                    if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
                    if(!previous||previous->vertex!=p->vertex)ok=SUCCEEDED(d->SetVertexShader(p->vertex->shader))&&ok;
                    if(!previous||previous->pixel!=p->pixel)ok=SUCCEEDED(d->SetPixelShader(p->pixel->shader))&&ok;
                    const auto& mesh=p->geometry();
                    if(p->resident){
                        const auto stride=mesh.streams[0].stride;
                        if(!bufferKnown||boundVertex!=p->gpuVertex[0]||boundStride!=stride)ok=SUCCEEDED(d->SetStreamSource(0,p->gpuVertex[0],0,stride))&&ok;
                        if(!bufferKnown||boundIndex!=p->gpuIndex)ok=SUCCEEDED(d->SetIndices(p->gpuIndex))&&ok;
                        bufferKnown=ok;boundVertex=p->gpuVertex[0];boundIndex=p->gpuIndex;boundStride=stride;
                        if(ok)ok=SUCCEEDED(mesh.indexed?d->DrawIndexedPrimitive(mesh.topology,INT(p->placement.vertexBase),0,mesh.vertexCount,p->placement.indexStart,mesh.primitiveCount):
                            d->DrawPrimitive(mesh.topology,p->placement.vertexBase,mesh.primitiveCount));
                        if(ok)++residentDraws;
                    }else{
                        if(ok)ok=SUCCEEDED(mesh.indexed?
                            d->DrawIndexedPrimitiveUP(mesh.topology,0,mesh.vertexCount,mesh.primitiveCount,mesh.indices.data(),D3DFMT_INDEX32,mesh.streams[0].bytes.data(),mesh.streams[0].stride):
                            d->DrawPrimitiveUP(mesh.topology,mesh.primitiveCount,mesh.streams[0].bytes.data(),mesh.streams[0].stride));
                        bufferKnown=false;if(ok)upBytes+=mesh.byteSize();
                    }
                    if(ok){++draws;previous=p.get();}
                }
                ok=saved.restore()&&ok;
                ok=(SUCCEEDED(batchState->Apply())||SUCCEEDED(batchState->Apply()))&&ok;
                ok=(SUCCEEDED(d->SetViewport(&saved.viewport))||SUCCEEDED(d->SetViewport(&saved.viewport)))&&ok;
            }
        }
        clearPending();bad|=!ok;return ok;
    }
    template<class Draw> void capture(IDirect3DVertexShader9* source,Draw draw,bool userMemory=false,
            const NorthlightDrawSnapshot::Draw* geometry=nullptr,const void* indices=nullptr,D3DFORMAT format=D3DFMT_UNKNOWN,const void* vertices=nullptr,UINT stride=0,
            const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>* captured=nullptr,IDirect3DVertexDeclaration9* capturedDeclaration=nullptr,const NorthlightPoseBounds::Handoff* pose=nullptr){
        if(bad||!source)return;
        // A material producer already proved this exact draw beyond 48 yards.
        // Consume only an unchanged bank/source/layout under the held draw gate.
        if(pose&&pose->outsideRange&&poseReader&&geometry&&captured&&*captured&&
           (*captured)->topology==geometry->topology&&(*captured)->primitiveCount==geometry->primitives&&(*captured)->indexed==geometry->indexed
#ifdef NORTHLIGHT_MODEL_NORMALS_TEST
           &&rangeEnabled
#endif
        ){
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
            NorthlightConstantEpoch::Stamp current;current.requestHeld=true;++poseBoundsTests;
            if(d->GetNPatchMode()==0&&SUCCEEDED(d->GetVertexDeclaration(declaration.out()))&&declaration.p&&
               capturedDeclaration==declaration.p&&poseReader(poseContext,current)&&pose->outside(captured->get(),source,declaration.p,current)){
                ++poseBoundsCulled;return;}
        }
        const D3DRENDERSTATETYPE admissionKeys[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_COLORWRITEENABLE,
            D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ZFUNC};
        DWORD states[8]={};if(!renderStates(admissionKeys,states)||!states[0]||!states[1]||states[2]||!(states[3]&7))return;
        const DWORD alpha=states[4],ref=states[5],func=states[6],comparison=states[7];
        if((alpha&&func!=D3DCMP_GREATER&&func!=D3DCMP_GREATEREQUAL)||(comparison!=D3DCMP_LESS&&comparison!=D3DCMP_LESSEQUAL&&comparison!=D3DCMP_EQUAL))return;
        auto* vs=vertex(source);if(!vs||!vs->shader)return;
        IDirect3DPixelShader9* native=nullptr;if(FAILED(d->GetPixelShader(&native))||!native)return;
        auto* ps=pixel(native,alpha!=0);release(native);if(!ps||!ps->shader)return;
        State saved(d,ps->control,userMemory);bool admissionTried=false,admissionOK=false;
        auto admit=[&]{
            if(admissionTried)return admissionOK;admissionTried=true;
            if(!saved.save(!deferred)||!saved.rt[0]||(frameTarget&&frameTarget!=saved.rt[0])||saved.viewport.X||saved.viewport.Y)return false;
            if(frameTarget){
                // The retained surface has immutable dimensions. Reset/endFrame
                // release this identity before another target can be admitted.
                if(width!=saved.viewport.Width||height!=saved.viewport.Height)return false;
            }else{
                D3DSURFACE_DESC desc{};if(FAILED(saved.rt[0]->GetDesc(&desc))||desc.Width!=saved.viewport.Width||desc.Height!=saved.viewport.Height)return false;
                if(!targets(desc.Width,desc.Height)){bad=true;return false;}
                frameTarget=saved.rt[0];frameTarget->AddRef();
            }
            return admissionOK=true;
        };
        // Establish a frame target before preparing packets: a resize resets
        // source caches/counters. Later far draws can avoid target reads entirely.
        if(!frameTarget&&!admit())return;
        const float control[]={0,48.f,alpha?(float(ref)+(func==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,alpha?1.f:0.f};
        if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose))return;
        if(!admit())return;
        if(deferred&&!pending.empty()){
            if(!finishCaptures())return;
            if(record(vs,ps,control,saved.viewport,admit,geometry,indices,format,vertices,stride,captured,capturedDeclaration,pose))return;
        }
        if(deferred&&!saved.saveRest())return;
        // Keep depth-tie ordering when a draw cannot use immutable replay.
        if(!finishCaptures())return;
        ++immediate;
        saved.changed=true;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        for(unsigned i=1;i<4;++i)if(saved.rt[i]){saved.targetChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderTarget(i,nullptr))&&ok;}
        ok=SUCCEEDED(d->SetRenderTarget(0,surface))&&ok;ok=SUCCEEDED(d->SetDepthStencilSurface(depth))&&ok;
        ok=SUCCEEDED(d->SetViewport(&saved.viewport))&&ok;
        // RT1..3 are unbound; their color-write masks cannot affect this pass.
        const DWORD values[]={TRUE,TRUE,D3DCMP_LESSEQUAL,FALSE,FALSE,FALSE,FALSE,FALSE,FALSE,15,0,0};
        // Most native opaque draws already have these values. Track attempted
        // changes before the driver call, so partial failures still restore.
        for(unsigned i=0;i<State::Count;++i)if(values[i]!=saved.values[i]){saved.renderChanged|=1u<<i;ok=SUCCEEDED(d->SetRenderState(State::keys[i],values[i]))&&ok;}
        if(ok&&!cleared){ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));cleared=ok;}
        ok=SUCCEEDED(d->SetVertexShader(vs->shader))&&ok;ok=SUCCEEDED(d->SetPixelShader(ps->shader))&&ok;
        ok=SUCCEEDED(d->SetPixelShaderConstantF(ps->control,control,1))&&ok;
        if(ok)ok=SUCCEEDED(draw());
        ok=saved.restore()&&ok;if(ok)++draws;else bad=true;
    }
};
