#pragma once
#include "draw_snapshot.h"
#include "displacement_geometry.h"
#include "displacement_shader_patch.h"
#include "actor_texture.h"
#include "material_height.h"
#include "replay_bounds.h"
#include "upload_lock.h"
#include "captured_constant_epoch.h"
#include "vertex_declaration_cache.h"
#include <memory>
#include <map>
#include <cfloat>

class NorthlightAutoDisplacement {
    friend struct DisplacementStateTestAccess;
    using Program=NorthlightActorDeformation::Program;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    using Height=NorthlightDisplacementGeometry::Height;
    struct Shader {IDirect3DVertexShader9* patched=nullptr,*source=nullptr;NorthlightDisplacementPatch::Binding binding;};
    struct Material {Program uv,view,uv1,blend;std::vector<unsigned> uvRegisters[2];bool layered=false,skinned=false;uint64_t id=0;unsigned first=0,count=0,influences=0;};
    struct Bounds {std::shared_ptr<const Mesh> source;float low[3],high[3];uint64_t used=0;std::unique_ptr<NorthlightReplayBounds::SkinEnvelope> skin;};
    struct Texture {IDirect3DTexture9* source=nullptr;Height height;uint64_t used=0,version=0;bool model=false;};
    struct Entry {
        std::shared_ptr<const Mesh> source;IDirect3DTexture9* texture=nullptr,*texture1=nullptr;
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexDeclaration9* decl=nullptr,*sourceDecl=nullptr;
        std::vector<float> uvConstants;unsigned vertices=0,primitives=0,stride=0,detail=0,address=0,domain=0,material=0;size_t bytes=0;uint64_t used=0,program=0,baseFrame=0,heightVersion=0,heightVersion1=0,order=0,fingerprint=0,materialFingerprint=0;
        ~Entry(){drop(texture);drop(texture1);drop(vb);drop(ib);drop(decl);drop(sourceDecl);}
    };
    IDirect3DDevice9* d;
    NorthlightDrawSnapshot::Frame snapshots{2u<<20,0};
    NorthlightVertexDeclarations::Cache declarations;
    void* constantContext=nullptr;NorthlightConstantEpoch::Reader constantReader=nullptr;
    NorthlightConstantEpoch::Stamp constantStamp;float constants[256*4]={};
    bool readConstants(unsigned first,unsigned count,bool skinned){
        namespace E=NorthlightConstantEpoch;
        E::Stamp before;const bool valid=skinned&&constantReader&&constantReader(constantContext,before)&&before.valid;
        unsigned lo=first,hi=first+count;
        if(valid&&E::banks(constantStamp,before,false,false))E::dirtySpan(constantStamp,before,lo,hi,lo,hi);
        constantStamp={};
        if(!skinned)std::fill(constants,constants+256*4,0.f);
        if(lo!=hi&&FAILED(d->GetVertexShaderConstantF(lo,constants+lo*4,hi-lo)))return false;
        E::Stamp after;if(valid&&constantReader(constantContext,after)&&E::exact(before,after))constantStamp=after;
        return true;
    }
    std::unordered_map<IDirect3DVertexShader9*,Shader> shaders;
    std::unordered_map<IDirect3DVertexShader9*,Material> materials;
    std::vector<Texture> textures;
    std::unordered_map<const Mesh*,Bounds> bounds;
    std::vector<std::unique_ptr<Entry>> entries;
    std::unordered_multimap<const Mesh*,Entry*> sourceLookup;
    std::multimap<uint64_t,Entry*> contentLookup;
    std::unordered_multimap<uint64_t,Entry*> materialLookup;
    bool indexedEntries=true;uint64_t entrySerial=0;
    std::vector<float> uvScratch;
    Entry* current=nullptr;float controls[4][4]={};
    float strength=.12f,range=12,worldStrength=.12f,worldRange=12,modelStrength=.03f,modelRange=8;unsigned worldDetail=4,modelDetail=2,polarity=0,heightDetails=75,heightBalance=50;unsigned detail=4,buildsThisFrame=0,readsThisFrame=0,poseVerticesThisFrame=0;
    size_t resident=0,boundsBytes=0,cacheBudget=64u<<20;uint64_t clock=0,materialClock=0,builds=0,hits=0,draws=0,triangles=0,refused=0;unsigned frames=0,domainDraws[4]={},stateRejected=0,heightRejected=0,shaderRejected=0,meshRejected=0,snapshotRejected=0;bool userDraw=false,indexedUserDraw=false;
    uint64_t snapshotAttempts=0,snapshotUP=0;std::array<uint64_t,size_t(NorthlightDrawSnapshot::Error::Unlock)+1> snapshotReasons{};
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
        const auto revision=NorthlightMaterialHeight::version(source);
        for(auto it=textures.begin();it!=textures.end();++it)if(it->source==source&&it->model==model){if(it->version!=revision){drop(it->source);textures.erase(it);break;}it->used=++clock;return it->height.width?&it->height:nullptr;}
        if(readsThisFrame>=2)return nullptr;++readsThisFrame;
        auto reject=[&]()->const Height*{if(textures.size()>=32){auto old=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(old->source);textures.erase(old);}Texture failed;failed.source=source;source->AddRef();failed.used=++clock;failed.version=revision;failed.model=model;textures.push_back(std::move(failed));return nullptr;};
        NorthlightMaterialHeight::Image image;if(!NorthlightMaterialHeight::read(source,image))return reject();
        if(textures.size()>=32){auto oldest=std::min_element(textures.begin(),textures.end(),[](const Texture& a,const Texture& b){return a.used<b.used;});drop(oldest->source);textures.erase(oldest);}
        Texture item;item.source=source;source->AddRef();item.used=++clock;item.version=revision;item.model=model;item.height.width=image.width;item.height.height=image.height;item.height.values.resize(size_t(image.width)*image.height);
        const auto profile=NorthlightMaterialHeight::profile(image,polarity,heightDetails,heightBalance);
        for(size_t i=0;i<item.height.values.size();++i)item.height.values[i]=model?NorthlightMaterialHeight::modelValue(image,unsigned(i%image.width),unsigned(i/image.width),polarity):NorthlightMaterialHeight::value(image.rgba[i*4]/255.f,image.rgba[i*4+1]/255.f,image.rgba[i*4+2]/255.f,profile);
        textures.push_back(std::move(item));return &textures.back().height;
    }
    bool viewRows(const Program& program,const float* constants){
        using Four=NorthlightActorDeformation::Four;Four input[16]={};unsigned position=16;
        for(auto i:program.inputs){if(i.usage==D3DDECLUSAGE_POSITION&&!i.index)position=i.reg;else return false;}
        if(position==16)return false;input[position]={0,0,0,1};Four origin;if(!NorthlightActorDeformation::evaluateFast(program,input,constants,origin))return false;
        for(unsigned r=0;r<3;++r)controls[r][3]=origin[r];
        for(unsigned k=0;k<3;++k){input[position]={0,0,0,1};input[position][k]=1;Four v;if(!NorthlightActorDeformation::evaluateFast(program,input,constants,v))return false;for(unsigned r=0;r<3;++r)controls[r][k]=v[r]-origin[r];}
        const float a=range*range,b=a*.5625f;controls[3][0]=strength;controls[3][1]=-1/(a-b);controls[3][2]=a/(a-b);controls[3][3]=0;return true;
    }
public:
    struct Args {NorthlightDrawSnapshot::Draw draw;const void* indices=nullptr;D3DFORMAT format=D3DFMT_UNKNOWN;const void* vertices=nullptr;UINT stride=0;};
    explicit NorthlightAutoDisplacement(IDirect3DDevice9* device):d(device){textures.reserve(32);snapshots.setSnapshotCacheLimit(32u<<20);snapshots.setChargeResidentHits(false);
        snapshots.setDeclarationProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});
        snapshots.setLayoutProvider(&declarations,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});}
    ~NorthlightAutoDisplacement(){reset();for(auto& s:shaders){drop(s.second.patched);drop(s.second.source);}}
    void tracking(uint64_t(*identity)(void*,bool),uint64_t(*version)(void*,bool),NorthlightCaptureMetadata::Read metadata){snapshots.setIdentityProvider(identity);snapshots.setVersionProvider(version);snapshots.setMetadataProvider(metadata);}
    void setConstantEpochSource(void* context,NorthlightConstantEpoch::Reader reader){constantStamp={};constantContext=context;constantReader=reader;}
    void configure(const NorthlightQuality::Settings& s){
        current=nullptr;if(worldDetail!=s.autoDisplacementDetail||modelDetail!=s.autoModelDisplacementDetail||polarity!=s.autoHeightPolarity||heightDetails!=s.autoHeightDetails||heightBalance!=s.autoHeightBalance)reset();
        worldDetail=s.autoDisplacementDetail;modelDetail=s.autoModelDisplacementDetail;worldStrength=s.autoDisplacementStrength*.0012f;worldRange=float(s.autoDisplacementDistance);modelStrength=s.autoModelDisplacementStrength*.0012f;modelRange=float(s.autoModelDisplacementDistance);
        polarity=s.autoHeightPolarity;heightDetails=s.autoHeightDetails;heightBalance=s.autoHeightBalance;strength=worldStrength;range=worldRange;detail=worldDetail;
        if(!s.autoBump||(s.autoBumpTerrain!=3&&s.autoBumpWalls!=3&&s.autoBumpGrass!=3&&s.autoBumpModels!=3))reset();
    }
    bool hasDisplacement()const{return worldStrength>0||modelStrength>0;}
    bool prepared()const{return current!=nullptr;}
    bool skinned(IDirect3DVertexShader9* original)const{auto it=materials.find(original);return it!=materials.end()&&it->second.skinned;}
    unsigned preparedPrimitives()const{return current?current->primitives:0;}
    void beginDraw(){current=nullptr;}
    void setMemoryPressure(bool on){
        current=nullptr;memoryPressure=on;snapshots.setSnapshotCacheLimit(on?8u<<20:32u<<20);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);refreshBudget(memory);
        if(on){bounds.clear();boundsBytes=0;snapshots.clearIndexCache();}
    }
    void trimMemory(){setMemoryPressure(memoryPressure);}
    void reset(){current=nullptr;constantStamp={};sourceLookup.clear();contentLookup.clear();materialLookup.clear();indexedEntries=true;entries.clear();bounds.clear();boundsBytes=resident=0;for(auto& t:textures)drop(t.source);textures.clear();snapshots.clearIndexCache();declarations.clear();}
    void endFrame(){current=nullptr;if(frames%60==0){MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(GlobalMemoryStatusEx(&memory))refreshBudget(memory);}buildsThisFrame=readsThisFrame=poseVerticesThisFrame=0;snapshots.clearFrame();if((draws||snapshotRejected)&&frames%600==0){logf("DISPLACEMENT cached=%zu bytes=%zu builds=%llu hits=%llu draws=%llu triangles=%llu refused=%llu terrain=%u walls=%u grass=%u models=%u stateRejected=%u heightRejected=%u shaderRejected=%u snapshotRejected=%u meshRejected=%u budgetMiB=%zu",entries.size(),resident,(unsigned long long)builds,(unsigned long long)hits,(unsigned long long)draws,(unsigned long long)triangles,(unsigned long long)refused,domainDraws[0],domainDraws[1],domainDraws[2],domainDraws[3],stateRejected,heightRejected,shaderRejected,snapshotRejected,meshRejected,cacheBudget>>20);
        logf("DISPLACEMENT snapshots attempts=%llu up=%llu buffered=%llu unknown=%llu arguments=%llu declaration=%llu stream=%llu instancing=%llu indexBuffer=%llu indexRange=%llu vertexRange=%llu budget=%llu lock=%llu unlock=%llu",(unsigned long long)snapshotAttempts,(unsigned long long)snapshotUP,(unsigned long long)(snapshotAttempts-snapshotUP),(unsigned long long)snapshotReasons[0],(unsigned long long)snapshotReasons[1],(unsigned long long)snapshotReasons[2],(unsigned long long)snapshotReasons[3],(unsigned long long)snapshotReasons[4],(unsigned long long)snapshotReasons[5],(unsigned long long)snapshotReasons[6],(unsigned long long)snapshotReasons[7],(unsigned long long)snapshotReasons[8],(unsigned long long)snapshotReasons[9],(unsigned long long)snapshotReasons[10]);}++frames;}
    void registerVariant(IDirect3DVertexShader9* original,const DWORD* code,size_t count,bool skinned=false){
        auto old=shaders.find(original);if(old!=shaders.end()){drop(old->second.patched);drop(old->second.source);shaders.erase(old);}std::vector<uint32_t> patched;Shader shader;
        if(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(code),count,patched,shader.binding,skinned)){const HRESULT result=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&shader.patched);if(FAILED(result))logf("DISPLACEMENT vertex shader rejected: 0x%08lx model=%08lx",static_cast<unsigned long>(result),static_cast<unsigned long>(code[0]));}
        shader.source=original;original->AddRef();shaders.emplace(original,shader);
    }
    void registerMaterial(IDirect3DVertexShader9* original,const DWORD* code,size_t count,unsigned domain){
        materials.erase(original);if(domain>4)return;Material m;const auto* words=reinterpret_cast<const uint32_t*>(code);
        if(!NorthlightActorDeformation::compile(words,count,m.uv,true)||!NorthlightActorDeformation::compile(words,count,m.view,false,(domain==1||domain==3)?1:2))return;
        m.skinned=m.view.skinned;m.influences=m.skinned?1:0;
        for(const auto& input:m.view.inputs)if(input.usage==D3DDECLUSAGE_BLENDWEIGHT){unsigned channels=0;for(const auto& op:m.view.operations)for(const auto& source:op.source)if(NorthlightShadowShader::regType(source.token)==1&&NorthlightShadowShader::regIndex(source.token)==input.reg){unsigned mask=op.code==9?15:op.code==8?7:(op.destination>>16)&15;for(unsigned c=0;c<4;++c)if(mask&(1u<<c))channels=std::max(channels,1u+((source.token>>(16+2*c))&3));}m.influences=std::clamp(channels,1u,4u);}
m.layered=NorthlightActorDeformation::compile(words,count,m.uv1,true,2,5,1)&&NorthlightActorDeformation::compile(words,count,m.blend,true,2,10,1,8);
        unsigned first=256,last=0;for(const auto* p:{&m.uv,&m.view,&m.uv1,&m.blend})for(const auto& op:p->operations)for(const auto& source:op.source){if(NorthlightShadowShader::regType(source.token)!=2)continue;unsigned i=NorthlightShadowShader::regIndex(source.token);bool defined=false;for(const auto& def:p->definitions)defined|=def.reg==i;if(!defined){first=std::min(first,i);last=std::max(last,i);}}
        if(first>last||last>=256)return;m.first=m.skinned?0:first;m.count=m.skinned?256:last-first+1;m.id=++materialClock;
        m.uvRegisters[0]=usedUVRegisters(m,false);m.uvRegisters[1]=usedUVRegisters(m,true);const bool skinned=m.skinned;materials.emplace(original,std::move(m));registerVariant(original,code,count,skinned);
    }
    bool prepare(IDirect3DVertexShader9* original,unsigned domain,unsigned material,const Args& args){
        current=nullptr;strength=domain==4?modelStrength:worldStrength;range=domain==4?modelRange:worldRange;detail=domain==4?modelDetail:domain==3?std::min(worldDetail,3u):worldDetail;auto shader=materials.find(original);auto patched=shaders.find(original);if(shader==materials.end()||strength<=0||domain>4||patched==shaders.end()||!patched->second.patched){++shaderRejected;return false;}
        DWORD z=0,write=0,blendState=0;d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ZWRITEENABLE,&write);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blendState);
        bool cutout=false;if(blendState&&write){DWORD alpha=0,src=0,dst=0,func=0;d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_ALPHAFUNC,&func);cutout=alpha&&src==D3DBLEND_SRCALPHA&&dst==D3DBLEND_INVSRCALPHA&&(func==D3DCMP_GREATER||func==D3DCMP_GREATEREQUAL);}
        if(!z||(!write&&domain!=1)||(blendState&&!cutout&&!(domain==1&&material==2))){++stateRejected;return false;}
        if(!readConstants(shader->second.first,shader->second.count,shader->second.skinned)||(!shader->second.skinned&&!viewRows(shader->second.view,constants)))return false;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;if(FAILED(d->GetVertexDeclaration(decl.out()))||!decl.p)return false;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(!declarations.get(decl.p,elements,count)||count>MAXD3DDECLLENGTH)return false;
        Mesh local;std::shared_ptr<const Mesh> source;
        NorthlightDrawSnapshot::Diagnostics why;++snapshotAttempts;if(args.vertices)++snapshotUP;
        if(args.vertices){if(!snapshots.readUP(decl.p,args.draw,args.indices,args.format,args.vertices,args.stride,local,&why))return snapshotFailure(why);}
        else if(!snapshots.read(d,decl.p,args.draw,local,&why,false,&source))return snapshotFailure(why);
        if(!source)source=std::make_shared<Mesh>(std::move(local));
        const bool equivalent=args.vertices||(domain==1&&material==2);const uint64_t fingerprint=equivalent?sourceFingerprint(*source):0;
        // UP snapshots have fresh identities even when every byte is unchanged.
        // Reuse the proven source before rebuilding its bounds or skin envelope.
        if(args.vertices){auto* e=firstCandidate(source,fingerprint,true,[&](const Entry& e){return e.sourceDecl==decl.p&&e.program==shader->second.id&&e.domain==domain&&sameSource(e,source,true);});if(e)source=e->source;}
        if(domain==1&&material!=2)candidates(source,fingerprint,equivalent,[&](Entry& e){if(e.sourceDecl==decl.p&&e.domain==domain&&sameSource(e,source,equivalent))e.baseFrame=0;});
        // Reject whole draws outside the sphere before decoding textures or subdividing.
        const auto position=std::find_if(elements,elements+count,[](const D3DVERTEXELEMENT9& e){return e.Stream==0&&e.Usage==0&&e.UsageIndex==0;});
        if(position==elements+count||position->Type!=D3DDECLTYPE_FLOAT3)return false;
        const auto& stream=source->streams[0];auto foundBounds=bounds.find(source.get());
        if(foundBounds==bounds.end()){
            Bounds b;b.source=source;for(unsigned i=0;i<3;++i){b.low[i]=FLT_MAX;b.high[i]=-FLT_MAX;}
            for(unsigned i=0;i<source->vertexCount;++i){if(size_t(i)*stream.stride+position->Offset+12>stream.bytes.size())return false;float p[3];std::memcpy(p,stream.bytes.data()+size_t(i)*stream.stride+position->Offset,12);for(unsigned k=0;k<3;++k){if(!std::isfinite(p[k]))return false;b.low[k]=std::min(b.low[k],p[k]);b.high[k]=std::max(b.high[k],p[k]);}}
            if(shader->second.view.kernel.skin){auto envelope=std::make_unique<NorthlightReplayBounds::SkinEnvelope>();bool valid=true;
                for(unsigned i=0;i<source->vertexCount&&valid;++i){NorthlightActorDeformation::Four input[16];valid=NorthlightActorDeformation::sampledVertexInputsAt(shader->second.view,*source,elements,count,i,input)&&envelope->add(input);}if(valid)b.skin=std::move(envelope);}
            const auto bytes=source->byteSize();if(bytes>(8u<<20))return false;
            while(!bounds.empty()&&(boundsBytes+bytes>(8u<<20)||bounds.size()>=1024)){auto oldest=std::min_element(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});boundsBytes-=oldest->second.source->byteSize();bounds.erase(oldest);}
            boundsBytes+=bytes;foundBounds=bounds.emplace(source.get(),std::move(b)).first;
        }
        float low[3],high[3];
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};NorthlightReplayBounds::Bounds skinBounds;
        if(shader->second.skinned&&foundBounds->second.skin&&foundBounds->second.skin->evaluate(constants,identity,skinBounds)){foundBounds->second.used=++clock;std::copy(skinBounds.low,skinBounds.low+3,low);std::copy(skinBounds.high,skinBounds.high+3,high);}
        else if(shader->second.skinned){if(source->vertexCount>8192-poseVerticesThisFrame)return false;poseVerticesThisFrame+=source->vertexCount;for(unsigned k=0;k<3;++k){low[k]=FLT_MAX;high[k]=-FLT_MAX;}
            for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four input[16],view;if(!NorthlightActorDeformation::sampledVertexInputsAt(shader->second.view,*source,elements,count,i,input)||!NorthlightActorDeformation::evaluateFast(shader->second.view,input,constants,view))return false;
                for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k]))return false;low[k]=std::min(low[k],view[k]);high[k]=std::max(high[k],view[k]);}}
        }else{foundBounds->second.used=++clock;const auto& b=foundBounds->second;
            for(unsigned r=0;r<3;++r){low[r]=high[r]=controls[r][3];for(unsigned k=0;k<3;++k){const float a=controls[r][k]*b.low[k],z=controls[r][k]*b.high[k];low[r]+=std::min(a,z);high[r]+=std::max(a,z);}}}
        float closestSquared=0;for(unsigned r=0;r<3;++r){float v=low[r]>0?low[r]:high[r]<0?high[r]:0;closestSquared+=v*v;}if(closestSquared>=range*range)return false;
        if(shader->second.skinned){const float a=range*range,b=a*.5625f;controls[0][0]=strength*std::clamp((a-closestSquared)/(a-b),0.f,1.f);controls[0][1]=controls[0][2]=controls[0][3]=0;}
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> tex,tex1;if(FAILED(d->GetTexture(0,tex.out()))||!tex.p||tex->GetType()!=D3DRTYPE_TEXTURE)return false;
        if(domain==2&&material==6&&(FAILED(d->GetTexture(1,tex1.out()))||!tex1.p||tex1->GetType()!=D3DRTYPE_TEXTURE||!shader->second.layered))return false;
        DWORD u=0,v=0;d->GetSamplerState(0,D3DSAMP_ADDRESSU,&u);d->GetSamplerState(0,D3DSAMP_ADDRESSV,&v);if((u!=D3DTADDRESS_WRAP&&u!=D3DTADDRESS_CLAMP)||(v!=D3DTADDRESS_WRAP&&v!=D3DTADDRESS_CLAMP))return false;
        const auto revision=NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex.p)),revision1=tex1.p?NorthlightMaterialHeight::version(static_cast<IDirect3DTexture9*>(tex1.p)):0;
        const unsigned address=u|(v<<8);uvKey(shader->second,constants,domain==2&&material==6,uvScratch);
        Entry* overlay=domain==1&&material==2?findOverlay(source,fingerprint,decl.p):nullptr;
        Entry* hit=domain==1&&material==2?nullptr:firstMaterial(materialFingerprint(equivalent?fingerprint:retainedFingerprint(source),shader->second.id,detail,address,domain,material,tex.p,tex1.p,revision,revision1,uvScratch),[&](Entry& e){if(e.sourceDecl!=decl.p||e.domain!=domain)return false;
            return e.program==shader->second.id&&e.detail==detail&&e.address==address&&e.material==material&&e.texture==tex.p&&e.texture1==tex1.p&&e.uvConstants==uvScratch&&e.heightVersion==revision&&e.heightVersion1==revision1&&sameSource(e,source,equivalent);});
        if(hit){current=hit;userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;hit->used=++clock;hit->baseFrame=uint64_t(frames)+1;++hits;return true;}
        if(overlay){current=overlay;userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;overlay->used=++clock;++hits;return true;}
        if((domain==1&&material==2)||buildsThisFrame>=2){++refused;return false;}
        auto* a=height(static_cast<IDirect3DTexture9*>(tex.p),domain==4);if(!a){++heightRejected;return false;}Height first=*a;const Height* second=nullptr;
        if(tex1.p){second=height(static_cast<IDirect3DTexture9*>(tex1.p));if(!second)return false;}
        std::vector<std::array<float,2>> uv,uv1;std::vector<float> weights;
        if(!NorthlightActorDeformation::textureUVs(shader->second.uv,*source,elements,count,constants,uv))return false;
        if(second){if(!NorthlightActorDeformation::textureUVs(shader->second.uv1,*source,elements,count,constants,uv1))return false;
            weights.resize(source->vertexCount);for(unsigned i=0;i<source->vertexCount;++i){NorthlightActorDeformation::Four inputs[16],value;if(!NorthlightActorDeformation::sampledVertexInputsAt(shader->second.blend,*source,elements,count,i,inputs,true)||!NorthlightActorDeformation::evaluateFast(shader->second.blend,inputs,constants,value))return false;if(!std::isfinite(value[3]))return false;weights[i]=value[3];}}
        if(source->primitiveCount>8192)return false;
        unsigned factor=domain==4?1u<<detail:NorthlightDisplacementGeometry::textureFactor(uv,first,detail);
        if(second)factor=std::max(factor,NorthlightDisplacementGeometry::textureFactor(uv1,*second,detail));
        if(domain==3)factor=std::min(factor,16u);
        ++buildsThisFrame;Mesh output;if(!NorthlightDisplacementGeometry::build(*source,elements,count,uv,first,factor,domain==1,u==D3DTADDRESS_CLAMP,v==D3DTADDRESS_CLAMP,output,second,second?&uv1:nullptr,second?&weights:nullptr,shader->second.influences)){++meshRejected;return false;}
        if(builds<8){float peak=0;for(unsigned i=0;i<output.vertexCount;++i){float delta[3];std::memcpy(delta,output.streams[0].bytes.data()+size_t(i)*output.streams[0].stride+stream.stride,12);peak=std::max(peak,std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2])*strength);}logf("DISPLACEMENT build domain=%u vertices=%u triangles=%u maxOffset=%.5f VScontrol=%u",domain,output.vertexCount,output.primitiveCount,peak,shaders.find(original)->second.binding.constant);}
        const auto indexBytes=NorthlightDisplacementGeometry::indexElementBytes(output);
        auto entry=std::make_unique<Entry>();entry->bytes=output.byteSize()-output.indices.size()*(sizeof(uint32_t)-indexBytes);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(!GlobalMemoryStatusEx(&memory)||memory.ullAvailVirtual<entry->bytes+source->byteSize()+(160u<<20)){++refused;return false;}
        refreshBudget(memory);if(entry->bytes+source->byteSize()>cacheBudget){++refused;return false;}
        makeRoom(source,entry->bytes);
        const auto vbBytes=UINT(output.streams[0].bytes.size()),ibBytes=UINT(output.indices.size()*indexBytes);void* data=nullptr;
        if(FAILED(d->CreateVertexBuffer(vbBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&entry->vb,nullptr))||FAILED(entry->vb->Lock(0,vbBytes,&data,NorthlightUpload::FreshBufferLock)))return false;std::memcpy(data,output.streams[0].bytes.data(),vbBytes);if(FAILED(entry->vb->Unlock()))return false;
        if(FAILED(d->CreateIndexBuffer(ibBytes,D3DUSAGE_WRITEONLY,indexBytes==sizeof(uint16_t)?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_DEFAULT,&entry->ib,nullptr))||FAILED(entry->ib->Lock(0,ibBytes,&data,NorthlightUpload::FreshBufferLock)))return false;NorthlightDisplacementGeometry::copyIndices(output,data);if(FAILED(entry->ib->Unlock()))return false;
        D3DVERTEXELEMENT9 outputElements[MAXD3DDECLLENGTH+1];std::copy(elements,elements+count,outputElements);
        outputElements[count-1]={0,WORD(stream.stride),D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,7};outputElements[count]=D3DDECL_END();if(FAILED(d->CreateVertexDeclaration(outputElements,&entry->decl)))return false;
        entry->source=source;entry->sourceDecl=decl.p;entry->sourceDecl->AddRef();entry->texture=static_cast<IDirect3DTexture9*>(tex.p);entry->texture->AddRef();entry->texture1=static_cast<IDirect3DTexture9*>(tex1.p);if(entry->texture1)entry->texture1->AddRef();entry->detail=detail;entry->program=shader->second.id;entry->domain=domain;entry->material=material;entry->uvConstants=uvScratch;entry->address=address;entry->heightVersion=revision;entry->heightVersion1=revision1;entry->vertices=output.vertexCount;entry->primitives=output.primitiveCount;entry->stride=output.streams[0].stride;entry->used=++clock;entry->baseFrame=uint64_t(frames)+1;current=entry.get();userDraw=args.vertices!=nullptr;indexedUserDraw=userDraw&&args.draw.indexed;retain(std::move(entry));++builds;return true;
    }
    template<class Draw> HRESULT draw(Draw original,bool native=false){
        if(!current)return original();
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> vs;if(FAILED(d->GetVertexShader(vs.out()))||!vs.p)return original();auto shader=shaders.find(vs.p);
        // World terrain-coverage variants keep the native input layout but are
        // created by a separate subsystem. Patch each live variant once.
        if(shader==shaders.end()&&shaders.size()<4096){UINT bytes=0;if(SUCCEEDED(vs->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536*4){std::vector<DWORD> words(bytes/4);if(SUCCEEDED(vs->GetFunction(words.data(),&bytes))){bool relative=false;for(auto word:words)if((word&0x80002000u)==0x80002000u&&NorthlightShadowShader::regType(word)==2)relative=true;registerVariant(vs.p,words.data(),words.size(),relative);}}shader=shaders.find(vs.p);}
        if(shader==shaders.end()||!shader->second.patched)return original();
        HRESULT hr=D3D_OK;bool bound=false;
        {
        struct State {IDirect3DDevice9* d;NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> vb;NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> ib;NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> decl;UINT offset=0,stride=0,constant=0,constantCount=4;float values[16];bool changed=false;IDirect3DVertexShader9* vs;
            ~State(){if(changed){d->SetStreamSource(0,vb.p,offset,stride);d->SetIndices(ib.p);d->SetVertexDeclaration(decl.p);d->SetVertexShader(vs);d->SetVertexShaderConstantF(constant,values,constantCount);}}} saved{d,{}, {}, {},0,0,shader->second.binding.constant,shader->second.binding.count,{},false,vs.p};
        if(FAILED(d->GetStreamSource(0,saved.vb.out(),&saved.offset,&saved.stride))||FAILED(d->GetIndices(saved.ib.out()))||FAILED(d->GetVertexDeclaration(saved.decl.out()))||FAILED(d->GetVertexShaderConstantF(saved.constant,saved.values,saved.constantCount)))return original();
        saved.changed=true;
        bound=SUCCEEDED(d->SetStreamSource(0,current->vb,0,current->stride))&&SUCCEEDED(d->SetIndices(current->ib))&&SUCCEEDED(d->SetVertexDeclaration(current->decl))&&SUCCEEDED(d->SetVertexShader(shader->second.patched))&&SUCCEEDED(d->SetVertexShaderConstantF(saved.constant,controls[0],saved.constantCount));
        if(bound){hr=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,current->vertices,0,current->primitives);if(SUCCEEDED(hr)){++draws;++domainDraws[current->domain-1];triangles+=current->primitives;}}
        }
        if(!bound)return original();
        if(native&&userDraw){d->SetStreamSource(0,nullptr,0,0);if(indexedUserDraw)d->SetIndices(nullptr);}
        return hr;
    }
};
