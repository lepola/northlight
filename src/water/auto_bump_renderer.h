#pragma once
#include "auto_bump_compiled_shaders.h"
#include "auto_bump_identities.h"
#include "water_shader_patch.h"
#include "quality_settings.h"
#include "auto_normal_cache.h"
#include "auto_parallax_renderer.h"
#include "auto_displacement_renderer.h"
#include "native_material_capture_patch.h"
#include "replay_gpu_cache.h"

// Prototype: one material-height replay per recognised draw; source textures remain intact.
// Included after drop and logf. Original resources stay intact; displacement draws use private subdivided buffers.
class NorthlightAutoBump {
    friend struct MaterialCaptureStateTestAccess;
    IDirect3DDevice9* d;
    NorthlightMaterialProfile* profile=nullptr;unsigned profileTag=0,profilePrimitives=0;uint64_t profileConfigFrame=~uint64_t(0);
    NorthlightAutoNormalCache normals;
    NorthlightSharedPOM sharedPOM;
    NorthlightAutoNormalCache::Lease sharedMaps[2];
    NorthlightAutoParallax parallax;
    NorthlightAutoDisplacement displacement;
    // Frame-local replay owns immutable vertex data and the generated map. It
    // never relies on game buffers/textures staying unchanged until the flush.
    static constexpr unsigned DeferredRasterCount=30;
    struct DeferredTexture {
        IDirect3DBaseTexture9* texture=nullptr;unsigned stage=0;DWORD sampler[13]={};uint64_t bytes=0;
        ~DeferredTexture(){drop(texture);}
    };
    struct Deferred {
        IDirect3DVertexShader9* vertex=nullptr;IDirect3DPixelShader9* pixel=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;NorthlightAutoNormalCache::Lease normal;
        float vertexConstants[256*4];std::vector<float> pixelConstants;int vertexInt[64],pixelInt[64];BOOL vertexBool[16],pixelBool[16];
        DWORD states[14],raster[DeferredRasterCount],addressU=0,addressV=0;RECT scissor{};D3DVIEWPORT9 viewport{};unsigned first=0,tag=0,normalStage=14;bool occlusion=false;
        std::vector<std::unique_ptr<DeferredTexture>> textures;
        ~Deferred(){drop(vertex);drop(pixel);drop(declaration);}
    };
    const D3DRENDERSTATETYPE deferredRasterKeys[DeferredRasterCount]={D3DRS_CULLMODE,D3DRS_ZENABLE,D3DRS_ZFUNC,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,D3DRS_SCISSORTESTENABLE,D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_CLIPPING,D3DRS_DITHERENABLE,D3DRS_WRAP0,D3DRS_WRAP7,D3DRS_SHADEMODE,D3DRS_FILLMODE,D3DRS_LASTPIXEL,D3DRS_ANTIALIASEDLINEENABLE,D3DRS_WRAP1,D3DRS_WRAP2,D3DRS_WRAP3,D3DRS_WRAP4,D3DRS_WRAP5,D3DRS_WRAP6,D3DRS_WRAP8,D3DRS_WRAP9,D3DRS_WRAP10,D3DRS_WRAP11,D3DRS_WRAP12,D3DRS_WRAP13,D3DRS_WRAP14,D3DRS_WRAP15};
    NorthlightDrawSnapshot::Frame deferredSnapshots{2u<<20,0};
    NorthlightVertexDeclarations::Cache deferredDeclarations;
    NorthlightReplayGPU::Cache deferredGPU;
    unsigned residentDraws=0,uploadDraws=0;uint64_t uploadDrawBytes=0;
    unsigned replaySetters=0,replaySkipped=0;
    std::vector<std::unique_ptr<Deferred>> deferred;size_t deferredBytes=0;bool deferredAllowed=false;
    std::unordered_map<void*,uint64_t> deferredTextures;uint64_t deferredTextureBytes=0;
    bool textureWriteGuarded=false;void(*pendingChanged)(void*)=nullptr;void* pendingContext=nullptr;
    void clearDeferred(){deferred.clear();deferredBytes=deferredTextureBytes=0;deferredTextures.clear();if(pendingChanged)pendingChanged(pendingContext);}
    unsigned queued=0,flushed=0,deferredFallback=0,deferredDiscarded=0,deferredUnsafe=0,deferredGeometry=0;
    unsigned queuedDomain[4]={},earlyRangeRejected=0,opaqueCoverage=0;
    unsigned deferredTextureDraws=0,textureBarriers=0,snapshotBudget=0,snapshotStreams=0,textureRefused=0,batchLimit=0;
    bool deferredReport=false;uint64_t deferredReportFrame=0;
    float projection[4]={1,1,-1,8};
    // Only these states are changed by the replay. A full state block per material
    // copied every shader constant/texture and rebound all MRT/depth surfaces.
    struct ReplayState {
        IDirect3DDevice9* d;
        NorthlightMaterialProfile* profile;
        IDirect3DPixelShader9* pixel=nullptr;
        IDirect3DVertexShader9* vertex;
        IDirect3DSurface9* target=nullptr;
        IDirect3DSurface9* depth=nullptr;
        IDirect3DVertexBuffer9* stream=nullptr;
        IDirect3DIndexBuffer9* indices=nullptr;
        UINT offset=0,stride=0;
        D3DVIEWPORT9 viewport;
        RECT scissor{};DWORD scissorEnabled=FALSE;
        float constant[24]={};unsigned constantBase=20,constantCount=5,normalStage=2,stateCount=14;
        IDirect3DBaseTexture9* texture2=nullptr,*texture3=nullptr;bool composite=false;
        const D3DSAMPLERSTATETYPE samplerKeys[8]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS};
        DWORD samplerValues[8]={},samplerValues3[8]={};unsigned samplerChanged=0,samplerChanged3=0;
        struct State{D3DRENDERSTATETYPE key;DWORD value,replay;};
        State states[16]={
            {D3DRS_ZWRITEENABLE,0,FALSE},{D3DRS_ALPHATESTENABLE,0,FALSE},
            {D3DRS_FOGENABLE,0,FALSE},{D3DRS_SRGBWRITEENABLE,0,FALSE},
            {D3DRS_COLORWRITEENABLE,0,15},{D3DRS_ALPHABLENDENABLE,0,TRUE},
            {D3DRS_BLENDOP,0,D3DBLENDOP_ADD},{D3DRS_SRCBLEND,0,D3DBLEND_SRCALPHA},
            {D3DRS_DESTBLEND,0,D3DBLEND_INVSRCALPHA},{D3DRS_SEPARATEALPHABLENDENABLE,0,TRUE},
            {D3DRS_BLENDOPALPHA,0,D3DBLENDOP_ADD},{D3DRS_SRCBLENDALPHA,0,D3DBLEND_ONE},
            {D3DRS_DESTBLENDALPHA,0,D3DBLEND_INVSRCALPHA},{D3DRS_COLORWRITEENABLE1,0,15},
            {D3DRS_COLORWRITEENABLE2,0,15},{D3DRS_COLORWRITEENABLE3,0,15}};
        bool changed=false,userMemory=false,auxiliary=false,sharedTargets=false;
        ReplayState(IDirect3DDevice9* device,IDirect3DVertexShader9* vs,const D3DVIEWPORT9& vp,NorthlightMaterialProfile* p=nullptr):d(device),profile(p),vertex(vs),viewport(vp){}
        bool save(bool up,bool twoLayers=false){
            composite=twoLayers;
            if(composite){if(FAILED(d->GetTexture(3,&texture3)))return false;for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(3,samplerKeys[i],&samplerValues3[i])))return false;}
            if(!pixel||!target||
               FAILED(d->GetPixelShaderConstantF(constantBase,constant,constantCount))||FAILED(d->GetTexture(normalStage,&texture2)))return false;
            if(FAILED(d->GetScissorRect(&scissor))||FAILED(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled)))return false;
            for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(normalStage,samplerKeys[i],&samplerValues[i])))return false;
            if(up){if(FAILED(d->GetStreamSource(0,&stream,&offset,&stride))||FAILED(d->GetIndices(&indices)))return false;userMemory=true;}
            return true;
        }
        ~ReplayState(){
            NorthlightMaterialProfile::Scope timing(changed?profile:nullptr,NorthlightMaterialProfile::CaptureRestore,0,true);
            if(changed){
                if(sharedTargets){d->SetRenderTarget(2,nullptr);d->SetRenderTarget(3,nullptr);}
                if(auxiliary)d->SetRenderTarget(1,nullptr);
                if(depth)d->SetDepthStencilSurface(nullptr);
                d->SetRenderTarget(0,target);if(depth)d->SetDepthStencilSurface(depth);d->SetViewport(&viewport);d->SetScissorRect(&scissor);d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissorEnabled);
                d->SetVertexShader(vertex);d->SetPixelShader(pixel);d->SetPixelShaderConstantF(constantBase,constant,constantCount);
                d->SetTexture(normalStage,texture2);for(unsigned i=0;i<8;++i)if(samplerChanged&(1u<<i))d->SetSamplerState(normalStage,samplerKeys[i],samplerValues[i]);
                if(composite){d->SetTexture(3,texture3);for(unsigned i=0;i<8;++i)if(samplerChanged3&(1u<<i))d->SetSamplerState(3,samplerKeys[i],samplerValues3[i]);}
                for(unsigned i=0;i<stateCount;++i){auto& s=states[i];if(s.value!=s.replay)d->SetRenderState(s.key,s.value);}
                // UP draws reset stream 0 and (for indexed UP) the index binding.
                if(userMemory){d->SetStreamSource(0,stream,offset,stride);d->SetIndices(indices);}
            }
            drop(pixel);drop(target);drop(depth);drop(stream);drop(indices);drop(texture2);drop(texture3);
        }
        void apply(){changed=true;for(unsigned i=0;i<stateCount;++i){auto& s=states[i];if(s.value!=s.replay)d->SetRenderState(s.key,s.replay);}}
    };
    struct Vertex {IDirect3DVertexShader9* patched=nullptr;unsigned kind=0;IDirect3DVertexShader9* shared=nullptr;bool sharedChecked=false,deferredSafe=false,failed=false;IDirect3DVertexShader9* pending=nullptr;uint64_t pendingHash=0;};
    bool sharedVertex(Vertex& vertex){if(!vertex.sharedChecked){vertex.sharedChecked=true;vertex.shared=NorthlightSharedPOM::vertexShader(d,vertex.patched);}return vertex.shared!=nullptr;}
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    unsigned pendingVertices=0;
    using VertexIdentity=bool(*)(void*,IDirect3DVertexShader9*,uint64_t&,unsigned&);
    VertexIdentity vertexIdentity=nullptr;void* vertexIdentityContext=nullptr;bool vertexOverflow=false;
    std::unordered_map<IDirect3DPixelShader9*,unsigned> pixels;
    IDirect3DPixelShader9* shaders[6]={},*sharedShaders[6]={};
    struct NativeCapture {IDirect3DPixelShader9* source=nullptr,*shader=nullptr;unsigned constant=128,first=256,last=0,samplers=0;bool deferredSafe=false;};
    // Rare CPU creation work can happen outside sampled draw scopes, including
    // recognized vertex registration while materials are disabled.
    struct ColdWork {unsigned vertexSources=0,vertexQueued=0,modelMisses=0,modelBuilt=0,staticCreates=0,staticBuilt=0;uint64_t sourceBytes=0;double vertexMs=0,modelMs=0,staticMs=0;} cold;
    struct ColdTimer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        ~ColdTimer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}};
    HRESULT createCaptureShader(const DWORD* code,IDirect3DPixelShader9** output){
        ++cold.staticCreates;ColdTimer timer{cold.staticMs};const HRESULT hr=d->CreatePixelShader(code,output);cold.staticBuilt+=SUCCEEDED(hr)&&*output;return hr;
    }
    bool compileVertex(IDirect3DVertexShader9* original,Vertex& vertex,const DWORD* code,size_t count){
        std::vector<std::uint32_t> patched;
        if(NorthlightWaterShaderPatch::patch(reinterpret_cast<const std::uint32_t*>(code),count,patched))return false;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> shader;
        if(FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),shader.out()))||!shader.p)return false;
        NorthlightWaterShaderPatch::Analysis analysis;if(!NorthlightWaterShaderPatch::analyse(reinterpret_cast<const uint32_t*>(code),count,analysis))vertex.deferredSafe=analysis.used[10]==0;
        displacement.registerMaterial(original,code,count,vertex.kind);displacement.registerVariant(shader.p,reinterpret_cast<const DWORD*>(patched.data()),patched.size(),displacement.skinned(original));
        vertex.patched=shader.p;shader.p=nullptr;return true;
    }
    bool ensureVertex(IDirect3DVertexShader9* original,Vertex& vertex){
        if(vertex.patched)return true;if(!vertex.pending||vertex.failed)return false;
        ++cold.vertexSources;ColdTimer timer{cold.vertexMs};bool ready=false;
        try{UINT bytes=0;if(SUCCEEDED(vertex.pending->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536*sizeof(DWORD)&&bytes%sizeof(DWORD)==0){
                std::vector<DWORD> code(bytes/sizeof(DWORD));if(SUCCEEDED(vertex.pending->GetFunction(code.data(),&bytes))&&bytes==code.size()*sizeof(DWORD)&&NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(code.data()),code.size())==vertex.pendingHash){cold.sourceBytes+=bytes;ready=compileVertex(original,vertex,code.data(),code.size());}}}catch(...){}
        if(ready){drop(vertex.pending);--pendingVertices;}else vertex.failed=true;return ready;
    }
    auto recoverVertex(IDirect3DVertexShader9* original){
        // An inactive registry may fill with shaders an eager patch would reject.
        // Recover a live missing consumer, without evicting a compiled variant.
        if(!vertexOverflow||!vertexIdentity)return vertices.end();uint64_t hash=0;unsigned family=0;
        if(!vertexIdentity(vertexIdentityContext,original,hash,family))return vertices.end();
        const unsigned k=family==3?4:kind(kAutoBumpVertex,hash);if(!k||!modes[k-1])return vertices.end();
        if(vertices.size()>=4096){auto unused=std::find_if(vertices.begin(),vertices.end(),[](const auto& entry){return entry.second.pending!=nullptr;});
            if(unused==vertices.end())return vertices.end();drop(unused->second.pending);--pendingVertices;vertices.erase(unused);}
        auto added=vertices.emplace(original,Vertex{nullptr,k}).first;original->AddRef();added->second.pending=original;added->second.pendingHash=hash;++pendingVertices;
        if(!ensureVertex(original,added->second))return vertices.end();return added;
    }
    std::unordered_map<IDirect3DPixelShader9*,NativeCapture> nativeCaptures[2][2];
    NativeCapture* modelCapture(IDirect3DPixelShader9* source,bool alpha,bool shared=false){
        auto& variants=nativeCaptures[shared][alpha];auto it=variants.find(source);if(it!=variants.end())return it->second.shader?&it->second:nullptr;
        if(variants.size()>=512)return nullptr;NativeCapture entry;entry.source=source;source->AddRef();UINT bytes=0;
        ++cold.modelMisses;ColdTimer timer{cold.modelMs};
        if(SUCCEEDED(source->GetFunction(nullptr,&bytes))&&bytes&&bytes<65536){std::vector<DWORD> code(bytes/4);std::vector<uint32_t> patched;
            cold.sourceBytes+=bytes;
            if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),patched,entry.constant,alpha,shared)){
                d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&entry.shader);
                NorthlightTerrainCoverage::Program program;
                if(!shared&&NorthlightTerrainCoverage::analyse(patched.data(),patched.size(),program)){
                    bool defined[256]={};for(const auto& op:program.ops)if(op.code==81)defined[NorthlightWaterShaderPatch::regIndex(patched[op.at+1])]=true;
                    entry.deferredSafe=true;for(const auto& op:program.ops){if(op.code==31||op.code==81||op.code==48||op.code==47||op.code==65534)continue;
                        for(unsigned i=1;i<=op.size;++i){const auto t=patched[op.at+i];const unsigned type=NorthlightWaterShaderPatch::regType(t),r=NorthlightWaterShaderPatch::regIndex(t);if(t&0x2000u)entry.deferredSafe=false;if(type==10&&r!=14){if(r<16)entry.samplers|=1u<<r;else entry.deferredSafe=false;}
                            if(type==2&&r<256&&!defined[r]){entry.first=std::min(entry.first,r);entry.last=std::max(entry.last,r);}}}
                    entry.deferredSafe&=entry.first<=entry.constant&&entry.last>=entry.constant+4&&entry.last<224;
                }
            }}
        auto added=variants.emplace(source,entry).first;cold.modelBuilt+=entry.shader!=nullptr;if(!entry.shader)logf("MATERIAL_MODEL capture unavailable; native material retained");return entry.shader?&added->second:nullptr;
    }
    IDirect3DTexture9* height=nullptr;
    IDirect3DSurface9* target=nullptr;
    // The composite validates the captured linear depth against final scene depth.
    // A private 1x depth lets the replay shade MSAA scenes without 2/4/8 copies
    // of the full-screen normal/occlusion targets or a resolve after every draw.
    IDirect3DSurface9* captureDepth=nullptr;
    IDirect3DTexture9* occlusionMap=nullptr;IDirect3DSurface9* occlusionTarget=nullptr;
    bool occlusionUnavailable=false;unsigned occlusionRetry=0;
    float occlusionControl[4]={.7f,8,.35f,0},occlusionLight[4]={};
    UINT width=0,heightPixels=0;
    bool cleared=false,drawn=false,failed=false,occlusionCleared=false,occlusionDrawn=false;
    bool enabled=false,terrain=true,walls=true,grass=true,memoryPressure=false;
    float strength=.75f,range=48;
    unsigned modes[4]={2,2,1,3};float pomControl[4]={.018f,16,16,0};
    unsigned draws[4]={},frames=0,rejected=0,invisibleRejected=0,noCaptureRequired=0;
    bool pomInRange=true;unsigned rangeTests=0,rangeRejected=0,pomRangeRejected=0;
    float captureRange(unsigned domain)const{
        if(strength>0)return range;
        if(occlusionControl[2]>0)return std::min(range,projection[3]);
        return modes[domain-1]==2&&pomControl[0]>0&&pomInRange&&occlusionControl[0]>0&&occlusionLight[3]>0?std::min({range,pomControl[1],projection[3]}):0;
    }
    bool captureNeeded(unsigned domain)const{return captureRange(domain)>0;}
    template<size_t N> static unsigned kind(const AutoBumpIdentity (&table)[N],std::uint64_t hash){
        auto i=std::lower_bound(table,table+N,hash,[](const AutoBumpIdentity& a,std::uint64_t h){return a.hash<h;});
        return i!=table+N&&i->hash==hash?i->kind:0;
    }
    bool check(HRESULT hr,const char* stage){
        if(SUCCEEDED(hr))return true;
        if(hr==E_OUTOFMEMORY||hr==D3DERR_OUTOFVIDEOMEMORY){
            trimMemory();logf("AUTOBUMP allocation retry stage=%s hr=%08lx; modes retained",stage,(unsigned long)hr);return false;
        }
        if(!failed)logf("AUTOBUMP failed stage=%s hr=%08lx; native rendering retained",stage,(unsigned long)hr);
        failed=true;return false;
    }
    bool recordCapture(const NorthlightAutoDisplacement::Args& args,NorthlightAutoNormalCache::Lease& normal,bool auxiliary,const ReplayState& saved,const NativeCapture& model,IDirect3DVertexShader9* vertex,const float* captureConstants,DWORD addressU,DWORD addressV){
        using P=NorthlightMaterialProfile;P::Scope timing(profile,P::DeferredRecord,profileTag);
        if(deferred.size()>=512){++batchLimit;return false;}
        if(d->GetNPatchMode()!=0||(args.draw.topology!=D3DPT_TRIANGLELIST&&args.draw.topology!=D3DPT_TRIANGLESTRIP&&args.draw.topology!=D3DPT_TRIANGLEFAN))return false;
        try{
            NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
            UINT extent[4]={};if(!deferredDeclarations.captureLayout(decl.p,extent))return false;
            for(unsigned i=1;i<4;++i)if(extent[i]){++snapshotStreams;return false;}
            std::vector<std::unique_ptr<DeferredTexture>> textures;uint64_t textureBytes=0;
            for(unsigned stage=0;stage<16;++stage)if(model.samplers&(1u<<stage)){
                auto binding=std::make_unique<DeferredTexture>();binding->stage=stage;
                if(!textureWriteGuarded||FAILED(d->GetTexture(stage,&binding->texture))){++textureRefused;return false;}
                if(binding->texture){
                    D3DSURFACE_DESC desc{};
                    if(binding->texture->GetType()!=D3DRTYPE_TEXTURE||FAILED(static_cast<IDirect3DTexture9*>(binding->texture)->GetLevelDesc(0,&desc))||(desc.Pool!=D3DPOOL_MANAGED&&desc.Pool!=D3DPOOL_DEFAULT)||(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))){++textureRefused;return false;}
                    binding->bytes=NorthlightAutoNormalMemory::mipBytes(desc.Format,desc.Width,desc.Height,binding->texture->GetLevelCount());
                    if(!deferredTextures.count(binding->texture))textureBytes+=binding->bytes;
                }
                for(unsigned i=0;i<13;++i)if(FAILED(d->GetSamplerState(stage,D3DSAMPLERSTATETYPE(i+1),binding->sampler+i)))return false;
                textures.push_back(std::move(binding));
            }
            if(textureBytes>(128ull<<20)-deferredTextureBytes){++batchLimit;return false;}
            NorthlightDrawSnapshot::Mesh local;std::shared_ptr<const NorthlightDrawSnapshot::Mesh> mesh;
            NorthlightDrawSnapshot::Diagnostics why;
            if(args.vertices){if(!deferredSnapshots.readUP(decl.p,args.draw,args.indices,args.format,args.vertices,args.stride,local,&why)){snapshotBudget+=why.error==NorthlightDrawSnapshot::Error::Budget;return false;}mesh=deferredSnapshots.retain(std::move(local));}
            else if(!deferredSnapshots.read(d,decl.p,args.draw,local,&why,false,&mesh)){snapshotBudget+=why.error==NorthlightDrawSnapshot::Error::Budget;return false;}
            if(!mesh)mesh=deferredSnapshots.retain(std::move(local));
            if(mesh->streams[0].bytes.empty())return false;for(unsigned i=1;i<4;++i)if(!mesh->streams[i].bytes.empty()){++snapshotStreams;return false;}
            DWORD planes=0;if(FAILED(d->GetRenderState(D3DRS_CLIPPLANEENABLE,&planes))||planes)return false;
            const size_t cost=sizeof(Deferred)+mesh->byteSize()+(model.last-model.first+1)*16+textures.size()*(sizeof(DeferredTexture)+sizeof(void*));if(cost>(8u<<20)-deferredBytes){++batchLimit;return false;}
            auto packet=std::make_unique<Deferred>();packet->mesh=mesh;packet->tag=profileTag;packet->occlusion=auxiliary;
            packet->declaration=decl.p;decl.p=nullptr;packet->pixel=model.shader;packet->pixel->AddRef();packet->vertex=vertex;vertex->AddRef();
            packet->first=model.first;packet->pixelConstants.resize((model.last-model.first+1)*4);packet->viewport=saved.viewport;packet->addressU=addressU;packet->addressV=addressV;packet->normalStage=saved.normalStage;
            for(unsigned i=0;i<14;++i)packet->states[i]=saved.states[i].replay;
            for(unsigned i=0;i<DeferredRasterCount;++i)if(FAILED(d->GetRenderState(deferredRasterKeys[i],packet->raster+i)))return false;
            if(FAILED(d->GetScissorRect(&packet->scissor))||FAILED(d->GetVertexShaderConstantF(0,packet->vertexConstants,256))||FAILED(d->GetPixelShaderConstantF(packet->first,packet->pixelConstants.data(),UINT(packet->pixelConstants.size()/4)))||
               FAILED(d->GetVertexShaderConstantI(0,packet->vertexInt,16))||FAILED(d->GetPixelShaderConstantI(0,packet->pixelInt,16))||FAILED(d->GetVertexShaderConstantB(0,packet->vertexBool,16))||FAILED(d->GetPixelShaderConstantB(0,packet->pixelBool,16)))return false;
            std::copy(captureConstants,captureConstants+20,packet->pixelConstants.begin()+(model.constant-packet->first)*4);
            packet->textures=std::move(textures);void* inserted[16]={};unsigned insertedCount=0;
            try{
                for(const auto& t:packet->textures)if(t->texture&&deferredTextures.emplace(t->texture,t->bytes).second){inserted[insertedCount++]=t->texture;deferredTextureBytes+=t->bytes;}
                deferred.push_back(std::move(packet));
            }catch(...){for(unsigned i=0;i<insertedCount;++i){auto entry=deferredTextures.find(inserted[i]);deferredTextureBytes-=entry->second;deferredTextures.erase(entry);}throw;}
            deferred.back()->normal=std::move(normal);deferredBytes+=cost;++queued;++queuedDomain[profileTag/4-1];deferredTextureDraws+=model.samplers!=0;if(pendingChanged)pendingChanged(pendingContext);timing.work(mesh->vertexCount,mesh->byteSize());return true;
        }catch(...){return false;}
    }
public:
    NorthlightDrawSnapshot::Frame& captureSources(){return deferredSnapshots;}
    explicit NorthlightAutoBump(IDirect3DDevice9* device):d(device),normals(device),sharedPOM(device),parallax(device,normals),displacement(device){
        deferredGPU.setLimit(32u<<20);
        deferredSnapshots.setSnapshotCacheLimit(24u<<20);deferredSnapshots.setChargeResidentHits(false);deferredSnapshots.setReplayContentReuse(true);
        displacement.shareCaptureSources(deferredSnapshots);
        deferredSnapshots.setDeclarationProvider(&deferredDeclarations,[](void* c,IDirect3DVertexDeclaration9* v,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(v,e,n);});
        deferredSnapshots.setLayoutProvider(&deferredDeclarations,[](void* c,IDirect3DVertexDeclaration9* v,UINT* e){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(v,e);});}
    void setDeferredCapture(bool on){deferredAllowed=on;}
    // Enabled only by the serialized game-resource writer, never from a backend
    // guess. Source writes drain matching packets before changing bytes.
    void setTextureWriteGuard(bool on,void(*changed)(void*)=nullptr,void* context=nullptr){
        if(!on&&textureWriteGuarded)finishCaptures();textureWriteGuarded=on;pendingChanged=changed;pendingContext=context;
    }
    bool pendingTextures()const{return !deferredTextures.empty();}
    bool samplesTexture(void* texture)const{return texture&&deferredTextures.count(texture);}
    void beforeTextureWrite(void* texture){if(texture&&deferredTextures.count(texture)){++textureBarriers;finishCaptures();}}
    bool finishCaptures(){
        if(deferred.empty())return true;using P=NorthlightMaterialProfile;P::Scope timing(profile,P::DeferredFlush,deferred.front()->tag,true);timing.work(deferred.size(),deferredBytes);
        struct Result {NorthlightAutoBump& owner;bool ok=false;~Result(){if(!ok){owner.deferredDiscarded+=unsigned(owner.deferred.size());owner.clearDeferred();owner.drawn=owner.occlusionDrawn=false;owner.check(E_FAIL,"deferred material replay");}}} result{*this};
        struct Constants {IDirect3DDevice9* d;float bank[256*4],pixel[224*4];bool valid=false;explicit Constants(IDirect3DDevice9* device):d(device){}~Constants(){if(valid){d->SetVertexShaderConstantF(0,bank,256);d->SetPixelShaderConstantF(0,pixel,224);}}} constants(d);
        constants.valid=SUCCEEDED(d->GetVertexShaderConstantF(0,constants.bank,256))&&SUCCEEDED(d->GetPixelShaderConstantF(0,constants.pixel,224));
        SavedState saved(d);if(!saved.ok||!constants.valid)return false;
        for(unsigned i=1;i<4;++i)if(FAILED(d->SetRenderTarget(i,nullptr)))return false;
        if(FAILED(d->SetDepthStencilSurface(nullptr))||FAILED(d->SetRenderTarget(0,target))||FAILED(d->SetDepthStencilSurface(captureDepth)))return false;
        if(FAILED(d->SetRenderState(D3DRS_STENCILENABLE,FALSE))||FAILED(d->SetRenderState(D3DRS_CLIPPLANEENABLE,0))||FAILED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)))return false;
        if(!cleared){if(FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0)))return false;cleared=true;}
        const bool auxiliary=std::any_of(deferred.begin(),deferred.end(),[](const auto& p){return p->occlusion;});
        if(auxiliary&&!occlusionCleared&&occlusionTarget){
            if(FAILED(d->SetRenderTarget(0,occlusionTarget))||FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))||FAILED(d->SetRenderTarget(0,target)))return false;
            occlusionCleared=true;
        }
        struct Bindings {IDirect3DVertexBuffer9* vertex[4]={};IDirect3DIndexBuffer9* index=nullptr;
            ~Bindings(){for(auto& v:vertex)drop(v);drop(index);}} bindings;
        bool success=true;const Deferred* previous=nullptr;
        IDirect3DBaseTexture9* boundTextures[16]={};bool knownTextures[16]={};
        DWORD samplerValues[16][13]={};unsigned knownSamplers[16]={};
        bool frequencyKnown=false,viewportKnown=false,auxiliaryKnown=false,boundAuxiliary=false;
        D3DVIEWPORT9 boundViewport{};
        for(auto& packet:deferred){
            auto& mesh=*packet->mesh;ReplayState keys(d,nullptr,{});HRESULT hr=D3D_OK;
            auto step=[&](HRESULT result){if(FAILED(result))hr=result;};
            // This cache lives only inside this uninterrupted replay. Creation,
            // buffer uploads and draws do not change these captured states.
            auto needed=[&](bool same){same?++replaySkipped:++replaySetters;return !same;};
            auto texture=[&](unsigned stage,IDirect3DBaseTexture9* value){
                if(needed(knownTextures[stage]&&boundTextures[stage]==value)){const HRESULT result=d->SetTexture(stage,value);step(result);if(SUCCEEDED(result)){boundTextures[stage]=value;knownTextures[stage]=true;}}};
            auto samplerState=[&](unsigned stage,D3DSAMPLERSTATETYPE key,DWORD value){const unsigned index=unsigned(key)-1,mask=1u<<index;
                if(needed((knownSamplers[stage]&mask)&&samplerValues[stage][index]==value)){const HRESULT result=d->SetSamplerState(stage,key,value);step(result);if(SUCCEEDED(result)){samplerValues[stage][index]=value;knownSamplers[stage]|=mask;}}};
            // Immutable snapshots already have revision-aware ownership. Reuse
            // the replay cache's bounded promotion, weak lifetime and fallback.
            const bool resident=deferredGPU.bind(d,packet->mesh,bindings.vertex,bindings.index,[](size_t){return true;});
            if(resident){step(d->SetStreamSource(0,bindings.vertex[0],0,mesh.streams[0].stride));step(d->SetIndices(bindings.index));}
            if(needed(previous&&previous->declaration==packet->declaration))step(d->SetVertexDeclaration(packet->declaration));
            if(needed(frequencyKnown)){const HRESULT result=d->SetStreamSourceFreq(0,1);step(result);frequencyKnown=SUCCEEDED(result);}
            if(needed(previous&&previous->vertex==packet->vertex))step(d->SetVertexShader(packet->vertex));
            if(needed(previous&&previous->pixel==packet->pixel))step(d->SetPixelShader(packet->pixel));
            if(needed(viewportKnown&&!std::memcmp(&boundViewport,&packet->viewport,sizeof boundViewport))){
                const HRESULT result=d->SetViewport(&packet->viewport);step(result);if(SUCCEEDED(result)){boundViewport=packet->viewport;viewportKnown=true;}}
            if(needed(previous&&!std::memcmp(&previous->scissor,&packet->scissor,sizeof(RECT))))step(d->SetScissorRect(&packet->scissor));
            for(unsigned i=0;i<14;++i)if(needed(previous&&previous->states[i]==packet->states[i]))step(d->SetRenderState(keys.states[i].key,packet->states[i]));
            for(unsigned i=0;i<DeferredRasterCount;++i)if(needed(previous&&previous->raster[i]==packet->raster[i]))step(d->SetRenderState(deferredRasterKeys[i],packet->raster[i]));
            texture(packet->normalStage,packet->normal.get());const DWORD sampler[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,packet->addressU,packet->addressV,FALSE,0,0};
            for(unsigned i=0;i<8;++i)samplerState(packet->normalStage,keys.samplerKeys[i],sampler[i]);
            for(const auto& t:packet->textures){texture(t->stage,t->texture);for(unsigned i=0;i<13;++i)samplerState(t->stage,D3DSAMPLERSTATETYPE(i+1),t->sampler[i]);}
            auto floatConstants=[&](bool vertex,UINT first,const float* values,const float* before,UINT count,bool comparable){
                if(!needed(comparable&&(!count||!std::memcmp(before,values,size_t(count)*4*sizeof(float)))))return;
                UINT begin=0,end=count;
                // Keep one call in the original bank order. Only byte-identical
                // outer float4 registers are already proven by the previous draw.
                if(comparable){
                    while(begin<end&&!std::memcmp(before+begin*4,values+begin*4,4*sizeof(float)))++begin;
                    while(end>begin&&!std::memcmp(before+(end-1)*4,values+(end-1)*4,4*sizeof(float)))--end;
                }
                const float* data=begin?values+begin*4:values;
                step(vertex?d->SetVertexShaderConstantF(first+begin,data,end-begin):d->SetPixelShaderConstantF(first+begin,data,end-begin));
            };
            floatConstants(true,0,packet->vertexConstants,previous?previous->vertexConstants:nullptr,256,previous!=nullptr);
            const bool comparablePixel=previous&&previous->first==packet->first&&previous->pixelConstants.size()==packet->pixelConstants.size();
            floatConstants(false,packet->first,packet->pixelConstants.data(),comparablePixel?previous->pixelConstants.data():nullptr,UINT(packet->pixelConstants.size()/4),comparablePixel);
            if(needed(previous&&!std::memcmp(previous->vertexInt,packet->vertexInt,sizeof(packet->vertexInt))))step(d->SetVertexShaderConstantI(0,packet->vertexInt,16));
            if(needed(previous&&!std::memcmp(previous->pixelInt,packet->pixelInt,sizeof(packet->pixelInt))))step(d->SetPixelShaderConstantI(0,packet->pixelInt,16));
            if(needed(previous&&!std::memcmp(previous->vertexBool,packet->vertexBool,sizeof(packet->vertexBool))))step(d->SetVertexShaderConstantB(0,packet->vertexBool,16));
            if(needed(previous&&!std::memcmp(previous->pixelBool,packet->pixelBool,sizeof(packet->pixelBool))))step(d->SetPixelShaderConstantB(0,packet->pixelBool,16));
            // A render-target bind may reset the viewport. Elide MRT1 only
            // when that reset is equivalent to the captured full-size viewport.
            const bool defaultViewport=!packet->viewport.X&&!packet->viewport.Y&&packet->viewport.Width==width&&packet->viewport.Height==heightPixels&&packet->viewport.MinZ==0.f&&packet->viewport.MaxZ==1.f;
            if(SUCCEEDED(hr)&&needed(auxiliaryKnown&&boundAuxiliary==packet->occlusion&&defaultViewport)){
                hr=d->SetRenderTarget(1,packet->occlusion?occlusionTarget:nullptr);
                if(SUCCEEDED(hr)){auxiliaryKnown=true;boundAuxiliary=packet->occlusion;viewportKnown=false;}}
            if(SUCCEEDED(hr)){P::Scope draw(profile,P::CaptureDraw,packet->tag);draw.work(mesh.primitiveCount);
                if(resident){++residentDraws;hr=mesh.indexed?d->DrawIndexedPrimitive(mesh.topology,0,0,mesh.vertexCount,0,mesh.primitiveCount):d->DrawPrimitive(mesh.topology,0,mesh.primitiveCount);}
                else {++uploadDraws;uploadDrawBytes+=mesh.byteSize();hr=mesh.indexed?d->DrawIndexedPrimitiveUP(mesh.topology,0,mesh.vertexCount,mesh.primitiveCount,mesh.indices.data(),D3DFMT_INDEX32,mesh.streams[0].bytes.data(),mesh.streams[0].stride):d->DrawPrimitiveUP(mesh.topology,mesh.primitiveCount,mesh.streams[0].bytes.data(),mesh.streams[0].stride);
                    // Keep no binding assumptions across the UP fallback.
                    frequencyKnown=viewportKnown=auxiliaryKnown=false;}}
            if(FAILED(hr)){success=false;break;}previous=packet.get();++flushed;++draws[packet->tag/4-1];if(packet->occlusion){occlusionDrawn=true;if(profile)profile->count(P::OcclusionDraw);}
        }
        if(success){clearDeferred();result.ok=true;}return success;
    }
    void setProfiler(NorthlightMaterialProfile* p){profile=p;normals.setProfiler(p);parallax.setProfiler(p);displacement.setProfiler(p);}
    void setMemoryPressure(bool on){
        if(memoryPressure==on)return;memoryPressure=on;deferredSnapshots.setSnapshotCacheLimit(on?4u<<20:24u<<20);
        if(on&&!deferred.empty()){deferredDiscarded+=unsigned(deferred.size());clearDeferred();drawn=occlusionDrawn=false;}
        deferredGPU.setLimit(on?0:32u<<20);
        if(on){deferredSnapshots.clearIndexCache();deferredDeclarations.clear();}
        normals.setMemoryPressure(on);displacement.setMemoryPressure(on);sharedPOM.setMemoryPressure(on);
        logf("MATERIAL_MEMORY pressure=%u normalBudgetMiB=%u geometryBudgetMiB=%u relief=active",unsigned(on),on?32:96,on?32:128);
    }
    void trimMemory(){if(!deferred.empty()){deferredDiscarded+=unsigned(deferred.size());clearDeferred();drawn=occlusionDrawn=false;}sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();normals.trimMemory();displacement.trimMemory();deferredSnapshots.clearIndexCache();deferredGPU.clear();}
    bool geometryPrepared()const{return displacement.prepared();}
    unsigned geometryPrimitives()const{return displacement.preparedPrimitives();}
    bool geometryHeightGPU()const{return displacement.preparedHeightGPU();}
    void beginDraw(){displacement.beginDraw();sharedMaps[0]={};sharedMaps[1]={};sharedPOM.beginDraw();pomInRange=true;profileTag=profilePrimitives=0;
        if(profile&&profile->recording()){deferredReport=true;deferredReportFrame=profile->frameNumber();}
        if(profile&&profile->recording()&&profileConfigFrame!=profile->frameNumber()){
            profileConfigFrame=profile->frameNumber();logf("MATERIAL_CONFIG frame=%llu enabled=%u modes=%u/%u/%u/%u normalGain=%.3f normalRange=%.1f pomDepth=%.6f pomRange=%.1f pomSteps=%.0f occlusionRange=%.1f cavity=%.3f selfShadow=%.3f shadowSteps=%.0f lightActive=%.3f sharedPOM=%u nativeGpuEnabled=%u",(unsigned long long)profileConfigFrame,unsigned(enabled),modes[0],modes[1],modes[2],modes[3],strength,range,pomControl[0],pomControl[1],pomControl[2],occlusionDistance(),occlusionControl[2],occlusionControl[0],occlusionControl[1],occlusionLight[3],unsigned(sharedPOM.canTry()),unsigned(active()));}}
    void setGeometryTracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){displacement.tracking(identity,version,metadata);sharedPOM.tracking(identity,version,metadata);deferredSnapshots.setIdentityProvider(identity);deferredSnapshots.setVersionProvider(version);deferredSnapshots.setMetadataProvider(metadata);}
    void setConstantEpochSource(void* context,NorthlightConstantEpoch::Reader reader){displacement.setConstantEpochSource(context,reader);}
    void setVertexIdentity(void* context,VertexIdentity reader){vertexIdentityContext=context;vertexIdentity=reader;}
    template<class Draw> HRESULT geometryDraw(Draw draw,bool native=false){return parallax.shared()?sharedPOM.drawBound(true):displacement.draw(draw,native);}
    void setProjection(float x,float y,float z){projection[0]=x;projection[1]=y;projection[2]=z;}
    void setLight(const float* light){std::copy(light,light+4,occlusionLight);}
    void configure(const NorthlightQuality::Settings& settings){
        displacement.configure(settings);normals.configure(settings);
        const bool changed=enabled!=(settings.autoBump!=0)||strength!=settings.autoBumpStrength*.01f||range!=float(settings.autoBumpDistance)||modes[0]!=settings.autoBumpTerrain||modes[1]!=settings.autoBumpWalls||modes[2]!=settings.autoBumpGrass||modes[3]!=settings.autoBumpModels||pomControl[0]!=settings.autoParallaxDepth*.00018f||pomControl[1]!=float(settings.autoParallaxDistance)||pomControl[2]!=float(settings.autoParallaxSteps);
        enabled=settings.autoBump!=0;strength=settings.autoBumpStrength*.01f;range=float(settings.autoBumpDistance);
        terrain=settings.autoBumpTerrain!=0;walls=settings.autoBumpWalls!=0;grass=settings.autoBumpGrass!=0;
        modes[0]=settings.autoBumpTerrain;modes[1]=settings.autoBumpWalls;modes[2]=settings.autoBumpGrass;modes[3]=settings.autoBumpModels;
        pomControl[0]=settings.autoParallaxDepth*.00018f;pomControl[1]=float(settings.autoParallaxDistance);pomControl[2]=float(settings.autoParallaxSteps);
        const float shadow=settings.autoParallaxShadows?settings.autoParallaxShadowStrength*.01f:0,cavity=settings.autoCavity?settings.autoCavityStrength*.01f:0;
        if(shadow!=occlusionControl[0]||cavity!=occlusionControl[2]||float(settings.autoParallaxShadowSteps)!=occlusionControl[1]||projection[3]!=float(settings.autoMaterialOcclusionDistance))logf("MATERIAL_OCCLUSION shadow=%.2f steps=%u cavity=%.2f sharedRange=%u",shadow,settings.autoParallaxShadowSteps,cavity,settings.autoMaterialOcclusionDistance);
        projection[3]=float(settings.autoMaterialOcclusionDistance);
        occlusionControl[0]=shadow;occlusionControl[1]=float(settings.autoParallaxShadowSteps);occlusionControl[2]=cavity;
        if(shadow<=0&&cavity<=0){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=false;}
        if(changed)logf("AUTOBUMP enabled=%d strength=%.2f distance=%.0f terrain=%d walls=%d grass=%d modes=%u/%u/%u/%u pomDepth=%.6f pomRange=%.0f pomSteps=%.0f",int(enabled),strength,range,int(terrain),int(walls),int(grass),modes[0],modes[1],modes[2],modes[3],pomControl[0],pomControl[1],pomControl[2]);
        if(!enabled)reset();
        bool captureRequired=false;for(unsigned i=0;i<4;++i)captureRequired|=modes[i]&&captureNeeded(i+1);
        if(!captureRequired){deferredDiscarded+=unsigned(deferred.size());clearDeferred();sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();drop(captureDepth);drop(target);drop(height);drop(occlusionTarget);drop(occlusionMap);drawn=cleared=false;occlusionUnavailable=false;occlusionRetry=0;}
    }
    ~NorthlightAutoBump(){reset();for(auto& v:vertices){drop(v.second.patched);drop(v.second.shared);drop(v.second.pending);}for(auto*& p:shaders)drop(p);for(auto*& p:sharedShaders)drop(p);for(auto& groups:nativeCaptures)for(auto& variants:groups)for(auto& n:variants){drop(n.second.shader);drop(n.second.source);}}
    void reset(){clearDeferred();deferredSnapshots.clearIndexCache();deferredDeclarations.clear();deferredGPU.clear();sharedMaps[0]={};sharedMaps[1]={};sharedPOM.reset();displacement.reset();normals.reset();drop(captureDepth);drop(target);drop(height);drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=false;occlusionRetry=0;width=heightPixels=0;cleared=drawn=failed=occlusionCleared=occlusionDrawn=false;}
    void endFrame(){
        deferredDiscarded+=unsigned(deferred.size());clearDeferred();
        if(deferredReport||cold.vertexSources||cold.vertexQueued||cold.modelMisses||cold.staticCreates)logf("MATERIAL_COLD frame=%llu sampled=%u enabled=%u vertexSources=%u vertexQueued=%u pendingVertices=%u vertexCpuMs=%.6f modelMisses=%u modelBuilt=%u modelCpuMs=%.6f staticCreates=%u staticBuilt=%u staticCpuMs=%.6f sourceBytes=%llu",(unsigned long long)(deferredReport?deferredReportFrame:frames),unsigned(deferredReport),unsigned(enabled),cold.vertexSources,cold.vertexQueued,pendingVertices,cold.vertexMs,cold.modelMisses,cold.modelBuilt,cold.modelMs,cold.staticCreates,cold.staticBuilt,cold.staticMs,(unsigned long long)cold.sourceBytes);
        cold={};
        if(deferredReport||((queued||flushed||deferredFallback||deferredDiscarded)&&frames%600==0))logf("MATERIAL_DEFERRED frame=%llu sampled=%u enabled=%u queued=%u flushed=%u snapshotFallback=%u liveState=%u preparedGeometry=%u discardedUnused=%u terrain=%u walls=%u grass=%u models=%u earlyRangeRejected=%u opaqueCoverage=%u packetLimit=512 packetMiB=8 sourceMiB=24",(unsigned long long)(deferredReport?deferredReportFrame:frames),unsigned(deferredReport),unsigned(deferredAllowed),queued,flushed,deferredFallback,deferredUnsafe,deferredGeometry,deferredDiscarded,queuedDomain[0],queuedDomain[1],queuedDomain[2],queuedDomain[3],earlyRangeRejected,opaqueCoverage);
        if(deferredReport||((queued||flushed||deferredFallback)&&frames%600==0)){
            logf("MATERIAL_REPLAY_CONTENT frame=%llu captureTests=%u captureHits=%u captureComparedBytes=%zu",(unsigned long long)(deferredReport?deferredReportFrame:frames),deferredSnapshots.replayContentTests(),deferredSnapshots.replayContentHits(),deferredSnapshots.replayContentComparedBytes());
            logf("MATERIAL_BATCH_SOURCE frame=%llu textureGuard=%u textureDraws=%u writeBarriers=%u refusedTexture=%u snapshotBudget=%u snapshotStreams=%u batchLimit=%u sourceHits=%u sourcePeerHits=%u sourceMisses=%u sourceReadBytes=%zu sourceEntries=%zu sourceBytes=%zu sourceLimitMiB=%zu",(unsigned long long)(deferredReport?deferredReportFrame:frames),unsigned(textureWriteGuarded),deferredTextureDraws,textureBarriers,textureRefused,snapshotBudget,snapshotStreams,batchLimit,deferredSnapshots.snapshotCacheHits(),deferredSnapshots.residentPeerHits(),deferredSnapshots.snapshotCacheMisses(),deferredSnapshots.bytesRead(),deferredSnapshots.snapshotCacheEntries(),deferredSnapshots.snapshotCacheBytes(),deferredSnapshots.snapshotCacheLimit()>>20);
            logf("MATERIAL_SOURCE_LIFETIME frame=%llu participants=%zu limitMiB=%zu retainedBytes=%zu liveBytes=%zu liveObjects=%zu metadataBytes=%zu sharedHits=%u gpuIdentityHits=%u gpuReconstructedHits=%u gpuIdentityBytes=%zu gpuChargedBytes=%zu gpuQueueBytes=%zu globalIdentityBytes=%zu globalCpuBytes=%zu",(unsigned long long)(deferredReport?deferredReportFrame:frames),deferredSnapshots.sharedSourceParticipants(),deferredSnapshots.sharedSourceLimit()>>20,deferredSnapshots.sharedSourceBytes(),deferredSnapshots.sourceLiveBytes(),deferredSnapshots.sourceLiveObjects(),deferredSnapshots.sharedSourceMetadataBytes(),deferredSnapshots.sharedSourceHits(),deferredGPU.stats().identityHits,deferredGPU.stats().reconstructedHits,deferredGPU.stats().identityBytes,deferredGPU.chargedBytes(),deferredGPU.queueBytes(),NorthlightSourceIdentity::identityLiveBytes.load(std::memory_order_relaxed),NorthlightSourceIdentity::cpuLiveBytes.load(std::memory_order_relaxed));
            displacement.reportSources(deferredReport?deferredReportFrame:frames);
        }
        if(deferredReport||((queued||flushed||deferredFallback)&&frames%600==0)){
            logf("MATERIAL_BATCH_STATE frame=%llu issued=%u skippedIdentical=%u",(unsigned long long)(deferredReport?deferredReportFrame:frames),replaySetters,replaySkipped);
            const auto& g=deferredGPU.stats();const auto population=deferredGPU.population();
            logf("MATERIAL_BATCH_GPU frame=%llu residentDraws=%u upDraws=%u upBytes=%llu uploadedBytes=%zu reusedBytes=%zu cacheBytes=%zu cacheLimitMiB=%zu residentMeshes=%zu probationMeshes=%zu created=%u createCpuMs=%.4f warmup=%u attemptDeferred=%u byteDeferred=%u timeDeferred=%u evicted=%u expired=%u countPressure=%u bytePressure=%u probationEvictions=%u residentEvictions=%u countOnlyProbationEvictions=%u",(unsigned long long)(deferredReport?deferredReportFrame:frames),residentDraws,uploadDraws,(unsigned long long)uploadDrawBytes,deferredGPU.uploaded(),deferredGPU.reused(),deferredGPU.bytes(),deferredGPU.limit()>>20,population.resident,population.probation,g.created,g.createMs,g.warmup,g.attemptDeferred,g.uploadByteDeferred,g.timeDeferred,g.evictions,g.expiredEntries,g.countPressure,g.bytePressure,g.probationEvictions,g.residentEvictions,g.countOnlyProbationEvictions);
        }
        residentDraws=uploadDraws=replaySetters=replaySkipped=0;uploadDrawBytes=0;deferredGPU.beginFrame();deferredReport=false;
        queued=flushed=deferredFallback=deferredDiscarded=deferredUnsafe=deferredGeometry=0;
        std::fill(std::begin(queuedDomain),std::end(queuedDomain),0);earlyRangeRejected=opaqueCoverage=0;
        deferredTextureDraws=textureBarriers=snapshotBudget=snapshotStreams=textureRefused=batchLimit=0;deferredSnapshots.clearFrame();
        if(occlusionRetry)--occlusionRetry;
        displacement.endFrame();
        sharedMaps[0]={};sharedMaps[1]={};sharedPOM.endFrame();if(frames%600==0)sharedPOM.report();
        normals.endFrame();
        parallax.endFrame();
        if(drawn&&frames%600==0){normals.report();auto& cache=NorthlightMaterialTextures::cache();const auto& c=cache.counts;logf("MHEIGHT cached=%u bytes=%zu uploads=%llu retained=%llu failed=%llu invalidated=%llu evicted=%llu",cache.size(),cache.bytes(),(unsigned long long)c.uploads,(unsigned long long)c.retained,(unsigned long long)c.failed,(unsigned long long)c.invalidations,(unsigned long long)c.evictions);}
        if((drawn||rangeTests)&&frames%600==0){logf("MATERIAL_RANGE proven=%u captureRejected=%u nativePomRejected=%u",rangeTests,rangeRejected,pomRangeRejected);displacement.reportHeightGPU();}
        if(drawn&&frames%600==0)logf("AUTOBUMP captured terrain=%u walls=%u grass=%u models=%u target=%ux%u bytes=%llu rejected=%u",draws[0],draws[1],draws[2],draws[3],width,heightPixels,(unsigned long long)width*heightPixels*8,rejected);
        if(frames%600==0&&(invisibleRejected||noCaptureRequired))logf("MATERIAL_ADMISSION invisible=%u noCaptureContribution=%u",invisibleRejected,noCaptureRequired);
        for(auto& n:draws)n=0;rejected=rangeTests=rangeRejected=pomRangeRejected=invisibleRejected=noCaptureRequired=0;cleared=drawn=occlusionCleared=occlusionDrawn=false;pomInRange=true;++frames;
    }
    bool active()const{return enabled&&(modes[0]||modes[1]||modes[2]||modes[3])&&(strength>0||occlusionControl[2]>0||
        ((modes[0]==2||modes[1]==2||modes[2]==2||modes[3]==2)&&pomControl[0]>0)||
        ((modes[0]==3||modes[1]==3||modes[2]==3||modes[3]==3)&&displacement.hasDisplacement()))&&!failed;}
    IDirect3DTexture9* texture()const{return active()&&drawn?height:nullptr;}
    IDirect3DTexture9* occlusionTexture()const{return active()&&drawn&&occlusionDrawn?occlusionMap:nullptr;}
    float occlusionDistance()const{return std::min(range,projection[3]);}
    float gain()const{return texture()?strength:0;}
    float distance()const{return range;}
    template<class Draw> HRESULT nativeDraw(Draw draw,IDirect3DVertexShader9* provenVS=nullptr,IDirect3DPixelShader9* provenPS=nullptr){
        using P=NorthlightMaterialProfile;
        const bool materialActive=active();
        auto native=[&]{P::Scope timing(profileTag?profile:nullptr,P::NativeDraw,profileTag,materialActive);timing.work(displacement.prepared()?displacement.preparedPrimitives():profilePrimitives);return draw();};
        if(!materialActive||!pomInRange||pomControl[0]<=0||(modes[0]!=2&&modes[1]!=2&&modes[2]!=2&&modes[3]!=2))return native();
        struct BoundShaders {IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;~BoundShaders(){drop(vs);drop(ps);}} bound;
        if((!provenVS&&FAILED(d->GetVertexShader(&bound.vs)))||(!provenPS&&FAILED(d->GetPixelShader(&bound.ps))))return native();
        auto v=vertices.find(provenVS?provenVS:bound.vs);if(v==vertices.end())v=recoverVertex(provenVS?provenVS:bound.vs);auto p=pixels.find(provenPS?provenPS:bound.ps);
        if(v==vertices.end()||p==pixels.end()||v->second.kind>4||modes[v->second.kind-1]!=2)return native();
        const unsigned domain=v->second.kind,material=p->second;
        if((domain==1&&material>2)||(domain==2&&material!=3&&material!=5&&material!=6)||(domain==3&&material!=4))return native();
        if(!ensureVertex(v->first,v->second))return native();
        return parallax.draw(v->first,v->second.patched,material,projection,pomControl,native,domain==4,bound.vs,bound.ps,&sharedPOM,v->second.shared,provenVS!=nullptr);
    }
    void registerVertex(IDirect3DVertexShader9* original,std::uint64_t hash,const DWORD* code,size_t count,unsigned modelFamily=0){
        auto found=vertices.find(original);if(found!=vertices.end()){drop(found->second.patched);drop(found->second.shared);if(found->second.pending){drop(found->second.pending);--pendingVertices;}vertices.erase(found);}
        const unsigned k=modelFamily==3?4:kind(kAutoBumpVertex,hash);if(!k)return;if(vertices.size()>=4096){vertexOverflow=true;return;}
        if(!active()||!modes[k-1]){auto added=vertices.emplace(original,Vertex{nullptr,k}).first;original->AddRef();added->second.pending=original;added->second.pendingHash=hash;++pendingVertices;++cold.vertexQueued;return;}
        ++cold.vertexSources;cold.sourceBytes+=count*sizeof(DWORD);ColdTimer timer{cold.vertexMs};
        Vertex vertex;vertex.kind=k;if(compileVertex(original,vertex,code,count))try{vertices.emplace(original,vertex);}catch(...){drop(vertex.patched);throw;}
    }
    void registerPixel(IDirect3DPixelShader9* original,std::uint64_t hash){
        pixels.erase(original);const unsigned k=kind(kAutoBumpPixel,hash);
        if(k&&pixels.size()<4096)pixels[original]=k;
    }
    template<class Draw> void capture(IDirect3DVertexShader9* original,Draw draw,bool userMemory=false,const NorthlightAutoDisplacement::Args* geometry=nullptr){
        pomInRange=true;
        if(!active()&&(!profile||!profile->recording()))return;
        auto v=vertices.find(original);if(v==vertices.end()&&active())v=recoverVertex(original);if(v==vertices.end())return;
        const unsigned domain=v->second.kind;
        using P=NorthlightMaterialProfile;profileTag=domain*4+modes[domain-1];profilePrimitives=geometry?geometry->draw.primitives:0;
        P::Scope timing(profile,P::Admission,profileTag);if(profile)profile->count(P::Candidates);
        if(!active())return;
        float nearestDepth=0;
        if((domain==1&&!terrain)||(domain==2&&!walls)||(domain==3&&!grass)||(domain==4&&!modes[3]))return;
        DWORD colorWrite=0,depthWrite=0;
        if(FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&depthWrite)))return;
        if(!(colorWrite&7)||(!depthWrite&&domain!=1)){++invisibleRejected;return;}
        if(!ensureVertex(original,v->second))return;
        const bool geometryMode=modes[domain-1]==3;bool skipGeometry=false;
        if(geometry&&(!geometryMode||!displacement.skinned(original))){float nearest=0;
            if(displacement.nearestMaterialDepth(original,*geometry,nearest,geometryMode)){++rangeTests;
                nearestDepth=nearest;
                if(nearest>=pomControl[1]){pomInRange=false;++pomRangeRejected;}
                skipGeometry=geometryMode&&nearest>displacement.geometryRange(domain);
                const float needed=std::max(captureRange(domain),geometryMode?displacement.geometryRange(domain):0.f);
                if(needed>0&&nearest>needed){++rangeRejected;++earlyRangeRejected;if(profile)profile->count(P::RangeRejected);return;}}
        }
        const bool needsCapture=captureNeeded(domain);
        if(!needsCapture&&modes[domain-1]!=3){++noCaptureRequired;if(profile)profile->count(P::NoContribution);return;}
        timing.next(P::CaptureSetup);
        IDirect3DPixelShader9* native=nullptr;if(FAILED(d->GetPixelShader(&native))||!native)return;
        auto p=pixels.find(native);if(p==pixels.end()){drop(native);return;}
        const unsigned material=p->second;
        if((domain==1&&material>2)||(domain==2&&material!=3&&material!=5&&material!=6)||(domain==3&&material!=4)){drop(native);return;}
        ReplayState saved(d,original,D3DVIEWPORT9{},profile);saved.pixel=native;NorthlightSharedPOM::Binding sharedBinding(d);
        NativeCapture* model=nullptr;
        DWORD z=0,zfunc=0,ref=0,alphaFunc=0,stencil=0;
        auto rs=[&](D3DRENDERSTATETYPE key,DWORD& value){return SUCCEEDED(d->GetRenderState(key,&value));};
        if(!rs(D3DRS_ZENABLE,z)||!rs(D3DRS_ZFUNC,zfunc)||!rs(D3DRS_ALPHAREF,ref)||
           !rs(D3DRS_ALPHAFUNC,alphaFunc)||!rs(D3DRS_STENCILENABLE,stencil)||!z||zfunc==D3DCMP_ALWAYS||zfunc==D3DCMP_NEVER||stencil)return;
        D3DVIEWPORT9 viewport{};if(FAILED(d->GetViewport(&viewport))||viewport.X||viewport.Y||viewport.Width<8||viewport.Height<8)return;
        for(unsigned i=1;i<4;++i){IDirect3DSurface9* extra=nullptr;d->GetRenderTarget(i,&extra);if(extra){drop(extra);return;}}
        if(FAILED(d->GetDepthStencilSurface(&saved.depth))||!saved.depth)return;
        saved.states[0].value=depthWrite;saved.states[4].value=colorWrite;
        for(unsigned i=0;i<saved.stateCount;++i){if(i==0||i==4)continue;auto& state=saved.states[i];if(FAILED(d->GetRenderState(state.key,&state.value)))return;}
        const auto& s=saved.states;
        const bool alpha=s[1].value!=0;
        const bool coverageAlpha=domain==4?alpha:alpha||s[5].value!=0;
        // These capture programs produce alpha exactly one. Their normal,
        // occlusion and UV outputs overwrite the destination without blending.
        if((domain==1&&material==1)||(domain==2&&!coverageAlpha)||(domain==4&&!alpha))saved.states[5].replay=FALSE;
        if(!(s[4].value&7)||(!s[0].value&&domain!=1)||
           (s[5].value&&(s[7].value!=D3DBLEND_SRCALPHA||s[8].value!=D3DBLEND_INVSRCALPHA))||
           (alpha&&alphaFunc!=D3DCMP_GREATER&&alphaFunc!=D3DCMP_GREATEREQUAL))return;
        if(FAILED(d->GetRenderTarget(0,&saved.target))||!saved.target)return;
        D3DSURFACE_DESC desc{};if(FAILED(saved.target->GetDesc(&desc)))return;
        if(desc.Width!=viewport.Width||desc.Height!=viewport.Height){++rejected;return;}
        if(width!=desc.Width||heightPixels!=desc.Height){reset();width=desc.Width;heightPixels=desc.Height;}
        // A distant proof needs no capture constants, samplers or private depth.
        // Resolve the target size first: reset() would discard prepared geometry.
        if(domain<=4&&geometryMode&&geometry&&!skipGeometry){displacement.prepare(original,domain,material,*geometry);
            nearestDepth=displacement.nearestPreparedDepth();
            if(needsCapture&&displacement.nearestPreparedDepth()>captureRange(domain)){++rangeTests;++rangeRejected;if(profile)profile->count(P::RangeRejected);return;}}
        if(!needsCapture){++noCaptureRequired;if(profile)profile->count(P::NoContribution);return;}
        const bool occlusionInRange=nearestDepth<occlusionDistance();
        const bool needsOcclusion=occlusionInRange&&(occlusionControl[2]>0||
            (modes[domain-1]==2&&pomControl[0]>0&&pomInRange&&occlusionControl[0]>0&&occlusionLight[3]>0));
        if(!occlusionInRange&&profile)profile->count(P::OcclusionFarSkipped);
        const DWORD* codes[]={kBumpOpaqueShader,kBumpLayerShader,kBumpWallShader,kBumpGrassShader,kBumpWallOpaqueShader,kBumpCompositeShader};
        const DWORD* sharedCodes[]={kBumpSharedOpaqueShader,kBumpSharedLayerShader,kBumpSharedWallShader,kBumpSharedGrassShader,kBumpSharedWallOpaqueShader,kBumpSharedCompositeShader};
        bool sharing=false;
        if(geometry&&modes[domain-1]==2&&pomInRange&&pomControl[0]>0&&sharedPOM.canTry()&&!(domain==4&&material==6)){
            P::Scope shared(profile,P::SharedPOM);sharing=parallax.supportsShared(native,material)&&sharedVertex(v->second)&&
                sharedPOM.prepare(geometry->draw,geometry->indices,geometry->format,geometry->vertices,geometry->stride,width,heightPixels,material==6);}
        if(sharing&&domain!=4&&!sharedShaders[material-1]&&FAILED(createCaptureShader(sharedCodes[material-1],&sharedShaders[material-1])))sharing=false;
        if(sharing&&!sharedBinding.bind(sharedPOM,false))sharing=false;
        if(domain==4){model=modelCapture(native,alpha,sharing);if(!model&&sharing){sharedBinding.restore();sharing=false;model=modelCapture(native,alpha);}if(!model)return;saved.constantBase=model->constant;saved.normalStage=14;}
        if(sharing){saved.constantCount=6;saved.stateCount=16;for(unsigned i=14;i<16;++i)if(FAILED(d->GetRenderState(saved.states[i].key,&saved.states[i].value)))return;}
        const bool multisampled=desc.MultiSampleType!=D3DMULTISAMPLE_NONE;
        // An opaque native wall does not consume diffuse/vertex alpha. Use
        // the existing map-only capture, avoiding a live diffuse dependency.
        const bool opaqueWall=domain==2&&material==3&&!coverageAlpha&&!sharing;
        const bool independent=(domain==4&&model&&model->deferredSafe&&(!model->samplers||textureWriteGuarded))||(domain==1&&material==1)||(domain==2&&material==5)||opaqueWall;
        const bool canDefer=deferredAllowed&&!memoryPressure&&multisampled&&!sharing&&!displacement.prepared()&&geometry&&independent&&v->second.deferredSafe;
        if(deferredAllowed&&multisampled){if(displacement.prepared())++deferredGeometry;else if(sharing||!independent||!v->second.deferredSafe)++deferredUnsafe;}
        saved.viewport=viewport;if(!canDefer&&!saved.save(userMemory&&!sharing,material==6&&domain!=4))return;
        if(multisampled){
            if(!captureDepth&&!check(d->CreateDepthStencilSurface(width,heightPixels,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&captureDepth,nullptr),"material depth"))return;
            saved.states[0].replay=saved.states[0].value;
        }else drop(saved.depth);
        if(!height){
            const std::uint64_t bytes=std::uint64_t(width)*heightPixels*8;
            MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
            if(bytes>(64ull<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<bytes+(128ull<<20)){++rejected;return;}
            if(!check(d->CreateTexture(width,heightPixels,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&height,nullptr),"height texture"))return;
        }
        if(!target&&!check(height->GetSurfaceLevel(0,&target),"height surface"))return;
        if(needsOcclusion&&!occlusionMap&&!occlusionUnavailable&&!occlusionRetry){
            D3DCAPS9 caps{};MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;const auto bytes=std::uint64_t(width)*heightPixels*4;
            const bool supported=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=2&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
            bool available=supported&&GlobalMemoryStatusEx(&memory)&&memory.ullAvailVirtual>bytes+(128ull<<20);
            if(available)available=SUCCEEDED(d->CreateTexture(width,heightPixels,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&occlusionMap,nullptr))&&SUCCEEDED(occlusionMap->GetSurfaceLevel(0,&occlusionTarget));
            if(!available){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=!supported;occlusionRetry=supported?60:0;logf("MATERIAL_OCCLUSION allocation retry=%u unsupported=%u; settings retained",occlusionRetry,unsigned(!supported));}
        }
        const unsigned captureIndex=model?0:opaqueWall?4:material-1;auto& captureShader=sharing?sharedShaders[captureIndex]:shaders[captureIndex];
        if(!model&&!captureShader&&!check(createCaptureShader(sharing?sharedCodes[captureIndex]:codes[captureIndex],&captureShader),"capture shader"))return;
        IDirect3DBaseTexture9* diffuse=nullptr;if(FAILED(d->GetTexture(0,&diffuse)))return;
        auto normal=normals.acquire(diffuse,domain==4);drop(diffuse);if(!normal)return;
        NorthlightAutoNormalCache::Lease normal1;
        if(material==6&&domain!=4){IDirect3DBaseTexture9* second=nullptr;if(FAILED(d->GetTexture(1,&second)))return;normal1=normals.acquire(second);drop(second);if(!normal1)return;}
        DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;d->GetSamplerState(0,D3DSAMP_ADDRESSU,&addressU);d->GetSamplerState(0,D3DSAMP_ADDRESSV,&addressV);
        if(domain==2&&!coverageAlpha)++opaqueCoverage;
        const float constants[]={domain==1?.18f:domain==2?.14f:.06f,captureRange(domain),alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:.001f,domain==4?(alpha?1.f:0.f):(coverageAlpha?0.f:1.f)};
        float pom[4];std::copy(pomControl,pomControl+4,pom);if(modes[domain-1]!=2||!parallax.supports(saved.pixel,material))pom[0]=0;
        float occult[4];std::copy(occlusionControl,occlusionControl+4,occult);occult[3]=needsOcclusion&&occlusionTarget?occlusionDistance():0.f;if(pom[0]<=0||occlusionLight[3]<=0)occult[0]=0;
        float bank[24];for(unsigned i=0;i<4;++i){bank[i]=constants[i];bank[4+i]=projection[i];bank[8+i]=pom[i];bank[12+i]=occult[i];bank[16+i]=occlusionLight[i];bank[20+i]=sharedPOM.control[i];}
        if(canDefer){
            const bool auxiliary=needsOcclusion&&occlusionTarget;
            NativeCapture world;world.shader=captureShader;world.constant=world.first=20;world.last=24;
            const auto limits=batchLimit;
            if(recordCapture(*geometry,normal,auxiliary,saved,model?*model:world,v->second.patched,bank,addressU,addressV)){drawn=true;return;}++deferredFallback;
            if(batchLimit!=limits&&!deferred.empty()){
                if(!finishCaptures())return;
                if(recordCapture(*geometry,normal,auxiliary,saved,model?*model:world,v->second.patched,bank,addressU,addressV)){--deferredFallback;drawn=true;return;}
            }
        }
        if(!finishCaptures())return;
        if(canDefer&&!saved.save(userMemory&&!sharing,material==6&&domain!=4))return;
        bool cacheBound=false;
        {P::Scope setup(profile,P::CaptureSetup,0,true);
        saved.apply();d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetTexture(saved.normalStage,normal.get());
        const DWORD normalSampler[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,addressU,addressV,FALSE,0,0};
        for(unsigned i=0;i<8;++i)if(saved.samplerValues[i]!=normalSampler[i]){saved.samplerChanged|=1u<<i;d->SetSamplerState(saved.normalStage,saved.samplerKeys[i],normalSampler[i]);}
        if(normal1){d->SetTexture(3,normal1.get());for(unsigned i=0;i<8;++i)if(saved.samplerValues3[i]!=normalSampler[i]){saved.samplerChanged3|=1u<<i;d->SetSamplerState(3,saved.samplerKeys[i],normalSampler[i]);}}
        if(multisampled)d->SetDepthStencilSurface(nullptr);
        if(!check(d->SetRenderTarget(0,target),"capture target"))return;
        if(multisampled&&!check(d->SetDepthStencilSurface(captureDepth),"material depth bind"))return;
        if(needsOcclusion&&occlusionTarget){
            // Clear only on the first actual near use; never erase normals already captured.
            if(!occlusionCleared){
                if(!check(d->SetRenderTarget(0,occlusionTarget),"occlusion clear target")||!check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0),"occlusion clear")||!check(d->SetRenderTarget(0,target),"normal target restore"))return;
                occlusionCleared=true;
            }
            saved.auxiliary=true;if(FAILED(d->SetRenderTarget(1,occlusionTarget))){drop(occlusionTarget);drop(occlusionMap);occlusionUnavailable=true;d->SetRenderTarget(1,nullptr);}}
        if(!cleared){if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|(multisampled?D3DCLEAR_ZBUFFER:0),0,1,0),"height clear"))return;cleared=true;}
        if(sharing){saved.sharedTargets=true;cacheBound=SUCCEEDED(d->SetRenderTarget(2,sharedPOM.target(0)))&&SUCCEEDED(d->SetRenderTarget(3,material==6?sharedPOM.target(1):nullptr));if(!cacheBound){d->SetRenderTarget(2,nullptr);d->SetRenderTarget(3,nullptr);}}
        bank[15]=saved.auxiliary?occlusionDistance():0.f;
        if(!check(d->SetScissorRect(&saved.scissor),"capture scissor")||!check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,saved.scissorEnabled),"capture scissor enable"))return;
        if(!check(d->SetVertexShader(sharing?v->second.shared:v->second.patched),"capture VS")||!check(d->SetPixelShader(model?model->shader:captureShader),"capture PS")||
           !check(d->SetPixelShaderConstantF(saved.constantBase,bank,saved.constantCount),"capture constants"))return;
        }
        P::Scope replay(profile,P::CaptureDraw,0,true);replay.work(displacement.prepared()?displacement.preparedPrimitives():profilePrimitives);
        if(check(sharing?sharedPOM.drawBound(false):displacement.draw(draw),"material replay")){drawn=true;++draws[domain-1];if(saved.auxiliary){occlusionDrawn=true;if(profile)profile->count(P::OcclusionDraw);}if(cacheBound){sharedMaps[0]=std::move(normal);sharedMaps[1]=std::move(normal1);for(unsigned i=0;i<2;++i){sharedPOM.maps[i]=sharedMaps[i].get();sharedPOM.addressU[i]=addressU;sharedPOM.addressV[i]=addressV;}sharedPOM.producedDraw();}}
    }
};
