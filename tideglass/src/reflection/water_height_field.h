#pragma once
#include "reflection/water_reference_height.h"
#include <map>
#include <limits>
#include <unordered_map>
#include <cstring>
#include <cstdint>
#include <memory>
#include <array>
namespace reflection {
// Terrain water is anchored to world coordinates, not the current camera's
// collision-query patch. Keep off-screen tiles so turning does not change the
// nearest wet point for a stationary object. This never rotates the mirror.
class WaterHeightField {
    using Key=std::pair<int,int>;
    uint64_t indexBuilds=0,observationHits=0,snapshotCopies=0,observation=0,revision=0,retentionScans=0,retentionGroupsSkipped=0;
    std::vector<uint64_t> observations;std::vector<uint32_t> freeSlots;
    std::shared_ptr<const std::vector<WaterSurface>> previousSurfaces;
    float previousCamera[2]={};bool observationKnown=false,cameraKnown=false;
    bool previousIndexed=false,previousMemo=false,previousStable=false,previousReuse=false;
    mutable uint64_t heightQueries=0;
    int map=-1;
    struct Entry {Key key;WaterSurface surface;};
    struct Node {double lo[2],hi[2];size_t begin,end;int left=-1,right=-1;};
    struct Tile {WaterSurface surface;uint32_t slot=0;};
    struct Geometry {
        using Page=std::map<Key,Tile>;
        std::map<Key,std::shared_ptr<Page>> pages;
        bool partitioned=false;
        size_t count=0;
        std::vector<Entry> entries;
        std::vector<Node> nodes;
        bool dirty=true;
    };
    using Queries=std::unordered_map<uint64_t,float>;
    std::shared_ptr<Geometry> geometry=std::make_shared<Geometry>();
    std::shared_ptr<Queries> queries=std::make_shared<Queries>();
    struct FixedQueries {
        struct Answer {uint64_t key=0;float height=0;bool known=false;};
        static constexpr unsigned sets=4096,ways=4;
        std::array<Answer,sets*ways> answers={};
        std::array<unsigned char,sets> next={};size_t used=0;
        static unsigned Bucket(uint64_t key){
            uint32_t hash=uint32_t(key)^(uint32_t(key>>32)*0x9e3779b9u);
            hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;return hash&(sets-1);
        }
        bool Find(uint64_t key,float& height) const {
            const auto* set=answers.data()+Bucket(key)*ways;
            for(unsigned i=0;i<ways;++i)if(set[i].known && set[i].key==key){height=set[i].height;return true;}
            return false;
        }
        bool Insert(uint64_t key,float height){
            const unsigned bucket=Bucket(key),way=next[bucket];auto& answer=answers[bucket*ways+way];
            const bool replaced=answer.known;used+=!replaced;answer={key,height,true};
            next[bucket]=static_cast<unsigned char>((way+1)%ways);return replaced;
        }
    };
    bool boundedMemo=false;
    std::shared_ptr<FixedQueries> fixedQueries;
    uint64_t memoHits=0,memoCapacityClears=0,memoReplacements=0;
    // Baseline X strips or optional XY cells share immutable pages. A changed
    // surface copies its page; empty pages are removed. Linear query ties are
    // explicitly resolved by the original tile key across either partition.
    static int Cell(int coordinate){return coordinate>=0?coordinate/1024:int((int64_t(coordinate)-1023)/1024);}
    Key PageKey(const Key& k) const{return {Cell(k.first),geometry->partitioned?Cell(k.second):0};}
    static bool PageInside(const Key& cell,const float* camera){
        // Quantization moves an anchor at most1/32yd. A full1yd pad also
        // protects this conservative test from floating-point boundary rounding.
        const double x=cell.first*64.,y=cell.second*64.;
        const double dx=std::max(std::fabs(x-1-camera[0]),std::fabs(x+65-camera[0]));
        const double dy=std::max(std::fabs(y-1-camera[1]),std::fabs(y+65-camera[1]));
        return dx*dx+dy*dy<=1024.*1024.;
    }
    void Partition(bool enabled){
        if(geometry->partitioned==enabled)return;
        auto old=geometry;geometry=std::make_shared<Geometry>();geometry->partitioned=enabled;
        // This one-time option transition preserves slots, query answers and
        // an already-built spatial index. No observed surface is lost/reordered.
        snapshotCopies+=old->count;retentionScans+=old->count;
        for(const auto& page:old->pages)for(const auto& tile:*page.second)SetTile(tile.first,tile.second);
        geometry->entries=old->entries;geometry->nodes=old->nodes;geometry->dirty=old->dirty;
    }
    static std::shared_ptr<Geometry> CloneGeometry(const std::shared_ptr<Geometry>& old,bool share){
        if(share)return old;
        auto copy=std::make_shared<Geometry>(*old);
        for(auto& page:copy->pages)page.second=std::make_shared<Geometry::Page>(*page.second);
        return copy;
    }
    void EditGeometry(bool changing=false){
        if(geometry.use_count()!=1){
            if(changing){
                auto copy=std::make_shared<Geometry>();copy->pages=geometry->pages;copy->count=geometry->count;copy->partitioned=geometry->partitioned;geometry=std::move(copy);
            }else geometry=std::make_shared<Geometry>(*geometry);
        }
    }
    const Tile* FindTile(const Key& k) const {
        const auto page=geometry->pages.find(PageKey(k));if(page==geometry->pages.end())return nullptr;
        const auto found=page->second->find(k);return found==page->second->end()?nullptr:&found->second;
    }
    void SetTile(const Key& k,const Tile& tile){
        EditGeometry(true);auto& page=geometry->pages[PageKey(k)];
        if(!page)page=std::make_shared<Geometry::Page>();
        else if(page.use_count()!=1){snapshotCopies+=page->size();page=std::make_shared<Geometry::Page>(*page);}
        auto old=page->lower_bound(k);
        if(old==page->end() || old->first!=k){page->emplace_hint(old,k,tile);++geometry->count;}
        else old->second=tile;
    }
    void EraseTile(const Key& k){
        EditGeometry(true);auto page=geometry->pages.find(PageKey(k));
        if(page->second.use_count()!=1){snapshotCopies+=page->second->size();page->second=std::make_shared<Geometry::Page>(*page->second);}
        page->second->erase(k);--geometry->count;if(page->second->empty())geometry->pages.erase(page);
    }
    void ClearQueries(){if(!queries->empty())queries=std::make_shared<Queries>();fixedQueries.reset();}
    void EditQueries(){if(queries.use_count()!=1)queries=std::make_shared<Queries>(*queries);}
    static bool same(const WaterSurface& a,const WaterSurface& b){
        return a.anchor==b.anchor && a.normal==b.normal && a.bounds==b.bounds;
    }
    int Build(size_t begin,size_t end){
        auto& entries=geometry->entries;auto& nodes=geometry->nodes;
        Node n={{1e300,1e300},{-1e300,-1e300},begin,end};
        for(size_t i=begin;i<end;++i)for(int c=0;c<2;++c){
            n.lo[c]=std::min(n.lo[c],double(entries[i].surface.bounds[c]));
            n.hi[c]=std::max(n.hi[c],double(entries[i].surface.bounds[c+3]));
        }
        int id=int(nodes.size());nodes.push_back(n);
        if(end-begin>8){
            int axis=n.hi[0]-n.lo[0]>=n.hi[1]-n.lo[1]?0:1;size_t mid=begin+(end-begin)/2;
            std::nth_element(entries.begin()+begin,entries.begin()+mid,entries.begin()+end,[axis](const Entry& a,const Entry& b){
                double x=double(a.surface.bounds[axis])+a.surface.bounds[axis+3];
                double y=double(b.surface.bounds[axis])+b.surface.bounds[axis+3];
                return x==y?a.key<b.key:x<y;
            });
            int left=Build(begin,mid),right=Build(mid,end);nodes[id].left=left;nodes[id].right=right;
        }
        return id;
    }
    void Index(){
        if(!geometry->dirty)return;
        EditGeometry();++indexBuilds;
        auto& entries=geometry->entries;auto& nodes=geometry->nodes;
        entries.clear();nodes.clear();entries.reserve(geometry->count);nodes.reserve(geometry->count*2);
        for(const auto& page:geometry->pages)for(const auto& e:*page.second)entries.push_back({e.first,e.second.surface});
        if(!entries.empty())Build(0,entries.size());
        geometry->dirty=false;
    }
    double Lower(int id,float x,float y) const{
        const auto& n=geometry->nodes[id];double dx=std::clamp(double(x),n.lo[0],n.hi[0])-x;
        double dy=std::clamp(double(y),n.lo[1],n.hi[1])-y;return dx*dx+dy*dy;
    }
    void Search(int id,float x,float y,double& best,Key& chosen,bool& known,float& height) const{
        // Equal-distance branches must remain eligible: the map's first key wins.
        if(Lower(id,x,y)>best)return;
        const auto& n=geometry->nodes[id];
        if(n.left>=0){
            int a=n.left,b=n.right;if(Lower(b,x,y)<Lower(a,x,y))std::swap(a,b);
            Search(a,x,y,best,chosen,known,height);Search(b,x,y,best,chosen,known,height);
        }else for(size_t i=n.begin;i<n.end;++i){const auto& e=geometry->entries[i];const auto& s=e.surface;
            double px=std::clamp(double(x),double(s.bounds[0]),double(s.bounds[3]));
            double py=std::clamp(double(y),double(s.bounds[1]),double(s.bounds[4]));
            double dx=px-x,dy=py-y,score=dx*dx+dy*dy;
            if(score>best || (score==best && known && !(e.key<chosen)))continue;
            double z=s.anchor[2]-(s.normal[0]*(px-s.anchor[0])+s.normal[1]*(py-s.anchor[1]))/s.normal[2];
            if(!std::isfinite(z))continue;
            best=score;chosen=e.key;height=float(z);known=true;
        }
    }
    static Key key(const WaterSurface& s){return {int(std::llround(s.anchor[0]*16)),int(std::llround(s.anchor[1]*16))};}
public:
    WaterHeightField()=default;
    // Default snapshots retain the original deep-copy control. The worker can
    // explicitly share immutable geometry/index/memo storage instead. Mutations
    // detach before any write; neither result can change another snapshot.
    WaterHeightField(const WaterHeightField& other,bool share=false):
        indexBuilds(other.indexBuilds),observationHits(other.observationHits),snapshotCopies(other.snapshotCopies),
        observation(other.observation),revision(other.revision),observations(other.observations),freeSlots(other.freeSlots),
        previousSurfaces(other.previousSurfaces),observationKnown(other.observationKnown),cameraKnown(other.cameraKnown),
        previousIndexed(other.previousIndexed),previousMemo(other.previousMemo),previousStable(other.previousStable),previousReuse(other.previousReuse),
        heightQueries(other.heightQueries),map(other.map),
        geometry(CloneGeometry(other.geometry,share)),
        queries(share?other.queries:std::make_shared<Queries>(*other.queries)),boundedMemo(other.boundedMemo),
        fixedQueries(other.fixedQueries && !share?std::make_shared<FixedQueries>(*other.fixedQueries):other.fixedQueries),
        memoHits(other.memoHits),memoCapacityClears(other.memoCapacityClears),memoReplacements(other.memoReplacements){
        std::copy(other.previousCamera,other.previousCamera+2,previousCamera);
        if(!share)snapshotCopies+=other.Count();
        retentionScans=other.retentionScans;retentionGroupsSkipped=other.retentionGroupsSkipped;
    }
    WaterHeightField& operator=(const WaterHeightField&)=delete;
    // Only the geometry is needed by draw preparation. The source detaches
    // before mutation; the draw worker owns its derived index/query cache.
    std::shared_ptr<WaterHeightField> DrawSnapshot() const {
        auto result=std::make_shared<WaterHeightField>();result->geometry=geometry;
        result->revision=revision;result->map=map;result->boundedMemo=boundedMemo;return result;
    }
    uint64_t SnapshotCopies() const{return snapshotCopies;}
    void Reset(){++revision;ClearQueries();geometry=std::make_shared<Geometry>();observations.clear();freeSlots.clear();previousSurfaces.reset();observationKnown=cameraKnown=false;map=-1;}
    size_t Count() const{return geometry->count;}
    uint64_t Revision() const{return revision;}
    uint64_t RetentionScans() const{return retentionScans;}
    uint64_t RetentionGroupsSkipped() const{return retentionGroupsSkipped;}
    size_t MemoizedCount() const{return boundedMemo?(fixedQueries?fixedQueries->used:0):queries->size();}
    void BoundedMemo(bool enabled){if(enabled!=boundedMemo){ClearQueries();boundedMemo=enabled;}}
    uint64_t MemoHits() const{return memoHits;}
    uint64_t MemoCapacityClears() const{return memoCapacityClears;}
    uint64_t MemoReplacements() const{return memoReplacements;}
    uint64_t ObservationHits() const{return observationHits;}
    uint64_t IndexBuilds() const{return indexBuilds;}
    uint64_t HeightQueries() const{return heightQueries;}
    // Shared unchanged-input fast path, also used before handing work to a CPU worker.
    bool ReuseObservation(const std::vector<WaterSurface>& surfaces,int mapId,const float* camera,bool indexed,bool persistentMemo,bool stableRetention,bool reuseUpdate,bool reuseObservation){
        if(map!=mapId || !reuseObservation || !reuseUpdate || !stableRetention || !cameraKnown ||
           previousCamera[0]!=camera[0] || previousCamera[1]!=camera[1] || !observationKnown ||
           previousIndexed!=indexed || previousMemo!=persistentMemo || previousStable!=stableRetention ||
           previousReuse!=reuseUpdate || previousSurfaces->size()!=surfaces.size())return false;
        for(size_t i=0;i<surfaces.size();++i)if(!same((*previousSurfaces)[i],surfaces[i]))return false;
        if(!persistentMemo)ClearQueries();if(indexed)Index();++observationHits;return true;
    }
    void Update(const std::vector<WaterSurface>& surfaces,int mapId,const float* camera,bool indexed=false,bool persistentMemo=false,bool stableRetention=true,bool reuseUpdate=false,bool reuseObservation=false,bool retentionReuse=false){
        reuseObservation=reuseObservation && reuseUpdate && stableRetention;
        bool changed=false;
        if(map!=mapId){Reset();map=mapId;}
        const bool sameCamera=cameraKnown && previousCamera[0]==camera[0] && previousCamera[1]==camera[1];
        // Equality is the same geometry contract used by tile updates. Never use
        // a queue pointer/signature as proof that heights have not changed.
        if(ReuseObservation(surfaces,mapId,camera,indexed,persistentMemo,stableRetention,reuseUpdate,reuseObservation))return;
        Partition(retentionReuse);
        observationKnown=false;
        // No full observation copy while walking. Seed after a stationary pair;
        // a changed camera immediately resumes the original history/eviction work.
        const bool remember=reuseObservation && sameCamera;
        previousCamera[0]=camera[0];previousCamera[1]=camera[1];cameraKnown=reuseObservation;
        previousIndexed=indexed;previousMemo=persistentMemo;previousStable=stableRetention;previousReuse=reuseUpdate;
        // Observation stamps are small, private contiguous bookkeeping. Geometry
        // and its index remain immutable across snapshots until a real edit.
        if(++observation==0){std::fill(observations.begin(),observations.end(),0);observation=1;}
        auto evict=[&](bool preserveObserved){
            std::vector<Key> removed;
            for(const auto& page:geometry->pages){
                if(retentionReuse && PageInside(page.first,camera)){++retentionGroupsSkipped;continue;}
                for(const auto& entry:*page.second){
                    ++retentionScans;const auto& tile=entry.second;
                    const double dx=tile.surface.anchor[0]-camera[0],dy=tile.surface.anchor[1]-camera[1];
                    if((!preserveObserved || observations[tile.slot]!=observation) && dx*dx+dy*dy>1024.0*1024.0)removed.push_back(entry.first);
                }
            }
            if(!removed.empty()){
                for(const auto& k:removed){freeSlots.push_back(FindTile(k)->slot);EraseTile(k);}
                geometry->dirty=changed=true;
            }
        };
        if(!stableRetention)evict(false);
        for(const auto& surface:surfaces){
            const auto k=key(surface);const auto* old=FindTile(k);
            uint32_t slot=0;
            if(!old){
                if(freeSlots.empty()){slot=uint32_t(observations.size());observations.push_back(0);}
                else{slot=freeSlots.back();freeSlots.pop_back();}
                SetTile(k,Tile{surface,slot});geometry->dirty=changed=true;
            }else{
                slot=old->slot;
                if(!same(old->surface,surface)){
                    SetTile(k,Tile{surface,slot});geometry->dirty=changed=true;
                }
            }
            observations[slot]=observation;
        }
        if(stableRetention)evict(true);
        if(geometry->count>32768){
            // Preserve all current input, including last-input-wins key ties.
            geometry=std::make_shared<Geometry>();geometry->partitioned=retentionReuse;observations.clear();freeSlots.clear();changed=true;
            for(const auto& surface:surfaces){
                const auto k=key(surface);const auto* old=FindTile(k);
                if(old)SetTile(k,Tile{surface,old->slot});
                else{const auto slot=uint32_t(observations.size());observations.push_back(observation);SetTile(k,Tile{surface,slot});}
            }
        }
        if(changed)++revision;
        if(!persistentMemo || changed)ClearQueries();
        if(indexed)Index();
        if(remember){previousSurfaces=std::make_shared<const std::vector<WaterSurface>>(surfaces);observationKnown=true;}
    }
    bool HeightAtIndexed(float x,float y,float& height,bool memoize=false){
        ++heightQueries;
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        uint32_t xb=0,yb=0;std::memcpy(&xb,&x,sizeof(x));std::memcpy(&yb,&y,sizeof(y));
        const uint64_t query=uint64_t(xb)|(uint64_t(yb)<<32);
        if(memoize){
            if(boundedMemo){if(fixedQueries && fixedQueries->Find(query,height)){++memoHits;return true;}}
            else{auto found=queries->find(query);if(found!=queries->end()){height=found->second;++memoHits;return true;}}
        }
        Index();if(geometry->nodes.empty())return false;
        double best=std::numeric_limits<double>::infinity();Key chosen={};bool known=false;
        Search(0,x,y,best,chosen,known,height);
        if(memoize && known){
            if(boundedMemo){
                if(!fixedQueries)fixedQueries=std::make_shared<FixedQueries>();
                else if(fixedQueries.use_count()!=1)fixedQueries=std::make_shared<FixedQueries>(*fixedQueries);
                memoReplacements+=fixedQueries->Insert(query,height);
            }else{
                if(queries->size()>=16384){++memoCapacityClears;ClearQueries();}
                EditQueries();queries->emplace(query,height);
            }
        }
        return known;
    }
    bool HeightAt(float x,float y,float& height) const{
        ++heightQueries;
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        double best=std::numeric_limits<double>::infinity();Key chosen={};bool known=false;
        for(const auto& page:geometry->pages)for(const auto& entry:*page.second){const auto& s=entry.second.surface;
            const double px=std::clamp(double(x),double(s.bounds[0]),double(s.bounds[3]));
            const double py=std::clamp(double(y),double(s.bounds[1]),double(s.bounds[4]));
            const double dx=px-x,dy=py-y,score=dx*dx+dy*dy;
            if(score>best || (score==best && known && !(entry.first<chosen)))continue;
            const double z=s.anchor[2]-(s.normal[0]*(px-s.anchor[0])+s.normal[1]*(py-s.anchor[1]))/s.normal[2];
            if(!std::isfinite(z))continue;
            height=float(z);best=score;chosen=entry.first;known=true;
        }
        return known;
    }
};
}
