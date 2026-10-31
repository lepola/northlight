#include "world_mesh_pages.h"
#include "shadow_terrain.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace NorthlightGI;
namespace P=NorthlightWorldMeshPages;
using NorthlightWorldMesh::WorldMeshUploadPlan;
static WorldVertex vertex(uint32_t i) {
    return {{float(i%123)-50.f,float(i%327)-100.f,float(i%91)-20.f},
            {float(i%11)*.125f,float(i%17)*-.0625f,float(i%7)*.25f},
            float(i%113)*.03125f,float(i%37)*-.125f};
}
static void initialize(WorldScene& source,WorldMeshUploadPlan& input,uint32_t vertices) {
    for(uint32_t i=0;i<vertices;++i)source.vertices.push_back(vertex(i));
    input.source=&source;input.vertexCount=vertices;input.materials.resize(3);
}
static void addBatch(WorldMeshUploadPlan& input,uint32_t triangles,uint32_t first,uint32_t span,int material=0) {
    NorthlightWorldMesh::Batch b;b.start=uint32_t(input.indices.size());b.count=triangles;b.material=material;
    b.terrain=material==1;b.chunkX=b.terrain?132:-1;b.chunkY=b.terrain?321:-1;
    for(uint32_t i=0;i<triangles*3;++i)input.indices.push_back(first+i%span);
    input.batches.push_back(b);input.triangleCount+=triangles;
}
static void verify(const WorldMeshUploadPlan& input,const P::Plan& output) {
    size_t originalBatch=0,originalTriangle=0,total=0;
    uint64_t vb=0,ib=0;
    for(const auto& page:output.pages){
        assert(!page.vertices.empty()&&!page.indices.empty());
        assert(page.vertices.size()*sizeof(WorldVertex)<=P::PageVertexByteLimit);
        assert(page.indices.size()*sizeof(uint16_t)<=P::PageIndexByteLimit);
        vb+=page.vertices.size()*sizeof(WorldVertex);ib+=page.indices.size()*sizeof(uint16_t);
    }
    std::vector<size_t> pageNext(output.pages.size());
    for(const auto& b:output.batches){
        assert(originalBatch<input.batches.size());const auto& before=input.batches[originalBatch];
        assert(b.page<output.pages.size());const auto& page=output.pages[b.page];
        assert(b.start==pageNext[b.page]);pageNext[b.page]+=size_t(b.count)*3;
        assert(b.count&&b.start+size_t(b.count)*3<=page.indices.size());
        assert(b.material==before.material&&b.terrain==before.terrain&&b.chunkX==before.chunkX&&b.chunkY==before.chunkY);
        assert(originalTriangle+b.count<=before.count);
        const float inf=std::numeric_limits<float>::infinity();Vec3 lo(inf,inf,inf),hi(-inf,-inf,-inf);
        uint32_t min=UINT32_MAX,max=0;
        for(uint32_t t=0;t<b.count;++t){
            for(unsigned k=0;k<3;++k){
                uint32_t originalIndex=input.indices[before.start+(originalTriangle+t)*3+k];
                uint32_t actualIndex=page.indices[b.start+t*3+k];assert(actualIndex<page.vertices.size());
                assert(std::memcmp(&input.source->vertices[originalIndex],&page.vertices[actualIndex],sizeof(WorldVertex))==0);
                const auto& v=page.vertices[actualIndex].position;
                lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);
                hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);
                min=std::min(min,actualIndex);max=std::max(max,actualIndex);
            }
        }
        assert(b.minVertex==min&&b.vertexCount==max-min+1);
        assert(b.boundsLow.x==lo.x&&b.boundsLow.y==lo.y&&b.boundsLow.z==lo.z);
        assert(b.boundsHigh.x==hi.x&&b.boundsHigh.y==hi.y&&b.boundsHigh.z==hi.z);
        total+=b.count;originalTriangle+=b.count;
        if(originalTriangle==before.count){++originalBatch;originalTriangle=0;}
    }
    for(size_t i=0;i<output.pages.size();++i)assert(pageNext[i]==output.pages[i].indices.size());
    assert(total==input.triangleCount&&originalBatch==input.batches.size());
    assert(output.vertexBytes==vb&&output.indexBytes==ib&&output.cpuBytes()>=vb+ib);
}
static const P::Options sequential{0,0}; // no content anchors: exact limit-driven layouts
static void testIndexMap(){
    P::IndexMap map;
    for(uint32_t pass=0;pass<16;++pass){
        map.clear();assert(map.size()==0);
        for(uint32_t i=0;i<=P::PageVertexLimit;++i){
            const uint32_t key=i*65536u+pass;
            assert(map.find(key)==P::IndexMap::Missing);
            assert(map.insert(key,i)==std::make_pair(i,true));
            assert(map.insert(key,i+1)==std::make_pair(i,false));
        }
        assert(map.size()==P::PageVertexLimit+1);
        for(uint32_t i=0;i<=P::PageVertexLimit;++i)assert(map.find(i*65536u+pass)==i);
    }
    std::puts("PASS bounded index map: sparse/colliding keys, duplicate inserts, overflowing preflight and repeated clear");
}
static void testVertexLimits(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,P::PageVertexLimit*4);
    addBatch(input,30000,0,P::PageVertexLimit*4,1);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.pages.size()>4);assert(plan.batches.size()==plan.pages.size());
    // Shared vertices are copied at page boundaries, but never lose attributes.
    assert(plan.vertexBytes>=scene.vertices.size()*sizeof(WorldVertex));
    std::puts("PASS huge batch: bounded vertex pages, exact attributes/order/bounds");
}
static void testIndexLimits(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,3);
    addBatch(input,100000,0,3,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));verify(input,plan);
    assert(plan.pages.size()==3);assert(plan.pages[0].indices.size()==(P::PageIndexLimit/3)*3);
    assert(plan.pages[0].vertices.size()==3);
    std::puts("PASS reused vertices: index limit and page-local index remapping");
}
static void testKeepBatches(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,20000);
    addBatch(input,3000,0,9000,0);addBatch(input,3000,10000,9000,1);
    addBatch(input,1000,10000,3000,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error,{},sequential));verify(input,plan);
    assert(plan.batches.size()==3&&plan.pages.size()==2);
    assert(plan.batches[0].page==0&&plan.batches[1].page==1&&plan.batches[2].page==1);
    assert(plan.pages[1].vertices.size()==9000); // Shared across batches.
    std::puts("PASS fitting batches stay whole; material/chunk boundaries and cross-batch sharing preserved");
}
static void testDegenerateAndExactBoundary(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,P::PageVertexLimit);
    addBatch(input,P::PageVertexLimit/3,0,P::PageVertexLimit,1);
    addBatch(input,1,P::PageVertexLimit-1,1,0); // All three indices are identical.
    addBatch(input,1,0,1,2);
    P::Plan plan;std::string error;assert(P::build(input,plan,error,{},sequential));verify(input,plan);
    assert(plan.pages.size()==1&&plan.pages[0].vertices.size()==P::PageVertexLimit);
    std::puts("PASS exact vertex-capacity boundary and repeated indices within a triangle");
}
// 0.3.216 (issue #66): content-defined boundaries. Patches are grids (~0.5 vertices per triangle, like real
// city plans) whose positions depend only on the patch id; source indices depend on the order, as in real builds.
static void addPatch(WorldScene& scene,WorldMeshUploadPlan& input,uint32_t id,uint32_t n=40){
    const uint32_t first=uint32_t(scene.vertices.size());
    for(uint32_t y=0;y<=n;++y)for(uint32_t x=0;x<=n;++x)
        scene.vertices.push_back({{float(id)*64.f+float(x),float(y),float((id*7+x*3+y)%5)},{0,0,1},float(x)/n,float(y)/n});
    NorthlightWorldMesh::Batch b;b.start=uint32_t(input.indices.size());b.count=n*n*2;b.material=id%3;
    for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x){
        const uint32_t a=first+y*(n+1)+x,c=a+n+1;
        for(uint32_t i:{a,a+1,c,a+1,c+1,c})input.indices.push_back(i);
    }
    input.batches.push_back(b);input.triangleCount+=b.count;input.vertexCount=uint32_t(scene.vertices.size());
}
static void makePlan(WorldScene& scene,WorldMeshUploadPlan& input,const std::vector<uint32_t>& ids){
    input.source=&scene;input.materials.resize(3);for(auto& m:input.materials){m.width=m.height=1;m.bgra={255,255,255,255};}
    for(uint32_t id:ids)addPatch(scene,input,id);
    input.vertexBytes=uint64_t(input.vertexCount)*32;input.indexBytes=uint64_t(input.triangleCount)*12;
}
static size_t sharedTail(const P::Plan& a,const P::Plan& b){
    size_t n=0;while(n<a.pages.size()&&n<b.pages.size()&&a.pages[a.pages.size()-1-n].key==b.pages[b.pages.size()-1-n].key)++n;return n;
}
static size_t matching(const P::Plan& a,const P::Plan& b){
    size_t n=0;for(const auto& p:b.pages)for(const auto& q:a.pages)if(p.key==q.key){++n;break;}return n;
}
static NorthlightMeshPageKey::Key key(uint64_t id){NorthlightMeshPageKey::Key k;k.lo=id;k.hi=~id;k.vertexBytes=32;k.indexBytes=12;return k;}
static void testAnchors(){
    std::vector<uint32_t> base;for(uint32_t i=0;i<300;++i)base.push_back(i);
    auto edit=base;edit.insert(edit.begin()+4,1000);                  // insertion early in the plan
    auto removed=base;removed.erase(removed.begin()+3);               // removal early in the plan
    P::Options off{0,0};
    struct Variant {const char* name;const std::vector<uint32_t>* ids;} variants[]={{"insert",&edit},{"remove",&removed},{"same",&base}};
    for(const auto& v:variants){
        WorldScene sa,sb;WorldMeshUploadPlan a,b;makePlan(sa,a,base);makePlan(sb,b,*v.ids);
        P::Plan pa,pb,oa,ob;std::string error;
        assert(P::build(a,pa,error)&&P::build(b,pb,error));if(auto* why=P::validatePages(a,pa)){std::puts(why);assert(false);}assert(!P::validatePages(b,pb));
        verify(a,pa);verify(b,pb);
        assert(P::build(a,oa,error,{},off)&&P::build(b,ob,error,{},off));verify(a,oa);verify(b,ob);
        // Batches that fit an empty page are never split by an anchor.
        assert(pa.batches.size()==a.batches.size()&&pb.batches.size()==b.batches.size());
        const size_t tail=sharedTail(pa,pb),reused=matching(pa,pb),sequentialReused=matching(oa,ob);
        std::printf("anchors %s: pages %zu/%zu sequential %zu/%zu; tail shared %zu; reused %zu of %zu (sequential %zu of %zu)\n",v.name,pa.pages.size(),pb.pages.size(),oa.pages.size(),ob.pages.size(),tail,reused,pb.pages.size(),sequentialReused,ob.pages.size());
        assert(pa.pages.size()>=16);
        if(v.ids==&base){assert(tail==pa.pages.size()&&reused==pb.pages.size());continue;}
        assert(tail+3>=pa.pages.size()-1&&tail>=pa.pages.size()*3/4);         // resynchronised shortly after the edit
        assert(reused>=pb.pages.size()*3/4&&reused>sequentialReused*2);
        // Identical key means identical bytes (key sizes and a full compare of the shared tail).
        for(size_t i=1;i<=tail;++i){const auto& x=pa.pages[pa.pages.size()-i];const auto& y=pb.pages[pb.pages.size()-i];
            assert(x.vertices.size()==y.vertices.size()&&x.indices==y.indices&&!std::memcmp(x.vertices.data(),y.vertices.data(),x.vertices.size()*sizeof(WorldVertex)));}
    }
    // Different content never shares a key; the key covers sizes, vertex bytes and index bytes.
    WorldScene s;WorldMeshUploadPlan in;makePlan(s,in,{1,2});P::Plan p;std::string error;assert(P::build(in,p,error)&&p.pages.size()>=1);
    auto page=p.pages[0];auto key=[&](const P::Page& g){return NorthlightMeshPageKey::keyOf(g.vertices.data(),g.vertices.size()*32,g.indices.data(),g.indices.size()*sizeof(uint16_t));};
    assert(key(page)==page.key);
    auto changed=page;changed.vertices[3].position.z+=1;assert(key(changed)!=page.key);
    changed=page;changed.indices[5]^=1;assert(key(changed)!=page.key);
    changed=page;changed.indices.insert(changed.indices.end(),{0,1,2});assert(key(changed)!=page.key);
    changed=page;std::swap(changed.indices[0],changed.indices[1]);assert(key(changed)!=page.key);
    assert(p.cpuBytes()>=sizeof(P::Page)*p.pages.size());
    auto badKey=p;badKey.pages[0].key.indexBytes+=4;
    assert(!P::validatePages(in,p)&&P::validatePages(in,badKey));
    // A batch larger than a page still splits at triangle boundaries and anchors close those pages early.
    WorldScene big;WorldMeshUploadPlan bi;bi.source=&big;bi.materials.resize(3);
    for(uint32_t i=0;i<P::PageVertexLimit*3;++i)big.vertices.push_back(vertex(i*5+1));bi.vertexCount=uint32_t(big.vertices.size());
    addBatch(bi,60000,0,bi.vertexCount,1);
    P::Plan seq,anch;assert(P::build(bi,seq,error,{},off)&&P::build(bi,anch,error));verify(bi,seq);verify(bi,anch);
    std::printf("anchors large batch: pages %zu sequential %zu\n",anch.pages.size(),seq.pages.size());
    std::puts("PASS content anchors: resynchronisation after insertion/removal, identical keys for identical bytes, whole batches, hard limits");
}
// 0.3.216 (issue #66): pages the GPU already holds are elided on the worker (data freed, key and sizes kept).
static void testElision(){
    std::vector<uint32_t> ids;for(uint32_t i=0;i<120;++i)ids.push_back(i);
    WorldScene scene;WorldMeshUploadPlan input;makePlan(scene,input,ids);
    P::Plan full,plan;std::string error;assert(P::build(input,full,error));assert(P::build(input,plan,error));assert(full.pages.size()>=8);
    // Elision inside build(): same pages, keys, batches and boundaries as without it, resident pages freed as they finish,
    // a lower worker peak, and failure/admission behave as before.
    {NorthlightMeshPageKey::KeySet some;for(size_t i=1;i<full.pages.size();i+=2)some.insert(full.pages[i].key);
     uint64_t peakPlain=0,peakElided=0;P::Plan plain,inner;
     P::Options o;o.peakLiveBytes=&peakPlain;assert(P::build(input,plain,error,{},o));
     o.resident=&some;o.peakLiveBytes=&peakElided;assert(P::build(input,inner,error,{},o));
     assert(inner.pages.size()==full.pages.size()&&inner.batches.size()==full.batches.size()&&inner.vertexBytes==full.vertexBytes&&inner.indexBytes==full.indexBytes);
     for(size_t i=0;i<inner.pages.size();++i){
         assert(inner.pages[i].key==full.pages[i].key&&inner.pages[i].elided==(i%2==1));
         if(inner.pages[i].elided)assert(inner.pages[i].vertices.capacity()==0&&inner.pages[i].indices.capacity()==0);
         else assert(inner.pages[i].indices==full.pages[i].indices&&!std::memcmp(inner.pages[i].vertices.data(),full.pages[i].vertices.data(),full.pages[i].vertices.size()*sizeof(WorldVertex)));
     }
     for(size_t i=0;i<inner.batches.size();++i)assert(inner.batches[i].page==full.batches[i].page&&inner.batches[i].start==full.batches[i].start&&inner.batches[i].count==full.batches[i].count&&inner.batches[i].minVertex==full.batches[i].minVertex);
     assert(!P::validatePages(input,inner)&&inner.cpuBytes()<full.cpuBytes()&&peakElided<peakPlain&&peakPlain>=full.cpuBytes()/2);
     // Everything resident: only the page being filled is ever live.
     NorthlightMeshPageKey::KeySet all;for(auto& p:full.pages)all.insert(p.key);
     uint64_t peakAll=0;P::Plan allElided;o.resident=&all;o.peakLiveBytes=&peakAll;assert(P::build(input,allElided,error,{},o));
     for(auto& p:allElided.pages)assert(p.elided&&p.vertices.capacity()==0);
     assert(peakAll<=2*(P::PageVertexByteLimit+P::PageIndexByteLimit)&&!P::validatePages(input,allElided));
     // A failed build changes nothing, with or without resident keys.
     unsigned admissions=0;assert(!P::build(input,inner,error,[&](uint64_t){return ++admissions!=3;},o));assert(inner.pages.size()==full.pages.size()&&inner.pages[1].elided);
    }
    // Nothing resident (no snapshot, another map's snapshot is never passed in): nothing elided, still fully valid.
    NorthlightMeshPageKey::KeySet none;auto stats=P::elide(plan,none);
    assert(stats.pages==0&&stats.bytes==0&&plan.cpuBytes()==full.cpuBytes()&&!P::validatePages(input,plan));
    // Half of the pages resident (plus a key that is not in the plan).
    NorthlightMeshPageKey::KeySet resident;resident.insert(key(777777));
    for(size_t i=0;i<full.pages.size();i+=2)resident.insert(full.pages[i].key);
    const size_t expected=(full.pages.size()+1)/2;
    stats=P::elide(plan,resident);
    assert(stats.pages==expected&&stats.bytes>0&&plan.cpuBytes()+stats.bytes<=full.cpuBytes()&&plan.cpuBytes()<full.cpuBytes());
    for(size_t i=0;i<plan.pages.size();++i){
        const auto& p=plan.pages[i];assert(p.key==full.pages[i].key&&p.elided==(i%2==0));
        if(p.elided){assert(p.vertices.capacity()==0&&p.indices.capacity()==0&&p.vertexCount()==full.pages[i].vertices.size()&&p.indexCount()==full.pages[i].indices.size());}
        else assert(p.vertices.size()==full.pages[i].vertices.size()&&!std::memcmp(p.vertices.data(),full.pages[i].vertices.data(),p.vertices.size()*sizeof(WorldVertex))&&p.indices==full.pages[i].indices);
    }
    assert(plan.vertexBytes==full.vertexBytes&&plan.indexBytes==full.indexBytes&&plan.batches.size()==full.batches.size());
    assert(!P::validatePages(input,plan));
    assert(P::elide(plan,resident).pages==0); // idempotent
    // The rest of the validation is intact: a bad batch range, key size or data inside an elided page is still refused.
    auto broken=plan;broken.batches[0].vertexCount=P::PageVertexLimit+1;assert(P::validatePages(input,broken));
    broken=plan;broken.pages[0].key.vertexBytes+=32;assert(P::validatePages(input,broken));
    broken=plan;broken.pages[0].vertices.push_back(vertex(1));assert(P::validatePages(input,broken));
    broken=plan;broken.pages[1].elided=true;assert(P::validatePages(input,broken)); // elided flag on a page that still has data
    broken=plan;broken.pages[1].key.indexBytes+=4;assert(P::validatePages(input,broken));
    std::printf("PASS page elision: %zu of %zu pages elided, plan CPU %.2f -> %.2f MiB, sizes and batches intact, validation still strict (seal() accounts pages->cpuBytes())\n",expected,full.pages.size(),double(full.cpuBytes())/1048576,double(plan.cpuBytes())/1048576);
}
static void testTransactionalFailure(){
    WorldScene scene;WorldMeshUploadPlan input;initialize(scene,input,3);addBatch(input,1,0,3);
    P::Plan plan;std::string error;assert(P::build(input,plan,error));
    const auto* priorVertices=plan.pages[0].vertices.data();const auto bytes=plan.cpuBytes();
    auto bad=[&](WorldMeshUploadPlan broken){assert(!P::build(broken,plan,error));assert(!error.empty());assert(plan.pages[0].vertices.data()==priorVertices&&plan.cpuBytes()==bytes);verify(input,plan);};
    assert(!P::build(input,plan,error,[](uint64_t){return false;}));
    assert(plan.pages[0].vertices.data()==priorVertices);
    for(unsigned rejectAt=1;rejectAt<=4;++rejectAt){unsigned admissions=0;
        assert(!P::build(input,plan,error,[&](uint64_t){return ++admissions!=rejectAt;}));
        assert(plan.pages[0].vertices.data()==priorVertices);verify(input,plan);
    }
    auto broken=input;broken.indices[2]=3;bad(broken);
    broken=input;broken.batches[0].start=UINT32_MAX;bad(broken);
    broken=input;broken.batches[0].count=UINT32_MAX;bad(broken);
    broken=input;broken.batches[0].material=3;bad(broken);
    broken=input;broken.batches.clear();bad(broken);
    broken=input;broken.batches[0].count=0;bad(broken);
    broken=input;broken.triangleCount=0;bad(broken);
    broken=input;broken.source=nullptr;bad(broken);
    scene.vertices[1].position.x=std::numeric_limits<float>::quiet_NaN();
    assert(!P::build(input,plan,error));assert(plan.pages[0].vertices.data()==priorVertices);
    scene.vertices[1]=vertex(1);
    WorldMeshUploadPlan empty;assert(P::build(empty,plan,error));verify(empty,plan);assert(plan.pages.empty()&&plan.batches.empty());
    std::puts("PASS malformed plan/index rejection, transactional failure and empty plan");
}
static void testSeal(){
    auto source=std::make_shared<WorldScene>();WorldMeshUploadPlan input;initialize(*source,input,3);addBatch(input,1,0,3);
    source->triangles.push_back({0,1,2,0});source->materials.resize(3);
    WorldPlacementCoverage owner;owner.uid=451;owner.modelKey="exact-static-owner";owner.matrix[0]=1;owner.translation={1,2,3};source->completePlacements.push_back(owner);
    input.ownedSource=source;input.vertexBytes=3*sizeof(WorldVertex);input.indexBytes=3*sizeof(uint32_t);
    for(auto& material:input.materials)material.bgra={255,255,255,255};
    std::string error;auto pages=std::make_shared<P::Plan>();assert(P::build(input,*pages,error));
    assert(!P::validatePages(input,*pages));
    assert(!P::seal(input,pages,error,[](uint64_t){return false;}));
    assert(input.source==source.get()&&!input.pagesSealed&&!input.indices.empty()&&!input.payload.live());
    auto broken=*pages;broken.batches[0].page=1;assert(P::validatePages(input,broken));
    broken=*pages;broken.batches[0].vertexCount=4;assert(P::validatePages(input,broken));
    broken=*pages;broken.batches[0].count=2;assert(P::validatePages(input,broken));
    broken=*pages;broken.indexBytes++;assert(P::validatePages(input,broken));
    broken=*pages;broken.pages[0].indices.push_back(0);assert(P::validatePages(input,broken));
    assert(!P::seal(input,std::make_shared<P::Plan>(broken),error));assert(input.source==source.get());
    assert(P::seal(input,pages,error));assert(!P::validateSealed(input));
    assert(input.source==nullptr&&!input.ownedSource&&input.indices.capacity()==0&&input.payload.share()->pages==pages&&input.materials.capacity()==0&&input.payload.live());
    assert(input.completePlacements.size()==1&&input.completePlacements[0].uid==owner.uid&&input.completePlacements[0].modelKey==owner.modelKey);
    assert(source->vertices.size()==3&&source->triangles.size()==1&&source->completePlacements[0].modelKey==owner.modelKey);
    assert(input.pagedCpuBytes==pages->cpuBytes());
    assert(!P::seal(input,pages,error));assert(!P::validateSealed(input));
    input.source=source.get();assert(P::validateSealed(input));input.source=nullptr;
    // Non-owning plans can seal without mutating/releasing their GI scene.
    WorldMeshUploadPlan borrowed;initialize(*source,borrowed,0);borrowed.vertexCount=3;borrowed.source=source.get();addBatch(borrowed,1,0,3);
    borrowed.vertexBytes=96;borrowed.indexBytes=12;for(auto& material:borrowed.materials)material.bgra={255,255,255,255};
    assert(P::seal(borrowed,pages,error));assert(!P::validateSealed(borrowed));assert(source->vertices.size()==3);
    std::puts("PASS sealed ownership: source release, exact placement proofs, retained GI, cheap validation and transactional failures");
}
static void testCompactSource(){
    auto scene=std::make_shared<WorldScene>();WorldMeshUploadPlan input;initialize(*scene,input,3);addBatch(input,1,0,3);
    scene->triangles.push_back({0,1,2,0});scene->materials.resize(3);
    scene->completePlacements.emplace_back();scene->completePlacements.back().modelKey="preserve-owner";
    input.vertexBytes=96;input.indexBytes=12;for(auto& m:input.materials)m.bgra={255,255,255,255};
    std::string error;P::Plan reference;assert(P::build(input,reference,error));
    const auto* vertices=scene->vertices.data();
    input.pagingVertices=std::make_shared<const std::vector<WorldVertex>>(std::move(*scene->vertices.flat()));
    input.completePlacements=std::move(scene->completePlacements);input.source=nullptr;scene.reset();
    assert(input.verticesForPaging()->data()==vertices);
    auto pages=std::make_shared<P::Plan>();assert(P::build(input,*pages,error));
    assert(pages->pages[0].vertices.capacity()==3); // a tiny anchored page does not retain 512 KiB
    assert(pages->pages[0].key==reference.pages[0].key&&pages->pages[0].indices==reference.pages[0].indices);
    assert(!std::memcmp(pages->pages[0].vertices.data(),reference.pages[0].vertices.data(),96));
    // Compaction is optional: an admission that refuses its exact-size copies (only those requests here) keeps
    // the page uncompacted with the same key and bytes instead of failing the build.
    {P::Plan kept;const auto refuseCompaction=[](uint64_t bytes){return bytes!=96&&bytes!=12;}; /* the exact 3-vertex / 3-index copies */
     assert(P::build(input,kept,error,refuseCompaction)&&error.empty());
     assert(kept.pages.size()==pages->pages.size()&&kept.pages[0].vertices.capacity()>3);
     assert(kept.pages[0].key==pages->pages[0].key&&kept.pages[0].indices==pages->pages[0].indices);
     assert(!std::memcmp(kept.pages[0].vertices.data(),pages->pages[0].vertices.data(),96));}
    auto broken=input;broken.source=reinterpret_cast<const WorldScene*>(1);
    P::Plan untouched=reference;assert(!P::build(broken,untouched,error));assert(!P::seal(broken,pages,error));
    broken=input;broken.vertexCount=4;assert(!P::build(broken,untouched,error));assert(!P::seal(broken,pages,error));
    assert(!P::seal(input,pages,error,[](uint64_t){return false;}));
    assert(input.pagingVertices&&input.completePlacements[0].modelKey=="preserve-owner");
    assert(P::seal(input,pages,error)&&!P::validateSealed(input));
    assert(!input.pagingVertices&&input.completePlacements[0].modelKey=="preserve-owner");
    std::puts("PASS compact paging: zero-copy vertices, exact pages, a refused compaction keeps the page, strict ownership/count checks, transactional sealing");
}
static void testSpooledPointTransmission(){
    auto scene=[](float x,bool terrain){WorldScene s;s.vertices={{{x,0,0},{0,0,1},0,0},{{x+1,0,0},{0,0,1},1,0},{{x,1,0},{0,0,1},0,1}};s.triangles={{0,1,2,0}};WorldMaterial m;m.terrain=terrain;m.width=m.height=2;m.alphaCutoff=.5f;m.rgba={11,29,47,255,61,79,97,0,113,131,149,255,167,181,199,127};s.materials.push_back(m);return s;};
    auto local=scene(0,false),terrain=scene(400,true),point=scene(2,false);point.materials[0].lampSourceId=17;
    WorldMeshUploadPlan regular,spooled;std::string error;
    assert(NorthlightShadowTerrain::build(local,terrain,{},regular,error,{},928,{},true,nullptr,nullptr,&point));
    assert(NorthlightShadowTerrain::build(local,terrain,{},spooled,error,{},928,{},true,nullptr,nullptr,&point,true));
    assert(regular.vertexCount==spooled.vertexCount&&regular.triangleCount==spooled.triangleCount);
    for(size_t i=0;i<regular.pagingIndexCount();++i)assert(regular.pagingIndex(i)==spooled.pagingIndex(i));
    assert(regular.materials.size()==spooled.materials.size());
    for(size_t i=0;i<regular.materials.size();++i){const auto& a=regular.materials[i];const auto& b=spooled.materials[i];std::vector<uint8_t> bytes(b.pixelBytes());if(b.spooled)spooled.temporary->read(b.fileOffset,bytes.data(),bytes.size());else bytes=b.bgra;assert(a.bgra==bytes&&a.alphaCutoff==b.alphaCutoff&&a.textureIdentity==b.textureIdentity);}
    auto a=std::make_shared<P::Plan>(),b=std::make_shared<P::Plan>();P::Options options;options.spooled=true;
    assert(P::build(regular,*a,error)&&P::build(spooled,*b,error,{},options));assert(a->pages.size()==b->pages.size());
    for(size_t i=0;i<a->pages.size();++i){const auto& before=a->pages[i];const auto& after=b->pages[i];assert(before.key==after.key&&after.spooled);std::vector<WorldVertex> vertices(before.vertices.size());std::vector<uint16_t> indices(before.indices.size());
        assert(b->copy(after,true,0,vertices.size()*sizeof(WorldVertex),vertices.data())&&b->copy(after,false,0,indices.size()*2,indices.data()));
        assert(!std::memcmp(vertices.data(),before.vertices.data(),vertices.size()*sizeof(WorldVertex))&&indices==before.indices);
    }
    assert(P::seal(spooled,b,error)&&!P::validateSealed(spooled));
    assert(spooled.payload.live()&&spooled.payload.share()->pages->temporary);
    const auto payload=spooled.payload.share();
    for(size_t i=0;i<regular.materials.size();++i){const auto& material=payload->materials[i];std::vector<uint8_t> bytes(material.pixelBytes());
        assert(payload->pages->copyMaterial(material,0,bytes.size(),bytes.data())&&bytes==regular.materials[i].bgra);
        assert(material.spooled==(bytes.size()>4)&&material.textureIdentity==regular.materials[i].textureIdentity);
        assert(!payload->pages->copyMaterial(material,bytes.size(),1,bytes.data()));
    }
    std::puts("PASS spooled point transmission: exact geometry, UVs, cutout texels, packed order, page bytes and retained recovery ownership");
}
int main(){testSpooledPointTransmission();testIndexMap();testVertexLimits();testIndexLimits();testKeepBatches();testDegenerateAndExactBoundary();testAnchors();testElision();testTransactionalFailure();testSeal();testCompactSource();std::puts("All world mesh page tests passed");}
