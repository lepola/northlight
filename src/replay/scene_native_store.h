#pragma once
#include "replay_gpu_batches.h"
#include <array>
#include <cassert>

namespace NorthlightGpuScene {
using Mesh=NorthlightDrawSnapshot::Mesh;
using Owner=std::shared_ptr<const Mesh>;
using Placement=NorthlightReplayGPU::Placement;
enum class Consumer:unsigned {Material=0,Shadow=1};
class NativeStore {
public:
 using Stats=NorthlightReplayGPU::Cache::Stats;
 using Population=NorthlightReplayGPU::Cache::Population;
 using ClearStats=NorthlightReplayGPU::Cache::ClearStats;
 using BatchStats=NorthlightReplayGPU::BatchedCache::BatchStats;
 struct Counters {size_t uploaded=0,reused=0;unsigned hits=0,crossOwner=0,reconstructed=0;};
private:
 struct Batch {
  IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
  size_t capacity=0,liveBytes=0;unsigned live=0,owner=0;
  ~Batch(){if(vb)vb->Release();if(ib)ib->Release();}
 };
 struct Entry {
  std::weak_ptr<const Mesh> source;NorthlightSourceIdentity::Handle identity;const Mesh* sourceAddress=nullptr;
  std::uint64_t hash=0,first=0,touched=0;unsigned owner=0,users=0;
  size_t metadata=0,bytes=0,reserved=0;Batch* batch=nullptr;bool pending=false;
  IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;Placement at;
  Entry* previous=nullptr;Entry* next=nullptr;
  ~Entry(){for(auto p:vb)if(p)p->Release();if(ib)ib->Release();}
 };
 struct Prehashed {
  size_t operator()(std::uint64_t value)const noexcept{
   if constexpr(sizeof(size_t)<sizeof(value))return size_t(value)^size_t(value>>32);
   else return size_t(value);
  }
 };
 using Table=std::unordered_multimap<std::uint64_t,std::unique_ptr<Entry>,Prehashed>;
 Table entries_;std::vector<std::unique_ptr<Batch>> batches_;
 std::array<std::vector<Entry*>,2> pending_;
 Entry* oldest_=nullptr;Entry* newest_=nullptr;IDirect3DDevice9* device_=nullptr;
 std::uint64_t frame_=0;bool explicitClock_=false;std::uint16_t untrackedEntries_=0;
 size_t limit_[2]={32u<<20,64u<<20},gpu_[2]={},metadata_[2]={},reserved_[2]={},attempted_[2]={};
 size_t retain_[2]={32u<<20,64u<<20};
 unsigned attempts_[2]={};bool ignoreTime_[2]={},timeDeferred_[2]={};
 Stats stats_[2];Counters counters_[2];ClearStats clears_[2];BatchStats batchStats_[2];
 static constexpr size_t EntryLimit=8192,UploadLimit=4u<<20;
 static constexpr unsigned AttemptLimit=16;
 static unsigned index(Consumer c){return unsigned(c);}
 static const auto& identity(const Mesh& mesh){return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;}
 static std::uint64_t hash(const Owner& mesh){const auto& id=identity(*mesh);return id?id->hash:std::uint64_t(reinterpret_cast<std::uintptr_t>(mesh.get()));}
 static bool sameSource(const Entry& entry,const Owner& mesh){
  return entry.sourceAddress==mesh.get()&&!entry.source.owner_before(mesh)&&!mesh.owner_before(entry.source);
 }
 auto find(const Owner& mesh){
  auto range=entries_.equal_range(hash(mesh));const auto& id=identity(*mesh);
  for(auto it=range.first;it!=range.second;++it){const auto& e=*it->second;
   if(id?NorthlightSourceIdentity::same(id,e.identity):!e.identity&&(sameSource(e,mesh)||e.source.lock()==mesh))return it;
  }return entries_.end();
 }
 void unlink(Entry& e){if(e.previous)e.previous->next=e.next;else oldest_=e.next;if(e.next)e.next->previous=e.previous;else newest_=e.previous;e.previous=e.next=nullptr;}
 void append(Entry& e){e.previous=newest_;e.next=nullptr;if(newest_)newest_->next=&e;else oldest_=&e;newest_=&e;}
 void touch(Entry& e){if(e.touched==frame_)return;e.touched=frame_;if(newest_!=&e){unlink(e);append(e);++stats_[e.owner].lruMoves;}}
 size_t charged(unsigned lane)const{return gpu_[lane]+metadata_[lane]+reserved_[lane];}
 void dropBatch(Batch* batch){gpu_[batch->owner]-=batch->capacity;auto it=std::find_if(batches_.begin(),batches_.end(),[&](const auto& b){return b.get()==batch;});assert(it!=batches_.end());batches_.erase(it);}
 void release(Entry& e){
  if(e.pending){auto& list=pending_[e.owner];list.erase(std::find(list.begin(),list.end(),&e));reserved_[e.owner]-=e.reserved;e.reserved=0;e.pending=false;}
  if(e.batch){auto* b=e.batch;b->liveBytes-=e.bytes;e.batch=nullptr;if(!--b->live)dropBatch(b);}
  else if(e.bytes){gpu_[e.owner]-=e.bytes;for(auto& p:e.vb){if(p)p->Release();p=nullptr;}if(e.ib)e.ib->Release();e.ib=nullptr;}
  e.bytes=0;e.at={};
 }
 void erase(Entry& e){
  release(e);metadata_[e.owner]-=e.metadata;unlink(e);auto range=entries_.equal_range(e.hash);
  for(auto it=range.first;it!=range.second;++it)if(it->second.get()==&e){untrackedEntries_-=!e.identity;entries_.erase(it);return;}assert(false);
 }
 void evict(Entry& e,unsigned cause,bool current=false){
  auto* batch=e.batch;const unsigned siblings=batch?batch->live-1:0;
  ++stats_[cause].evictions;(e.bytes?stats_[cause].residentEvictions:stats_[cause].probationEvictions)++;
  erase(e);if(!siblings)return;
  for(auto it=entries_.begin();it!=entries_.end();){Entry& other=*it->second;++it;
   if(other.batch==batch&&(current||other.touched!=frame_)){++stats_[cause].evictions;++stats_[cause].residentEvictions;erase(other);}}
 }
 bool room(unsigned lane,size_t extra,bool promotion,bool current=false,size_t ceiling=SIZE_MAX){
  ceiling=std::min(ceiling,limit_[lane]);
  const bool count=!promotion&&entries_.size()>=EntryLimit;const bool bytes=charged(lane)+extra>ceiling;
  stats_[lane].countPressure+=count;stats_[lane].bytePressure+=bytes;
  while(charged(lane)+extra>ceiling||(!promotion&&entries_.size()>=EntryLimit)){
   ++stats_[lane].evictionChecks;Entry* victim=nullptr;
   for(auto* e=oldest_;e;e=e->next){
    if(!current&&e->touched==frame_)continue;
    if(charged(lane)+extra>ceiling&&e->owner!=lane)continue;
    if(charged(lane)+extra>ceiling){victim=e;break;}
    if(!victim)victim=e;if(!e->bytes&&!e->pending){victim=e;break;}
   }
   if(!victim){++stats_[lane].roomRejected;return false;}evict(*victim,lane,current);
  }return true;
 }
 bool transferMetadata(Entry& e,unsigned lane){
  if(e.owner==lane)return true;if(!room(lane,e.metadata,true))return false;
  metadata_[e.owner]-=e.metadata;metadata_[lane]+=e.metadata;e.owner=lane;return true;
 }
 template<class Buffer>static bool write(Buffer* b,UINT offset,const void* data,size_t bytes){
  void* out=nullptr;if(FAILED(b->Lock(offset,UINT(bytes),&out,NorthlightUpload::FreshBufferLock)))return false;
  if(out)std::memcpy(out,data,bytes);NorthlightLockMeter::staging(bytes);const auto hr=b->Unlock();return out&&SUCCEEDED(hr);
 }
 static bool batchable(const Mesh& m){return m.streams[0].stride&&!m.streams[0].bytes.empty()&&m.streams[0].bytes.size()%m.streams[0].stride==0&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();}
 bool separate(Entry& e,const Mesh& m){
  Entry built;
  for(unsigned s=0;s<4;++s){auto& bytes=m.streams[s].bytes;if(bytes.empty())continue;
   if(FAILED(device_->CreateVertexBuffer(UINT(bytes.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built.vb[s],nullptr))||!built.vb[s]||!write(built.vb[s],0,bytes.data(),bytes.size()))return false;}
  if(!m.indices.empty()){const UINT size=UINT(m.indices.size()*4);if(FAILED(device_->CreateIndexBuffer(size,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built.ib,nullptr))||!built.ib||!write(built.ib,0,m.indices.data(),size))return false;}
  for(unsigned s=0;s<4;++s)std::swap(e.vb[s],built.vb[s]);std::swap(e.ib,built.ib);e.bytes=m.byteSize();gpu_[e.owner]+=e.bytes;++batchStats_[e.owner].separateUploads;return true;
 }
 void commitPrepared(unsigned lane){
  auto& list=pending_[lane];if(list.empty())return;
  assert(list.size()<=AttemptLimit);std::array<Entry*,AttemptLimit> members;std::array<Owner,AttemptLimit> meshes;size_t memberCount=0,vertices=0,indices=0;
  try{batches_.reserve(batches_.size()+1);
   for(auto* e:list){auto m=e->source.lock();if(!m)continue;auto stride=m->streams[0].stride;
    e->at.vertexBase=UINT((vertices+stride-1)/stride);vertices=size_t(e->at.vertexBase)*stride+m->streams[0].bytes.size();
    e->at.indexStart=UINT(indices/4);indices+=m->indices.size()*4;members[memberCount]=e;meshes[memberCount++]=std::move(m);}
  }catch(...){while(!list.empty())release(*list.back());return;}
  // Ending reservations must preserve the layout just calculated.
  for(auto* e:list){reserved_[lane]-=e->reserved;e->reserved=0;e->pending=false;}list.clear();if(!memberCount)return;
  struct Timer {double& target;std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();~Timer(){target+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}} timer{stats_[lane].createMs};
  auto batch=std::make_unique<Batch>();batch->capacity=vertices+indices;batch->owner=lane;
  auto fail=[&]{++batchStats_[lane].batchFailures;for(size_t i=0;i<memberCount;++i)members[i]->at={};};
  if(FAILED(device_->CreateVertexBuffer(UINT(vertices),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vb,nullptr))||!batch->vb||
    (indices&&(FAILED(device_->CreateIndexBuffer(UINT(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->ib,nullptr))||!batch->ib))){fail();return;}
  void* out=nullptr;if(FAILED(batch->vb->Lock(0,UINT(vertices),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
  if(out)for(size_t i=0;i<memberCount;++i){const auto& v=meshes[i]->streams[0];std::memcpy(static_cast<uint8_t*>(out)+size_t(members[i]->at.vertexBase)*v.stride,v.bytes.data(),v.bytes.size());}
  NorthlightLockMeter::staging(vertices);auto hr=batch->vb->Unlock();if(!out||FAILED(hr)){fail();return;}
  if(indices){out=nullptr;if(FAILED(batch->ib->Lock(0,UINT(indices),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
   if(out)for(size_t i=0;i<memberCount;++i)if(!meshes[i]->indices.empty())std::memcpy(static_cast<uint8_t*>(out)+size_t(members[i]->at.indexStart)*4,meshes[i]->indices.data(),meshes[i]->indices.size()*4);
   NorthlightLockMeter::staging(indices);hr=batch->ib->Unlock();if(!out||FAILED(hr)){fail();return;}}
  auto* b=batch.get();batches_.push_back(std::move(batch));gpu_[lane]+=b->capacity;
  for(size_t i=0;i<memberCount;++i){auto& e=*members[i];e.batch=b;e.bytes=meshes[i]->byteSize();++b->live;b->liveBytes+=e.bytes;counters_[lane].uploaded+=e.bytes;++stats_[lane].created;++batchStats_[lane].batchedUploads;}
 }
 void commit(unsigned lane)noexcept{
  try{commitPrepared(lane);}catch(...){++batchStats_[lane].batchFailures;while(!pending_[lane].empty())release(*pending_[lane].back());for(auto& item:entries_)if(!item.second->bytes)item.second->at={};}
 }
 void bindReady(const Entry& e,const Mesh& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Placement& at){
  IDirect3DVertexBuffer9* streams[4]={e.batch?e.batch->vb:e.vb[0],e.vb[1],e.vb[2],e.vb[3]};
  for(unsigned s=0;s<4;++s)if(vb[s]!=streams[s]){if(vb[s])vb[s]->Release();vb[s]=streams[s];if(vb[s])vb[s]->AddRef();}
  auto* index=e.batch?(mesh.indices.empty()?nullptr:e.batch->ib):e.ib;
  if(ib!=index){if(ib)ib->Release();ib=index;if(ib)ib->AddRef();}at=e.at;
 }
 bool preserve(Entry& entry,unsigned other){
  if(!(entry.users&(1u<<other))){
   if(!entry.batch)return false;
   // An unused oldest entry must not evict siblings still used by another pass.
   const bool shared=std::any_of(entries_.begin(),entries_.end(),[&](const auto& item){return item.second->batch==entry.batch&&(item.second->users&(1u<<other));});
   if(!shared)return false;
  }
  const auto source=entry.owner;
  if(entry.batch){auto* batch=entry.batch;size_t meta=0;for(auto& item:entries_)if(item.second->batch==batch)meta+=item.second->metadata;
   if(batch->capacity+meta>retain_[other]||!room(other,batch->capacity+meta,true,true,retain_[other]))return false;
   gpu_[source]-=batch->capacity;gpu_[other]+=batch->capacity;batch->owner=other;
   for(auto it=entries_.begin();it!=entries_.end();){auto& e=*it->second;++it;
    if(e.batch!=batch)continue;
    metadata_[source]-=e.metadata;metadata_[other]+=e.metadata;e.owner=other;
    if(!e.users)erase(e);
   }return true;
  }
  if(entry.pending)release(entry);
  const auto need=entry.bytes+entry.metadata;if(need>retain_[other]||!room(other,need,true,true,retain_[other]))return false;
  gpu_[source]-=entry.bytes;gpu_[other]+=entry.bytes;metadata_[source]-=entry.metadata;metadata_[other]+=entry.metadata;entry.owner=other;return true;
 }
public:
 NativeStore()=default;NativeStore(const NativeStore&)=delete;NativeStore& operator=(const NativeStore&)=delete;
 ~NativeStore(){clearAll();}
 void advance(std::uint64_t frame){
  if(frame_==frame)return;frame_=frame;for(unsigned i=0;i<2;++i){stats_[i]={};counters_[i]={};attempted_[i]=attempts_[i]=0;timeDeferred_[i]=false;}
  if(untrackedEntries_)for(auto it=entries_.begin();it!=entries_.end();){auto& e=*it->second;++it;if(!e.identity&&e.source.expired()){++stats_[e.owner].expiredEntries;stats_[e.owner].expiredBytes+=e.bytes;erase(e);}}
  commit(0);commit(1);
 }
 void setFrame(std::uint64_t frame){explicitClock_=true;advance(frame);}
 bool currentFrame(std::uint64_t frame)const{return explicitClock_&&frame_==frame;}
 void boundary(Consumer consumer){if(!explicitClock_)advance(frame_+1);auto lane=index(consumer);stats_[lane]={};counters_[lane]={};}
 std::uint64_t frame()const{return frame_;}
 template<class Admission>bool bind(Consumer consumer,IDirect3DDevice9* device,const Owner& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit,Placement& at){
  const auto lane=index(consumer);at={};timeDeferred_[lane]=false;if(!mesh||!mesh->byteSize())return false;if(device_&&device_!=device)return false;device_=device;retain_[lane]=limit_[lane];
  try{auto it=find(mesh);
   if(it==entries_.end()){
    auto e=std::make_unique<Entry>();e->source=mesh;e->sourceAddress=mesh.get();e->identity=identity(*mesh);e->hash=hash(mesh);e->first=e->touched=frame_;e->owner=lane;e->users=1u<<lane;
    e->metadata=sizeof(Entry)+64+(e->identity?e->identity->bytes():0);
    if(e->metadata>limit_[lane]||!room(lane,e->metadata,false))return false;
    auto inserted=entries_.emplace(e->hash,std::move(e));untrackedEntries_+=!inserted->second->identity;metadata_[lane]+=inserted->second->metadata;append(*inserted->second);++stats_[lane].warmup;return false;
   }
   auto& e=*it->second;const bool wasResident=e.bytes!=0;e.users|=1u<<lane;touch(e);
   if(e.identity){++stats_[lane].identityHits;if(!sameSource(e,mesh)){
    if(e.source.lock()!=mesh){++stats_[lane].reconstructedHits;++counters_[lane].reconstructed;}e.source=mesh;e.sourceAddress=mesh.get();}}
   if(e.pending)return false;
   if(!e.bytes){
    const auto bytes=mesh->byteSize();if(e.first==frame_){++stats_[lane].warmup;return false;}
    if(attempts_[lane]>=AttemptLimit){++stats_[lane].uploadDeferred;++stats_[lane].attemptDeferred;return false;}
    if(bytes>UploadLimit-attempted_[lane]){++stats_[lane].uploadDeferred;++stats_[lane].uploadByteDeferred;return false;}
    if(NorthlightReplayGPU::CreateTimeBudget&&!ignoreTime_[lane]&&attempts_[lane]&&stats_[lane].createMs>=NorthlightReplayGPU::createBudgetMs()){++stats_[lane].uploadDeferred;++stats_[lane].timeDeferred;timeDeferred_[lane]=true;return false;}
    if(!admit(bytes)){++stats_[lane].admissionRejected;return false;}
    // Admission may submit other work; keep the pending-array and byte bounds valid afterwards.
    if(attempts_[lane]>=AttemptLimit){++stats_[lane].uploadDeferred;++stats_[lane].attemptDeferred;return false;}
    if(bytes>UploadLimit-attempted_[lane]){++stats_[lane].uploadDeferred;++stats_[lane].uploadByteDeferred;return false;}
    const bool batched=batchable(*mesh);const size_t reserve=bytes+(batched?mesh->streams[0].stride:0);
    if(!transferMetadata(e,lane)||!room(lane,reserve,true))return false;
    ++attempts_[lane];attempted_[lane]+=bytes;
    if(batched){pending_[lane].push_back(&e);e.pending=true;e.reserved=reserve;reserved_[lane]+=reserve;if(!ignoreTime_[lane])return false;commit(lane);if(!e.bytes)return false;}
    else{const auto start=std::chrono::steady_clock::now();bool ready=separate(e,*mesh);stats_[lane].createMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(!ready)return false;counters_[lane].uploaded+=e.bytes;++stats_[lane].created;}
   }
   if(wasResident){++counters_[lane].hits;counters_[lane].reused+=e.bytes;}counters_[lane].crossOwner+=e.owner!=lane;bindReady(e,*mesh,vb,ib,at);return true;
  }catch(...){return false;}
 }
 bool commitPending(Consumer consumer){const auto lane=index(consumer);const auto before=gpu_[lane];commit(lane);return before!=gpu_[lane];}
 bool resident(const Owner& mesh){if(!mesh)return false;auto it=find(mesh);return it!=entries_.end()&&it->second->bytes;}
 void ignoreTime(Consumer consumer,bool on){ignoreTime_[index(consumer)]=on;}
 bool timeDeferred(Consumer consumer)const{return timeDeferred_[index(consumer)];}
 size_t bytes(Consumer consumer)const{return gpu_[index(consumer)];}
 size_t chargedBytes(Consumer consumer)const{return charged(index(consumer));}
 size_t limit(Consumer consumer)const{return limit_[index(consumer)];}
 const Counters& counters(Consumer consumer)const{return counters_[index(consumer)];}
 const Stats& stats(Consumer consumer){auto lane=index(consumer);stats_[lane].identityBytes=metadata_[lane];return stats_[lane];}
 const ClearStats& clearStats(Consumer consumer)const{return clears_[index(consumer)];}
 Population population(Consumer consumer)const{Population p;for(const auto& item:entries_)if(item.second->owner==index(consumer)){++p.entries;p.resident+=item.second->bytes!=0;}p.probation=p.entries-p.resident;return p;}
 BatchStats batchStats(Consumer consumer)const{auto result=batchStats_[index(consumer)];result.batches=result.batchBytes=result.liveBytes=0;for(auto& b:batches_)if(b->owner==index(consumer)){++result.batches;result.batchBytes+=b->capacity;result.liveBytes+=b->liveBytes;}result.separateBytes=gpu_[index(consumer)]-result.batchBytes;return result;}
 size_t metadataBytes(Consumer consumer)const{return metadata_[index(consumer)];}
 template<class Visit>void buffers(Visit visit)const{for(const auto& batch:batches_){visit(batch->vb);visit(batch->ib);}for(const auto& item:entries_)if(!item.second->batch){for(auto* vb:item.second->vb)visit(vb);visit(item.second->ib);}}
 size_t bytes()const{return gpu_[0]+gpu_[1];}size_t chargedBytes()const{return charged(0)+charged(1);}
 static constexpr size_t entryLimit(){return EntryLimit;}
 void clearAll(){oldest_=newest_=nullptr;for(auto& p:pending_)p.clear();entries_.clear();untrackedEntries_=0;batches_.clear();device_=nullptr;for(unsigned i=0;i<2;++i)gpu_[i]=metadata_[i]=reserved_[i]=0;}
 void trim(Consumer consumer,size_t bytes){const auto lane=index(consumer);retain_[lane]=std::min(limit_[lane],bytes);while(charged(lane)>bytes){Entry* victim=oldest_;while(victim&&victim->owner!=lane)victim=victim->next;if(!victim)break;if(!preserve(*victim,1-lane))evict(*victim,lane,true);}}
 void setLimit(Consumer consumer,size_t bytes){const auto lane=index(consumer);limit_[lane]=std::min(bytes,size_t(lane?64u<<20:32u<<20));trim(consumer,limit_[lane]);}
 void clear(Consumer consumer){const auto lane=index(consumer);++clears_[lane].calls;clears_[lane].entries+=population(consumer).entries;clears_[lane].bytes+=gpu_[lane];for(auto& item:entries_)item.second->users&=~(1u<<lane);trim(consumer,0);}
};
class NativeClient {
 NativeStore& store_;Consumer consumer_;
public:
 using Stats=NativeStore::Stats;using Population=NativeStore::Population;using ClearStats=NativeStore::ClearStats;
 NativeClient(NativeStore& store,Consumer consumer):store_(store),consumer_(consumer){}
 void clear(){store_.clear(consumer_);}void beginFrame(){store_.boundary(consumer_);}
 void trimTo(size_t bytes){store_.trim(consumer_,bytes);}void setLimit(size_t bytes){store_.setLimit(consumer_,bytes);}
 size_t limit()const{return store_.limit(consumer_);}size_t bytes()const{return store_.bytes(consumer_);}size_t chargedBytes()const{return store_.chargedBytes(consumer_);}size_t queueBytes()const{return 0;}
 size_t uploaded()const{return store_.counters(consumer_).uploaded;}size_t reused()const{return store_.counters(consumer_).reused;}unsigned hits()const{return store_.counters(consumer_).hits;}
 const Stats& stats()const{return store_.stats(consumer_);}const ClearStats& clearStats()const{return store_.clearStats(consumer_);}Population population()const{return store_.population(consumer_);}
 static constexpr size_t entryLimit(){return NativeStore::entryLimit();}bool timeDeferredLast()const{return store_.timeDeferred(consumer_);}void ignoreTimeBudget(bool on){store_.ignoreTime(consumer_,on);}
 const auto& counters()const{return store_.counters(consumer_);}
 auto batchStats()const{return store_.batchStats(consumer_);}bool commitPending(){return store_.commitPending(consumer_);}bool resident(const Owner& mesh){return store_.resident(mesh);}
 template<class Admission>bool bind(IDirect3DDevice9* device,const Owner& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit,Placement& at){return store_.bind(consumer_,device,mesh,vb,ib,admit,at);}
};
}

namespace NorthlightReplayGPU {
inline auto batchStats(const NorthlightGpuScene::NativeClient& c){return c.batchStats();}
template<class Replays,class Bind>void commitAdmissions(NorthlightGpuScene::NativeClient& c,Replays& replays,Bind bind){
 if(c.commitPending())for(auto& p:replays)if(!p->gpuCached&&c.resident(p->shared))bind(*p);
}
template<class Replay,class Admission>bool bindResident(NorthlightGpuScene::NativeClient& c,IDirect3DDevice9* d,Replay& p,bool allowCache,Admission admit){
 Placement at;p.gpuCached=allowCache&&c.bind(d,p.shared,p.stream,p.index,admit,at);
 if(p.gpuCached){for(unsigned s=0;s<4;++s){p.offset[s]=0;p.stride[s]=p.mesh().streams[s].stride;}
  p.start=p.indexed?at.indexStart:at.vertexBase;p.base=p.indexed?INT(at.vertexBase):0;}
 else p.base=0;return p.gpuCached;
}
}
