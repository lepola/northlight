#include <cstdlib>
#include <new>
#include <utility>
static bool watchAllocations=false,failAllocations=false;
static size_t allocations=0;
void* operator new(std::size_t size){if(watchAllocations)++allocations;if(failAllocations)throw std::bad_alloc();if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#include "lookup_identity_harness.h"
#include <map>
using namespace NorthlightDrawSnapshot;
static std::map<void*,uint64_t> revisions;
static uint64_t identity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t version(void* p,bool){const auto found=revisions.find(p);return found==revisions.end()?0:found->second;}
static void configure(Frame& frame){frame.setIdentityProvider(identity);frame.setVersionProvider(version);frame.setChargeResidentHits(false);frame.setLookup(Frame::Lookup::Map);}
static bool layout(void*,IDirect3DVertexDeclaration9*,UINT* extent){extent[0]=16;extent[1]=4;return true;}
template<class Work>static std::pair<bool,size_t> observed(Work work,bool fail=false){
    allocations=0;watchAllocations=true;failAllocations=fail;
    try{const bool result=work();failAllocations=watchAllocations=false;return {result,allocations};}
    catch(...){failAllocations=watchAllocations=false;assert(!"LOOKUP_EXCEPTION_ESCAPED");return {};}
}
static uint64_t independentHash(const std::vector<uint64_t>& words){uint64_t h=1469598103934665603ull;for(auto word:words)h=(h^word)*1099511628211ull;return h;}
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static unsigned serialization(Frame& frame,Declaration& declaration){
        unsigned cases=0;
        for(size_t changed=0;changed<=sizeof(Frame::CacheKey)/4;++changed){
            Frame::CacheKey key;key.declaration=&declaration;key.privateDomain=&frame;
            if(changed){uint32_t value;std::memcpy(&value,reinterpret_cast<unsigned char*>(&key)+(changed-1)*4,4);value^=uint32_t(0xa35c1000u+changed);std::memcpy(reinterpret_cast<unsigned char*>(&key)+(changed-1)*4,&value,4);}
            auto expected=key;expected.declaration=nullptr;expected.privateDomain=nullptr;
            std::vector<uint64_t> words;words.reserve(4+sizeof key/4+declaration.e.size());words.push_back(1);words.push_back(1);
            for(size_t at=0;at<sizeof key;at+=4){uint32_t word;std::memcpy(&word,reinterpret_cast<unsigned char*>(&expected)+at,4);words.push_back(word);}
            words.push_back(declaration.e.size());for(const auto& e:declaration.e)words.push_back(uint64_t(e.Stream)|(uint64_t(e.Offset)<<16)|(uint64_t(e.Type)<<32)|(uint64_t(e.Method)<<40)|(uint64_t(e.Usage)<<48)|(uint64_t(e.UsageIndex)<<56));
            const auto stored=frame.sourceIdentity(key,&declaration);assert(stored&&stored->words==words&&stored->words.capacity()==words.capacity()&&stored->hash==independentHash(words));
            Frame::IdentityWords borrowed;auto lookup=key;assert(frame.sourceIdentityWords(lookup,&declaration,borrowed));
            assert(borrowed.count==words.size()&&std::equal(borrowed.begin(),borrowed.end(),words.begin())&&NorthlightSourceIdentity::Key::fingerprint(borrowed)==stored->hash);++cases;
        }
        return cases;
    }
    static bool allocatingControl(Frame& frame,Declaration& declaration){Frame::CacheKey key;return bool(frame.sourceIdentity(key,&declaration));}
    static void forceCollision(Frame& frame){
        auto* entry=frame.sources_->lru.oldest();assert(entry&&entry->mesh->sourceIdentity);const auto original=entry->mesh->sourceIdentity;auto words=original->words;
        constexpr uint64_t prime=1099511628211ull,inverse=14886173955864302971ull;static_assert(prime*inverse==1,"FNV inverse");
        uint64_t prefix=1469598103934665603ull;for(size_t i=0;i<words.size()-2;++i)prefix=(prefix^words[i])*prime;
        words[words.size()-2]^=uint64_t(1)<<63;const uint64_t middle=(prefix^words[words.size()-2])*prime;words.back()=middle^(original->hash*inverse);
        auto colliding=NorthlightSourceIdentity::make(std::move(words));assert(colliding->hash==original->hash&&colliding->words!=original->words);
        auto changed=std::make_shared<Mesh>(*entry->mesh);changed->sourceIdentity=std::move(colliding);entry->mesh=std::move(changed);
    }
};
}
struct Provider {unsigned mode=0;Declaration* declaration=nullptr;
    static bool read(void* context,IDirect3DVertexDeclaration9*,const D3DVERTEXELEMENT9*& elements,UINT& count){auto& p=*static_cast<Provider*>(context);if(p.mode==1)return false;if(p.mode==2)throw std::bad_alloc();elements=p.mode==3?nullptr:p.declaration->e.data();count=p.mode==4?MAXD3DDECLLENGTH+2:UINT(p.declaration->e.size());return true;}
};
struct BadDeclaration:Declaration {unsigned mode=0;HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{if(mode==1)return D3DERR_INVALIDCALL;if(mode==2)throw std::bad_alloc();if(mode==3){*count=MAXD3DDECLLENGTH+2;return D3D_OK;}return Declaration::GetDeclaration(out,count);}};
int main(){
    unsigned serialized=0;
    {
        Device d;Frame frame(2u<<20,0);configure(frame);frame.setLayoutProvider(nullptr,layout);Mesh scratch;Diagnostics why;
        for(void* p:{static_cast<void*>(&d.vb[0]),static_cast<void*>(&d.vb[1]),static_cast<void*>(&d.ib)})revisions[p]=1;
        Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};std::shared_ptr<const Mesh> first,next;
        serialized+=SnapshotTestAccess::serialization(frame,d.decl);
        Declaration maximum;maximum.e.resize(MAXD3DDECLLENGTH+1);serialized+=SnapshotTestAccess::serialization(frame,maximum);
        assert(frame.read(&d,&d.decl,draw,scratch,&why,false,&first)&&first&&first->sourceIdentity);
        Declaration alias=d.decl;const auto bytes=frame.bytesRead();
        auto result=observed([&]{return frame.read(&d,&alias,draw,scratch,&why,false,&next,false,true);});assert(result.first&&next==first&&!result.second&&frame.bytesRead()==bytes);
        auto allocating=observed([&]{return SnapshotTestAccess::allocatingControl(frame,d.decl);});assert(allocating.first&&allocating.second>=2);
        auto failed=observed([&]{return SnapshotTestAccess::allocatingControl(frame,d.decl);},true);assert(!failed.first&&failed.second==1);
        result=observed([&]{return frame.read(&d,&alias,draw,scratch,&why,false,&next,false,true);},true);assert(result.first&&next==first&&!result.second);
        Draw miss=draw;miss.base=0;result=observed([&]{return frame.read(&d,&alias,miss,scratch,&why,false,&next,false,true);});assert(!result.first&&!result.second&&!next&&frame.bytesRead()==bytes);
        Frame exhausted(0,0);configure(exhausted);result=observed([&]{return exhausted.read(&d,&alias,miss,scratch,&why,false,&next);});assert(!result.first&&!result.second&&why.error==Error::Budget&&exhausted.bytesRead()==0);
        // Metadata-provider failures stay inside the optional proof boundary.
        Provider provider{0,&alias};frame.setDeclarationProvider(&provider,Provider::read);
        for(unsigned mode=1;mode<=4;++mode){provider.mode=mode;result=observed([&]{return frame.read(&d,&alias,draw,scratch,&why,false,&next,false,true);});assert(!result.first&&!next&&!result.second&&frame.bytesRead()==bytes);}
        frame.setDeclarationProvider(nullptr,nullptr);BadDeclaration bad;
        for(unsigned mode=1;mode<=3;++mode){bad.mode=mode;result=observed([&]{return frame.read(&d,&bad,draw,scratch,&why,false,&next,false,true);});assert(!result.first&&!next&&!result.second);}
        // Every serialized field is proof, even with a real FNV hash collision.
        SnapshotTestAccess::forceCollision(frame);
        if(next){std::fputs("LOOKUP_COLLISION_ACCEPTED\n",stderr);std::abort();}
        // A direct equality mutant must be detected before relying on a hash.
        const bool collision=frame.read(&d,&alias,draw,scratch,&why,false,&next,false,true);
        if(collision||next){std::fputs("LOOKUP_COLLISION_ACCEPTED\n",stderr);std::abort();}
        first.reset();next.reset();frame.clearSnapshotCache();assert(!frame.sharedSourceBytes());
    }
    {
        Device d;Frame source,peer;configure(source);configure(peer);source.setLayoutProvider(nullptr,layout);peer.setLayoutProvider(nullptr,layout);peer.setResidentPeer(&source);
        for(void* p:{static_cast<void*>(&d.vb[0]),static_cast<void*>(&d.vb[1]),static_cast<void*>(&d.ib)})revisions[p]=1;
        Mesh scratch;Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};std::shared_ptr<const Mesh> first,next;assert(source.read(&d,&d.decl,draw,scratch,nullptr,false,&first));
        Declaration alias=d.decl;auto result=observed([&]{return peer.read(&d,&alias,draw,scratch,nullptr,false,&next,false,true);});assert(result.first&&next==first&&peer.residentPeerHits()==1&&!result.second&&peer.bytesRead()==0);
        // New revisions and unknown dynamic provenance cannot borrow old bytes.
        ++revisions[&d.vb[0]];assert(!peer.read(&d,&alias,draw,scratch,nullptr,false,&next,false,true)&&!next);
        assert(peer.read(&d,&alias,draw,scratch,nullptr,false,&next)&&next&&next!=first&&!NorthlightSourceIdentity::same(next->sourceIdentity,first->sourceIdentity));
        revisions[&d.vb[0]]=0;assert(!peer.read(&d,&alias,draw,scratch,nullptr,false,&next,false,true)&&!next);
        first.reset();next.reset();source.clearSnapshotCache();peer.clearSnapshotCache();
    }
    assert(!NorthlightSourceIdentity::identityLiveBytes.load()&&!NorthlightSourceIdentity::cpuLiveObjects.load());
    std::printf("LOOKUP_IDENTITY_PASS serializedCases=%u storedCapacity=exact rawWords=exact hash=exact lookupAllocations=0 failureBoundary=checked peer=checked revisions=checked caps=unchanged\n",serialized);
}
