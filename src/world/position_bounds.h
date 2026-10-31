#pragma once
#include "draw_snapshot.h"
#include "position_bounds_scan.h"

// Bounds of the declared vertex range contain every legal indexed draw, even
// when some vertices are unused. No IB, UV, normal or mesh copy is needed.
// Only completely write-tracked buffers can supply this optional early proof.
namespace NorthlightPositionBounds {
class Cache {
public:
    struct Maintenance {unsigned coldScans=0,revisionScans=0,evictions=0,budgetRejected=0;};
private:
    struct Key {
        uint64_t identity=0;UINT begin=0,stride=0,count=0;
        bool operator==(const Key& b)const{return identity==b.identity&&begin==b.begin&&stride==b.stride&&count==b.count;}
    };
    struct Hash {size_t operator()(const Key& k)const{
        uint64_t h=k.identity;for(UINT v:{k.begin,k.stride,k.count})h=(h^v)*1099511628211ull;
        return std::hash<uint64_t>{}(h);
    }};
    struct Entry {Key key;uint64_t revision=0;UINT size=0;float low[3],high[3];NorthlightIntrusiveLRU::Links<Entry> lru;};
    std::unordered_map<Key,Entry,Hash> entries;
    NorthlightIntrusiveLRU::List<Entry> lru;
    uint64_t(*identity)(void*,bool)=nullptr;uint64_t(*version)(void*,bool)=nullptr;
    NorthlightCaptureMetadata::Read metadata=nullptr;
    size_t bytes=0;unsigned hits=0,reads=0,unknown=0;
    Maintenance maintenance_;
    NorthlightCaptureMetadata::Info stamp(IDirect3DVertexBuffer9* vb)const{
        NorthlightCaptureMetadata::Info info;
        if(metadata){const NorthlightCaptureMetadata::Request request{vb,false};metadata(&request,&info,1);return info;}
        if(!identity||!version)return info;
        D3DVERTEXBUFFER_DESC desc{};if(FAILED(vb->GetDesc(&desc)))return info;
        info.identity=identity(vb,false);info.revision=version(vb,false);info.size=desc.Size;info.known=true;return info;
    }
public:
    void tracking(uint64_t(*i)(void*,bool),uint64_t(*v)(void*,bool),NorthlightCaptureMetadata::Read m){clear();identity=i;version=v;metadata=m;}
    void clear(){lru.clear();entries.clear();beginFrame();}
    void beginFrame(){bytes=0;hits=reads=unknown=0;maintenance_={};}
    const Maintenance& maintenance()const{return maintenance_;}
    size_t bytesRead()const{return bytes;}
    unsigned cacheHits()const{return hits;}
    unsigned scans()const{return reads;}
    unsigned failures()const{return unknown;}
    size_t count()const{return entries.size();}
    bool read(IDirect3DDevice9* device,const NorthlightDrawSnapshot::Draw& draw,UINT positionOffset,float* low,float* high)noexcept{
      try{
        if(!device||!draw.primitives||(draw.topology!=D3DPT_TRIANGLELIST&&draw.topology!=D3DPT_TRIANGLESTRIP))return false;
        const uint64_t count=draw.indexed?draw.vertices:draw.topology==D3DPT_TRIANGLELIST?uint64_t(draw.primitives)*3:uint64_t(draw.primitives)+2;
        const int64_t first=draw.indexed?int64_t(draw.base)+draw.minimum:draw.start;
        if(!count||count>65536||first<0||uint64_t(first)+count>uint64_t(UINT(-1))+1)return false;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9> vb;UINT offset=0,stride=0,frequency=0;
        if(FAILED(device->GetStreamSource(0,vb.out(),&offset,&stride))||!vb.p||
           FAILED(device->GetStreamSourceFreq(0,&frequency))||frequency!=1||stride>4096||
           positionOffset>stride||stride-positionOffset<12)return false;
        const auto before=stamp(vb.p);
        if(!before.known||!before.identity||!before.revision){++unknown;return false;}
        const uint64_t begin=uint64_t(offset)+uint64_t(first)*stride+positionOffset,span=(count-1)*stride+12;
        if(begin+span>before.size||begin>UINT(-1)||span>(4u<<20))return false;
        const Key key{before.identity,UINT(begin),stride,UINT(count)};
        auto found=entries.find(key);
        if(found!=entries.end()&&found->second.revision==before.revision&&found->second.size==before.size){
            auto& e=found->second;std::copy(e.low,e.low+3,low);std::copy(e.high,e.high+3,high);lru.touch(e);++hits;return true;
        }
        // This budgets actual position reads, independently of full geometry.
        if(bytes+count*12>(2u<<20)){++maintenance_.budgetRejected;return false;}
        if(found==entries.end())++maintenance_.coldScans;else ++maintenance_.revisionScans;
        bytes+=size_t(count*12);++reads;
        Entry value;value.key=key;value.revision=before.revision;value.size=before.size;
        NorthlightReplayCopies::Reader<IDirect3DVertexBuffer9> reader(vb.p);void* data=nullptr;
        if(FAILED(reader.lock(UINT(begin),UINT(span),&data)))return false;
        const bool valid=data&&scanPositions(data,size_t(count),stride,value.low,value.high);
        if(FAILED(reader.unlock())||!valid)return false;
        const auto after=stamp(vb.p);
        if(!after.known||after.identity!=before.identity||after.revision!=before.revision||after.size!=before.size){++unknown;return false;}
        if(found==entries.end()){
            if(entries.size()>=4096){auto* old=lru.oldest();const auto oldKey=old->key;lru.remove(*old);entries.erase(oldKey);++maintenance_.evictions;}
            found=entries.emplace(key,std::move(value)).first;lru.append(found->second);
        }else{auto& e=found->second;e.revision=value.revision;e.size=value.size;std::copy(value.low,value.low+3,e.low);std::copy(value.high,value.high+3,e.high);lru.touch(e);}
        std::copy(found->second.low,found->second.low+3,low);std::copy(found->second.high,found->second.high+3,high);return true;
      }catch(...){return false;}
    }
};
}
