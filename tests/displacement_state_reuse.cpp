#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <new>
#include <unordered_map>
static int failAllocationAfter=-1;
void* operator new(std::size_t n){if(failAllocationAfter==0){failAllocationAfter=-1;throw std::bad_alloc();}if(failAllocationAfter>0)--failAllocationAfter;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#include "forwarders.h"
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static int nativeGpuConfig=-1;
static void logf(const char* format,...){if(!std::strstr(format,"MATERIAL_CONFIG "))return;char line[2048];va_list args;va_start(args,format);std::vsnprintf(line,sizeof line,format,args);va_end(args);const auto* flag=std::strstr(line,"nativeGpuEnabled=");nativeGpuConfig=flag?std::atoi(flag+17):-1;}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "device_mirror.h"
#include "mirror_resource_forwarders.h"

struct DisplacementStateTestAccess {
    using Mesh=NorthlightDrawSnapshot::Mesh;
    static void cache();
    static void rotation(){
        using namespace NorthlightActorDeformation;
        Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0}};
        for(unsigned row=0;row<3;++row)p.operations.push_back({9,0x80000000u|((1u<<row)<<16),{{0x90e40000u,0},{0xa0e40000u|row,0},{}}});
        p.operations.push_back({1,0x80080000u,{{0xa0000003u,0},{},{}}});buildKernel(p);
        NorthlightAutoDisplacement renderer(nullptr);renderer.range=12;renderer.strength=.36f;
        float worstOld=0,worstNew=0,oldOffsetError=0,newOffsetError=0;
        for(float x:{5947.7f,30000.f})for(unsigned step=0;step<1440;++step){
            const float angle=float(step)*.00436332313f,c=std::cos(angle),s=std::sin(angle),y=854.14f,z=148.87f;
            float constants[1024]={};const float rows[12]={c,s,0,-c*x-s*y,-s,c,0,s*x-c*y,0,0,1,-z};
            std::copy(rows,rows+12,constants);constants[12]=1;
            assert(renderer.viewRows(p,constants));
            Four input[16]={},origin;input[0]={0,0,0,1};assert(evaluateFast(p,input,constants,origin));
            float old[3][4];for(unsigned r=0;r<3;++r)old[r][3]=origin[r];
            for(unsigned k=0;k<3;++k){input[0]={0,0,0,1};input[0][k]=1;Four unit;assert(evaluateFast(p,input,constants,unit));for(unsigned r=0;r<3;++r)old[r][k]=unit[r]-origin[r];}
            input[0]={x+10,y,z,1};Four native;assert(evaluateFast(p,input,constants,native));
            float oldSquared=0,newSquared=0,nativeSquared=0;
            for(unsigned r=0;r<3;++r){float before=0,after=0;for(unsigned k=0;k<4;++k){before+=old[r][k]*input[0][k];after+=renderer.controls[r][k]*input[0][k];assert(std::abs(renderer.controls[r][k]-constants[r*4+k])<=2e-10f);}
                oldSquared+=before*before;newSquared+=after*after;nativeSquared+=native[r]*native[r];
                worstOld=std::max(worstOld,std::abs(before-native[r]));worstNew=std::max(worstNew,std::abs(after-native[r]));}
            auto offset=[&](float squared){return .4f*renderer.controls[3][0]*std::clamp(squared*renderer.controls[3][1]+renderer.controls[3][2],0.f,1.f);};
            oldOffsetError=std::max(oldOffsetError,std::abs(offset(oldSquared)-offset(nativeSquared)));
            newOffsetError=std::max(newOffsetError,std::abs(offset(newSquared)-offset(nativeSquared)));
        }
        assert(worstOld>.1f&&worstNew<.004f&&worstNew<worstOld*.01f);
        assert(oldOffsetError>.01f&&newOffsetError<.0001f);
        std::printf("DISPLACEMENT_ROTATION_PASS rotations=2880 strengthPercent=300 oldViewError=%g newViewError=%g oldOffsetError=%g newOffsetError=%g nativeFloatPathUnchanged=1\n",worstOld,worstNew,oldOffsetError,newOffsetError);
    }
    static void boundsCache(){
        NorthlightAutoDisplacement g(nullptr);std::vector<std::shared_ptr<const Mesh>> sources;
        for(unsigned i=0;i<64;++i){auto source=std::make_shared<Mesh>();source->streams[0].bytes.resize(512u<<10);sources.push_back(source);
            NorthlightAutoDisplacement::Bounds b{};b.low[0]=float(i);g.retainBounds(source,std::move(b));}
        assert(g.bounds.size()==64&&g.boundsBytes<(64u<<10));
        for(unsigned repeat=0;repeat<10;++repeat)for(unsigned i=0;i<sources.size();++i){auto hit=g.cachedBounds(sources[i]);assert(hit!=g.bounds.end()&&hit->second.low[0]==float(i));}
        std::weak_ptr<const Mesh> weak=sources[0];sources.clear();assert(weak.expired());
        // Same address, different control block: an expired source cannot prove a new mesh.
        alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto first=std::shared_ptr<const Mesh>(new(storage) Mesh,[](const Mesh* p){p->~Mesh();});
        NorthlightAutoDisplacement::Bounds b{};g.retainBounds(first,std::move(b));first.reset();
        auto replacement=std::shared_ptr<const Mesh>(new(storage) Mesh,[](const Mesh* p){p->~Mesh();});assert(g.cachedBounds(replacement)==g.bounds.end());g.reset();assert(!g.boundsBytes&&g.bounds.empty());
        // The live scene thrashes a 1024-entry cap with tiny metadata records.
        sources.clear();for(unsigned i=0;i<1536;++i){auto source=std::make_shared<Mesh>();sources.push_back(source);NorthlightAutoDisplacement::Bounds bound{};bound.low[0]=float(i);g.retainBounds(source,std::move(bound));}
        const auto misses=g.boundsMisses;for(unsigned repeat=0;repeat<4;++repeat)for(unsigned i=0;i<sources.size();++i){auto hit=g.cachedBounds(sources[i]);assert(hit!=g.bounds.end()&&hit->second.low[0]==float(i));}
        assert(g.boundsMisses==misses&&g.boundsBytes<(256u<<10));
        for(unsigned i=1536;i<5000;++i){auto source=std::make_shared<Mesh>();sources.push_back(source);NorthlightAutoDisplacement::Bounds bound{};g.retainBounds(source,std::move(bound));}
        assert(g.bounds.size()==4096&&g.boundsBytes<=(8u<<20));g.reset();assert(!g.boundsBytes&&g.bounds.empty());
        std::puts("DISPLACEMENT_BOUNDS_WORKING_SET_PASS entries=1536 warmHits=6144 misses=0 entryCap=4096 byteCapMiB=8");
        std::puts("DISPLACEMENT_BOUNDS_CACHE_PASS workingSetMiB=32 boundsKiB_under=64 warmHits=640 weak_lifetime address_reuse reset");
    }
    static void capture();
    static void kernels();
    static void primitiveLimit();
    static void posed(){
        using namespace NorthlightWaterShaderPatch;using namespace NorthlightActorDeformation;
        NorthlightAutoDisplacement g(nullptr);Program p;p.major=3;p.positionRegister=0;p.skinned=true;
        p.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};
        Operation address;address.code=46;address.destination=dst(3,0,1);address.source[0].token=src(1,1,0);p.operations.push_back(address);
        Operation add;add.code=2;add.destination=dst(0,0);add.source[0].token=src(1,0);add.source[1].token=src(2,0)|0x2000;add.source[1].address=src(3,0,0);p.operations.push_back(add);
        const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
        auto mesh=std::make_shared<Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->streams[0].stride=16;mesh->streams[0].bytes.resize(48);
        for(unsigned i=0;i<3;++i){float v[]={float(i),float(i),float(i)};std::memcpy(mesh->streams[0].bytes.data()+16*i,v,12);mesh->streams[0].bytes[16*i+12]=uint8_t(i);}
        std::shared_ptr<const Mesh> source=mesh;float low[3],high[3];
        auto check=[&]{assert(g.distantPoseBounds(p,source,decl,3,low,high));for(unsigned i=0;i<3;++i){Four input[16],v;assert(sampledVertexInputsAt(p,*source,decl,3,i,input)&&evaluateFast(p,input,g.constants,v));for(unsigned k=0;k<3;++k)assert(v[k]>=low[k]&&v[k]<=high[k]);}};
        bool ready=false;for(unsigned f=0;f<20&&!ready;++f){for(unsigned i=0;i<3;++i)g.constants[4*i+2]=100+float(f+i);ready=g.distantPoseBounds(p,source,decl,3,low,high);g.endFrame();}assert(ready);
        for(float z:{200.f,-200.f}){g.endFrame();for(unsigned i=0;i<3;++i)g.constants[4*i+2]=z+float(i);g.poseVerticesThisFrame=8192;g.lastDepthLower=0;check();assert(g.lastDepthLower>100);}
        for(float z:{0.f,3.f}){g.endFrame();for(unsigned i=0;i<3;++i)g.constants[4*i+2]=z+float(i);g.lastDepthLower=0;assert(!g.distantPoseBounds(p,source,decl,3,low,high)&&g.lastDepthLower==0);} // Near bounds/fade remain on the original path.
        auto previous=source;mesh=std::make_shared<Mesh>(*source);float changed=500;for(unsigned i=0;i<3;++i)std::memcpy(mesh->streams[0].bytes.data()+16*i+8,&changed,4);source=mesh;
        ready=false;for(unsigned f=0;f<20&&!ready;++f){g.endFrame();ready=g.distantPoseBounds(p,source,decl,3,low,high);}assert(ready&&low[2]>500); // New immutable generation cannot reuse the near source envelope.
        previous=source;mesh=std::make_shared<Mesh>(*source);changed=0;std::memcpy(mesh->streams[0].bytes.data()+8,&changed,4);source=mesh;g.endFrame();assert(!g.distantPoseBounds(p,source,decl,3,low,high)); // A far vertex alone is not a culling proof.
        Program unsupported=p;unsupported.operations[1].code=6;assert(!g.distantPoseBounds(unsupported,source,decl,3,low,high));
        g.reset();assert(!g.poseEnvelopes.entries()&&!g.envelopeBudget.vertices);
        std::puts("DISPLACEMENT_POSE_ENVELOPE_PASS relative_palette current_pose new_source cold_fallback budget reset");
    }
    static void positionOnly(IDirect3DVertexDeclaration9* declaration){
        NorthlightAutoDisplacement g(nullptr);float vertices[]={1,2,60,0,3,4,61,0,5,6,62,0};Mesh mesh;
        const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,false};
        assert(g.rangeSnapshots.readUP(declaration,draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
        assert(mesh.streams[0].stride==16&&mesh.vertexCount==3&&mesh.streams[1].bytes.empty());
        for(unsigned i=0;i<3;++i)assert(!std::memcmp(mesh.streams[0].bytes.data()+i*16,vertices+i*4,12));
        assert(!g.snapshots.readUP(declaration,draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
        std::puts("MATERIAL_POSITION_ONLY_PASS unused_unbound_stream full_geometry_still_required");
    }
    static void rangeProgram(NorthlightAutoDisplacement& g,IDirect3DVertexShader9* key,bool skinned=false){
        using namespace NorthlightWaterShaderPatch;
        NorthlightAutoDisplacement::Material m;m.first=0;m.count=1;m.skinned=skinned;m.view.major=3;m.view.positionRegister=0;m.view.inputs.push_back({0,D3DDECLUSAGE_POSITION,0});
        NorthlightActorDeformation::Operation copy;copy.code=1;copy.destination=dst(0,0);copy.source[0].token=src(1,0);m.view.operations.push_back(copy);
        NorthlightActorDeformation::Operation move;move.code=2;move.destination=dst(0,0,7);move.source[0].token=src(0,0);move.source[1].token=src(2,0);m.view.operations.push_back(move);
        g.materials[key]=std::move(m);
        g.constants[2]=123;g.constantStamp.valid=true;
    }
    static void rangePreservesPose(const NorthlightAutoDisplacement& g){assert(g.constants[2]==123&&g.constantStamp.valid);}
    static void rangeBudget(NorthlightAutoDisplacement& g,IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){
        std::vector<float> vertices(90000,1.f);NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,30000,0,10000,false},nullptr,D3DFMT_UNKNOWN,vertices.data(),12};float nearest=0;
        for(unsigned i=0;i<8;++i)g.nearestMaterialDepth(shader,args,nearest);
        assert(g.rangeSnapshots.bytesRead()>0&&g.rangeSnapshots.bytesRead()<=(512u<<10));assert(g.snapshots.bytesRead()==0);
        NorthlightDrawSnapshot::Mesh mesh;assert(g.snapshots.readUP(decl,args.draw,nullptr,D3DFMT_UNKNOWN,vertices.data(),12,mesh));
        assert(g.snapshots.bytesRead()==vertices.size()*sizeof(float));g.endFrame();assert(!g.snapshots.bytesRead()&&!g.rangeSnapshots.bytesRead());
        assert(g.snapshots.snapshotCacheLimit()+g.rangeSnapshots.snapshotCacheLimit()+g.skinRangeSnapshots.snapshotCacheLimit()==(32u<<20));
        std::puts("MATERIAL_RANGE_BUDGET_PASS isolated_reads warm_tracking combined_cache_cap");
    }
    static bool read(NorthlightAutoDisplacement& g,bool skinned=true){return g.readConstants(skinned?0:20,skinned?256:4,skinned);}
    static const float* values(const NorthlightAutoDisplacement& g){return g.constants;}
    static bool certified(const NorthlightAutoDisplacement& g){return g.constantStamp.valid;}
    static void declaration(NorthlightAutoDisplacement& g,IDirect3DVertexDeclaration9* d,UINT stride,bool accepted=true,bool indexed=false){
        const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;assert(g.declarations.get(d,e,n));
        float vertices[16]={};NorthlightDrawSnapshot::Mesh mesh;
        const NorthlightDrawSnapshot::Draw draw{D3DPT_TRIANGLELIST,0,0,3,0,1,indexed};const uint16_t indices[]={0,1,2};
        assert(g.snapshots.readUP(d,draw,indexed?indices:nullptr,indexed?D3DFMT_INDEX16:D3DFMT_UNKNOWN,vertices,stride,mesh)==accepted);
        assert(n==2&&e[1].Stream==0xff); // The source end element must never be overwritten.
    }
};
void DisplacementStateTestAccess::cache(){
    using G=NorthlightAutoDisplacement;using Mesh=NorthlightDrawSnapshot::Mesh;using E=G::Entry;
    auto mesh=[](unsigned n){auto p=std::make_shared<Mesh>();p->vertexCount=3;p->primitiveCount=1;p->indexed=true;p->indices={0,1,2};p->streams[0].bytes.resize(256);std::memcpy(p->streams[0].bytes.data(),&n,sizeof n);return std::shared_ptr<const Mesh>(p);};
    auto entry=[](const std::shared_ptr<const Mesh>& s,size_t bytes,uint64_t used,unsigned program=1){auto e=std::make_unique<E>();e->source=s;e->bytes=bytes;e->used=used;e->domain=1;e->program=program;e->baseFrame=1;return e;};
    G g(nullptr);std::shared_ptr<const Mesh> query;
    for(unsigned i=0;i<256;++i){query=mesh(i);g.retain(entry(query,100,i+1));}
    unsigned visits=0;g.candidates(query,0,false,[&](const E& e){assert(e.source==query);++visits;});assert(visits==1);
    g.indexedEntries=false;unsigned linear=0;g.candidates(query,0,false,[&](const E&){++linear;});assert(linear==256);g.indexedEntries=true;
    auto alias=std::make_shared<Mesh>(*query);alias->streams[0].stride=96;alias->streams[1].bytes={1,2,3};
    const auto fingerprint=G::sourceFingerprint(*query);assert(G::sourceFingerprint(*alias)==fingerprint);
    auto find=[&](const std::shared_ptr<const Mesh>& s,bool equivalent,unsigned program=1){return g.firstCandidate(s,G::sourceFingerprint(*s),equivalent,[&](const E& e){return e.program==program&&G::sameSource(e,s,equivalent);});};
    assert(!find(alias,false)&&find(alias,true)->source==query);
    // A sampled-content collision must still compare the unsampled bytes.
    auto collision=std::make_shared<Mesh>(*query);collision->streams[0].bytes[128]=1;assert(G::sourceFingerprint(*collision)==fingerprint);assert(!find(collision,true)&&!g.findOverlay(collision,fingerprint,nullptr));
    auto* original=find(alias,true);g.retain(entry(alias,200,500,2));assert(find(alias,true)==original&&find(alias,true,2)->source==alias);
    original->uvConstants={-0.f};const std::vector<float> zero={0.f},nan={std::numeric_limits<float>::quiet_NaN()};
    assert(g.firstCandidate(query,fingerprint,true,[&](const E& e){return e.uvConstants==zero;})==original);
    assert(!g.firstCandidate(query,fingerprint,true,[&](const E& e){return e.uvConstants==nan;}));
    auto keyed=entry(query,100,501,3);keyed->uvConstants={-0.f};auto* keyedPointer=keyed.get();g.retain(std::move(keyed));
    auto key=[&](const E& e,const std::vector<float>& uv){return G::materialFingerprint(G::sourceFingerprint(*e.source),e.program,e.detail,e.address,e.domain,e.material,e.texture,e.texture1,e.heightVersion,e.heightVersion1,uv);};
    const auto exact=key(*keyedPointer,zero);assert(exact==keyedPointer->materialFingerprint);
    assert(g.firstMaterial(exact,[&](const E& e){return e.uvConstants==zero&&G::sameSource(e,alias,true);})==keyedPointer);
    assert(!g.firstMaterial(exact,[&](const E& e){return e.uvConstants==nan;})); // Even a hash collision cannot authorize NaN equality.
    assert(!g.firstMaterial(exact,[&](const E& e){return G::sameSource(e,collision,true);}));
    for(unsigned field=0;field<10;++field){auto changed=entry(query,100,1,3);changed->uvConstants=zero;
        switch(field){case 0:changed->program=4;break;case 1:changed->detail=1;break;case 2:changed->address=1;break;case 3:changed->domain=2;break;case 4:changed->material=1;break;case 5:changed->texture=reinterpret_cast<IDirect3DTexture9*>(uintptr_t(1));break;case 6:changed->texture1=reinterpret_cast<IDirect3DTexture9*>(uintptr_t(1));break;case 7:changed->heightVersion=1;break;case 8:changed->heightVersion1=1;break;case 9:changed->uvConstants={1};break;}
        const auto changedKey=key(*changed,changed->uvConstants);changed->texture=changed->texture1=nullptr;assert(!g.firstMaterial(changedKey,[](const E&){return true;}));}
    keyedPointer->baseFrame=0;
    // Latest successful base wins, and equal stamps preserve insertion order.
    assert(g.findOverlay(query,fingerprint,nullptr)->source==alias);original->used=500;assert(g.findOverlay(query,fingerprint,nullptr)==original);
    g.candidates(query,fingerprint,true,[&](E& e){if(G::sameSource(e,query,true))e.baseFrame=0;});assert(!g.findOverlay(query,fingerprint,nullptr));
    // Source sharing charges only one payload and releases that charge on its last owner.
    g.reset();auto shared=mesh(42);const auto sourceBytes=shared->byteSize();std::weak_ptr<const Mesh> lifetime=shared;
    for(unsigned i=0;i<10;++i)g.retain(entry(shared,100,i+1));assert(g.resident==1000+sourceBytes);
    for(unsigned i=0;i<9;++i){g.evictOldest();assert(g.resident==(9-i)*100+sourceBytes);assert(g.sourceLookup.size()==9-i&&g.contentLookup.size()==9-i&&g.materialLookup.size()==9-i);}
    shared.reset();assert(!lifetime.expired());g.evictOldest();assert(lifetime.expired()&&g.resident==0&&g.sourceLookup.empty()&&g.contentLookup.empty());
    // Evicting the last old owner while admitting a new variant restores the source charge.
    shared=mesh(7);g.retain(entry(shared,100,1));g.cacheBudget=sourceBytes+110;g.makeRoom(shared,110);assert(g.entries.empty());g.retain(entry(shared,110,2));assert(g.resident==sourceBytes+110);
    auto another=mesh(8);g.retain(entry(another,100,3));g.memoryPressure=true;MEMORYSTATUSEX memory{};memory.ullAvailVirtual=1024ull<<20;
    // Pressure uses the same eviction path, including every candidate index.
    g.entries[0]->bytes=40u<<20;g.resident+=40u<<20;g.resident-=110;g.refreshBudget(memory);assert(g.resident==100+another->byteSize()&&g.entries.size()==1);
    assert(!find(shared,false)&&find(another,false));g.reset();assert(g.resident==0&&g.sourceLookup.empty()&&g.contentLookup.empty());
    // Rewritten tracked terrain keeps a new source identity even when its full
    // payload is unchanged. Private subdivision may reuse the older geometry.
    auto stable=std::make_shared<Mesh>(*mesh(17));stable->streams[0].stride=16;
    stable->sourceIdentity=NorthlightSourceIdentity::make({1,1});
    auto stableEntry=entry(stable,100,1);stableEntry->program=1;g.retain(std::move(stableEntry));
    for(unsigned revision=2;revision<1002;++revision){auto rewritten=std::make_shared<Mesh>(*stable);
        rewritten->sourceIdentity=NorthlightSourceIdentity::make({1,revision});rewritten->replayIdentity=stable->sourceIdentity;
        const auto identity=rewritten->sourceIdentity;
        assert(g.geometrySource(rewritten,nullptr,1,1)==stable&&rewritten->sourceIdentity==identity);
    }
    assert(g.sourceAliases==1000&&g.entries.size()==1);
    for(unsigned change=0;change<14;++change){auto changed=std::make_shared<Mesh>(*stable);changed->replayIdentity=stable->sourceIdentity;
        auto* declaration=static_cast<IDirect3DVertexDeclaration9*>(nullptr);unsigned program=1,domain=1;
        switch(change){case 0:changed->replayIdentity.reset();break;case 1:++changed->vertexCount;break;case 2:++changed->primitiveCount;break;
        case 3:changed->topology=D3DPT_TRIANGLESTRIP;break;case 4:changed->indexed=false;break;case 5:changed->indices[0]=2;break;
        case 6:changed->streams[0].bytes[128]^=1;break;case 7:++changed->streams[0].stride;break;case 8:changed->streams[1].bytes={1};break;
        case 9:++changed->streams[1].stride;break;case 10:declaration=reinterpret_cast<IDirect3DVertexDeclaration9*>(uintptr_t(1));break;
        case 11:program=2;break;case 12:domain=2;break;case 13:changed->streams[3].bytes={1};break;}
        assert(g.geometrySource(changed,declaration,program,domain)==changed);
    }
    g.reset();assert(g.geometrySource(stable,nullptr,1,1)==stable);
    std::puts("DISPLACEMENT_REWRITTEN_SOURCE_PASS revisions=1000 aliases=1000 full_payload_layout_program_controls=14 current_write_identity_preserved");
    // Both partial index-population failures must leave the ordered fallback usable.
    for(int failure=0;failure<3;++failure){g.entries.reserve(4);g.sourceLookup.reserve(4);g.materialLookup.reserve(4);auto next=entry(shared,100,1);failAllocationAfter=failure;g.retain(std::move(next));assert(failAllocationAfter==-1&&!g.indexedEntries&&g.sourceLookup.empty()&&g.contentLookup.empty()&&g.materialLookup.empty());assert(find(shared,false)&&g.firstMaterial(0,[](const E&){return true;}));g.reset();}
    {G empty(nullptr);auto next=entry(shared,100,1);failAllocationAfter=0;bool failed=false;try{empty.retain(std::move(next));}catch(const std::bad_alloc&){failed=true;}assert(failed&&empty.resident==0&&empty.entries.empty()&&empty.sourceLookup.empty());}
    // Unknown and future errors remain visible rather than disappearing from the total.
    for(unsigned i=0;i<g.snapshotReasons.size();++i){NorthlightDrawSnapshot::Diagnostics why;why.error=NorthlightDrawSnapshot::Error(i);assert(!g.snapshotFailure(why));}
    NorthlightDrawSnapshot::Diagnostics unknown;unknown.error=NorthlightDrawSnapshot::Error(999);g.snapshotFailure(unknown);uint64_t sum=0;for(auto count:g.snapshotReasons)sum+=count;assert(sum==g.snapshotRejected&&g.snapshotReasons[0]==2);
    std::printf("DISPLACEMENT_LOOKUP_PASS indexedVisits=%u linearVisits=%u sharedBytes=%zu repeatedChargeBytes=%zu\n",visits,linear,1000+sourceBytes,10*(100+sourceBytes));
}
struct Backend:ForwardIDirect3DDevice9 {
    float bank[1024]={};bool fail=false;IDirect3DDevice9* mutate=nullptr;
    Backend():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{
        if(fail)return D3DERR_INVALIDCALL;std::memcpy(out,bank+first*4,count*16);
        if(mutate){auto* writer=mutate;mutate=nullptr;const float changed[4]={21,22,23,24};assert(SUCCEEDED(writer->SetVertexShaderConstantF(64,changed,1)));}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT first,const float* in,UINT count)override{if(fail)return D3DERR_INVALIDCALL;std::memcpy(bank+first*4,in,count*16);return D3D_OK;}
};
struct Meter:ForwardIDirect3DDevice9 {
    unsigned calls=0,registers=0;bool fail=false;explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* out,UINT count)override{++calls;registers+=count;return fail?D3DERR_INVALIDCALL:real->GetVertexShaderConstantF(first,out,count);}
};
struct Declaration:IDirect3DVertexDeclaration9 {
    unsigned refs=1,reads=0;D3DVERTEXELEMENT9 elements[2];bool fail=false;
    explicit Declaration(WORD offset=0,WORD stream=0):elements{{stream,offset,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()}{}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{++reads;if(fail||*count<2)return D3DERR_INVALIDCALL;std::memcpy(out,elements,sizeof elements);*count=2;return D3D_OK;}
};
struct RangeBackend:Backend {
    Declaration decl;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&decl;decl.AddRef();return D3D_OK;}
};
static void capturedPositionBounds(){
    struct Buffered:RangeBackend {
        unsigned bufferQueries=0;
        HRESULT STDMETHODCALLTYPE GetStreamSource(UINT,IDirect3DVertexBuffer9**,UINT*,UINT*)override{++bufferQueries;return D3DERR_INVALIDCALL;}
    } backend;
    NorthlightAutoDisplacement g(&backend);auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(uintptr_t(1));
    DisplacementStateTestAccess::rangeProgram(g,shader);
    float vertices[]={0,0,50000,1,0,50001,0,1,50002};
    NorthlightAutoDisplacement::Args up{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,12},buffered=up;buffered.vertices=nullptr;
    NorthlightCaptureMetadata::PositionBounds bounds;bounds.owner=&buffered;bounds.shader=shader;bounds.known=true;
    bounds.low[2]=50000;bounds.high[0]=bounds.high[1]=1;bounds.high[2]=50002;
    for(bool geometry:{false,true})for(float distance:{0.f,7.99f,8.f,8.01f,15.99f,16.f,16.01f,47.99f,48.f,48.01f,60.f,150.f}){
        backend.bank[2]=distance-50000;float expected=0,actual=0;
        assert(g.nearestMaterialDepth(shader,up,expected,geometry));
        assert(g.nearestMaterialDepth(shader,buffered,actual,geometry,&bounds)&&!std::memcmp(&actual,&expected,sizeof actual)&&!backend.bufferQueries);
    }
    for(unsigned invalid=0;invalid<5;++invalid){auto bad=bounds;float nearest=0;const auto before=backend.bufferQueries;
        if(invalid==0)bad.owner=&up;if(invalid==1)bad.shader=nullptr;if(invalid==2)bad.known=false;if(invalid==3)bad.low[2]=NAN;if(invalid==4)bad.high[2]=bad.low[2]-1;
        assert(!g.nearestMaterialDepth(shader,buffered,nearest,true,&bad)&&backend.bufferQueries>before);
    }
    DisplacementStateTestAccess::rangeProgram(g,shader,true);float nearest=0;assert(!g.nearestMaterialDepth(shader,buffered,nearest,true,&bounds));
    std::puts("CAPTURED_POSITION_BOUNDS_PASS exact_current_transform thresholds_8_16_48 no_buffer_queries current_draw_shader_validity finite_bounds skin_fallback");
}
struct SplitDeclaration:Declaration {
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
        if(*count<3)return D3DERR_INVALIDCALL;const D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{1,0,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};std::memcpy(out,e,sizeof e);*count=3;return D3D_OK;
    }
};
template<class Interface> struct CaptureShader:Interface {
    unsigned refs=1,reads=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetFunction(void*,UINT*)override{++reads;return E_FAIL;}
};
struct CaptureSurface:IDirect3DSurface9 {
    unsigned refs=1;UINT size=64;D3DMULTISAMPLE_TYPE samples=D3DMULTISAMPLE_2_SAMPLES;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID)override{return E_FAIL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD)override{return 0;}
    DWORD STDMETHODCALLTYPE GetPriority()override{return 0;}
    void STDMETHODCALLTYPE PreLoad()override{}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_SURFACE;}
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID,void**)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* out)override{*out={D3DFMT_A8R8G8B8,D3DRTYPE_SURFACE,D3DUSAGE_RENDERTARGET,D3DPOOL_DEFAULT,samples,0,size,size};return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT*,const RECT*,DWORD)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE UnlockRect()override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetDC(HDC*)override{return E_FAIL;}
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC)override{return E_FAIL;}
};
struct CaptureBackend:Backend {
    CaptureShader<IDirect3DVertexShader9> vs;CaptureShader<IDirect3DPixelShader9> ps;CaptureSurface surface;
    struct SkinDeclaration:Declaration {
        HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
            if(*count<3)return D3DERR_INVALIDCALL;
            const D3DVERTEXELEMENT9 e[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};std::memcpy(out,e,sizeof e);*count=3;return D3D_OK;
        }
    } decl;
    unsigned pixelQueries=0,pixelConstants=0,textures=0,samplers=0,depthAllocations=0;DWORD colorWrite=15,depthWrite=TRUE;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out)override{++pixelQueries;*out=&ps;ps.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&decl;decl.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out)override{*out={0,0,surface.size,surface.size,0,1};return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** out)override{*out=n?nullptr:&surface;if(n)return D3DERR_NOTFOUND;surface.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out)override{*out=&surface;surface.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{
        *out=key==D3DRS_ZENABLE?TRUE:key==D3DRS_ZWRITEENABLE?depthWrite:key==D3DRS_ZFUNC?D3DCMP_LESSEQUAL:key==D3DRS_COLORWRITEENABLE?colorWrite:0;return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT,float*,UINT)override{++pixelConstants;return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD*)override{++samplers;return E_FAIL;}
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD,IDirect3DBaseTexture9** out)override{++textures;*out=nullptr;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*)override{++depthAllocations;return E_FAIL;}
};
struct LimitTexture:NorthlightMirrorResources::ForwardIDirect3DTexture9 {
    static MirrorGate& testGate(){static MirrorGate gate;return gate;}
    unsigned refs=1,levels=0;LimitTexture():ForwardIDirect3DTexture9(nullptr,testGate(),[](void*,const char*){},nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_TEXTURE;}
    DWORD STDMETHODCALLTYPE GetLevelCount()override{return 1;}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT,D3DSURFACE_DESC*)override{++levels;return E_FAIL;}
};
struct LimitBackend:CaptureBackend {
    LimitTexture texture;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage,IDirect3DBaseTexture9** out)override{++textures;*out=stage?nullptr:&texture;if(*out)(*out)->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD* out)override{++samplers;*out=D3DTADDRESS_WRAP;return D3D_OK;}
};
void DisplacementStateTestAccess::kernels(){
    using namespace NorthlightActorDeformation;using namespace NorthlightWaterShaderPatch;
    CaptureBackend d;NorthlightAutoDisplacement g(&d);NorthlightAutoDisplacement::Material m;m.skinned=true;m.id=1;m.count=256;m.view.major=3;m.view.positionRegister=0;m.view.skinned=true;m.view.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};m.view.definitions={{0,{3,1,0,0}}};
    auto op=[&](unsigned code,Word target,Word a,Word b=0,Word address=0){Operation o;o.code=code;o.destination=target;o.source[0].token=a;o.source[0].address=address;o.source[1].token=b;m.view.operations.push_back(o);};
    op(1,dst(0,0,8),src(2,0,0x55));op(5,dst(0,1,1),src(2,0,0),src(1,1,0));op(46,dst(3,0,1),src(0,1,0));
    for(unsigned axis=0;axis<3;++axis){op(9,dst(0,0,1u<<axis),src(2,31+axis)|0x2000,src(1,0),src(3,0,0));d.bank[(31+axis)*4+axis]=1;}
    buildKernel(m.view);assert(m.view.kernel.singleSkin()&&!m.view.kernel.skin);g.materials[&d.vs]=std::move(m);g.shaders[&d.vs].patched=&d.vs;d.vs.AddRef();g.envelopeBudget.maxVertices=g.envelopeBudget.maxOperations=0;
    struct V{float x,y,z;unsigned bone;} v[]={{1,2,100,0},{2,3,100,0},{3,4,100,0}};NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,v,16};
    for(unsigned pass=0;pass<2;++pass){g.materials[&d.vs].view.kernel.evaluator=pass?Kernel::SingleFirst:Kernel::Generic;
        assert(!g.prepare(&d.vs,4,1,args)&&g.poseKernelVertices[pass]==3&&g.poseVerticesThisFrame==3);
        for(unsigned i=0;i<4;++i)assert(g.poseKernelVertices[i]==(i==pass?3u:0u));g.endFrame();for(auto count:g.poseKernelVertices)assert(count==0);}
    std::puts("MATERIAL_POSE_KERNEL_COUNTER_PASS exact_generic_single_vertices unchanged_budget far_return frame_reset");
}
void DisplacementStateTestAccess::primitiveLimit(){
    LimitBackend d;NorthlightAutoDisplacement g(&d);rangeProgram(g,&d.vs);g.materials[&d.vs].id=1;
    g.materials[&d.vs].uv=g.materials[&d.vs].view;g.materials[&d.vs].uv.textureCoordinates=true;
    d.vs.AddRef();g.shaders[&d.vs].patched=&d.vs;
    std::vector<float> vertices(8193*3*4);for(unsigned i=0;i<8193*3;++i)vertices[i*4+3]=1;
    NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,8193*3,0,8193,false},nullptr,D3DFMT_UNKNOWN,vertices.data(),16};
    assert(!g.prepare(&d.vs,1,1,args)&&g.primitiveRejected==1&&g.readsThisFrame==0&&d.texture.levels==0&&g.textures.empty()&&!g.buildsThisFrame);
    // Existing exact entries retain priority over the cold-build limit.
    Mesh mesh;assert(g.snapshots.readUP(&d.decl,args.draw,nullptr,D3DFMT_UNKNOWN,vertices.data(),16,mesh));
    // Measure only the existing UV evaluator's avoidable work, not a game FPS
    // prediction. This uses the same source and function the old gate followed.
    const D3DVERTEXELEMENT9* elements=nullptr;UINT elementCount=0;assert(g.declarations.get(&d.decl,elements,elementCount));
    std::array<double,3> uvMs;for(auto& ms:uvMs){const auto begin=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<16;++repeat){std::vector<std::array<float,2>> uv;assert(NorthlightActorDeformation::textureUVs(g.materials[&d.vs].uv,mesh,elements,elementCount,g.constants,uv)&&uv.size()==mesh.vertexCount);}
        ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/16;}
    std::sort(uvMs.begin(),uvMs.end());std::printf("MATERIAL_REJECTED_UV_BENCH_PASS vertices=%u old_uv_work_median_ms=%.6f new_uv_calls=0\n",mesh.vertexCount,uvMs[1]);
    auto entry=std::make_unique<NorthlightAutoDisplacement::Entry>();entry->source=std::make_shared<Mesh>(std::move(mesh));entry->sourceDecl=&d.decl;d.decl.AddRef();
    entry->program=1;entry->domain=1;entry->material=1;entry->detail=g.worldDetail;entry->address=D3DTADDRESS_WRAP|(D3DTADDRESS_WRAP<<8);entry->texture=&d.texture;d.texture.AddRef();
    g.retain(std::move(entry));assert(g.prepare(&d.vs,1,1,args)&&g.prepared()&&g.hits==1&&g.primitiveRejected==1&&d.texture.levels==0);
    g.reset();args.draw.vertices=8192*3;args.draw.primitives=8192;
    assert(!g.prepare(&d.vs,1,1,args)&&g.primitiveRejected==1&&g.readsThisFrame==1&&d.texture.levels>0&&!g.buildsThisFrame);
    std::puts("MATERIAL_PRIMITIVE_LIMIT_PASS over8192_zero_height_reads zero_uv zero_build existing_hit_preserved boundary8192_retained");
}
struct MaterialCaptureStateTestAccess {
    static void inactiveNativeProfile(){
        struct ProfileDevice:CaptureBackend {
            unsigned creates=0,live=0,timestamps=0;
            static uint64_t& clockReads(){static uint64_t reads=0;return reads;}
            static uint64_t clock(){return ++clockReads();}
            struct Query:IDirect3DQuery9 {
                ProfileDevice& d;D3DQUERYTYPE type;unsigned refs=1;
                Query(ProfileDevice& device,D3DQUERYTYPE t):d(device),type(t){++d.live;}
                HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
                ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
                ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n){--d.live;delete this;}return n;}
                HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{return E_FAIL;}
                D3DQUERYTYPE STDMETHODCALLTYPE GetType()override{return type;}
                DWORD STDMETHODCALLTYPE GetDataSize()override{return type==D3DQUERYTYPE_TIMESTAMPDISJOINT?4:8;}
                HRESULT STDMETHODCALLTYPE Issue(DWORD)override{d.timestamps+=type==D3DQUERYTYPE_TIMESTAMP;return D3D_OK;}
                HRESULT STDMETHODCALLTYPE GetData(void*,DWORD,DWORD)override{return S_FALSE;}
            };
            HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** out)override{++creates;*out=new Query(*this,type);return D3D_OK;}
        } d;
        {NorthlightMaterialProfile profile(&d,ProfileDevice::clock);NorthlightAutoBump b(&d);setup(b,d,4);b.setProfiler(&profile);profile.beginFrame(127,true);assert(d.creates==2);
            std::vector<unsigned> submitted;for(unsigned phase=0;phase<3;++phase){b.enabled=phase==1;b.profileConfigFrame=~uint64_t(0);b.beginDraw();assert(nativeGpuConfig==(phase==1?1:0));const unsigned before=d.timestamps;const auto reads=ProfileDevice::clockReads();
                for(unsigned i=0;i<32;++i){if(!b.enabled)b.capture(&d.vs,[]{return D3D_OK;});else b.profileTag=19;
                    const HRESULT expected=i&1?E_FAIL:D3D_OK;assert(b.nativeDraw([&]{submitted.push_back(phase*32+i);return expected;})==expected);}
                assert(d.timestamps-before==(phase==1?64u:0u));assert(ProfileDevice::clockReads()-reads==(phase==1?192u:128u));}
            for(unsigned i=0;i<96;++i)assert(submitted[i]==i);assert(d.creates==66&&!d.pixelQueries&&!d.pixelConstants&&!d.textures&&!d.samplers&&!d.depthAllocations);
            profile.endFrame();profile.reset();assert(!d.live);
        }assert(d.vs.refs==1);
        std::puts("MATERIAL_INACTIVE_NATIVE_PROFILE_PASS off_on_off_timestamp_issues=0,64,0 native_draws=96 exact_returns cpu_admission_retained no_render_state_changes query_refs_released");
    }
    static void lazyVertex(){
        using namespace NorthlightWaterShaderPatch;
        struct Source:CaptureShader<IDirect3DVertexShader9> {
            std::vector<DWORD> code{0xfffe0300,0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000000,dst(6,0),0x02000001,dst(6,0),src(1,0),65535};
            bool fail=false,shortRead=false;
            uint64_t hash()const{return fnv1a64(reinterpret_cast<const uint32_t*>(code.data()),code.size());}
            HRESULT STDMETHODCALLTYPE GetFunction(void* out,UINT* bytes)override{++reads;if(fail)return E_FAIL;const UINT n=UINT(code.size()*4);if(out){if(*bytes<n)return E_FAIL;std::memcpy(out,code.data(),n);}*bytes=n-(out&&shortRead?4:0);return D3D_OK;}
        };
        struct Device:CaptureBackend {
            bool fail=false;std::vector<std::vector<DWORD>> created;
            HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*)override{return E_FAIL;}
            HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out)override{
                size_t n=1;while((code[n]&65535)!=65535)n+=1+((code[n]&65535)==65534?(code[n]>>16)&32767:(code[n]>>24)&15);
                created.emplace_back(code,code+n+1);*out=fail?nullptr:&vs;if(*out)(*out)->AddRef();return fail?E_FAIL:D3D_OK;
            }
        };
        auto enable=[](NorthlightAutoBump& b){b.enabled=true;b.modes[3]=1;b.strength=1;};
        Device eager;Source baseline;
        {NorthlightAutoBump b(&eager);enable(b);b.registerVertex(&baseline,baseline.hash(),baseline.code.data(),baseline.code.size(),3);assert(b.vertices.at(&baseline).patched&&b.vertices.at(&baseline).kind==4);}
        assert(!eager.created.empty()&&baseline.refs==1&&eager.vs.refs==1);
        Device lazy;Source source;
        {NorthlightAutoBump b(&lazy);b.modes[3]=1;b.registerVertex(&source,source.hash(),source.code.data(),source.code.size(),3);
            assert(!source.reads&&source.refs==2&&lazy.created.empty()&&b.pendingVertices==1&&b.cold.vertexQueued==1&&!b.cold.vertexSources);
            b.reset();assert(source.refs==2&&b.pendingVertices==1);enable(b);
            b.capture(&source,[]{return D3D_OK;});assert(b.vertices.at(&source).patched&&!b.pendingVertices&&source.reads==2&&lazy.created==eager.created);
            const unsigned reads=source.reads;const size_t created=lazy.created.size();b.capture(&source,[]{return D3D_OK;});assert(source.reads==reads&&lazy.created.size()==created);
        }assert(source.refs==1&&lazy.vs.refs==1);
        // A disabled domain also defers even while another domain is enabled.
        {Device d;Source s;NorthlightAutoBump b(&d);b.enabled=true;b.modes[0]=1;b.modes[3]=0;b.registerVertex(&s,s.hash(),s.code.data(),s.code.size(),3);assert(!s.reads&&d.created.empty());enable(b);assert(b.ensureVertex(&s,b.vertices.at(&s)));assert(d.created==eager.created);}
        // Read/identity/driver/allocation failures cannot compile the wrong program or retry each draw.
        for(unsigned failure=0;failure<5;++failure){Device d;Source s;
            {NorthlightAutoBump b(&d);b.registerVertex(&s,s.hash()+(failure==2),s.code.data(),s.code.size(),3);enable(b);s.fail=failure==0;s.shortRead=failure==1;d.fail=failure==3;if(failure==4)failAllocationAfter=0;
                assert(!b.ensureVertex(&s,b.vertices.at(&s)));failAllocationAfter=-1;const auto reads=s.reads;const auto creates=d.created.size();assert(!b.ensureVertex(&s,b.vertices.at(&s))&&s.reads==reads&&d.created.size()==creates);
                assert(b.pendingVertices==1&&s.refs==2&&b.vertices.at(&s).failed);if(failure!=3)assert(d.created.empty());
            }assert(s.refs==1&&d.vs.refs==1);
        }
        {Device d;Source s;NorthlightAutoBump b(&d);for(unsigned i=0;i<8;++i){b.registerVertex(&s,s.hash(),s.code.data(),s.code.size(),3);assert(s.refs==2&&b.pendingVertices==1);}b.registerVertex(&s,0,s.code.data(),s.code.size(),0);assert(s.refs==1&&!b.pendingVertices&&b.vertices.empty());
            failAllocationAfter=0;bool failed=false;try{b.registerVertex(&s,s.hash(),s.code.data(),s.code.size(),3);}catch(const std::bad_alloc&){failed=true;}failAllocationAfter=-1;assert(failed&&s.refs==1&&b.vertices.empty());}
        // Live identity recovery cannot let unsupported deferred entries exhaust supported capacity.
        struct Identity {Source* valid=nullptr;uint64_t hash=0;unsigned calls=0;};
        auto lookup=[](void* context,IDirect3DVertexShader9* shader,uint64_t& hash,unsigned& family){auto& i=*static_cast<Identity*>(context);++i.calls;if(shader!=i.valid)return false;hash=i.hash;family=3;return true;};
        {Device d;Source good,unknown;std::vector<std::unique_ptr<Source>> rejected;rejected.reserve(4096);Identity identity{&good,good.hash()};
            {NorthlightAutoBump b(&d);b.setVertexIdentity(&identity,lookup);
                for(unsigned i=0;i<4096;++i){rejected.push_back(std::make_unique<Source>());auto& s=*rejected.back();s.fail=true;b.registerVertex(&s,s.hash(),s.code.data(),s.code.size(),3);}
                b.registerVertex(&good,good.hash(),good.code.data(),good.code.size(),3);assert(b.vertexOverflow&&b.vertices.size()==4096&&b.pendingVertices==4096&&!good.reads&&good.refs==1&&d.created.empty());
                // Off remains inert, including the overflow callback.
                b.capture(&good,[]{return D3D_OK;});assert(!identity.calls&&!good.reads);enable(b);
                b.capture(&unknown,[]{return D3D_OK;});assert(identity.calls==1&&!unknown.reads&&d.created.empty());
                b.capture(&good,[]{return D3D_OK;});assert(good.reads==2&&b.vertices.at(&good).patched&&b.vertices.size()==4096&&b.pendingVertices==4095&&d.created==eager.created);
                size_t retained=0;for(auto& s:rejected)retained+=s->refs==2;assert(retained==4095);
            }for(auto& s:rejected)assert(s->refs==1);assert(good.refs==1&&d.vs.refs==1);
        }
        {Device d;Source good;Identity identity{&good,good.hash()+1};NorthlightAutoBump b(&d);b.vertexOverflow=true;b.setVertexIdentity(&identity,lookup);enable(b);b.capture(&good,[]{return D3D_OK;});assert(good.reads==2&&d.created.empty()&&b.vertices.at(&good).failed);}
        {Device d;Source good;std::vector<std::unique_ptr<Source>> compiled;compiled.reserve(4096);Identity identity{&good,good.hash()};
            {NorthlightAutoBump b(&d);enable(b);b.vertexOverflow=true;b.setVertexIdentity(&identity,lookup);for(unsigned i=0;i<4096;++i){compiled.push_back(std::make_unique<Source>());d.vs.AddRef();b.vertices[compiled.back().get()]={&d.vs,4};}
                b.capture(&good,[]{return D3D_OK;});assert(b.vertices.size()==4096&&!good.reads&&d.created.empty()&&!b.pendingVertices);}
            assert(d.vs.refs==1);
        }
        // Replacement removes retained identity before allocator address reuse.
        {Device d;NorthlightAutoBump b(&d);alignas(Source) unsigned char storage[sizeof(Source)];auto* s=new(storage) Source;b.registerVertex(s,s->hash(),s->code.data(),s->code.size(),3);assert(s->refs==2);b.registerVertex(s,0,s->code.data(),s->code.size());assert(s->refs==1);s->~Source();s=new(storage) Source;s->code[9]=src(1,0,0);b.registerVertex(s,s->hash(),s->code.data(),s->code.size(),3);enable(b);assert(b.ensureVertex(s,b.vertices.at(s)));b.registerVertex(s,0,s->code.data(),s->code.size());assert(s->refs==1);s->~Source();}
        std::puts("MATERIAL_LAZY_VERTEX_PASS off_zero_getfunction_create exact_eager_bytecode off_on_first_capture domain_toggle reset cached_failures allocation refs replacement_aba cap4096 overflow_hash_proof compiled_cap_retained");
    }
    static NorthlightAutoDisplacement& setup(NorthlightAutoBump& b,CaptureBackend& d,unsigned domain){
        b.enabled=true;b.width=b.heightPixels=d.surface.size;
        for(auto& mode:b.modes)mode=0;b.modes[domain-1]=3;
        d.vs.AddRef();b.vertices[&d.vs]={&d.vs,domain};b.pixels[&d.ps]=1;return b.displacement;
    }
    static unsigned rejected(const NorthlightAutoBump& b){return b.rangeRejected;}
    static void noCapture(){
        CaptureBackend d;NorthlightAutoBump b(&d);setup(b,d,1);b.strength=0;b.occlusionControl[0]=b.occlusionControl[2]=0;b.modes[0]=2;assert(b.active());
        unsigned draws=0;b.capture(&d.vs,[&]{++draws;return D3D_OK;});assert(!draws&&!d.pixelQueries&&!d.pixelConstants&&!d.samplers&&!d.textures&&!d.depthAllocations);
        b.modes[0]=1;assert(!b.active());b.modes[0]=0;b.occlusionControl[0]=1;assert(!b.active());
        std::puts("MATERIAL_NO_CONTRIBUTION_PASS no_capture_setup native_pom_independent all_domains_off");
    }
    static void disabledAndCold(){
        struct ColdBackend:CaptureBackend {bool success=false;HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out)override{*out=success?&ps:nullptr;if(*out)(*out)->AddRef();return success?D3D_OK:E_FAIL;}} d;
        struct NativeSource:CaptureShader<IDirect3DPixelShader9> {HRESULT STDMETHODCALLTYPE GetFunction(void* out,UINT* bytes)override{
            using namespace NorthlightWaterShaderPatch;const DWORD code[]={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x02000001,dst(8,0),src(2,0),65535};
            ++reads;if(out){if(*bytes<sizeof code)return E_FAIL;std::memcpy(out,code,sizeof code);}*bytes=sizeof code;return D3D_OK;}} source;
        {NorthlightAutoBump b(&d);setup(b,d,4);b.enabled=false;
            unsigned native=0;b.beginDraw();b.capture(&d.vs,[&]{++native;return D3D_OK;});assert(b.nativeDraw([&]{++native;return D3D_OK;})==D3D_OK&&native==1);
            b.beforeTextureWrite(&d.ps);assert(b.finishCaptures()&&!b.pendingTextures());
            assert(!d.pixelQueries&&!d.pixelConstants&&!d.textures&&!d.samplers&&!d.depthAllocations&&b.deferred.empty()&&b.deferredSnapshots.bytesRead()==0);
            assert(!b.modelCapture(&d.ps,false));assert(b.cold.modelMisses==1&&!b.cold.modelBuilt);const double measured=b.cold.modelMs;
            for(unsigned i=0;i<16;++i)assert(!b.modelCapture(&d.ps,false));assert(b.cold.modelMisses==1&&b.cold.modelMs==measured);
            const DWORD invalidVS[]={0xfffe0300,65535};b.registerVertex(&d.vs,0,invalidVS,2,3);assert(!b.cold.vertexSources&&b.cold.vertexQueued==1&&b.pendingVertices==1);
            assert(!b.modelCapture(&source,false)&&b.cold.modelMisses==2&&!b.cold.modelBuilt);
            const auto sourceReads=source.reads;const auto modelMs=b.cold.modelMs;assert(!b.modelCapture(&source,false)&&source.reads==sourceReads&&b.cold.modelMs==modelMs);
            IDirect3DPixelShader9* output=nullptr;assert(FAILED(b.createCaptureShader(invalidVS,&output))&&!output);d.success=true;
            assert(SUCCEEDED(b.createCaptureShader(invalidVS,&output))&&output);drop(output);assert(b.cold.staticCreates==2&&b.cold.staticBuilt==1);
            assert(b.modelCapture(&source,true)&&b.cold.modelMisses==3&&b.cold.modelBuilt==1);
            b.endFrame();assert(!b.cold.vertexSources&&!b.cold.modelMisses&&!b.cold.staticCreates);
        }
        assert(d.ps.refs==1&&d.vs.refs==1&&source.refs==1);
        std::puts("MATERIAL_DISABLED_PASS no_capture snapshots textures state_queries barriers native_draw_retained");
        std::puts("MATERIAL_COLD_PROFILE_PASS disabled_vertex_registration cached_failure_no_retiming static_creation_success_failure frame_reset");
    }
    static void effectiveRange(){
        CaptureBackend d;NorthlightAutoBump b(&d);auto& g=setup(b,d,4);b.modes[3]=1;
        assert(b.captureRange(4)==48);b.strength=0;assert(b.captureRange(4)==8);b.occlusionControl[2]=0;assert(!b.captureNeeded(4));
        b.modes[3]=2;b.occlusionLight[3]=1;assert(b.captureRange(4)==8);b.modes[3]=3;assert(!b.captureNeeded(4));
        b.strength=.75f;b.modes[3]=1;DisplacementStateTestAccess::rangeProgram(g,&d.vs);
        float vertices[]={0,0,60,0,1,0,60,0,0,1,60,0};NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
        unsigned draws=0;b.beginDraw();b.capture(&d.vs,[&]{++draws;return D3D_OK;},true,&args);
        assert(b.rangeRejected==1&&!d.pixelQueries&&!d.textures&&!d.samplers&&!d.depthAllocations&&!draws);
        // A large object crossing the visible range cannot be rejected as a whole.
        b.endFrame();vertices[2]=1;b.beginDraw();b.capture(&d.vs,[&]{++draws;return D3D_OK;},true,&args);assert(!b.rangeRejected&&d.pixelQueries>0);
        // Early displacement admission must preserve its own consumer even
        // when normal detail has a shorter range.
        b.endFrame();b.modes[3]=3;b.range=4;for(unsigned i=0;i<3;++i)vertices[4*i+2]=6;
        b.beginDraw();b.capture(&d.vs,[&]{++draws;return D3D_OK;},true,&args);assert(!b.earlyRangeRejected&&d.pixelQueries>0);
        b.endFrame();for(unsigned i=0;i<3;++i)vertices[4*i+2]=10;const auto queries=d.pixelQueries;
        b.beginDraw();b.capture(&d.vs,[&]{++draws;return D3D_OK;},true,&args);assert(b.earlyRangeRejected==1&&d.pixelQueries==queries);
        std::puts("MATERIAL_EARLY_DISPLACEMENT_PASS max_capture_geometry_range far_zero_state_queries near_consumer_retained");
        std::puts("MATERIAL_EFFECT_RANGE_PASS normal_48 cavity_shadow_shared_8 inactive_0 rigid_m2_far_skipped crossing_retained");
    }
    static void invisible(){
        for(unsigned domain:{1u,4u})for(bool depthOnly:{false,true}){if(domain==1&&!depthOnly)continue;
            CaptureBackend d;NorthlightAutoBump b(&d);setup(b,d,domain);d.colorWrite=depthOnly?0:15;d.depthWrite=depthOnly?TRUE:FALSE;
            unsigned draws=0;b.capture(&d.vs,[&]{++draws;return D3D_OK;});assert(!draws&&!d.pixelQueries&&!d.pixelConstants&&!d.samplers&&!d.textures&&!d.decl.reads&&!d.depthAllocations);}
        std::puts("MATERIAL_INVISIBLE_GATE_PASS depth_only noncaptured_translucency no_shader_queries no_geometry_reads");
    }
};
void DisplacementStateTestAccess::capture(){
    MaterialCaptureStateTestAccess::effectiveRange();
    using namespace NorthlightWaterShaderPatch;using namespace NorthlightActorDeformation;
    for(unsigned domain:{1u,4u})for(bool skinned:{false,true})for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES}){
        CaptureBackend d;d.surface.samples=samples;{
            NorthlightAutoBump b(&d);auto& g=MaterialCaptureStateTestAccess::setup(b,d,domain);
            NorthlightAutoDisplacement::Material m;m.skinned=true;m.id=1;m.count=3;m.view.major=3;m.view.positionRegister=0;m.view.skinned=true;
            m.view.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};
            Operation address;address.code=46;address.destination=dst(3,0,1);address.source[0].token=src(1,1,0);m.view.operations.push_back(address);
            Operation add;add.code=2;add.destination=dst(0,0);add.source[0].token=src(1,0);add.source[1].token=src(2,0)|0x2000;add.source[1].address=src(3,0,0);m.view.operations.push_back(add);
            if(!skinned){m.skinned=m.view.skinned=false;m.view.inputs.resize(1);m.view.operations.erase(m.view.operations.begin());m.view.operations[0].source[1].token=src(2,0);m.view.operations[0].source[1].address=0;}
            g.materials[&d.vs]=std::move(m);d.vs.AddRef();g.shaders[&d.vs].patched=&d.vs;
            struct V{float x,y,z;unsigned bone;} vertices[]={{0,0,0,0},{1,0,0,1},{0,1,0,2}};
            NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};
            auto seed=[&]{Mesh mesh;assert(g.snapshots.readUP(&d.decl,args.draw,nullptr,D3DFMT_UNKNOWN,vertices,16,mesh));
                auto e=std::make_unique<NorthlightAutoDisplacement::Entry>();e->source=std::make_shared<Mesh>(std::move(mesh));e->sourceDecl=&d.decl;d.decl.AddRef();e->program=1;e->domain=domain;g.retain(std::move(e));};
            unsigned replayed=0;auto capture=[&]{b.beginDraw();b.capture(&d.vs,[&]{++replayed;return D3D_OK;},true,&args);};
            for(UINT size:{64u,128u}){ // A size change clears geometry; warm a fresh immutable source afterwards.
                d.surface.size=size;for(unsigned i=0;i<3;++i)d.bank[4*i+2]=100;
                capture();seed();bool ready=false;
                for(unsigned f=0;f<20&&!ready;++f){b.endFrame();capture();ready=MaterialCaptureStateTestAccess::rejected(b)>0;}assert(ready);
                const auto reads=d.ps.reads,constants=d.pixelConstants,textures=d.textures,samplers=d.samplers,allocations=d.depthAllocations;
                for(unsigned f=0;f<8;++f){b.endFrame();capture();assert(MaterialCaptureStateTestAccess::rejected(b)>0);}
                assert(d.ps.reads==reads&&d.pixelConstants==constants&&d.textures==textures&&d.samplers==samplers&&d.depthAllocations==allocations&&!replayed);
                unsigned native=0;assert(SUCCEEDED(b.nativeDraw([&]{++native;return D3D_OK;}))&&native==1);
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=0;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));
                if(domain==1)assert(d.pixelConstants==constants+1); // Near/unknown proof reaches capture, even when its state save fails.
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=NAN;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));
                b.endFrame();for(unsigned i=0;i<3;++i)d.bank[4*i+2]=100;
                auto& op=g.materials[&d.vs].view.operations.back();const auto code=op.code;op.code=6;capture();assert(!MaterialCaptureStateTestAccess::rejected(b));op.code=code;
            }
            if(skinned){g.reset();g.endFrame();std::vector<V> large(9000);for(auto& v:large)v={0,0,0,0};for(unsigned i=0;i<3;++i)d.bank[4*i+2]=0;
                const auto posed=g.poseFallbackVertices,scanned=g.objectBoundsVertices,rejected=g.poseBudgetRejected;
                NorthlightAutoDisplacement::Args huge{{D3DPT_TRIANGLELIST,0,0,9000,0,3000,false},nullptr,D3DFMT_UNKNOWN,large.data(),16};
                assert(!g.prepare(&d.vs,domain,1,huge));assert(g.poseBudgetRejected==rejected+1&&g.poseFallbackVertices==posed&&g.objectBoundsVertices==scanned&&g.bounds.empty());}
        }assert(d.vs.refs==1&&d.ps.refs==1&&d.surface.refs==1&&d.decl.refs==1);
    }
    std::puts("MATERIAL_CAPTURE_ADMISSION_PASS farDraws=128 rigid_skinned zero_capture_setup near_unknown_nonfinite_fail_open native_retained msaa resize refs");
    std::puts("MATERIAL_REJECTED_POSE_WORK_PASS vertices=9000 objectScans=0 retainedObjectBounds=0 poseEvaluations=0");
}
static void sharedGeometry(){
    using namespace NorthlightDrawSnapshot;
    Mesh mesh;mesh.vertexCount=4;mesh.primitiveCount=2;mesh.topology=D3DPT_TRIANGLESTRIP;mesh.indexed=true;mesh.indices={0,1,2,3};
    mesh.streams[0].stride=12;mesh.streams[0].bytes.resize(48);mesh.streams[1].stride=4;mesh.streams[1].bytes.resize(16);
    for(unsigned i=0;i<4;++i){float p[]={float(i),float(i+1),float(i+2)};std::memcpy(mesh.streams[0].bytes.data()+12*i,p,12);std::memcpy(mesh.streams[1].bytes.data()+4*i,&i,4);}
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{1,0,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    std::vector<uint8_t> bytes;std::vector<D3DVERTEXELEMENT9> layout;UINT stride=0;assert(NorthlightSharedPOM::expand(mesh,elements,3,bytes,layout,stride)&&stride==20&&bytes.size()==120);
    const unsigned order[]={0,1,2,2,1,3};for(unsigned i=0;i<6;++i){assert(!std::memcmp(bytes.data()+20*i,mesh.streams[0].bytes.data()+12*order[i],12));assert(!std::memcmp(bytes.data()+20*i+12,&order[i],4));float id;std::memcpy(&id,bytes.data()+20*i+16,4);assert(id==float(i/3));}
    assert(layout[1].Stream==0&&layout[1].Offset==12&&layout[2].UsageIndex==6&&layout[3].Stream==0xff);
    mesh.indices[3]=4;assert(!NorthlightSharedPOM::expand(mesh,elements,3,bytes,layout,stride));
    using namespace NorthlightWaterShaderPatch;const DWORD vs[]={0xfffe0300,0x0200001f,0x80000000,dst(1,0),0x0200001f,0x80000000,dst(6,0),0x02000001,dst(6,0),src(1,0),65535};
    std::vector<uint32_t> patched,again;assert(NorthlightSharedPOM::vertex(vs,sizeof(vs)/4,patched));assert(!NorthlightSharedPOM::vertex(reinterpret_cast<DWORD*>(patched.data()),patched.size(),again));
    std::puts("SHARED_POM_GEOMETRY_PASS exact_attributes multistream strip_winding triangle_ids invalid_indices occupied_semantic");
}
int main(){
    DisplacementStateTestAccess::rotation();
    DisplacementStateTestAccess::boundsCache();
    MaterialCaptureStateTestAccess::invisible();
    MaterialCaptureStateTestAccess::noCapture();
    MaterialCaptureStateTestAccess::disabledAndCold();
    MaterialCaptureStateTestAccess::inactiveNativeProfile();
    MaterialCaptureStateTestAccess::lazyVertex();
    sharedGeometry();
    DisplacementStateTestAccess::capture();
    DisplacementStateTestAccess::cache();
    DisplacementStateTestAccess::primitiveLimit();
    DisplacementStateTestAccess::posed();
    DisplacementStateTestAccess::kernels();
    capturedPositionBounds();
    {SplitDeclaration decl;DisplacementStateTestAccess::positionOnly(&decl);}
    {RangeBackend backend;NorthlightAutoDisplacement g(&backend);auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(uintptr_t(1));
     DisplacementStateTestAccess::rangeProgram(g,shader);
     float vertices[]={0,0,50000,1,0,50001,0,1,50002};NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,12};float nearest=0;
     for(float distance:{47.99f,48.f,48.01f,60.f,150.f}){backend.bank[2]=distance-50000;assert(g.nearestMaterialDepth(shader,args,nearest));assert(nearest<=double(vertices[2])+backend.bank[2]);if(distance>=60)assert(nearest>48);}
     backend.bank[2]=-50000;assert(g.nearestMaterialDepth(shader,args,nearest)&&nearest==0);
     DisplacementStateTestAccess::rangePreservesPose(g);
     DisplacementStateTestAccess::rangeProgram(g,shader,true);assert(!g.nearestMaterialDepth(shader,args,nearest));
     DisplacementStateTestAccess::rangeProgram(g,shader);vertices[2]=NAN;assert(!g.nearestMaterialDepth(shader,args,nearest));
     DisplacementStateTestAccess::rangeBudget(g,shader,&backend.decl);
     std::puts("MATERIAL_RANGE_PASS conservative_large_translation boundary_pose_changes unknown_skinned nonfinite");}
    Backend backend;DeviceMirror mirror;ExtensionDevice extension(&backend,&mirror);Meter meter(&extension);
    NorthlightAutoDisplacement g(&meter);
    auto attach=[&]{g.setConstantEpochSource(&extension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});};
    auto read=[&]{assert(DisplacementStateTestAccess::read(g));assert(!std::memcmp(DisplacementStateTestAccess::values(g),backend.bank,sizeof backend.bank));};
    attach();read();assert(meter.calls==1&&meter.registers==256);
    for(unsigned i=0;i<100;++i)read();assert(meter.calls==1); // No call or bank copy on an unchanged epoch.
    float row[8]={3,4,5,6,7,8,9,10};assert(SUCCEEDED(extension.SetVertexShaderConstantF(31,row,2)));
    read();assert(meter.calls==2&&meter.registers==288); // A write crossing two blocks reads both.
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(252,row,1)));read();assert(meter.registers==304);
    assert(SUCCEEDED(extension.SetVertexShaderConstantF(64,row,1)));meter.fail=true;assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));meter.fail=false;read();
    assert(meter.registers==576);const auto invalidatedStart=meter.registers;
    mirror.invalidate();backend.bank[70*4]=9;read();assert(meter.registers==invalidatedStart+256);
    {ExtensionDevice::RawScope raw(extension);backend.bank[80*4]=10;read();assert(!DisplacementStateTestAccess::certified(g));}
    read();assert(meter.registers==invalidatedStart+768);
    mirror.recording=true;mirror.invalidate();read();assert(!DisplacementStateTestAccess::certified(g));mirror.recording=false;mirror.invalidate();read();
    backend.fail=true;mirror.invalidate();assert(!DisplacementStateTestAccess::read(g));assert(!DisplacementStateTestAccess::certified(g));backend.fail=false;read();
    mirror.enabled=false;mirror.invalidate();backend.bank[90*4]=11;read();assert(!DisplacementStateTestAccess::certified(g));mirror.enabled=true;mirror.invalidate();read();
    mirror.invalidate();const auto old=backend.bank[64*4];backend.mutate=&extension;assert(DisplacementStateTestAccess::read(g));assert(DisplacementStateTestAccess::values(g)[64*4]==old);
    assert(!DisplacementStateTestAccess::certified(g)&&!mirror.vsFloatKnown[64]);read();assert(DisplacementStateTestAccess::certified(g)&&DisplacementStateTestAccess::values(g)[64*4]==21);
    const auto before=meter.calls;assert(DisplacementStateTestAccess::read(g,false));read();assert(meter.calls==before+2);
    g.setConstantEpochSource(nullptr,nullptr);read();read();assert(!DisplacementStateTestAccess::certified(g));attach();read();g.reset();read();
    // A new device's equal serials cannot inherit the old object's bank.
    Backend other;other.bank[0]=12;DeviceMirror otherMirror;ExtensionDevice otherExtension(&other,&otherMirror);NorthlightAutoDisplacement next(&otherExtension);
    next.setConstantEpochSource(&otherExtension,[](void* c,NorthlightConstantEpoch::Stamp& s){return static_cast<ExtensionDevice*>(c)->constantStamp(s);});assert(DisplacementStateTestAccess::read(next));assert(DisplacementStateTestAccess::values(next)[0]==12);

    Declaration decl;for(unsigned i=0;i<100;++i)DisplacementStateTestAccess::declaration(g,&decl,12,true,i%2);assert(decl.reads==1&&decl.refs==2);
    g.reset();assert(decl.refs==1);DisplacementStateTestAccess::declaration(g,&decl,12);assert(decl.reads==2);
    Declaration unsupported(0,4);DisplacementStateTestAccess::declaration(g,&unsupported,12,false);assert(unsupported.reads==1);
    Declaration failed;failed.fail=true;const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
    NorthlightVertexDeclarations::Cache cache;assert(!cache.get(&failed,elements,count));failed.fail=false;assert(cache.get(&failed,elements,count));assert(failed.reads==2);
    std::vector<std::unique_ptr<Declaration>> layouts;for(unsigned i=0;i<129;++i){layouts.push_back(std::make_unique<Declaration>());assert(cache.get(layouts.back().get(),elements,count));}
    assert(layouts[0]->refs==1);assert(cache.get(layouts[0].get(),elements,count));assert(layouts[0]->reads==2);cache.clear();for(auto& layout:layouts)assert(layout->refs==1);
    alignas(Declaration) unsigned char storage[sizeof(Declaration)];auto* reused=new(storage) Declaration;
    DisplacementStateTestAccess::declaration(g,reused,12);assert(reused->refs==2);g.reset();assert(reused->refs==1);reused->~Declaration();
    reused=new(storage) Declaration(4);DisplacementStateTestAccess::declaration(g,reused,16);assert(reused->reads==1&&reused->elements[0].Offset==4);g.reset();reused->~Declaration();
    std::puts("PASS displacement state reuse: exact and partial epochs, invalidation, inactive sources, failed/raced reads, source replacement, immutable declarations, UP layouts and address reuse");
}
