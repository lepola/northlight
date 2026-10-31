#pragma once
#include "actor_deformation.h"
#include "actor_shadow_selection.h"

namespace NorthlightActorDeformation {
// Only immutable snapshot inputs are reused. Pose constants, camera transforms,
// shader evaluation, sampled vertex and the resulting distance stay current.
// Two-way fixed mapping bounds memory and lookup work. Exact metadata checks and weak
// ownership reject collisions, reused addresses and shader/declaration changes.
class SampledVertexCache {
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        const Program* program=nullptr;const void* declaration=nullptr;
        const NorthlightDrawSnapshot::Mesh* key=nullptr;std::uint64_t touched=0;
        std::vector<Input> inputs;
        std::vector<D3DVERTEXELEMENT9> elements;
        Four values[16]{};
    };
    std::array<Entry,2048> entries_;std::uint64_t clock_=0;
public:
    unsigned hits=0,misses=0;
    void clear(){for(auto& e:entries_)e=Entry{};hits=misses=0;clock_=0;}
    void beginFrame(){hits=misses=0;}
    bool distance(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const void* declaration,
        const D3DVERTEXELEMENT9* elements,size_t count,const float* constants,
        const float* inverseView,const float* camera,float& squared,float* world=nullptr){
        // The skinning template evaluates faster than a lookup costs: decode directly.
        if(!owner||owner.get()!=&mesh||!elements||!count||count>MAXD3DDECLLENGTH+1||program.textureCoordinates||program.kernel.skin)
            return sampledDistanceSquared(program,mesh,elements,count,constants,inverseView,camera,squared,world);
        std::uint64_t hash=(reinterpret_cast<uintptr_t>(owner.get())>>4)^((reinterpret_cast<uintptr_t>(&program)>>4)*0x9e3779b1u)^
            (reinterpret_cast<uintptr_t>(declaration)>>4);
        // Allocator strides often have identical low pointer bits. Mix all
        // address bits and use two ways so neighboring allocations do not
        // continuously evict each other's immutable inputs.
        hash^=hash>>33;hash*=0xff51afd7ed558ccdULL;hash^=hash>>33;
        hash*=0xc4ceb9fe1a85ec53ULL;hash^=hash>>33;
        const auto slot=(hash%(entries_.size()/2))*2;
        auto matches=[&](const Entry& e){
        bool same=e.key==&mesh&&e.program==&program&&e.declaration==declaration&&e.owner.lock()==owner&&
            e.inputs.size()==program.inputs.size()&&e.elements.size()==count;
        if(same)for(size_t i=0;i<e.inputs.size();++i){const auto& a=e.inputs[i];const auto& b=program.inputs[i];
            if(a.reg!=b.reg||a.usage!=b.usage||a.index!=b.index){same=false;break;}}
        if(same)for(size_t i=0;i<count;++i){const auto& a=e.elements[i];const auto& b=elements[i];
            if(a.Stream!=b.Stream||a.Offset!=b.Offset||a.Type!=b.Type||a.Method!=b.Method||a.Usage!=b.Usage||a.UsageIndex!=b.UsageIndex){same=false;break;}}
        return same;};
        ++clock_;
        for(unsigned way=0;way<2;++way){auto& entry=entries_[slot+way];if(matches(entry)){
            entry.touched=clock_;++hits;return sampledDistanceFromInputs(program,entry.values,constants,inverseView,camera,squared,world);}}
        auto& e=entries_[slot+(entries_[slot+1].touched<entries_[slot].touched?1:0)];
        ++misses;Four values[16];
        if(!sampledVertexInputs(program,mesh,elements,count,values))return false;
        try{
            // Publish only a complete entry; allocation failure keeps the same
            // computed inputs and never changes this draw's distance result.
            Entry next;next.owner=owner;next.program=&program;next.declaration=declaration;next.key=&mesh;next.touched=clock_;
            next.inputs=program.inputs;next.elements.assign(elements,elements+count);
            std::memcpy(next.values,values,sizeof values);e=std::move(next);
        }catch(...){}
        return sampledDistanceFromInputs(program,values,constants,inverseView,camera,squared,world);
    }
};
// Rigid palette test for the stable actor quota. The result depends only on
// immutable snapshot bytes and the declaration, so it is cached per owner like
// sampled inputs; draws without a shared owner are scanned (bounded) directly.
class RigidBoneCache {
    // Fixed-size entries (no per-entry allocation). The cached value depends only
    // on the immutable snapshot bytes and the declaration; whether the program
    // reads BLENDINDICES is checked per call. 8192 entries, 8-way (about 1 MiB),
    // cover the crowd working set that thrashed the old 1024x2 table every frame.
    static constexpr size_t InlineElements=10,Ways=8;
    struct Entry {std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;const NorthlightDrawSnapshot::Mesh* key=nullptr;const void* declaration=nullptr;
        D3DVERTEXELEMENT9 elements[InlineElements]={};std::uint32_t count=0,lanes=0;float bone=NAN;std::uint64_t touched=0;};
    std::vector<Entry> entries_;std::uint64_t clock_=0;
public:
    RigidBoneCache():RigidBoneCache(8192){}
    explicit RigidBoneCache(size_t capacity):entries_(std::max(Ways,((capacity+Ways-1)/Ways)*Ways)){}
    static constexpr UINT MaxVertices=16384;
    static constexpr unsigned BlendWeight=1,BlendIndices=2; /* D3DDECLUSAGE values; test stubs omit the enum */
    unsigned hits=0,misses=0;std::size_t scannedVertices=0;
    void clear(){for(auto& e:entries_)e=Entry{};hits=misses=0;scannedVertices=0;clock_=0;}
    void beginFrame(){hits=misses=0;scannedVertices=0;}
    static bool readsIndices(const Program& program){for(const auto& i:program.inputs)if(i.usage==BlendIndices)return true;return false;}
    float scanMesh(const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count,unsigned lanes=4){
        if(lanes!=1&&lanes!=4)return NAN;
        if(!elements||!count||count>MAXD3DDECLLENGTH+1||!mesh.vertexCount||mesh.vertexCount>MaxVertices)return NAN;
        const D3DVERTEXELEMENT9* index=nullptr;const D3DVERTEXELEMENT9* weight=nullptr;
        for(size_t j=0;j<count;++j){const auto& e=elements[j];if(e.Stream==0xff)break;if(e.UsageIndex!=0)continue;
            if(e.Usage==BlendIndices){if(index)return NAN;index=&e;}
            if(e.Usage==BlendWeight){if(weight)return NAN;weight=&e;}}
        auto fits=[&](const D3DVERTEXELEMENT9* e){if(!e)return true;if(e->Stream>=4||e->Method!=D3DDECLMETHOD_DEFAULT)return false;
            const auto& s=mesh.streams[e->Stream];const unsigned size=NorthlightDrawSnapshot::declarationBytes(e->Type);
            return size&&unsigned(e->Offset)+size<=s.stride&&std::uint64_t(mesh.vertexCount)*s.stride<=s.bytes.size();};
        if(!index||!fits(index)||!fits(weight))return NAN;
        scannedVertices+=mesh.vertexCount;
        return NorthlightActorShadowSelection::rigidBone(mesh.vertexCount,weight!=nullptr&&lanes!=1,[&](size_t v,float* i,float* w){
            Four a,b;const auto& si=mesh.streams[index->Stream];
            if(!decodeElement(si.bytes.data()+v*si.stride+index->Offset,index->Type,a))return false;
            if(weight&&lanes!=1){const auto& sw=mesh.streams[weight->Stream];if(!decodeElement(sw.bytes.data()+v*sw.stride+weight->Offset,weight->Type,b))return false;
                for(unsigned l=0;l<4;++l)w[l]=b[l];}
            for(unsigned l=0;l<4;++l)i[l]=a[lanes==1?0:l];return true;});
    }
    float scan(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count){
        return readsIndices(program)?scanMesh(mesh,elements,count):NAN;
    }
    float bone(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,
        const void* declaration,const D3DVERTEXELEMENT9* elements,size_t count,unsigned lanes=4){
        if(!readsIndices(program))return NAN;
        if(!owner||owner.get()!=&mesh||!elements||!count||count>InlineElements)return scanMesh(mesh,elements,count,lanes);
        std::uint64_t hash=(reinterpret_cast<uintptr_t>(owner.get())>>4)^((reinterpret_cast<uintptr_t>(declaration)>>4)*0x9e3779b1u);
        hash^=hash>>33;hash*=0xff51afd7ed558ccdULL;hash^=hash>>33;
        const auto slot=(hash%(entries_.size()/Ways))*Ways;++clock_;
        for(unsigned way=0;way<Ways;++way){auto& e=entries_[slot+way];
            bool same=e.key==&mesh&&e.declaration==declaration&&e.count==count&&e.lanes==lanes;
            if(same)for(size_t i=0;i<count;++i){const auto& a=e.elements[i];const auto& b=elements[i];
                if(a.Stream!=b.Stream||a.Offset!=b.Offset||a.Type!=b.Type||a.Method!=b.Method||a.Usage!=b.Usage||a.UsageIndex!=b.UsageIndex){same=false;break;}}
            if(same&&e.owner.lock()==owner){e.touched=clock_;++hits;return e.bone;}}
        ++misses;const float result=scanMesh(mesh,elements,count,lanes);
        Entry* victim=&entries_[slot];for(unsigned way=1;way<Ways;++way)if(entries_[slot+way].touched<victim->touched)victim=&entries_[slot+way];
        victim->owner=owner;victim->key=&mesh;victim->declaration=declaration;std::memcpy(victim->elements,elements,count*sizeof *elements);
        victim->count=std::uint32_t(count);victim->lanes=lanes;victim->bone=result;victim->touched=clock_;
        return result;
    }
};
}
