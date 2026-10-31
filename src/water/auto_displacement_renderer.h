#pragma once
#include "material_profile.h"
#include "render_thread_probe.h"
#include "draw_snapshot.h"
#include "displacement_geometry.h"
#include "displacement_shader_patch.h"
#include "actor_texture.h"
#include "material_height.h"
#include "replay_bounds.h"
#include "replay_bounds_metadata.h"
#include "upload_lock.h"
#include "captured_constant_epoch.h"
#include "vertex_declaration_cache.h"
#include "intrusive_lru.h"
#include "position_bounds.h"
#include <memory>
#include <map>
#include <cfloat>

class NorthlightAutoDisplacement {
    NorthlightMaterialProfile* profile=nullptr;
    friend struct DisplacementStateTestAccess;
    using Program=NorthlightActorDeformation::Program;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    using Height=NorthlightDisplacementGeometry::Height;
    struct Shader {IDirect3DVertexShader9* patched=nullptr,*source=nullptr,*gpu=nullptr;NorthlightDisplacementPatch::Binding binding,gpuBinding;bool pending=false,skinned=false;uint64_t hash=0;};
    struct Material {Program uv,view,uv1,blend;std::vector<unsigned> uvRegisters[2];bool layered=false,skinned=false,clearPoseInputs=true;unsigned rangeRegister=17;uint64_t id=0;unsigned first=0,count=0,influences=0;};
    struct Bounds {std::weak_ptr<const Mesh> source;float low[3],high[3];uint64_t used=0;size_t bytes=0;std::unique_ptr<NorthlightReplayBounds::SkinEnvelope> skin;
        const Mesh* key=nullptr;NorthlightIntrusiveLRU::Links<Bounds> lru;};
    struct Texture {IDirect3DTexture9* source=nullptr,*gpu=nullptr;Height height;uint64_t used=0,version=0;bool model=false;};
    struct Entry {
        std::shared_ptr<const Mesh> source;IDirect3DTexture9* texture=nullptr,*texture1=nullptr,*height=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr,*sourceDecl=nullptr;
        std::vector<float> uvConstants;unsigned vertices=0,primitives=0,stride=0,detail=0,address=0,domain=0,material=0;size_t bytes=0;uint64_t used=0,program=0,baseFrame=0,heightVersion=0,heightVersion1=0,order=0,fingerprint=0,materialFingerprint=0;
        ~Entry(){drop(texture);drop(texture1);drop(height);drop(vb);drop(ib);drop(decl);drop(sourceDecl);}
    };
    IDirect3DDevice9* d;
    NorthlightDrawSnapshot::Frame snapshots{2u<<20,0};
    NorthlightDrawSnapshot::Frame rangeSnapshots{512u<<10,0},skinRangeSnapshots{256u<<10,0};
    NorthlightPositionBounds::Cache positionBounds;
    unsigned capturedBoundsHits=0;
    NorthlightVertexDeclarations::Cache declarations;
    void* constantContext=nullptr;NorthlightConstantEpoch::Reader constantReader=nullptr;
    NorthlightConstantEpoch::Stamp constantStamp,rangeConstantStamp;float constants[256*4]={};
    float rangeConstants[256*4]={}; // Range admission must not invalidate the cached model pose bank.
    bool readConstantBank(unsigned first,unsigned count,bool skinned,float* bank,NorthlightConstantEpoch::Stamp& stamp){
        namespace E=NorthlightConstantEpoch;
        E::Stamp before;const bool valid=skinned&&constantReader&&constantReader(constantContext,before)&&before.valid;
        unsigned lo=first,hi=first+count;
        if(valid&&E::banks(stamp,before,false,false))E::dirtySpan(stamp,before,lo,hi,lo,hi);
        stamp={};
        if(!skinned)std::fill(bank,bank+256*4,0.f);
        if(lo!=hi&&FAILED(d->GetVertexShaderConstantF(lo,bank+lo*4,hi-lo)))return false;
        E::Stamp after;if(valid&&constantReader(constantContext,after)&&E::exact(before,after))stamp=after;
        return true;
    }
    bool readConstants(unsigned first,unsigned count,bool skinned){return readConstantBank(first,count,skinned,constants,constantStamp);}
    std::unordered_map<IDirect3DVertexShader9*,Shader> shaders;
    std::unordered_map<IDirect3DVertexShader9*,Material> materials;
    std::vector<Texture> textures;
    std::unordered_map<const Mesh*,Bounds> bounds;
    NorthlightIntrusiveLRU::List<Bounds> boundsLRU;
    NorthlightReplayBounds::EnvelopeCache poseEnvelopes;
    NorthlightReplayMetadata::Cache<NorthlightReplayBounds::Prepared,IDirect3DVertexShader9,IDirect3DVertexDeclaration9> poseMetadata;
    NorthlightReplayBounds::Budget envelopeBudget;
    NorthlightActorDeformation::PreparedBindingCache poseBindings;
    std::vector<std::unique_ptr<Entry>> entries;
    std::unordered_multimap<const Mesh*,Entry*> sourceLookup;
    std::multimap<uint64_t,Entry*> contentLookup;
    std::unordered_multimap<uint64_t,Entry*> materialLookup;
    bool indexedEntries=true;uint64_t entrySerial=0;
    std::vector<float> uvScratch;
    Entry* current=nullptr;float controls[5][4]={},lastDepthLower=0;
    struct ModelOverlay {std::unique_ptr<Entry> mesh;NorthlightDrawSnapshot::Draw draw;float bank[256*4]={},controls[5][4]={};bool bones[75]={};};
    std::vector<std::unique_ptr<ModelOverlay>> modelOverlays;size_t modelOverlayBytes=0;bool currentOverlay=false;uint64_t modelOverlayRecorded=0,modelOverlayMatched=0,modelOverlayDeferred=0,modelOverlayDrawn=0;
    NorthlightDrawSnapshot::Draw preparedDraw{};
    // Audited SM2/SM3 single-bone diffuse_t1 lit/unlit programs: clip c2..c5,
    // bone palette c31 onward. Their reflection passes use the same transform.
    static bool modelBaseShader(uint64_t hash){return hash==0x2e3dc5694f4a01afull||hash==0xff32338728131932ull||hash==0x47147097cf6c8081ull||hash==0x309724ed4598da94ull;}
    void clearModelOverlays(){modelOverlays.clear();modelOverlayBytes=0;currentOverlay=false;}
    void rememberModelOverlay(){
        if(!current||!current->source||!current->source->sourceIdentity||userDraw||!preparedDraw.indexed||modelOverlays.size()>=32||current->bytes>(16u<<20)-modelOverlayBytes)return;
        try{auto item=std::make_unique<ModelOverlay>();const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(!current->sourceDecl||!declarations.get(current->sourceDecl,elements,count))return;
        const auto* bone=std::find_if(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Stream==0&&e.Usage==D3DDECLUSAGE_BLENDINDICES&&e.UsageIndex==0;});if(bone==elements+count)return;
        const auto& stream=current->source->streams[0];const auto size=NorthlightDrawSnapshot::declarationBytes(bone->Type);
        for(unsigned i=0;i<current->source->vertexCount;++i){const size_t pos=size_t(i)*stream.stride+bone->Offset;NorthlightActorDeformation::Four value;
            if(!size||pos+size>stream.bytes.size()||!NorthlightActorDeformation::decodeElement(stream.bytes.data()+pos,bone->Type,value)||!std::isfinite(value[0])||value[0]<0||value[0]>74||value[0]!=std::floor(value[0]))return;item->bones[unsigned(value[0])]=true;}
        auto e=std::make_unique<Entry>();e->source=current->source;e->vb=current->vb;e->ib=current->ib;e->decl=current->decl;e->height=current->height;e->sourceDecl=current->sourceDecl;
        if(e->vb)e->vb->AddRef();if(e->ib)e->ib->AddRef();if(e->decl)e->decl->AddRef();if(e->height)e->height->AddRef();if(e->sourceDecl)e->sourceDecl->AddRef();
        e->vertices=current->vertices;e->primitives=current->primitives;e->stride=current->stride;e->address=current->address;e->domain=4;e->bytes=current->bytes;
        item->mesh=std::move(e);item->draw=preparedDraw;std::memcpy(item->bank,constants,sizeof constants);std::memcpy(item->controls,controls,sizeof controls);modelOverlays.push_back(std::move(item));modelOverlayBytes+=current->bytes;++modelOverlayRecorded;}catch(...){return;}
    }
    bool gpuHeightChecked=false,gpuHeightSupported=false;uint64_t gpuDraws=0;
    unsigned variantSources=0,variantCPU=0,variantGPU=0,variantReadFailed=0;size_t variantBytes=0;double variantMs=0;
    bool gpuHeights(){
        if(gpuHeightChecked)return gpuHeightSupported;gpuHeightChecked=true;
        D3DCAPS9 caps{};D3DDEVICE_CREATION_PARAMETERS cp{};NorthlightDrawSnapshot::Ref<IDirect3D9> api;D3DDISPLAYMODE mode{};
        const DWORD linear=D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MAGFLINEAR;
        return gpuHeightSupported=d&&SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&(caps.VertexTextureFilterCaps&linear)==linear&&
            SUCCEEDED(d->GetCreationParameters(&cp))&&SUCCEEDED(d->GetDirect3D(api.out()))&&api.p&&SUCCEEDED(api->GetAdapterDisplayMode(cp.AdapterOrdinal,&mode))&&
            SUCCEEDED(api->CheckDeviceFormat(cp.AdapterOrdinal,cp.DeviceType,mode.Format,D3DUSAGE_QUERY_VERTEXTEXTURE|D3DUSAGE_QUERY_FILTER,D3DRTYPE_TEXTURE,D3DFMT_R32F));
    }
    bool ensureVariant(Shader& shader){
        if(!shader.pending)return shader.patched!=nullptr;
        shader.pending=false;++variantSources;
        const auto start=std::chrono::steady_clock::now();
        struct Timer {double& total;std::chrono::steady_clock::time_point start;~Timer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{variantMs,start};
        UINT bytes=0;if(FAILED(shader.source->GetFunction(nullptr,&bytes))||bytes<8||bytes>65536*sizeof(DWORD)||bytes%sizeof(DWORD)){++variantReadFailed;return false;}
        std::vector<DWORD> words(bytes/sizeof(DWORD));if(FAILED(shader.source->GetFunction(words.data(),&bytes))||bytes!=words.size()*sizeof(DWORD)){++variantReadFailed;return false;}
        variantBytes+=bytes;std::vector<uint32_t> patched;
        if(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(words.data()),words.size(),patched,shader.binding,shader.skinned)){
            const HRESULT result=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader.patched);variantCPU+=SUCCEEDED(result)&&shader.patched;
            if(FAILED(result))logf("DISPLACEMENT vertex shader rejected: 0x%08lx model=%08lx",static_cast<unsigned long>(result),static_cast<unsigned long>(words[0]));}
        if(gpuHeights()&&NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(words.data()),words.size(),patched,shader.gpuBinding,shader.skinned,true)){
            const HRESULT result=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader.gpu);variantGPU+=SUCCEEDED(result)&&shader.gpu;
            if(FAILED(result))logf("DISPLACEMENT GPU height shader rejected: 0x%08lx",static_cast<unsigned long>(result));}
        return shader.patched!=nullptr;
    }
    float strength=.12f,range=12,worldStrength=.12f,worldRange=12,modelStrength=.03f,modelRange=8;unsigned worldDetail=4,modelDetail=2,polarity=0,heightDetails=75,heightBalance=50;unsigned detail=4,buildsThisFrame=0,readsThisFrame=0,poseVerticesThisFrame=0;
    size_t resident=0,boundsBytes=0,cacheBudget=64u<<20;uint64_t clock=0,materialClock=0,builds=0,hits=0,draws=0,triangles=0,refused=0;unsigned frames=0,domainDraws[4]={},stateRejected=0,heightRejected=0,shaderRejected=0,meshRejected=0,snapshotRejected=0;bool userDraw=false,indexedUserDraw=false;
    uint64_t snapshotAttempts=0,snapshotUP=0;std::array<uint64_t,size_t(NorthlightDrawSnapshot::Error::Unlock)+1> snapshotReasons{};
    uint64_t distanceRejected=0,poseBudgetRejected=0,poseEnvelopeHits=0,poseFallbackVertices=0,objectBoundsVertices=0,boundsHits=0,boundsMisses=0,primitiveRejected=0,sourceAliases=0;
    unsigned poseKernelVertices[4]={}; // Completed fallback vertices, by evaluator (not budget refusals).
    auto cachedBounds(const std::shared_ptr<const Mesh>& source){
        auto found=bounds.find(source.get());
        // Weak ownership avoids charging/retaining full geometry for a tiny bound.
        // Control-block identity still rejects expired snapshots and address reuse.
        if(found!=bounds.end()&&(found->second.source.owner_before(source)||source.owner_before(found->second.source))){boundsBytes-=found->second.bytes;boundsLRU.remove(found->second);bounds.erase(found);found=bounds.end();}
        if(found==bounds.end())++boundsMisses;else ++boundsHits;return found;
    }
    auto retainBounds(const std::shared_ptr<const Mesh>& source,Bounds&& value){
        value.source=source;value.key=source.get();value.used=++clock;
        value.bytes=sizeof(Bounds)+4*sizeof(void*)+(value.skin?sizeof(NorthlightReplayBounds::SkinEnvelope):0);
        // Keep exact oldest-use eviction without scanning the rotating working
        // set. Hash-table rehash preserves these non-owning list nodes.
        while(!bounds.empty()&&(boundsBytes+value.bytes>(8u<<20)||bounds.size()>=4096)){
            auto* old=boundsLRU.oldest();const auto* key=old->key;boundsBytes-=old->bytes;boundsLRU.remove(*old);bounds.erase(key);}
        auto added=bounds.emplace(source.get(),std::move(value));if(added.second){boundsBytes+=added.first->second.bytes;boundsLRU.append(added.first->second);}return added.first;
    }
    void touchBounds(Bounds& value){value.used=++clock;boundsLRU.touch(value);}
    NorthlightReplayBounds::Status poseEnvelope(const Program& program,const std::shared_ptr<const Mesh>& source,const D3DVERTEXELEMENT9* elements,UINT count,const float* bank,NorthlightReplayBounds::Bounds& result,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* declaration){
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        const auto prepared=poseMetadata.get(shader,declaration,[&]{
            const auto start=NorthlightReplayBounds::BoundsClock::now();auto value=NorthlightReplayBounds::EnvelopeCache::prepareProgram(program,elements,count);
            poseEnvelopes.chargeCheapPreparation(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(NorthlightReplayBounds::BoundsClock::now()-start).count()));return value;});
        return prepared?poseEnvelopes.calculatePrepared(*prepared,*source,source,bank,identity,envelopeBudget,result):poseEnvelopes.calculate(program,*source,source,elements,count,bank,identity,envelopeBudget,result);
    }
    bool distantPoseBounds(const Program& program,const std::shared_ptr<const Mesh>& source,const D3DVERTEXELEMENT9* elements,UINT count,float* low,float* high,IDirect3DVertexShader9* shader=nullptr,IDirect3DVertexDeclaration9* declaration=nullptr){
        NorthlightMaterialProfile::Scope timing(profile,NorthlightMaterialProfile::PoseEnvelope);
        NorthlightReplayBounds::Bounds result;
        if(poseEnvelope(program,source,elements,count,constants,result,shader,declaration)!=NorthlightReplayBounds::Status::Valid)return false;
        ++poseEnvelopeHits;
        const float origin[3]={};if(!NorthlightReplayBounds::outsideSphere(result,origin,range))return false;
        std::copy(result.low,result.low+3,low);std::copy(result.high,result.high+3,high);
        lastDepthLower=std::max(0.f,std::nextafter(low[2]>0?low[2]:high[2]<0?-high[2]:0,-INFINITY)-.001f);return true;
    }
    static unsigned rangeInput(const Program& program){
        unsigned input=16;for(const auto& i:program.inputs){if(i.usage!=D3DDECLUSAGE_POSITION||i.index||i.reg>=16)return 16;input=i.reg;}
        if(!NorthlightReplayBounds::detail::linear(program))return 16;
        for(const auto& op:program.operations)for(const auto& source:op.source)if(source.token&0x2000)return 16;
        return input;
    }
    static bool nearestViewDepth(const Program& program,unsigned input,const float* low,const float* high,const float* bank,float& nearest){
        if(input>=16)return false;
        using namespace NorthlightReplayBounds::detail;Vector view;
        auto fill=[&](Vector& position){for(unsigned k=0;k<3;++k){const float middle=float((double(low[k])+high[k])*.5);position[k]={middle,std::max(double(middle)-low[k],double(high[k])-middle)};}position[3]={1,0};};
        if(program.inputs.size()==1&&program.inputs[0].reg==input){Vector position={};fill(position);if(!evaluateIntervals<true>(program,&position,bank,view,false,input))return false;}
        else{Vector inputs[16]={};fill(inputs[input]);if(!evaluateIntervals(program,inputs,bank,view))return false;}
        const double lo=double(view[2].value)-view[2].error,hi=double(view[2].value)+view[2].error;
        if(!std::isfinite(lo)||!std::isfinite(hi))return false;
        nearest=std::max(0.f,std::nextafter(float(lo>0?lo:hi<0?-hi:0),-INFINITY)-.001f);return true;
    }
    static bool sameSource(const Entry& e,const std::shared_ptr<const Mesh>& source,bool equivalent){return e.source==source||(equivalent&&e.source->vertexCount==source->vertexCount&&e.source->primitiveCount==source->primitiveCount&&e.source->topology==source->topology&&e.source->indexed==source->indexed&&e.source->streams[0].bytes==source->streams[0].bytes&&e.source->indices==source->indices);}
    static uint64_t sourceFingerprint(const Mesh& source){
        uint64_t h=1469598103934665603ull;auto add=[&](uint64_t v){h^=v;h*=1099511628211ull;};
        add(source.vertexCount);add(source.primitiveCount);add(source.topology);add(source.indexed);
        // A bounded shortlist, never proof: full source bytes still authorize every alias.
        auto sample=[&](const void* data,size_t size){add(size);const auto* p=static_cast<const unsigned char*>(data);const size_t n=std::min(size,size_t(32));
            auto span=[&](size_t begin){size_t i=0;for(;i+sizeof(uint64_t)<=n;i+=sizeof(uint64_t)){uint64_t word;std::memcpy(&word,p+begin+i,sizeof word);add(word);}for(;i<n;++i)add(p[begin+i]);};span(0);span(size-n);};
        sample(source.streams[0].bytes.data(),source.streams[0].bytes.size());sample(source.indices.data(),source.indices.size()*sizeof(uint32_t));return h;
    }
    static uint64_t materialFingerprint(uint64_t source,uint64_t program,unsigned detail,unsigned address,unsigned domain,unsigned material,const void* texture,const void* texture1,uint64_t version,uint64_t version1,const std::vector<float>& uv){
        uint64_t h=source;auto add=[&](uint64_t v){h^=v;h*=1099511628211ull;};add(program);add(detail);add(address);add(domain);add(material);add(reinterpret_cast<uintptr_t>(texture));add(reinterpret_cast<uintptr_t>(texture1));add(version);add(version1);add(uv.size());for(float value:uv)add(std::hash<float>{}(value));return h;
    }
    template<class Visit> void candidates(const std::shared_ptr<const Mesh>& source,uint64_t fingerprint,bool equivalent,Visit visit){
        if(!indexedEntries){for(auto& e:entries)visit(*e);}
        else if(equivalent){const auto range=contentLookup.equal_range(fingerprint);for(auto it=range.first;it!=range.second;++it)visit(*it->second);}
        else{const auto range=sourceLookup.equal_range(source.get());for(auto it=range.first;it!=range.second;++it)visit(*it->second);}
    }
    template<class Match> Entry* firstCandidate(const std::shared_ptr<const Mesh>& source,uint64_t fingerprint,bool equivalent,Match match){
        // Equivalent keys keep insertion order, so UP canonicalization can stop at its original first match.
        if(indexedEntries&&equivalent){const auto range=contentLookup.equal_range(fingerprint);for(auto it=range.first;it!=range.second;++it)if(match(*it->second))return it->second;return nullptr;}
        if(!indexedEntries){for(auto& e:entries)if(match(*e))return e.get();return nullptr;}
        Entry* first=nullptr;candidates(source,fingerprint,equivalent,[&](Entry& e){if((!first||e.order<first->order)&&match(e))first=&e;});return first;
    }
    template<class Match> Entry* firstMaterial(uint64_t fingerprint,Match match){
        Entry* first=nullptr;auto visit=[&](Entry& e){if((!first||e.order<first->order)&&match(e))first=&e;};
        if(!indexedEntries){for(auto& e:entries)visit(*e);}else{const auto range=materialLookup.equal_range(fingerprint);for(auto it=range.first;it!=range.second;++it)visit(*it->second);}return first;
    }
    std::shared_ptr<const Mesh> geometrySource(const std::shared_ptr<const Mesh>& source,IDirect3DVertexDeclaration9* declaration,uint64_t program,unsigned domain){
        // A tracked buffer rewrite can preserve every vertex/index. Keep the
        // current snapshot's write identity intact; only reuse private geometry
        // after checking the full contents and layout again.
        if(!source->replayIdentity)return source;
        auto* hit=firstCandidate(source,sourceFingerprint(*source),true,[&](const Entry& e){
            if(e.sourceDecl!=declaration||e.program!=program||e.domain!=domain||!sameSource(e,source,true))return false;
            for(unsigned s=0;s<4;++s)if(e.source->streams[s].stride!=source->streams[s].stride||e.source->streams[s].bytes!=source->streams[s].bytes)return false;
            return true;
        });
        if(!hit||hit->source==source)return source;
        ++sourceAliases;return hit->source;
    }
    Entry* findOverlay(const std::shared_ptr<const Mesh>& source,uint64_t fingerprint,IDirect3DVertexDeclaration9* declaration){
        Entry* overlay=nullptr;candidates(source,fingerprint,true,[&](Entry& e){if(e.sourceDecl==declaration&&e.domain==1&&e.baseFrame==uint64_t(frames)+1&&(!overlay||e.used>overlay->used||(e.used==overlay->used&&e.order<overlay->order))&&sameSource(e,source,true))overlay=&e;});return overlay;
    }
    bool retains(const Mesh* source,const Entry* except=nullptr)const{for(const auto& e:entries)if(e.get()!=except&&e->source.get()==source)return true;return false;}
    uint64_t retainedFingerprint(const std::shared_ptr<const Mesh>& source)const{const auto it=sourceLookup.find(source.get());return it!=sourceLookup.end()?it->second->fingerprint:sourceFingerprint(*source);}
    void evictOldest(){
        auto old=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a->used<b->used;});Entry* e=old->get();
        resident-=e->bytes;if(!retains(e->source.get(),e))resident-=e->source->byteSize();
        auto erase=[&](auto& lookup,const auto& key){auto range=lookup.equal_range(key);for(auto it=range.first;it!=range.second;)if(it->second==e)it=lookup.erase(it);else ++it;};
        erase(sourceLookup,e->source.get());erase(contentLookup,e->fingerprint);erase(materialLookup,e->materialFingerprint);entries.erase(old);
    }
    void makeRoom(const std::shared_ptr<const Mesh>& source,size_t bytes){while(!entries.empty()&&(resident+bytes+(retains(source.get())?0:source->byteSize())>cacheBudget||entries.size()>=256))evictOldest();}
    void retain(std::unique_ptr<Entry> entry){
        const size_t bytes=entry->bytes+(retains(entry->source.get())?0:entry->source->byteSize());entry->order=++entrySerial;entry->fingerprint=sourceFingerprint(*entry->source);
        entry->materialFingerprint=materialFingerprint(entry->fingerprint,entry->program,entry->detail,entry->address,entry->domain,entry->material,entry->texture,entry->texture1,entry->heightVersion,entry->heightVersion1,entry->uvConstants);
        entries.push_back(std::move(entry));resident+=bytes;Entry* e=entries.back().get();
        if(indexedEntries)try{sourceLookup.emplace(e->source.get(),e);contentLookup.emplace(e->fingerprint,e);materialLookup.emplace(e->materialFingerprint,e);}catch(...){sourceLookup.clear();contentLookup.clear();materialLookup.clear();indexedEntries=false;}
    }
    bool snapshotFailure(const NorthlightDrawSnapshot::Diagnostics& why){++snapshotRejected;const auto reason=size_t(why.error);++snapshotReasons[reason<snapshotReasons.size()?reason:0];return false;}
    static std::vector<unsigned> usedUVRegisters(const Material& m,bool layered){
        bool used[256]={};for(const auto* p:{&m.uv,&m.uv1,&m.blend})if(p==&m.uv||layered)for(const auto& op:p->operations)for(const auto& source:op.source){if(NorthlightShadowShader::regType(source.token)!=2)continue;const auto index=NorthlightShadowShader::regIndex(source.token);bool defined=false;for(const auto& def:p->definitions)defined|=def.reg==index;if(!defined&&index<256)used[index]=true;}
        std::vector<unsigned> result;for(unsigned i=0;i<256;++i)if(used[i])result.push_back(i);return result;
    }
    static void uvKey(const Material& m,const float* constants,bool layered,std::vector<float>& key){
        const auto& registers=m.uvRegisters[layered?1:0];key.clear();key.reserve(registers.size()*5);for(auto i:registers){key.push_back(float(i));key.insert(key.end(),constants+i*4,constants+i*4+4);}
    }
    bool memoryPressure=false;
    void refreshBudget(const MEMORYSTATUSEX& memory){
        if(memoryPressure)cacheBudget=32u<<20;
        else if(memory.ullAvailVirtual<(896ull<<20))cacheBudget=32u<<20;
        else if(memory.ullAvailVirtual>=(1024ull<<20))cacheBudget=128u<<20;
        while(resident>cacheBudget&&!entries.empty())evictOldest();
    }
    const Height* height(IDirect3DTexture9* source,bool model=false){
        NorthlightMaterialProfile::Scope timing(profile,NorthlightMaterialProfile::HeightRead);
        const auto revision=NorthlightMaterialHeight::version(source);
        for(auto it=textures.begin();it!=textures.end();++it)if(it->source==source&&it->model==model){if(it->version!=revision){drop(it->gpu);drop(it->source);textures.erase(it);break;}it->used=++clock;return it->height.width?&it->height:nullptr;}
        if(readsThisFrame>=2)return nullptr;++readsThisFrame;
        auto reject=[&]()->const Height*{if(textures.size()>=32){auto old=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(old->gpu);drop(old->source);textures.erase(old);}Texture failed;failed.source=source;source->AddRef();failed.used=++clock;failed.version=revision;failed.model=model;textures.push_back(std::move(failed));return nullptr;};
        NorthlightMaterialHeight::Image image;if(!NorthlightMaterialHeight::read(source,image))return reject();
        if(textures.size()>=32){auto oldest=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(oldest->gpu);drop(oldest->source);textures.erase(oldest);}
        Texture item;item.source=source;source->AddRef();item.used=++clock;item.version=revision;item.model=model;item.height.width=image.width;item.height.height=image.height;item.height.values.resize(size_t(image.width)*image.height);
        const auto profile=NorthlightMaterialHeight::profile(image,polarity,heightDetails,heightBalance);
        for(size_t i=0;i<item.height.values.size();++i)item.height.values[i]=model?NorthlightMaterialHeight::modelValue(image,unsigned(i%image.width),unsigned(i/image.width),polarity):NorthlightMaterialHeight::value(image.rgba[i*4]/255.f,image.rgba[i*4+1]/255.f,image.rgba[i*4+2]/255.f,profile);
        textures.push_back(std::move(item));return &textures.back().height;
    }
    IDirect3DTexture9* heightTexture(IDirect3DTexture9* source,bool model){
        NorthlightMaterialProfile::Scope timing(profile,NorthlightMaterialProfile::GeometryUpload);
        for(auto& t:textures)if(t.source==source&&t.model==model&&t.height.width){
            if(t.gpu)return t.gpu;
            const auto& h=t.height;IDirect3DTexture9* map=nullptr;
            if(FAILED(d->CreateTexture(h.width,h.height,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&map,nullptr)))return nullptr;
            D3DLOCKED_RECT lock{};if(FAILED(map->LockRect(0,&lock,nullptr,0))){drop(map);return nullptr;}
            const bool valid=lock.pBits&&lock.Pitch>=INT(h.width*sizeof(float));
            if(valid)for(unsigned y=0;y<h.height;++y)std::memcpy(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,h.values.data()+size_t(y)*h.width,h.width*sizeof(float));
            const auto hr=map->UnlockRect(0);if(!valid||FAILED(hr)){drop(map);return nullptr;}
            t.gpu=map;return map;
        }return nullptr;
    }
    bool viewRows(const Program& program,const float* constants){
        // Unit-point differences in float quantize matrix coefficients when
        // translation is large. Multiplying those errors by world coordinates
        // makes the distance fade crawl as the camera rotates. Extract in double;
        // native deformation and the final GPU constants remain float.
        using Four=std::array<double,4>;Four input[16]={};unsigned position=16;
        for(auto i:program.inputs){if(i.usage==D3DDECLUSAGE_POSITION&&!i.index)position=i.reg;else return false;}
        if(position==16)return false;input[position]={0,0,0,1};Four origin;if(!NorthlightActorDeformation::evaluate<true>(program,input,constants,origin))return false;
        for(unsigned r=0;r<3;++r)controls[r][3]=origin[r];
        for(unsigned k=0;k<3;++k){input[position]={0,0,0,1};input[position][k]=1;Four v;if(!NorthlightActorDeformation::evaluate<true>(program,input,constants,v))return false;for(unsigned r=0;r<3;++r)controls[r][k]=float(v[r]-origin[r]);}
        const float a=range*range,b=a*.5625f;controls[3][0]=strength;controls[3][1]=-1/(a-b);controls[3][2]=a/(a-b);controls[3][3]=0;return true;
    }
public:
    void setProfiler(NorthlightMaterialProfile* p){profile=p;}
    struct Args {NorthlightDrawSnapshot::Draw draw;const void* indices=nullptr;D3DFORMAT format=D3DFMT_UNKNOWN;const void* vertices=nullptr;UINT stride=0;};
    explicit NorthlightAutoDisplacement(IDirect3DDevice9* device):d(device){textures.reserve(32);snapshots.setSnapshotCacheLimit(24u<<20);rangeSnapshots.setSnapshotCacheLimit(4u<<20);skinRangeSnapshots.setSnapshotCacheLimit(4u<<20);
        // Optional range admission must never consume displacement's read budget.
        for(auto* frame:{&snapshots,&rangeSnapshots,&skinRangeSnapshots}){frame->setChargeResidentHits(false);
            frame->setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});
            frame->setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});}
        // Rigid range admission reads POSITION0 only. UV/color stream writes
        // must not invalidate immutable position bounds or spend its read budget.
        rangeSnapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){
            const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;if(!static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n))return false;
            bool found=false;for(UINT i=0;i<n&&e[i].Stream!=0xff;++i)if(e[i].Usage==D3DDECLUSAGE_POSITION&&!e[i].UsageIndex){
                if(found||e[i].Stream!=0||e[i].Type!=D3DDECLTYPE_FLOAT3||e[i].Method!=D3DDECLMETHOD_DEFAULT)return false;
                found=true;extent[0]=UINT(e[i].Offset)+12;}
            return found;});}
    ~NorthlightAutoDisplacement(){reset();for(auto& s:shaders){drop(s.second.patched);drop(s.second.gpu);drop(s.second.source);}}
    void tracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){positionBounds.tracking(identity,version,metadata);for(auto* frame:{&snapshots,&rangeSnapshots,&skinRangeSnapshots}){frame->setIdentityProvider(identity);frame->setVersionProvider(version);frame->setMetadataProvider(metadata);}}
    void setConstantEpochSource(void* context,NorthlightConstantEpoch::Reader reader){constantStamp={};rangeConstantStamp={};constantContext=context;constantReader=reader;}
    void shareCaptureSources(NorthlightDrawSnapshot::Frame& capture){snapshots.shareSnapshotPool(capture);snapshots.setReplayContentReuse(true);snapshots.setResidentPeer(&capture);capture.setResidentPeer(&snapshots);skinRangeSnapshots.setResidentPeer(&capture);}
    void reportSources(uint64_t frame)const{
        const auto& boundsWork=positionBounds.maintenance();
        logf("MATERIAL_POSITION_BOUNDS frame=%llu hits=%u scans=%u positionBytes=%zu entries=%zu unknown=%u coldScans=%u revisionScans=%u evictions=%u budgetRejected=%u capturedBoundsHits=%u",(unsigned long long)frame,positionBounds.cacheHits(),positionBounds.scans(),positionBounds.bytesRead(),positionBounds.count(),positionBounds.failures(),boundsWork.coldScans,boundsWork.revisionScans,boundsWork.evictions,boundsWork.budgetRejected,capturedBoundsHits);
        const auto& e=poseEnvelopes.stats();
        logf("MATERIAL_REPLAY_CONTENT frame=%llu geometryTests=%u geometryHits=%u geometryComparedBytes=%zu",(unsigned long long)frame,snapshots.replayContentTests(),snapshots.replayContentHits(),snapshots.replayContentComparedBytes());
        logf("MATERIAL_GEOMETRY_LIFETIME frame=%llu participants=%zu sharedHits=%u uniqueBytes=%zu liveBytes=%zu liveObjects=%zu",(unsigned long long)frame,snapshots.sharedSourceParticipants(),snapshots.sharedSourceHits(),snapshots.uniqueSourceBytes(),snapshots.sourceLiveBytes(),snapshots.sourceLiveObjects());
        logf("MATERIAL_GEOMETRY_SOURCE frame=%llu residentHits=%u peerHits=%u misses=%u readBytes=%zu entries=%zu bytes=%zu limitMiB=%zu skinPeerHits=%u skinReadBytes=%zu rangeReadBytes=%zu poseVertices=%u envelopeEntries=%zu envelopeReady=%zu envelopeCold=%zu envelopeBuilt=%zu envelopeBuildVertices=%zu envelopeTimeDeferred=%zu envelopeOperationDeferred=%zu envelopeUnsupported=%zu poseGenericVertices=%u poseSingleBoneVertices=%u poseCommutedVertices=%u poseTemplateVertices=%u",(unsigned long long)frame,snapshots.snapshotCacheHits(),snapshots.residentPeerHits(),snapshots.snapshotCacheMisses(),snapshots.bytesRead(),snapshots.snapshotCacheEntries(),snapshots.snapshotCacheBytes(),snapshots.snapshotCacheLimit()>>20,skinRangeSnapshots.residentPeerHits(),skinRangeSnapshots.bytesRead(),rangeSnapshots.bytesRead(),poseVerticesThisFrame,poseEnvelopes.entries(),e.ready,e.cold,e.completed,e.buildVertices,e.timeDeferred,e.operationDeferred,e.unsupported,poseKernelVertices[0],poseKernelVertices[1],poseKernelVertices[2],poseKernelVertices[3]);
    }
    void configure(const NorthlightQuality::Settings& s){
        current=nullptr;if(worldDetail!=s.autoDisplacementDetail||modelDetail!=s.autoModelDisplacementDetail||polarity!=s.autoHeightPolarity||heightDetails!=s.autoHeightDetails||heightBalance!=s.autoHeightBalance)reset();
        worldDetail=s.autoDisplacementDetail;modelDetail=s.autoModelDisplacementDetail;worldStrength=s.autoDisplacementStrength*.0012f;worldRange=float(s.autoDisplacementDistance);modelStrength=s.autoModelDisplacementStrength*.0012f;modelRange=float(s.autoModelDisplacementDistance);
        polarity=s.autoHeightPolarity;heightDetails=s.autoHeightDetails;heightBalance=s.autoHeightBalance;strength=worldStrength;range=worldRange;detail=worldDetail;
        if(!s.autoBump||(s.autoBumpTerrain!=3&&s.autoBumpWalls!=3&&s.autoBumpGrass!=3&&s.autoBumpModels!=3))reset();
    }
    bool hasDisplacement()const{return worldStrength>0||modelStrength>0;}
    bool prepared()const{return current!=nullptr;}
    bool preparedHeightGPU()const{return current&&current->height;}
    float nearestPreparedDepth()const{return lastDepthLower;}
    float geometryRange(unsigned domain)const{return (domain==4?modelStrength:worldStrength)>0?(domain==4?modelRange:worldRange):0;}
    void reportHeightGPU()const{logf("DISPLACEMENT heightGPU supported=%u draws=%llu outsideRange=%llu poseBudgetRejected=%llu poseEnvelopeHits=%llu poseFallbackVertices=%llu objectBoundsVertices=%llu boundsHits=%llu boundsMisses=%llu boundsEntries=%zu boundsBytes=%zu primitiveRejected=%llu sourceAliases=%llu",unsigned(gpuHeightSupported),(unsigned long long)gpuDraws,(unsigned long long)distanceRejected,(unsigned long long)poseBudgetRejected,(unsigned long long)poseEnvelopeHits,(unsigned long long)poseFallbackVertices,(unsigned long long)objectBoundsVertices,(unsigned long long)boundsHits,(unsigned long long)boundsMisses,bounds.size(),boundsBytes,(unsigned long long)primitiveRejected,(unsigned long long)sourceAliases);}
    void reportModelOverlay()const{logf("M2_OVERLAY_SYNC recorded=%llu matched=%llu deferred=%llu immediate=%llu",(unsigned long long)modelOverlayRecorded,(unsigned long long)modelOverlayMatched,(unsigned long long)modelOverlayDeferred,(unsigned long long)modelOverlayDrawn);}
    // Reflected UVs need not support height inference: overlays reuse the base
    // mesh while retaining their own native UV calculation.
    static bool modelOverlayShader(uint64_t hash){return modelBaseShader(hash)||hash==0x3add5efbef08550bull||hash==0x7207348e2340eb59ull||hash==0xf63b8d3c26b5efd7ull||hash==0x26584edafcf71f11ull;}
    bool skinned(IDirect3DVertexShader9* original)const{auto it=shaders.find(original);return it!=shaders.end()&&it->second.skinned;}
    // Rigid affine bounds and cached skin envelopes. Unknown/budget-limited
    // draws retain per-pixel checks. UP skin draws have no reusable source owner.
    bool nearestMaterialDepth(IDirect3DVertexShader9* original,const Args& args,float& nearest,bool geometrySource=false,const NorthlightCaptureMetadata::PositionBounds* captured=nullptr){
        using P=NorthlightMaterialProfile;P::Scope timing(profile,P::BoundsPose);
        auto shader=materials.find(original);if(shader==materials.end())return false;
        const auto& program=shader->second.view;
        const bool skin=shader->second.skinned;
        if(skin&&(args.vertices||!program.skinned))return false;
        const unsigned input=skin?16:shader->second.rangeRegister==17?rangeInput(program):shader->second.rangeRegister;if(!skin&&input==16)return false;
        {P::Scope read(profile,P::Constants);if(!readConstantBank(shader->second.first,shader->second.count,skin,rangeConstants,rangeConstantStamp))return false;}
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(!declarations.get(decl.p,elements,count))return false;
        const auto position=std::find_if(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Stream==0&&e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0&&e.Type==D3DDECLTYPE_FLOAT3;});
        if(position==elements+count)return false;
        if(!skin&&!args.vertices&&position->Method==D3DDECLMETHOD_DEFAULT&&captured&&captured->known&&captured->owner==&args&&captured->shader==original&&
           captured->low[0]<=captured->high[0]&&captured->low[1]<=captured->high[1]&&captured->low[2]<=captured->high[2]&&
           nearestViewDepth(program,input,captured->low,captured->high,rangeConstants,nearest)){++capturedBoundsHits;return true;}
        if(geometrySource&&!skin&&!args.vertices&&position->Method==D3DDECLMETHOD_DEFAULT){
            float low[3],high[3];P::Scope scan(profile,P::Snapshot);const auto before=positionBounds.bytesRead();
            const bool known=positionBounds.read(d,args.draw,position->Offset,low,high);
            const auto bytes=positionBounds.bytesRead()-before;scan.work(bytes/12,bytes);scan.next(P::BoundsPose);
            if(known)return nearestViewDepth(program,input,low,high,rangeConstants,nearest);
        }
        Mesh local;std::shared_ptr<const Mesh> source;
        P::Scope read(profile,P::Snapshot);
        if(args.vertices){if(!rangeSnapshots.readUP(decl.p,args.draw,args.indices,args.format,args.vertices,args.stride,local))return false;}
        else if(geometrySource){if(!snapshots.read(d,decl.p,args.draw,local,nullptr,false,&source,false,true))return false;}
        else if(!(skin?skinRangeSnapshots:rangeSnapshots).read(d,decl.p,args.draw,local,nullptr,false,&source))return false;
        if(!source)source=snapshots.retain(std::move(local));
        read.work(source->vertexCount,source->byteSize());read.next(P::BoundsPose);
        if(skin){
            P::Scope envelope(profile,P::PoseEnvelope);
            NorthlightReplayBounds::Bounds result;
            if(poseEnvelope(program,source,elements,count,rangeConstants,result,original,decl.p)!=NorthlightReplayBounds::Status::Valid)return false;
            const float lo=result.low[2],hi=result.high[2];nearest=std::max(0.f,std::nextafter(lo>0?lo:hi<0?-hi:0,-INFINITY)-.001f);return true;
        }
        auto found=cachedBounds(source);
        if(found==bounds.end()){
            if(profile)profile->count(P::BoundsVertices,source->vertexCount);
            Bounds b;for(unsigned k=0;k<3;++k){b.low[k]=FLT_MAX;b.high[k]=-FLT_MAX;}
            const auto& stream=source->streams[0];for(unsigned i=0;i<source->vertexCount;++i){
                if(size_t(i)*stream.stride+position->Offset+12>stream.bytes.size())return false;
                float p[3];std::memcpy(p,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);
                for(unsigned k=0;k<3;++k){if(!std::isfinite(p[k]))return false;b.low[k]=std::min(b.low[k],p[k]);b.high[k]=std::max(b.high[k],p[k]);}}
            found=retainBounds(source,std::move(b));
        }
        auto& b=found->second;touchBounds(b);
        return nearestViewDepth(program,input,b.low,b.high,rangeConstants,nearest);
    }
    unsigned preparedPrimitives()const{return current?current->primitives:0;}
    void beginDraw(){current=nullptr;currentOverlay=false;lastDepthLower=0;}
    void setMemoryPressure(bool on,NorthlightStreaming::CpuRetirement* retirement=nullptr,NorthlightStreaming::CpuRetirementBatch* batch=nullptr){
        current=nullptr;memoryPressure=on;
        using Clock=std::chrono::steady_clock;const bool timed=NorthlightRenderThreadProbe::profiling();
        auto started=timed?Clock::now():Clock::time_point{};
        auto phase=[&]{if(!timed)return 0.;const auto ended=Clock::now();const double ms=std::chrono::duration<double,std::milli>(ended-started).count();started=ended;return ms;};
        // All three pools are discarded in pressure mode; do not first evict
        // part of each pool just to reach a cap that its clear already satisfies.
        size_t deferred=0;if(on){deferred+=snapshots.clearIndexCache(retirement,batch);deferred+=rangeSnapshots.clearIndexCache(retirement,batch);deferred+=skinRangeSnapshots.clearIndexCache(retirement,batch);}
        const double sourcesMs=phase();
        snapshots.setSnapshotCacheLimit(on?6u<<20:24u<<20);rangeSnapshots.setSnapshotCacheLimit(on?1u<<20:4u<<20);skinRangeSnapshots.setSnapshotCacheLimit(on?1u<<20:4u<<20);
        const double capsMs=phase();
        // Pressure selects the fixed 32 MiB budget without reading OS values.
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(!on)GlobalMemoryStatusEx(&memory);refreshBudget(memory);
        const double geometryMs=phase();
        if(on){positionBounds.clear();boundsLRU.clear();bounds.clear();boundsBytes=0;poseEnvelopes.clear();poseMetadata.clear();}
        const double boundsMs=phase();
        logf("DISPLACEMENT_MEMORY pressure=%u profiled=%u sourcesMs=%.3f capsMs=%.3f geometryMs=%.3f boundsMs=%.3f deferredMiB=%.3f batched=%u",unsigned(on),unsigned(timed),sourcesMs,capsMs,geometryMs,boundsMs,double(deferred)/(1u<<20),unsigned(batch!=nullptr));
    }
    void trimMemory(NorthlightStreaming::CpuRetirement* retirement=nullptr,NorthlightStreaming::CpuRetirementBatch* batch=nullptr){setMemoryPressure(memoryPressure,retirement,batch);}
    void reset(){positionBounds.clear();current=nullptr;clearModelOverlays();constantStamp={};rangeConstantStamp={};sourceLookup.clear();contentLookup.clear();materialLookup.clear();indexedEntries=true;entries.clear();boundsLRU.clear();bounds.clear();poseEnvelopes.clear();poseMetadata.clear();poseBindings={};envelopeBudget.reset();boundsBytes=resident=0;for(auto& t:textures){drop(t.gpu);drop(t.source);}textures.clear();snapshots.clearIndexCache();rangeSnapshots.clearIndexCache();skinRangeSnapshots.clearIndexCache();declarations.clear();}
    void endFrame(){current=nullptr;clearModelOverlays();if(variantSources)logf("MATERIAL_VARIANT_COLD frame=%u sources=%u cpuBuilt=%u gpuBuilt=%u readFailed=%u sourceBytes=%zu cpuMs=%.6f",frames,variantSources,variantCPU,variantGPU,variantReadFailed,variantBytes,variantMs);variantSources=variantCPU=variantGPU=variantReadFailed=0;variantBytes=0;variantMs=0;if(frames%60==0){MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(GlobalMemoryStatusEx(&memory))refreshBudget(memory);}buildsThisFrame=readsThisFrame=poseVerticesThisFrame=0;for(auto& vertices:poseKernelVertices)vertices=0;poseEnvelopes.beginFrame(false);poseMetadata.beginFrame();envelopeBudget.reset();snapshots.clearFrame();rangeSnapshots.clearFrame();skinRangeSnapshots.clearFrame();positionBounds.beginFrame();capturedBoundsHits=0;if((draws||snapshotRejected)&&frames%600==0){logf("DISPLACEMENT cached=%zu bytes=%zu builds=%llu hits=%llu draws=%llu triangles=%llu refused=%llu terrain=%u walls=%u grass=%u models=%u stateRejected=%u heightRejected=%u shaderRejected=%u snapshotRejected=%u meshRejected=%u budgetMiB=%zu",entries.size(),resident,(unsigned long long)builds,(unsigned long long)hits,(unsigned long long)draws,(unsigned long long)triangles,(unsigned long long)refused,domainDraws[0],domainDraws[1],domainDraws[2],domainDraws[3],stateRejected,heightRejected,shaderRejected,snapshotRejected,meshRejected,cacheBudget>>20);
        logf("DISPLACEMENT snapshots attempts=%llu up=%llu buffered=%llu unknown=%llu arguments=%llu declaration=%llu stream=%llu instancing=%llu indexBuffer=%llu indexRange=%llu vertexRange=%llu budget=%llu lock=%llu unlock=%llu",(unsigned long long)snapshotAttempts,(unsigned long long)snapshotUP,(unsigned long long)(snapshotAttempts-snapshotUP),(unsigned long long)snapshotReasons[0],(unsigned long long)snapshotReasons[1],(unsigned long long)snapshotReasons[2],(unsigned long long)snapshotReasons[3],(unsigned long long)snapshotReasons[4],(unsigned long long)snapshotReasons[5],(unsigned long long)snapshotReasons[6],(unsigned long long)snapshotReasons[7],(unsigned long long)snapshotReasons[8],(unsigned long long)snapshotReasons[9],(unsigned long long)snapshotReasons[10]);}++frames;}
    void registerVariant(IDirect3DVertexShader9* original,const DWORD* code,size_t count,bool skinned=false){
        auto old=shaders.find(original);if(old!=shaders.end()){drop(old->second.patched);drop(old->second.gpu);drop(old->second.source);shaders.erase(old);}Shader shader;
        // Preserve unsupported-program admission before spending pose budgets.
        // The retained source already owns its bytecode; device variants are
        // needed only after a draw passes its existing geometry distance proof.
        std::vector<uint32_t> validated;shader.pending=NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(code),count,validated,shader.binding,skinned);
        shader.source=original;shader.skinned=skinned;shader.hash=NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(code),count);original->AddRef();shaders.emplace(original,shader);
    }
    void registerMaterial(IDirect3DVertexShader9* original,const DWORD* code,size_t count,unsigned domain){
        poseMetadata.invalidateShader(original);materials.erase(original);if(domain>4)return;Material m;const auto* words=reinterpret_cast<const uint32_t*>(code);
        const bool overlay=domain==4&&modelOverlayShader(NorthlightWaterShaderPatch::fnv1a64(words,count));
        if(overlay)registerVariant(original,code,count,true);
        if(!NorthlightActorDeformation::compile(words,count,m.uv,true)||!NorthlightActorDeformation::compile(words,count,m.view,false,(domain==1||domain==3)?1:2))return;
        m.skinned=m.view.skinned;m.rangeRegister=m.skinned?16:rangeInput(m.view);m.influences=m.skinned?1:0;
        unsigned provided=0;bool inputsValid=true;for(const auto& input:m.view.inputs)if(input.reg<16)provided|=1u<<input.reg;else{inputsValid=false;break;}
        m.clearPoseInputs=!inputsValid||NorthlightActorDeformation::clearVertexInputsMask(m.view,provided);
        for(const auto& input:m.view.inputs)if(input.usage==D3DDECLUSAGE_BLENDWEIGHT){unsigned channels=0;for(const auto& op:m.view.operations)for(const auto& source:op.source)if(NorthlightShadowShader::regType(source.token)==1&&NorthlightShadowShader::regIndex(source.token)==input.reg){unsigned mask=op.code==9?15:op.code==8?7:(op.destination>>16)&15;for(unsigned c=0;c<4;++c)if(mask&(1u<<c))channels=std::max(channels,1u+((source.token>>(16+2*c))&3));}m.influences=std::clamp(channels,1u,4u);}
m.layered=NorthlightActorDeformation::compile(words,count,m.uv1,true,2,5,1)&&NorthlightActorDeformation::compile(words,count,m.blend,true,2,10,1,8);
        unsigned first=256,last=0;for(const auto* p:{&m.uv,&m.view,&m.uv1,&m.blend})for(const auto& op:p->operations)for(const auto& source:op.source){if(NorthlightShadowShader::regType(source.token)!=2)continue;unsigned i=NorthlightShadowShader::regIndex(source.token);bool defined=false;for(const auto& def:p->definitions)defined|=def.reg==i;if(!defined){first=std::min(first,i);last=std::max(last,i);}}
        if(first>last||last>=256)return;m.first=m.skinned?0:first;m.count=m.skinned?256:last-first+1;m.id=++materialClock;
        m.uvRegisters[0]=usedUVRegisters(m,false);m.uvRegisters[1]=usedUVRegisters(m,true);const bool skinned=m.skinned;materials.emplace(original,std::move(m));if(!overlay)registerVariant(original,code,count,skinned);
    }
    bool prepare(IDirect3DVertexShader9* original,unsigned domain,unsigned material,const Args& args){
        using P=NorthlightMaterialProfile;P::Scope timing(profile,P::Admission);
        current=nullptr;currentOverlay=false;preparedDraw=args.draw;lastDepthLower=0;strength=domain==4?modelStrength:worldStrength;range=domain==4?modelRange:worldRange;detail=domain==4?modelDetail:domain==3?std::min(worldDetail,3u):worldDetail;auto shader=materials.find(original);auto patched=shaders.find(original);if(shader==materials.end()||strength<=0||domain>4||patched==shaders.end()||(!patched->second.pending&&!patched->second.patched)){++shaderRejected;return false;}
        DWORD z=0,write=0,blendState=0;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ZWRITEENABLE,&write);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blendState);
        bool cutout=false;if(blendState&&write){DWORD alpha=0,src=0,dst=0,func=0;d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_ALPHAFUNC,&func);cutout=alpha&&src==D3DBLEND_SRCALPHA&&dst==D3DBLEND_INVSRCALPHA&&(func==D3DCMP_GREATER||func==D3DCMP_GREATEREQUAL);}
        if(!z||(!write&&domain!=1)||(blendState&&!cutout&&!(domain==1&&material==2))){++stateRejected;return false;}
        timing.next(P::Constants);
        if(!readConstants(shader->second.first,shader->second.count,shader->second.skinned)||(!shader->second.skinned&&!viewRows(shader->second.view,constants)))return false;
        timing.next(P::Snapshot);
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(!declarations.get(decl.p,elements,count)||count>MAXD3DDECLLENGTH)return false;
        Mesh local;std::shared_ptr<const Mesh> source;
        NorthlightDrawSnapshot::Diagnostics why;++snapshotAttempts;if(args.vertices)++snapshotUP;
        if(args.vertices){if(!snapshots.readUP(decl.p,args.draw,args.indices,args.format,args.vertices,args.stride,local,&why))return snapshotFailure(why);}
        else if(!snapshots.read(d,decl.p,args.draw,local,&why,false,&source))return snapshotFailure(why);
        if(!source)source=snapshots.retain(std::move(local));
        timing.work(source->vertexCount,source->byteSize());timing.next(P::BoundsPose);
        if(!args.vertices)source=geometrySource(source,decl.p,shader->second.id,domain);
        const bool equivalent=args.vertices||(domain==1&&material==2);const uint64_t fingerprint=equivalent?sourceFingerprint(*source):0;
        // UP snapshots have fresh identities even when every byte is unchanged.
        // Reuse the proven source before rebuilding its bounds or skin envelope.
        if(args.vertices){auto* e=firstCandidate(source,fingerprint,true,[&](const Entry& e){return e.sourceDecl==decl.p&&e.program==shader->second.id&&e.domain==domain&&sameSource(e,source,true);});if(e)source=e->source;}
        if(domain==1&&material!=2)candidates(source,fingerprint,equivalent,[&](Entry& e){if(e.sourceDecl==decl.p&&e.domain==domain&&sameSource(e,source,equivalent))e.baseFrame=0;});
        // Reject whole draws outside the sphere before decoding textures or subdividing.
        const auto position=std::find_if(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Stream==0&&e.Usage==0&&e.UsageIndex==0;});
        if(position==elements+count||position->Type!=D3DDECLTYPE_FLOAT3)return false;
        float low[3],high[3];
        if(shader->second.skinned&&distantPoseBounds(shader->second.view,source,elements,count,low,high,original,decl.p)){++distanceRejected;return false;}
        // Generic skinning uses exact posed vertices below, never object-space
        // bounds. Building/scanning/retaining those before a budget refusal was
        // unbounded work with no consumer, especially when the source churned.
        const bool exactSkin=shader->second.skinned&&shader->second.view.kernel.skin;
        const bool needsBounds=!shader->second.skinned||exactSkin;
        const auto& stream=source->streams[0];auto foundBounds=needsBounds?cachedBounds(source):bounds.end();
        if(needsBounds&&foundBounds==bounds.end()){
            Bounds b;for(unsigned i=0;i<3;++i){b.low[i]=FLT_MAX;b.high[i]=-FLT_MAX;}
            objectBoundsVertices+=source->vertexCount;
            if(profile)profile->count(P::BoundsVertices,source->vertexCount);
            for(unsigned i=0;i<source->vertexCount;++i){if(size_t(i)*stream.stride+position->Offset+12>stream.bytes.size())return false;float p[3];std::memcpy(p,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);for(unsigned k=0;k<3;++k){if(!std::isfinite(p[k]))return false;b.low[k]=std::min(b.low[k],p[k]);b.high[k]=std::max(b.high[k],p[k]);}}
            if(shader->second.view.kernel.skin){auto envelope=std::make_unique<NorthlightReplayBounds::SkinEnvelope>();bool valid=true;
                for(unsigned i=0;i<source->vertexCount&&valid;++i){NorthlightActorDeformation::Four input[16];valid=NorthlightActorDeformation::sampledVertexInputsAt(shader->second.view,*source,elements,count,i,input)&&envelope->add(input);}if(valid)b.skin=std::move(envelope);}
            foundBounds=retainBounds(source,std::move(b));
        }
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};NorthlightReplayBounds::Bounds skinBounds;
        const auto exactEnvelope=[&]{if(!exactSkin||!foundBounds->second.skin)return false;P::Scope envelope(profile,P::PoseEnvelope);return foundBounds->second.skin->evaluate(constants,identity,skinBounds);};
        if(exactEnvelope()){touchBounds(foundBounds->second);std::copy(skinBounds.low,skinBounds.low+3,low);std::copy(skinBounds.high,skinBounds.high+3,high);
            // Reuse the audited, outward-rounded skin envelope for normal capture.
            lastDepthLower=std::max(0.f,std::nextafter(low[2]>0?low[2]:high[2]<0?-high[2]:0,-INFINITY)-.001f);}
        else if(shader->second.skinned){P::Scope pose(profile,P::PoseFallback);if(source->vertexCount>8192-poseVerticesThisFrame){++poseBudgetRejected;if(profile)profile->count(P::PoseBudget);return false;}poseVerticesThisFrame+=source->vertexCount;poseFallbackVertices+=source->vertexCount;if(profile)profile->count(P::PoseVertices,source->vertexCount);pose.work(source->vertexCount);for(unsigned k=0;k<3;++k){low[k]=FLT_MAX;high[k]=-FLT_MAX;}
            const auto prepared=poseBindings.acquire(shader->second.view,*source,elements,count);if(!prepared)return false;
            if(shader->second.view.kernel.singleSkin()){if(!NorthlightActorDeformation::singleSkinBoundsPrepared<true>(shader->second.view,*source,*prepared,constants,low,high))return false;}
            else {const bool clearInputs=shader->second.clearPoseInputs;
            for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four input[16],view;if(clearInputs)std::fill(std::begin(input),std::end(input),NorthlightActorDeformation::Four{});
                for(const auto& b:prepared->bindings){const auto& s=source->streams[b.element.Stream];if(!NorthlightActorDeformation::decodeElement(s.bytes.data()+size_t(i)*s.stride+b.element.Offset,b.element.Type,input[b.input.reg]))return false;}
                if(!NorthlightActorDeformation::evaluateFast(shader->second.view,input,constants,view))return false;
                for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k]))return false;low[k]=std::min(low[k],view[k]);high[k]=std::max(high[k],view[k]);}}}
            const auto& kernel=shader->second.view.kernel;poseKernelVertices[kernel.singleSkin()?1:kernel.commutedSkin()?2:kernel.skin?3:0]+=source->vertexCount;
        }else{touchBounds(foundBounds->second);const auto& b=foundBounds->second;
            nearestViewDepth(shader->second.view,shader->second.rangeRegister==17?rangeInput(shader->second.view):shader->second.rangeRegister,b.low,b.high,constants,lastDepthLower);
            for(unsigned r=0;r<3;++r){low[r]=high[r]=controls[r][3];for(unsigned k=0;k<3;++k){const float a=controls[r][k]*b.low[k],z=controls[r][k]*b.high[k];low[r]+=std::min(a,z);high[r]+=std::max(a,z);}}}
        float closestSquared=0;for(unsigned r=0;r<3;++r){float v=low[r]>0?low[r]:high[r]<0?high[r]:0;closestSquared+=v*v;}if(closestSquared>=range*range){++distanceRejected;return false;}
        if(shader->second.skinned){const float a=range*range,b=a*.5625f;controls[0][0]=strength*std::clamp((a-closestSquared)/(a-b),0.f,1.f);controls[0][1]=controls[0][2]=controls[0][3]=0;}
        if(!ensureVariant(patched->second)){++shaderRejected;return false;}
        timing.next(P::HeightRead);
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> tex,tex1;if(FAILED(d->GetTexture(0,tex.out()))||!tex.p||tex->GetType()!=D3DRTYPE_TEXTURE)return false;
        if(domain==2&&material==6&&(FAILED(d->GetTexture(1,tex1.out()))||!tex1.p||tex1->GetType()!=D3DRTYPE_TEXTURE||!shader->second.layered))return false;
        DWORD u=0,v=0;d->GetSamplerState(0,D3DSAMP_ADDRESSU,&u);d->GetSamplerState(0,D3DSAMP_ADDRESSV,&v);if((u!=D3DTADDRESS_WRAP&&u!=D3DTADDRESS_CLAMP)||(v!=D3DTADDRESS_WRAP&&v!=D3DTADDRESS_CLAMP))return false;
        const auto revision=NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex.p)),revision1=tex1.p?NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex1.p)):0;
        const unsigned address=u|(v<<8);uvKey(shader->second,constants,domain==2&&material==6,uvScratch);
        Entry* overlay=domain==1&&material==2?findOverlay(source,fingerprint,decl.p):nullptr;
        Entry* hit=domain==1&&material==2?nullptr:firstMaterial(materialFingerprint(equivalent?fingerprint:retainedFingerprint(source),shader->second.id,detail,address,domain,material,tex.p,tex1.p,revision,revision1,uvScratch),[&](Entry& e){if(e.sourceDecl!=decl.p||e.domain!=domain)return false;
            return e.program==shader->second.id&&e.detail==detail&&e.address==address&&e.material==material&&e.texture==tex.p&&e.texture1==tex1.p&&e.uvConstants==uvScratch&&e.heightVersion==revision&&e.heightVersion1==revision1&&sameSource(e,source,equivalent);});
        if(hit){current=hit;userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;hit->used=++clock;hit->baseFrame=uint64_t(frames)+1;++hits;if(profile)profile->count(P::GeometryHit);return true;}
        if(overlay){current=overlay;userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;overlay->used=++clock;++hits;if(profile)profile->count(P::GeometryHit);return true;}
        if((domain==1&&material==2)||buildsThisFrame>=2){++refused;return false;}
        // Preserve existing entries and the distance proof, but do not decode
        // height/UV data for a mesh the existing build limit always rejects.
        if(source->primitiveCount>8192){++primitiveRejected;return false;}
        const auto* firstHeight=height(static_cast<IDirect3DTexture9*>(tex.p),domain==4);if(!firstHeight){++heightRejected;return false;}Height retainedFirst;const Height* second=nullptr;
        if(tex1.p){retainedFirst=*firstHeight;firstHeight=&retainedFirst;second=height(static_cast<IDirect3DTexture9*>(tex1.p));if(!second)return false;}
        const Height& first=*firstHeight;
        timing.next(P::UV);
        std::vector<std::array<float,2>> uv,uv1;std::vector<float> weights;
        if(!NorthlightActorDeformation::textureUVs(shader->second.uv,*source,elements,count,constants,uv))return false;
        if(second){if(!NorthlightActorDeformation::textureUVs(shader->second.uv1,*source,elements,count,constants,uv1))return false;
            weights.resize(source->vertexCount);for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four inputs[16],value;if(!NorthlightActorDeformation::sampledVertexInputsAt(shader->second.blend,*source,elements,count,i,inputs,true)||!NorthlightActorDeformation::evaluateFast(shader->second.blend,inputs,constants,value))return false;if(!std::isfinite(value[3]))return false;weights[i]=value[3];}}
        unsigned factor=domain==4?1u<<detail:NorthlightDisplacementGeometry::textureFactor(uv,first,detail);
        if(second)factor=std::max(factor,NorthlightDisplacementGeometry::textureFactor(uv1,*second,detail));
        if(domain==3)factor=std::min(factor,16u);
        IDirect3DTexture9* gpuHeight=nullptr;
        const bool layoutFree=std::none_of(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==6;});
        if(!second&&patched->second.gpu&&stream.stride<=232&&count+2<=MAXD3DDECLLENGTH&&layoutFree)gpuHeight=heightTexture(static_cast<IDirect3DTexture9*>(tex.p),domain==4);
        timing.next(P::Subdivide);
        ++buildsThisFrame;Mesh output;if(!NorthlightDisplacementGeometry::build(*source,elements,count,uv,first,factor,domain==1,u==D3DTADDRESS_CLAMP,v==D3DTADDRESS_CLAMP,output,second,second?&uv1:nullptr,second?&weights:nullptr,shader->second.influences,gpuHeight!=nullptr)){++meshRejected;return false;}
        timing.work(output.primitiveCount,output.byteSize());if(profile)profile->count(P::GeometryBuilt);timing.next(P::GeometryUpload);
        if(builds<8){float peak=0;for(unsigned i=0;i<output.vertexCount;++i){float delta[3];std::memcpy(delta,output.streams[0].bytes.data()+size_t(i)*output.streams[0].stride+stream.stride,12);peak=std::max(peak,std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2])*strength);}logf("DISPLACEMENT build domain=%u vertices=%u triangles=%u maxOffset=%.5f VScontrol=%u",domain,output.vertexCount,output.primitiveCount,peak,shaders.find(original)->second.binding.constant);}
        const auto indexBytes=NorthlightDisplacementGeometry::indexElementBytes(output);
        auto entry=std::make_unique<Entry>();entry->bytes=output.byteSize()-output.indices.size()*(sizeof(uint32_t)-indexBytes)+(gpuHeight?first.values.size()*sizeof(float):0);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<entry->bytes+source->byteSize()+(160u<<20)){++refused;return false;}
        refreshBudget(memory);if(entry->bytes+source->byteSize()>cacheBudget){++refused;return false;}
        makeRoom(source,entry->bytes);
        const auto vbBytes=UINT(output.streams[0].bytes.size()),ibBytes=UINT(output.indices.size()*indexBytes);void* data=nullptr;
        if(FAILED(d->CreateVertexBuffer(vbBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&entry->vb,nullptr))||FAILED(entry->vb->Lock(0,vbBytes,&data,NorthlightUpload::FreshBufferLock)))return false;std::memcpy(data,output.streams[0].bytes.data(),vbBytes);if(FAILED(entry->vb->Unlock()))return false;
        if(FAILED(d->CreateIndexBuffer(ibBytes,D3DUSAGE_WRITEONLY,indexBytes==sizeof(uint16_t)?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&entry->ib,nullptr))||FAILED(entry->ib->Lock(0,ibBytes,&data,NorthlightUpload::FreshBufferLock)))return false;NorthlightDisplacementGeometry::copyIndices(output,data);if(FAILED(entry->ib->Unlock()))return false;
        D3DVERTEXELEMENT9 outputElements[MAXD3DDECLLENGTH+1];std::copy(elements,elements+count,outputElements);
        outputElements[count-1]={0,WORD(stream.stride),D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,7};unsigned end=count;
        if(gpuHeight)outputElements[end++]={0,WORD(stream.stride+12),D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,6};
        outputElements[end]=D3DDECL_END();if(FAILED(d->CreateVertexDeclaration(outputElements,&entry->decl)))return false;
        entry->height=gpuHeight;if(gpuHeight)gpuHeight->AddRef();
        entry->source=source;entry->sourceDecl=decl.p;entry->sourceDecl->AddRef();entry->texture=static_cast<IDirect3DTexture9*>(tex.p);entry->texture->AddRef();entry->texture1=static_cast<IDirect3DTexture9*>(tex1.p);if(entry->texture1)entry->texture1->AddRef();entry->detail=detail;entry->program=shader->second.id;entry->domain=domain;entry->material=material;entry->uvConstants=uvScratch;entry->address=address;entry->heightVersion=revision;entry->heightVersion1=revision1;entry->vertices=output.vertexCount;entry->primitives=output.primitiveCount;entry->stride=output.streams[0].stride;entry->used=++clock;entry->baseFrame=uint64_t(frames)+1;current=entry.get();userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;retain(std::move(entry));++builds;return true;
    }
    bool prepareModelOverlay(IDirect3DVertexShader9* original,const Args& args){
        if(modelOverlays.empty()||args.vertices||!args.draw.indexed||args.draw.topology!=D3DPT_TRIANGLELIST)return false;
        auto shader=shaders.find(original);if(shader==shaders.end()||!modelOverlayShader(shader->second.hash))return false;
        DWORD z=0,write=0,blend=0,src=0,dst=0;
        if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&write))||FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))||FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||!z||write||!blend||src!=D3DBLEND_SRCALPHA||(dst!=D3DBLEND_ONE&&dst!=D3DBLEND_INVSRCALPHA))return false;
        float bank[256*4];if(FAILED(d->GetVertexShaderConstantF(0,bank,256)))return false;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        Mesh local;std::shared_ptr<const Mesh> source;if(!snapshots.read(d,decl.p,args.draw,local,nullptr,false,&source)||!source||!source->sourceIdentity)return false;
        for(auto it=modelOverlays.rbegin();it!=modelOverlays.rend();++it){auto& candidate=**it;const auto& a=candidate.draw;const auto& b=args.draw;
            if(candidate.mesh->sourceDecl!=decl.p)continue;
            if(a.topology!=b.topology||a.base!=b.base||a.minimum!=b.minimum||a.vertices!=b.vertices||a.start!=b.start||a.primitives!=b.primitives||!NorthlightSourceIdentity::same(candidate.mesh->source->sourceIdentity,source->sourceIdentity))continue;
            // Audited stock lit/unlit single-bone programs use the same clip matrix and bone palette.
            if(std::memcmp(candidate.bank+2*4,bank+2*4,4*4*sizeof(float)))continue;bool samePose=true;for(unsigned bone=0;bone<75;++bone)if(candidate.bones[bone]&&std::memcmp(candidate.bank+(31+3*bone)*4,bank+(31+3*bone)*4,3*4*sizeof(float))){samePose=false;break;}if(!samePose)continue;
            current=candidate.mesh.get();currentOverlay=true;userDraw=indexedUserDraw=false;std::memcpy(controls,candidate.controls,sizeof controls);++modelOverlayMatched;return true;
        }return false;
    }
    bool preparedModelOverlay()const{return current&&currentOverlay;}
    template<class Draw,class Submit> HRESULT drawBound(Draw original,bool native,Submit submit){
        if(!current)return original();
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> vs;if(FAILED(d->GetVertexShader(vs.out()))||!vs.p)return original();auto shader=shaders.find(vs.p);
        // World terrain-coverage variants keep the native input layout but are
        // created by a separate subsystem. Patch each live variant once.
        if(shader==shaders.end()&&shaders.size()<4096){UINT bytes=0;if(SUCCEEDED(vs->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536*4){std::vector<DWORD> words(bytes/4);if(SUCCEEDED(vs->GetFunction(words.data(),&bytes))){bool relative=false;for(auto word:words)if((word&0x80002000u)==0x80002000u&&NorthlightShadowShader::regType(word)==2)relative=true;registerVariant(vs.p,words.data(),words.size(),relative);}}shader=shaders.find(vs.p);}
        if(shader==shaders.end()||!ensureVariant(shader->second))return original();
        const bool gpu=current->height&&shader->second.gpu;if(current->height&&!gpu)return original();
        const auto& binding=gpu?shader->second.gpuBinding:shader->second.binding;
        if(gpu){const unsigned midpoint=binding.count==2?1:4;std::fill(controls[midpoint],controls[midpoint]+4,0.f);controls[midpoint][0]=.5f;}
        HRESULT hr=D3D_OK;bool bound=false;
        {
        struct State {IDirect3DDevice9* d;NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> vb;NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> ib;NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;UINT offset=0,stride=0,constant=0,constantCount=4;float values[20];bool changed=false;IDirect3DVertexShader9* vs;
            NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;DWORD stage=0;DWORD samplers[6]={};bool textureChanged=false;
            static constexpr D3DSAMPLERSTATETYPE key(unsigned i){return i==0?D3DSAMP_MINFILTER:i==1?D3DSAMP_MAGFILTER:i==2?D3DSAMP_MIPFILTER:i==3?D3DSAMP_ADDRESSU:i==4?D3DSAMP_ADDRESSV:D3DSAMP_SRGBTEXTURE;}
            ~State(){if(textureChanged){d->SetTexture(stage,texture.p);for(unsigned i=0;i<6;++i)d->SetSamplerState(stage,key(i),samplers[i]);}if(changed){d->SetStreamSource(0,vb.p,offset,stride);d->SetIndices(ib.p);d->SetVertexDeclaration(decl.p);d->SetVertexShader(vs);d->SetVertexShaderConstantF(constant,values,constantCount);}}} saved{d,{}, {}, {},0,0,binding.constant,binding.count,{},false,vs.p,{},0,{},false};
        if(FAILED(d->GetStreamSource(0,saved.vb.out(),&saved.offset,&saved.stride))||FAILED(d->GetIndices(saved.ib.out()))||FAILED(d->GetVertexDeclaration(saved.decl.out()))||FAILED(d->GetVertexShaderConstantF(saved.constant,saved.values,saved.constantCount)))return original();
        bool gpuReady=true;
        if(gpu){saved.stage=D3DVERTEXTEXTURESAMPLER0+binding.sampler;if(FAILED(d->GetTexture(saved.stage,saved.texture.out())))return original();
            for(unsigned i=0;i<6;++i)if(FAILED(d->GetSamplerState(saved.stage,State::key(i),&saved.samplers[i])))return original();
            saved.textureChanged=true;const DWORD values[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_NONE,current->address&255,(current->address>>8)&255,FALSE};
            gpuReady=SUCCEEDED(d->SetTexture(saved.stage,current->height));for(unsigned i=0;i<6&&gpuReady;++i)gpuReady=SUCCEEDED(d->SetSamplerState(saved.stage,State::key(i),values[i]));}
        saved.changed=true;
        bound=gpuReady&&SUCCEEDED(d->SetStreamSource(0,current->vb,0,current->stride))&&SUCCEEDED(d->SetIndices(current->ib))&&SUCCEEDED(d->SetVertexDeclaration(current->decl))&&SUCCEEDED(d->SetVertexShader(gpu?shader->second.gpu:shader->second.patched))&&SUCCEEDED(d->SetVertexShaderConstantF(saved.constant,controls[0],saved.constantCount));
        if(bound){hr=submit(current->vertices,current->primitives);if(SUCCEEDED(hr)){++draws;if(gpu)++gpuDraws;++domainDraws[current->domain-1];triangles+=current->primitives;if(native&&currentOverlay)++modelOverlayDrawn;if(native&&!currentOverlay&&current->domain==4&&modelBaseShader(shader->second.hash))rememberModelOverlay();}}
        }
        if(!bound)return original();
        if(native&&userDraw){d->SetStreamSource(0,nullptr,0,0);if(indexedUserDraw)d->SetIndices(nullptr);}
        return hr;
    }
    template<class Draw> HRESULT draw(Draw original,bool native=false){return drawBound(original,native,[&](unsigned vertices,unsigned primitives){return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,vertices,0,primitives);});}
    template<class Submit> bool deferModelOverlay(Submit submit){if(!preparedModelOverlay())return false;bool called=false,accepted=false;drawBound([]{return D3D_OK;},false,[&](unsigned vertices,unsigned primitives){called=true;accepted=submit(vertices,primitives);return D3D_OK;});if(called&&accepted)++modelOverlayDeferred;return called&&accepted;}
};
