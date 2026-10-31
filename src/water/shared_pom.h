#pragma once
#include "draw_snapshot.h"
#include "vertex_declaration_cache.h"
#include "water_shader_patch.h"

// A cache hit identifies the draw AND its triangle, not just the screen pixel.
// Uncovered MSAA samples and overlapping primitives keep the original raymarch.
class NorthlightSharedPOM {
    friend struct MaterialCaptureStateTestAccess;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    struct Entry {
        std::shared_ptr<const Mesh> source;IDirect3DVertexDeclaration9* sourceDecl=nullptr,*decl=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;UINT stride=0,primitives=0;size_t bytes=0;uint64_t used=0,frame=0;
        ~Entry(){drop(sourceDecl);drop(decl);drop(vb);}
    };
    IDirect3DDevice9* d;
    NorthlightDrawSnapshot::Frame snapshots{512u<<10,0};NorthlightVertexDeclarations::Cache declarations;
    std::vector<std::unique_ptr<Entry>> entries;Entry* current=nullptr;
    std::unordered_map<const Mesh*,Entry*> lookup;bool indexed=true;
    IDirect3DTexture9* textures[2]={};IDirect3DSurface9* targets[2]={};
    UINT width=0,height=0,nextTag=1;size_t resident=0,uploadBytes=0;unsigned builds=0,retry=0;
    uint64_t clock=0,frame=0,produced=0,consumed=0,tagClears=0;bool enabled=false,pressure=false,ready=false,up=false,indexedUP=false;
    bool clearTags(const D3DRECT* rect=nullptr){
        // Only target bindings, viewport and scissor change here. In particular,
        // do not round-trip the shader constant banks through a full state block.
        struct Targets {
            IDirect3DDevice9* d;NorthlightDrawSnapshot::Ref<IDirect3DSurface9> rt[4],depth;D3DVIEWPORT9 viewport{};DWORD scissor=0;bool changed=false;
            bool save(){for(unsigned i=0;i<4;++i){const auto hr=d->GetRenderTarget(i,rt[i].out());if(FAILED(hr)&&hr!=D3DERR_NOTFOUND)return false;}
                return rt[0].p&&SUCCEEDED(d->GetDepthStencilSurface(depth.out()))&&SUCCEEDED(d->GetViewport(&viewport))&&SUCCEEDED(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor));}
            ~Targets(){if(!changed)return;d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);d->SetRenderTarget(0,rt[0].p);
                for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,rt[i].p);d->SetDepthStencilSurface(depth.p);d->SetViewport(&viewport);d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);}
        } saved{d};if(!saved.save())return false;saved.changed=true;
        if(FAILED(d->SetDepthStencilSurface(nullptr)))return false;
        for(unsigned i=1;i<4;++i)if(FAILED(d->SetRenderTarget(i,nullptr)))return false;
        if(FAILED(d->SetRenderTarget(0,targets[0]))||FAILED(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE))||FAILED(d->Clear(rect?1:0,rect,D3DCLEAR_TARGET,0,1,0)))return false;
        if(!rect)++tagClears;return true;
    }
    bool allocate(UINT w,UINT h,bool dual){
        if(pressure||retry)return false;
        if(width!=w||height!=h){releaseTargets();width=w;height=h;}
        const size_t cost=size_t(w)*h*(textures[0]?(dual&&!textures[1]?8:0):dual?24:16);
        if(!cost)return true;
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        if(size_t(w)*h*(dual?24:16)>(192u<<20)||!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<cost+(256u<<20))return false;
        D3DCAPS9 caps{};D3DDEVICE_CREATION_PARAMETERS cp{};NorthlightDrawSnapshot::Ref<IDirect3D9> api;D3DDISPLAYMODE mode{};
        if(FAILED(d->GetDeviceCaps(&caps))||caps.NumSimultaneousRTs<4||!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)||
           !(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING)||FAILED(d->GetCreationParameters(&cp))||FAILED(d->GetDirect3D(api.out()))||!api.p||FAILED(api->GetAdapterDisplayMode(cp.AdapterOrdinal,&mode))){retry=60;return false;}
        for(unsigned i=0;i<(dual?2u:1u);++i){if(textures[i])continue;
            const auto format=i?D3DFMT_G32R32F:D3DFMT_A32B32G32R32F;
            if(FAILED(api->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,mode.Format,D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_TEXTURE,format))||
               FAILED(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&textures[i],nullptr))||FAILED(textures[i]->GetSurfaceLevel(0,&targets[i]))){releaseTargets();retry=60;return false;}
            if(!i){nextTag=1;if(!clearTags()){releaseTargets();retry=60;return false;}}
        }return true;
    }
    void releaseTargets(){for(auto*& t:targets)drop(t);for(auto*& t:textures)drop(t);ready=false;width=height=0;}
    static bool equal(const Mesh& a,const Mesh& b){
        if(a.vertexCount!=b.vertexCount||a.primitiveCount!=b.primitiveCount||a.topology!=b.topology||a.indexed!=b.indexed||a.indices!=b.indices)return false;
        for(unsigned i=0;i<4;++i)if(a.streams[i].stride!=b.streams[i].stride||a.streams[i].bytes!=b.streams[i].bytes)return false;return true;
    }
public:
    float control[4]={};IDirect3DTexture9* maps[2]={};DWORD addressU[2]={},addressV[2]={};
    explicit NorthlightSharedPOM(IDirect3DDevice9* device):d(device){
        // Full-pass 4K timings currently favor the original path. Keep reuse an
        // explicit profiling opt-in until its extra MRT bandwidth pays for itself.
        char value[2]={};enabled=GetEnvironmentVariableA("NORTHLIGHT_EXPERIMENTAL_SHARED_POM",value,2)==1&&value[0]=='1';
        snapshots.setSnapshotCacheLimit(8u<<20);snapshots.setChargeResidentHits(false);
        snapshots.setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});
        snapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});
    }
    ~NorthlightSharedPOM(){reset();}
    bool canTry()const{return enabled&&!pressure&&!retry;}
    static IDirect3DVertexShader9* vertexShader(IDirect3DDevice9* d,IDirect3DVertexShader9* source){
        try {
        UINT bytes=0;if(!source||FAILED(source->GetFunction(nullptr,&bytes))||!bytes||bytes>65536||bytes%4)return nullptr;
        std::vector<DWORD> code(bytes/4);std::vector<uint32_t> patched;IDirect3DVertexShader9* result=nullptr;
        if(SUCCEEDED(source->GetFunction(code.data(),&bytes))&&vertex(code.data(),code.size(),patched))d->CreateVertexShader(reinterpret_cast<DWORD*>(patched.data()),&result);return result;
        }catch(...){return nullptr;}
    }
    static bool vertex(const DWORD* code,size_t count,std::vector<uint32_t>& out){
        using namespace NorthlightWaterShaderPatch;
        if(!code||count<2||code[0]!=0xfffe0300u||code[count-1]!=65535)return false;
        bool inputs[16]={},outputs[12]={};size_t declEnd=1;
        for(size_t at=1;at+1<count;){unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;if(n>=count-at)return false;
            if(op==31){if(n!=2||((code[at+1]&15)==5&&((code[at+1]>>16)&15)==6))return false;declEnd=at+3;}
            if(op!=65534)for(unsigned i=op==31?2:1;i<=n;++i){if((op==81||op==48||op==47)&&i>1)break;auto t=code[at+i];auto type=regType(t),index=regIndex(t);if(type==1){if(index>=16)return false;inputs[index]=true;}if(type==6){if(index>=12)return false;outputs[index]=true;}}
            at+=n+1;
        }
        unsigned in=0,varying=0;while(in<16&&inputs[in])++in;while(varying<12&&outputs[varying])++varying;if(in==16||varying==12)return false;
        out.assign(code,code+count-1);out.insert(out.begin()+declEnd,{0x0200001f,0x80060005,dst(1,in,1),0x0200001f,0x80060005,dst(6,varying,1)});
        out.insert(out.end(),{0x02000001,dst(6,varying,1),src(1,in,0),65535});return true;
    }
    static bool expand(const Mesh& source,const D3DVERTEXELEMENT9* elements,UINT count,std::vector<uint8_t>& bytes,std::vector<D3DVERTEXELEMENT9>& layout,UINT& stride){
        if(count<2||count>=MAXD3DDECLLENGTH||!source.primitiveCount||source.primitiveCount>32768||(source.topology!=D3DPT_TRIANGLELIST&&source.topology!=D3DPT_TRIANGLESTRIP))return false;
        UINT offsets[4]={};stride=0;for(unsigned i=0;i<4;++i){offsets[i]=stride;if(source.streams[i].stride>240-stride)return false;stride+=source.streams[i].stride;}
        if(!stride||stride%4)return false;
        layout.assign(elements,elements+count-1);for(auto& e:layout){if(e.Stream>=4||(e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==6))return false;e.Offset+=WORD(offsets[e.Stream]);e.Stream=0;}
        layout.push_back({0,WORD(stride),D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,6});layout.push_back(D3DDECL_END());
        stride+=4;bytes.resize(size_t(source.primitiveCount)*3*stride);
        for(unsigned p=0;p<source.primitiveCount;++p)for(unsigned corner=0;corner<3;++corner){
            unsigned c=corner;if(source.topology==D3DPT_TRIANGLESTRIP&&(p&1)&&c<2)c=1-c;
            size_t at=source.topology==D3DPT_TRIANGLELIST?size_t(p)*3+c:p+c;
            if(source.indexed&&at>=source.indices.size())return false;const size_t v=source.indexed?source.indices[at]:at;if(v>=source.vertexCount)return false;
            auto* dst=bytes.data()+(size_t(p)*3+corner)*stride;
            for(unsigned s=0;s<4;++s){const auto& stream=source.streams[s];if(!stream.stride)continue;if((v+1)*stream.stride>stream.bytes.size())return false;std::memcpy(dst+offsets[s],stream.bytes.data()+v*stream.stride,stream.stride);}
            const float id=float(p);std::memcpy(dst+stride-4,&id,4);
        }
        return true;
    }
    void tracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);}
    void beginDraw(){current=nullptr;ready=false;maps[0]=maps[1]=nullptr;}
    void reset(){beginDraw();releaseTargets();lookup.clear();indexed=true;entries.clear();resident=0;snapshots.clearIndexCache();declarations.clear();retry=0;}
    void setMemoryPressure(bool on){pressure=on;if(on)reset();}
    void endFrame(){beginDraw();++frame;builds=0;uploadBytes=0;if(retry)--retry;snapshots.clearFrame();}
    void report()const{logf("POM_SHARED produced=%llu consumed=%llu cached=%zu geometryBytes=%zu uvBytes=%llu",(unsigned long long)produced,(unsigned long long)consumed,entries.size(),resident,(unsigned long long)width*height*(textures[0]?16:0)+(unsigned long long)width*height*(textures[1]?8:0));}
    bool prepare(const NorthlightDrawSnapshot::Draw& draw,const void* indices,D3DFORMAT format,const void* vertices,UINT userStride,UINT w,UINT h,bool dual){
        beginDraw();if(!canTry())return false;
        try {
        DWORD shade=0;if(FAILED(d->GetRenderState(D3DRS_SHADEMODE,&shade))||shade!=D3DSHADE_GOURAUD)return false;
        for(unsigned i=0;i<8;++i){DWORD wrap=0;if(FAILED(d->GetRenderState(D3DRENDERSTATETYPE(D3DRS_WRAP0+i),&wrap))||wrap)return false;}
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(!declarations.get(decl.p,elements,count))return false;
        for(UINT i=0;i<count;++i)if(elements[i].Usage==D3DDECLUSAGE_TEXCOORD&&elements[i].UsageIndex==6)return false;
        if(!allocate(w,h,dual))return false;
        Mesh local;std::shared_ptr<const Mesh> source;
        if(vertices){if(!snapshots.readUP(decl.p,draw,indices,format,vertices,userStride,local))return false;}
        else if(!snapshots.read(d,decl.p,draw,local,nullptr,false,&source))return false;
        if(!source)source=std::make_shared<Mesh>(std::move(local));
        if(!vertices&&indexed){auto hit=lookup.find(source.get());if(hit!=lookup.end()&&hit->second->sourceDecl==decl.p)current=hit->second;}
        else for(auto& e:entries)if(e->sourceDecl==decl.p&&(e->source==source||(vertices&&equal(*e->source,*source)))){current=e.get();break;}
        if(!current){
            if(builds>=2||!source->primitiveCount||source->primitiveCount>32768)return false;
            size_t packedStride=4;for(const auto& s:source->streams)packedStride+=s.stride;if(packedStride>244)return false;
            const size_t dataCost=size_t(source->primitiveCount)*3*packedStride,cost=source->byteSize()+dataCost;if(cost>(24u<<20)||dataCost>(2u<<20)-uploadBytes)return false;
            while(!entries.empty()&&(resident+cost>(24u<<20)||entries.size()>=256)){auto old=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a->used<b->used;});
                if(frame-(*old)->frame<=1)return false; // Keep a working set that exceeds the optional cache on its original POM path.
                if(indexed)lookup.erase((*old)->source.get());resident-=(*old)->bytes;entries.erase(old);}
            std::vector<uint8_t> data;std::vector<D3DVERTEXELEMENT9> layout;UINT stride=0;if(!expand(*source,elements,count,data,layout,stride))return false;
            auto e=std::make_unique<Entry>();void* dataOut=nullptr;
            if(FAILED(d->CreateVertexBuffer(UINT(data.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&e->vb,nullptr))||FAILED(e->vb->Lock(0,UINT(data.size()),&dataOut,NorthlightUpload::FreshBufferLock)))return false;
            std::memcpy(dataOut,data.data(),data.size());if(FAILED(e->vb->Unlock())||FAILED(d->CreateVertexDeclaration(layout.data(),&e->decl)))return false;
            e->source=source;e->sourceDecl=decl.p;decl->AddRef();e->stride=stride;e->primitives=source->primitiveCount;e->bytes=cost;current=e.get();entries.push_back(std::move(e));resident+=cost;uploadBytes+=data.size();++builds;
            if(indexed)try{lookup.emplace(source.get(),current);}catch(...){indexed=false;lookup.clear();}
        }
        if(nextTag>0xffffff-current->primitives){if(!clearTags()){current=nullptr;return false;}nextTag=1;}
        control[0]=1.f/w;control[1]=1.f/h;control[2]=float(nextTag);nextTag+=current->primitives;control[3]=0;
        current->used=++clock;current->frame=frame;up=vertices!=nullptr;indexedUP=up&&draw.indexed;return true;
        }catch(...){current=nullptr;return false;}
    }
    IDirect3DSurface9* target(unsigned n)const{return targets[n];}
    IDirect3DTexture9* texture(unsigned n)const{return textures[n];}
    void producedDraw(){ready=true;++produced;}
    bool available()const{return ready&&current;}
    uint64_t clearCount()const{return tagClears;}
    uint64_t producedCount()const{return produced;}
    uint64_t nativeCount()const{return consumed;}
    HRESULT drawBound(bool native){if(native)++consumed;return d->DrawPrimitive(D3DPT_TRIANGLELIST,0,current->primitives);}
    class Binding {
        IDirect3DDevice9* d;NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> stream;NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> indices;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;UINT offset=0,stride=0;bool changed=false,native=false,up=false,indexedUP=false;
    public:
        explicit Binding(IDirect3DDevice9* device):d(device){}
        bool bind(NorthlightSharedPOM& owner,bool nativeDraw){
            if(!owner.current||FAILED(d->GetStreamSource(0,stream.out(),&offset,&stride))||FAILED(d->GetIndices(indices.out()))||FAILED(d->GetVertexDeclaration(decl.out())))return false;
            changed=true;if(FAILED(d->SetStreamSource(0,owner.current->vb,0,owner.current->stride))||FAILED(d->SetVertexDeclaration(owner.current->decl))){restore();return false;}
            native=nativeDraw;up=owner.up;indexedUP=owner.indexedUP;return true;
        }
        void restore(){if(!changed)return;d->SetStreamSource(0,stream.p,offset,stride);d->SetIndices(indices.p);d->SetVertexDeclaration(decl.p);changed=false;
            if(native&&up){d->SetStreamSource(0,nullptr,0,0);if(indexedUP)d->SetIndices(nullptr);}}
        ~Binding(){restore();}
    };
};
