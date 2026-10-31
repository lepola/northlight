#include "mesh_buffer_store.h"
#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <set>

namespace S=NorthlightMeshBufferStore;
using S::Key;
constexpr uint32_t K64=NorthlightMeshBufferStore::RoundBytes;
static Key key(uint64_t id,uint32_t vb=300000,uint32_t ib=100000){Key k;k.lo=id*0x9E3779B97F4A7C15ull+1;k.hi=~id;k.vertexBytes=vb;k.indexBytes=ib;return k;}
static uint32_t round64(uint32_t b){return (b+K64-1)/K64*K64;}
// What the renderer does for a claimed buffer: create the missing resources (Create* steps of the upload cursor).
static void createResources(S::Store& st){
    for(size_t u=0;u<st.staging().upload.size();++u){
        const uint32_t id=st.staging().uploadBuffer[u];const Key& k=st.staging().keys[st.staging().upload[u]];
        assert(st.writable(id)&&st.markWriting(id)&&!st.entries()[id].keyValid); // the old content dies before the first write
        if(st.entries()[id].vbCapacity<k.vertexBytes)assert(st.setCapacity(id,true,round64(k.vertexBytes)));
        if(st.entries()[id].ibCapacity<k.indexBytes)assert(st.setCapacity(id,false,round64(k.indexBytes)));
    }
}
static std::vector<Key> keys(std::initializer_list<uint64_t> ids){std::vector<Key> v;for(auto i:ids)v.push_back(key(i));return v;}
static S::Stats build(S::Store& st,const std::vector<Key>& k){assert(st.beginStaging(k));createResources(st);return st.commit();}
static void verifyInvariants(const S::Store& st){
    std::set<uint32_t> active(st.table().begin(),st.table().end());
    for(uint32_t id=0;id<st.entries().size();++id){
        const auto& e=st.entries()[id];
        if(active.count(id))assert(e.use==S::Use::Active&&e.usable());
        else assert(e.use!=S::Use::Active);
        if(e.keyValid)assert(e.usable());
        if(!st.stagingOpen())assert(e.use!=S::Use::Claimed);
    }
}
int main(){
    // Count allocations, not payload writes: reused idle resources cost no new VA.
    {S::Store st;uint64_t v=17,i=19;auto has=[&](uint32_t id,bool vertex){return vertex?st.entries()[id].vbCapacity!=0:st.entries()[id].ibCapacity!=0;};
     assert(!st.stagingGrowth(has,v,i)&&v==17&&i==19);assert(st.beginStaging(keys({1,2})));
     assert(st.stagingGrowth(has,v,i)&&v==2*round64(300000)&&i==2*round64(100000));createResources(st);st.commit();
     assert(st.beginStaging(keys({3})));createResources(st);st.commit(); // two old pages are idle
     assert(st.beginStaging(keys({4,5})));assert(st.stagingGrowth(has,v,i)&&v==0&&i==0);
     auto missing=[&](uint32_t id,bool vertex){return vertex?false:has(id,false);};
     assert(st.stagingGrowth(missing,v,i)&&v==2*round64(300000)&&i==0);
     assert(st.markWriting(st.staging().uploadBuffer[0]));st.cancel();verifyInvariants(st);
     assert(st.beginStaging({key(6,500000,100000)}));assert(st.stagingGrowth(has,v,i)&&v==round64(500000)&&i==0);st.cancel();}
    // A complete idle pool avoids an otherwise false 343 MiB upload refusal.
    {S::Store st;std::vector<Key> oldKeys,newKeys;for(unsigned n=0;n<64;++n){oldKeys.push_back(key(100+n));newKeys.push_back(key(1000+n));}
     build(st,oldKeys);build(st,{key(999)});assert(st.beginStaging(newKeys));uint64_t v=0,i=0;
     auto has=[&](uint32_t id,bool vertex){return vertex?st.entries()[id].vbCapacity!=0:st.entries()[id].ibCapacity!=0;};
     assert(st.stagingGrowth(has,v,i)&&!v&&!i);using namespace NorthlightGeometryMemory;
     auto oldBudget=uploadBudget(64ull*300000,64ull*100000,MiB,MiB);oldBudget.largest=18*MiB;
     auto newBudget=uploadBudget(v,i,MiB,MiB);newBudget.largest=18*MiB;
     const Sample sample{343*MiB,100*MiB,true};assert(!admits(sample,oldBudget)&&admits(sample,newBudget));
     st.cancel();verifyInvariants(st);}
    // Zero-hit: everything uploads into new buffers; the old table is untouched until commit.
    {S::Store st;auto s=build(st,keys({1,2,3,4}));
     assert(s.pages==4&&s.reusedPages==0&&s.uploadedPages==4&&st.table().size()==4&&s.uploadedCapacity==4*(round64(300000)+round64(100000)));
     for(uint32_t id:st.table())assert(st.entries()[id].keyValid);
     verifyInvariants(st);
     // Identical build: all hits, no upload, same buffers.
     const auto before=st.table();
     assert(st.beginStaging(keys({1,2,3,4}))&&st.staging().upload.empty()&&st.staging().stats.reusedPages==4);
     s=st.commit();assert(s.uploadedPages==0&&s.uploadedCapacity==0&&st.table()==before);verifyInvariants(st);
     // Replaced pages: misses claim NEW buffers (no idle yet), the active ones are never claimed.
     assert(st.beginStaging(keys({1,9,3,8})));
     for(uint32_t id:st.staging().uploadBuffer){for(uint32_t a:before)assert(a!=id);assert(st.entries()[id].use==S::Use::Claimed);}
     assert(st.staging().stats.reusedPages==2&&st.staging().stats.uploadedPages==2&&st.staging().upload==std::vector<uint32_t>({1,3}));
     createResources(st);s=st.commit();verifyInvariants(st);
     assert(st.table()[0]==before[0]&&st.table()[2]==before[2]&&st.table()[1]!=before[1]);
     // The replaced buffers are idle, keep their keys, and a build that wants them back reuses them without upload.
     assert(st.entries()[before[1]].use==S::Use::Idle&&st.entries()[before[1]].keyValid&&st.entries()[before[1]].key==key(2));
     assert(st.idleBytes()==2*(round64(300000)+round64(100000)));
     assert(st.beginStaging(keys({1,2,3,4}))&&st.staging().upload.empty()&&st.staging().table[1]==before[1]&&st.staging().table[3]==before[3]);
     s=st.commit();assert(s.reusedPages==4&&st.idleBytes()==2*(round64(300000)+round64(100000)));verifyInvariants(st);
     // Same pages in another order: still all hits.
     assert(st.beginStaging(keys({4,3,2,1}))&&st.staging().upload.empty());st.commit();verifyInvariants(st);}
    // New pages take idle buffers (invalid key first, then valid), not active ones; sizes must fit.
    {S::Store st;build(st,keys({1,2,3,4}));
     auto ids=st.table();
     build(st,keys({1,2,5,6}));                                  // 3,4 idle (valid keys); 5,6 in new buffers
     assert(st.entries()[ids[2]].use==S::Use::Idle&&st.entries()[ids[3]].use==S::Use::Idle);
     const size_t entriesBefore=st.entries().size();
     assert(st.beginStaging(keys({1,2,5,6,7,8})));                // two misses: the two idle buffers are claimed, nothing new is created
     assert(st.entries().size()==entriesBefore&&st.staging().stats.uploadedPages==2);
     for(uint32_t id:st.staging().uploadBuffer)assert(id==ids[2]||id==ids[3]);
     assert(st.entries()[ids[2]].keyValid&&st.entries()[ids[3]].keyValid);     // untouched claims still hold their content
     assert(st.markWriting(ids[2])&&!st.entries()[ids[2]].keyValid&&!st.markWriting(ids[0])&&st.entries()[ids[0]].keyValid); // a written claim is invalid; an active buffer is not writable
     st.cancel();                                                  // ids[2]: partial content (idle, invalid); ids[3]: never written, still valid
     assert(st.entries()[ids[2]].use==S::Use::Idle&&!st.entries()[ids[2]].keyValid&&st.entries()[ids[3]].use==S::Use::Idle&&st.entries()[ids[3]].keyValid);verifyInvariants(st);
     assert(st.beginStaging(keys({1,2,5,6,4}))&&st.staging().upload.empty()&&st.staging().table[4]==ids[3]);st.cancel();   // the untouched buffer is still a hit
     // The cancelled write does not resurrect the old key: page 3 misses now and takes the invalid-key buffer first.
     assert(st.beginStaging(keys({1,2,5,6,3})));
     assert(st.staging().stats.uploadedPages==1&&st.staging().uploadBuffer[0]==ids[2]);
     st.cancel();verifyInvariants(st);
     assert(st.table()==std::vector<uint32_t>({ids[0],ids[1],st.table()[2],st.table()[3]}));}
    // Capacity: a too-small idle buffer is skipped (a new one is created), a larger one is used.
    {auto setup=[](S::Store& st){build(st,{key(1,100000,60000),key(2,500000,500000),key(3,300000,100000)});auto ids=st.table();build(st,{key(9,1000,1000)});return ids;}; // 1,2,3 idle
     {S::Store st;auto ids=setup(st);
      assert(st.beginStaging({key(10,400000,200000)}));            // fits only the 500000/500000 buffer
      assert(st.staging().uploadBuffer[0]==ids[1]);st.cancel();}
     {S::Store st;auto ids=setup(st);
      assert(st.beginStaging({key(10,1000,1000),key(11,1000,1000)}));  // least capacity first: the two smallest idle buffers
      assert(st.staging().uploadBuffer[0]==ids[0]&&st.staging().uploadBuffer[1]==ids[2]);st.cancel();}
     {S::Store st;auto ids=setup(st);
      assert(st.beginStaging({key(10,900000,100)}));               // nothing big enough: the idle buffer that grows least is recreated, no new slot
      assert(st.staging().uploadBuffer[0]==ids[1]&&st.entries().size()==4);st.cancel();verifyInvariants(st);}
     {S::Store st;build(st,keys({1}));                            // no idle buffer at all: a new slot
      assert(st.beginStaging(keys({2}))&&st.staging().uploadBuffer[0]==1&&st.entries().back().empty());st.cancel();}
     {S::Store st;auto ids=setup(st);                             // a tiny page: the oversized buffers are tier 3, the smallest one wins
      assert(st.beginStaging({key(10,1000,1000)}));st.cancel();
      S::Store t;build(t,{key(1,500000,500000),key(2,200000,100000)});auto j=t.table();build(t,{key(9,1000,1000)}); // 1,2 idle valid
      assert(t.beginStaging({key(10,150000,60000)}));              // 2 fits within 2x: tier 1 beats the bigger buffer
      assert(t.staging().uploadBuffer[0]==j[1]);t.cancel();
      // An invalid oversized buffer is only taken after a close valid fit.
      assert(t.beginStaging({key(11,1000,1000)}));t.markWriting(t.staging().uploadBuffer[0]);const auto taken=t.staging().uploadBuffer[0];t.cancel();
      assert(taken==j[1]||taken==j[0]);
      assert(t.beginStaging({key(12,1000,1000)}));assert(t.staging().uploadBuffer[0]==taken);t.cancel();(void)ids;}}   // the invalid one is the cheapest to give up
    // Identical pages inside one build share one buffer and upload once.
    {S::Store st;assert(st.beginStaging(keys({5,5,6,5})));
     assert(st.staging().upload.size()==2&&st.staging().table[0]==st.staging().table[1]&&st.staging().table[1]==st.staging().table[3]&&st.staging().stats.reusedPages==2);
     createResources(st);st.commit();verifyInvariants(st);assert(st.activeBytes()==2*(round64(300000)+round64(100000)));}
    // Only claimed buffers may be created or written.
    {S::Store st;build(st,keys({1}));const uint32_t id=st.table()[0];
     assert(!st.writable(id)&&!st.setCapacity(id,true,K64)&&!st.writable(77));
     assert(st.beginStaging(keys({2}))&&st.writable(st.staging().uploadBuffer[0])&&!st.writable(id));
     assert(!st.beginStaging(keys({3})));                         // one staging at a time
     assert(st.trimIdle(UINT64_MAX,[](uint32_t){assert(false);})==0); // no trim while a staging is open
     st.cancel();}
    // Trim: only idle buffers; invalid keys first, then oldest idle; bounded; ids of the rest stay.
    {S::Store st;build(st,keys({1,2,3,4}));auto ids=st.table();
     build(st,keys({1,5,6,7}));                                   // 2,3,4 idle, stamp 2
     build(st,keys({1,5,6,8}));                                   // 7 idle, stamp 3 -> idle: 2,3,4 (older), 7
     assert(st.beginStaging(keys({1,5,6,8,9,10})));for(uint32_t id:st.staging().uploadBuffer)st.markWriting(id);st.cancel();   // claims two idle and writes them: invalid-key idle
     std::vector<uint32_t> order;
     const uint64_t per=round64(300000)+round64(100000);
     std::vector<bool> validAtDrop;
     const uint64_t freed=st.trimIdle(per*2,[&](uint32_t id){order.push_back(id);validAtDrop.push_back(st.entries()[id].keyValid);});
     assert(freed==per*2&&order.size()==2&&!validAtDrop[0]&&!validAtDrop[1]); // the two cancelled claims go first
     const auto beforeAll=st.poolBytes();
     for(uint32_t id:st.table())assert(st.entries()[id].use==S::Use::Active);
     // At least one buffer even if it is larger than maxBytes.
     assert(st.trimIdle(1,[&](uint32_t id){order.push_back(id);})==per&&order.size()==3);
     assert(st.poolBytes()==beforeAll-per);
     // Everything idle goes; active ones stay; the trailing empty slots are popped but ids of the rest are stable.
     const auto active=st.table();
     st.trimIdle(UINT64_MAX,[&](uint32_t){});
     assert(st.table()==active&&st.idleBytes()==0&&st.poolBytes()==st.activeBytes());verifyInvariants(st);}
    // Order check: invalid first (highest id first), then valid by stamp.
    {S::Store st;build(st,keys({1,2,3,4,5}));auto ids=st.table();
     build(st,keys({1}));                                         // 2,3,4,5 idle together (same stamp): by id
     build(st,keys({1,6}));                                       // page 6 takes the first of the equal idle buffers
     std::vector<uint32_t> order;st.trimIdle(UINT64_MAX,[&](uint32_t id){order.push_back(id);});
     assert(st.table()[1]==ids[1]&&(order==std::vector<uint32_t>{ids[2],ids[3],ids[4]}));
     S::Store t;build(t,keys({1,2}));auto j=t.table();build(t,keys({3,4}));   // 1,2 idle (stamp 2)
     build(t,keys({1,3,5}));                                      // 2 is idle valid, 4 idle valid(stamp 3): 1 reused; 5 claims one of the idle ones
     verifyInvariants(t);}
    {S::Store st;build(st,keys({1,2,3,4}));auto ids=st.table();build(st,keys({1,2,3}));build(st,keys({1,2}));  // idle: 4 since commit 2, 3 since commit 3
     std::vector<uint32_t> order;st.trimIdle(UINT64_MAX,[&](uint32_t id){order.push_back(id);});
     assert((order==std::vector<uint32_t>{ids[3],ids[2]})&&st.entries().size()==2);}  // oldest first; the empty tail is popped
    // Elided pages (0.3.216): the published key set is the ACTIVE buffers' keys; an elided page must hit, else nothing changes.
    {S::Store st;build(st,keys({1,2,3,4}));const auto ids=st.table();
     build(st,keys({1,2,5,6}));                                   // 3,4 idle (valid), 5,6 active
     auto active=st.activeKeys();
     assert(active.size()==4&&active.count(key(1))&&active.count(key(5))&&!active.count(key(3))&&!active.count(key(4)));
     const auto tableBefore=st.table();const auto poolBefore=st.poolBytes();
     // Elided pages that are resident hit: no claim, no upload.
     assert(st.beginStaging(keys({1,2,5,6,7}),{1,1,1,1,0})&&st.elidedMisses()==0&&st.staging().upload.size()==1);st.cancel();
     // An idle buffer is a hit too (an elided page whose buffer went idle in between).
     assert(st.beginStaging(keys({3,2,5,6}),{1,1,1,1})&&st.staging().upload.empty()&&st.staging().table[0]==ids[2]);st.cancel();
     // Its buffer released (trim): the elided page misses, nothing is claimed, the active table and every entry stay as they were.
     std::vector<S::Entry> before(st.entries());
     st.trimIdle(UINT64_MAX,[](uint32_t){});
     assert(!st.beginStaging(keys({3,2,5,6,8}),{1,1,1,1,0})&&st.elidedMisses()==1&&!st.stagingOpen()&&st.table()==tableBefore);
     for(const auto& e:st.entries())assert(e.use!=S::Use::Claimed);
     // The same page not elided uploads normally; no flags at all behave as before.
     assert(st.beginStaging(keys({3,2,5,6,8}),{0,1,1,1,0})&&st.elidedMisses()==0&&st.staging().upload.size()==2);st.cancel();
     assert(st.beginStaging(keys({3,2,5,6,8}))&&st.staging().upload.size()==2);st.cancel();
     (void)poolBefore;(void)before;
     // After a reset nothing is resident.
     st.reset();assert(st.activeKeys().empty()&&!st.beginStaging(keys({1}),{1}));assert(st.beginStaging(keys({1}),{0}));st.cancel();}
    // Same content can have different Reset lifetimes. Filter existing hits,
    // while cancelled staging and compatible idle reuse keep their old rules.
    {S::Store st;build(st,keys({71,72}));const auto original=st.table();
     std::vector<unsigned> pool(st.entries().size(),0);
     auto managed=[&](uint32_t id){return id<pool.size()&&pool[id]==0;};
     auto temporary=[&](uint32_t id){return id<pool.size()&&pool[id]==1;};
     assert(st.activeKeys(managed).size()==2&&st.activeKeys(temporary).empty()&&st.activeKeys().size()==2);
     const auto bytes=st.poolBytes();
     assert(!st.beginStaging(keys({71,72}),{1,1},temporary)&&st.elidedMisses()==2&&!st.stagingOpen());
     assert(st.table()==original&&st.poolBytes()==bytes);verifyInvariants(st);
     assert(st.beginStaging(keys({71,72}),{},temporary)&&st.staging().stats.uploadedPages==2);
     for(auto id:st.staging().uploadBuffer)assert(std::find(original.begin(),original.end(),id)==original.end());
     st.cancel();assert(st.table()==original);verifyInvariants(st);
     assert(st.beginStaging(keys({71,72}),{},temporary));pool.resize(st.entries().size());
     for(auto id:st.staging().uploadBuffer)pool[id]=1;
     createResources(st);st.commit();assert(st.activeKeys(managed).empty()&&st.activeKeys(temporary).size()==2);
     // Prefer compatible idle originals over incompatible active twins.
     assert(st.beginStaging(keys({71,72}),{},managed)&&st.staging().upload.empty()&&st.staging().table==original);
     st.commit();assert(st.activeKeys(managed).size()==2&&st.activeKeys(temporary).empty());verifyInvariants(st);
     // A mixed generation reuses compatible hits and uploads only the new key.
     assert(st.beginStaging(keys({71,72,73,73}),{1,1,0,0},managed));
     assert(st.staging().stats.reusedPages==3&&st.staging().stats.uploadedPages==1&&st.staging().table[2]==st.staging().table[3]);
     createResources(st);st.commit();verifyInvariants(st);
    }
    // Randomised: active buffers are never touched, committed tables are consistent, cancels leave no claims.
    {std::mt19937 rng(7);S::Store st;
     for(int round=0;round<4000;++round){
         std::vector<Key> k;const unsigned n=1+rng()%40;
         for(unsigned i=0;i<n;++i)k.push_back(key(rng()%60,(1+rng()%16)*K64/2+rng()%1000,(1+rng()%8)*K64/2));
         struct Snap {uint32_t id;uint32_t vb,ib;Key key;};std::vector<Snap> active;
         for(uint32_t id:st.table())active.push_back({id,st.entries()[id].vbCapacity,st.entries()[id].ibCapacity,st.entries()[id].key});
         assert(st.beginStaging(k));
         for(uint32_t id:st.staging().uploadBuffer){assert(st.writable(id));for(const auto& a:active)assert(a.id!=id);}
         for(const auto& a:active){const auto& e=st.entries()[a.id];assert(e.use==S::Use::Active&&e.keyValid&&e.key==a.key&&e.vbCapacity==a.vb&&e.ibCapacity==a.ib);}
         for(uint32_t i=0;i<k.size();++i){const uint32_t id=st.staging().table[i];assert(id!=S::None);
             const auto& e=st.entries()[id];if(e.use!=S::Use::Claimed)assert(e.keyValid&&e.key==k[i]);}
         if(rng()%5==0){st.cancel();for(const auto& e:st.entries())assert(e.use!=S::Use::Claimed);for(const auto& a:active)assert(st.entries()[a.id].use==S::Use::Active);}
         else{createResources(st);
              for(uint32_t u=0;u<st.staging().upload.size();++u){const auto& e=st.entries()[st.staging().uploadBuffer[u]];const Key& kk=k[st.staging().upload[u]];assert(e.vbCapacity>=kk.vertexBytes&&e.ibCapacity>=kk.indexBytes);}
              const auto stats=st.commit();assert(stats.pages==k.size()&&stats.reusedPages+stats.uploadedPages==k.size());
              for(uint32_t i=0;i<k.size();++i){const auto& e=st.entries()[st.table()[i]];assert(e.use==S::Use::Active&&e.keyValid&&e.key==k[i]);}}
         verifyInvariants(st);
         if(rng()%7==0){const auto before=st.table();st.trimIdle(rng()%4?UINT64_MAX:K64*3,[&](uint32_t id){assert(st.entries()[id].use==S::Use::Idle);});assert(st.table()==before);verifyInvariants(st);}
     }}
    std::puts("mesh buffer store ok");
}
