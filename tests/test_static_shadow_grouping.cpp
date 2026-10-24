#ifndef STATIC_SHADOW_IMPLEMENTATION
#define STATIC_SHADOW_IMPLEMENTATION "static_shadow_scene.cpp"
#endif
#include STATIC_SHADOW_IMPLEMENTATION
#include <cassert>
#include <filesystem>

using namespace StaticShadow;
static void u32(FILE* f,uint32_t value){const unsigned char bytes[]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};assert(std::fwrite(bytes,1,4,f)==4);}
static void scalar(FILE* f,float value){uint32_t bits;std::memcpy(&bits,&value,4);u32(f,bits);}
static void vec(FILE* f,Vec3 value){scalar(f,value.x);scalar(f,value.y);scalar(f,value.z);}
static void save(const std::filesystem::path& path,const NorthlightGI::WorldScene& scene){
    FILE* f=std::fopen(path.string().c_str(),"wb");assert(f);assert(std::fwrite("FGS2",1,4,f)==4);u32(f,2);u32(f,uint32_t(scene.vertices.size()));u32(f,uint32_t(scene.triangles.size()));u32(f,uint32_t(scene.materials.size()));
    for(const auto& v:scene.vertices){vec(f,v.position);vec(f,v.normal);scalar(f,v.u);scalar(f,v.v);}
    for(const auto& t:scene.triangles){u32(f,t.v0);u32(f,t.v1);u32(f,t.v2);u32(f,t.material);}
    for(const auto& m:scene.materials){vec(f,m.albedo);u32(f,m.width);u32(f,m.height);scalar(f,m.alphaCutoff);u32(f,uint32_t(m.rgba.size()));assert(std::fwrite(m.rgba.data(),1,m.rgba.size(),f)==m.rgba.size());}
    assert(!std::fclose(f));
}
static uint64_t digest(const Model& m){
    uint64_t hash=HashSeed;
    auto word=[&](const auto& value){hash=hashBytes(hash,&value,sizeof value);};
    word(m.contentRevision);word(m.index16);word(m.uploadPrepared);word(m.bytes);
    for(const auto& v:m.vertices){word(v.x);word(v.y);word(v.z);word(v.u);word(v.v);}
    for(size_t i=0;i<m.indexCount();++i)word(m.indexAt(i));
    for(const auto& b:m.batches){word(b.firstIndex);word(b.indexCount);word(b.material);word(b.low.x);word(b.low.y);word(b.low.z);word(b.high.x);word(b.high.y);word(b.high.z);}
    for(size_t i=0;i<m.materials.size();++i){const auto& x=m.materials[i];word(x.alphaCutoff);word(x.width);word(x.height);word(x.addressU);word(x.addressV);word(m.materialKeys[i]);for(auto alpha:*m.uploadAlpha[i])word(alpha);}
    assert(m.drawModel&&m.drawModel->batches.size()==m.batches.size());
    return hash;
}
static void checkGroups(const NorthlightGI::WorldScene& source,const Model& actual){
    using Group=std::tuple<uint32_t,int,int,int>;std::map<Group,std::vector<uint32_t>> expected;
    for(const auto& t:source.triangles){Vec3 c=(source.vertices[t.v0].position+source.vertices[t.v1].position+source.vertices[t.v2].position)/3.f;auto& ids=expected[{t.material,int(std::floor(c.x/64)),int(std::floor(c.y/64)),int(std::floor(c.z/64))}];ids.insert(ids.end(),{t.v0,t.v1,t.v2});}
    assert(expected.size()==actual.batches.size());size_t group=0,index=0;
    for(const auto& pair:expected){const auto& batch=actual.batches[group++];assert(batch.firstIndex==index&&batch.indexCount==pair.second.size()&&batch.material==std::get<0>(pair.first));Vec3 low={1e30f,1e30f,1e30f},high=-low;
        for(auto id:pair.second){assert(actual.indexAt(index++)==id);const auto p=source.vertices[id].position;low=mn(low,p);high=mx(high,p);}
        assert(!std::memcmp(&low,&batch.low,sizeof low)&&!std::memcmp(&high,&batch.high,sizeof high));}
    assert(index==actual.indexCount());
}
static NorthlightGI::WorldScene fixture(bool wide){
    NorthlightGI::WorldScene s;
    for(unsigned cell=0;cell<64;++cell){const Vec3 p(float(int(cell%8)-4)*64+2,float(int(cell/8)-4)*64+2,float(cell%3)*64+2);s.vertices.push_back({p,{0,0,1},0,0});s.vertices.push_back({p+Vec3(5,0,0),{0,0,1},1,0});s.vertices.push_back({p+Vec3(0,5,0),{0,0,1},0,1});}
    s.materials.resize(3);
    for(unsigned i=0;i<3;++i){auto& m=s.materials[i];m.width=2;m.height=1;m.alphaCutoff=float(i)*.37f;m.rgba={255,16,32,uint8_t(i?i*70:255),255,32,16,255};}
    for(unsigned i=0;i<8192;++i){const unsigned cell=((i/8)*7)%64,first=cell*3,material=(i/16)%3;s.triangles.push_back({first,first+1,first+2,material});}
    if(wide){s.vertices.resize(65537,s.vertices[0]);s.triangles.push_back({0,1,65536,0});}
    return s;
}
int main(int argc,char** argv){
    assert(argc>=2);const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    uint64_t fixtureDigest=HashSeed;std::string error;
    for(bool wide:{false,true}){
        auto source=fixture(wide);Model packed;auto copy=source;assert(packModel(std::move(copy),"fixture",packed,error));checkGroups(source,packed);
        const auto path=root/(wide?"wide.fgs":"compact.fgs");save(path,source);Model decoded;assert(loadCompact(path.string(),"fixture",64u<<20,decoded,error));checkGroups(source,decoded);
        assert(decoded.contentRevision==packed.contentRevision&&decoded.materialKeys==packed.materialKeys&&decoded.index16==packed.index16);for(size_t i=0;i<decoded.indexCount();++i)assert(decoded.indexAt(i)==packed.indexAt(i));
        const auto a=digest(packed),b=digest(decoded);fixtureDigest=hashBytes(fixtureDigest,&a,sizeof a);fixtureDigest=hashBytes(fixtureDigest,&b,sizeof b);
        auto mutated=source;mutated.materials[1].addressU=3;mutated.materials[1].addressV=2;mutated.materials[2].alphaCutoff=.21f;Model changed;assert(packModel(std::move(mutated),"fixture",changed,error));checkGroups(source,changed);assert(changed.materials[1].addressU==3&&changed.materials[1].addressV==2&&changed.materials[2].alphaCutoff==.21f&&changed.contentRevision!=packed.contentRevision);
        const auto c=digest(changed);fixtureDigest=hashBytes(fixtureDigest,&c,sizeof c);
        source.triangles[100].material=3;Model invalid;assert(!packModel(std::move(source),"invalid",invalid,error));
        auto fresh=fixture(wide);Model reset;assert(packModel(std::move(fresh),"fixture",reset,error)&&digest(reset)==a); // No last-key state across jobs.
    }
    std::printf("STATIC_GROUP_FIXTURE digest=%016llx\n",(unsigned long long)fixtureDigest);
    for(int argument=2;argument<argc;++argument){
        uint64_t exact=0;size_t vertices=0,indices=0,batches=0;std::vector<double> timings;
        for(unsigned run=0;run<9;++run){Model model;const auto start=Clock::now();assert(loadCompact(argv[argument],"asset",64u<<20,model,error));const double elapsed=ms(start);const auto current=digest(model);if(run)assert(current==exact);exact=current;vertices=model.vertices.size();indices=model.indexCount();batches=model.batches.size();if(run)timings.push_back(elapsed);}
        std::sort(timings.begin(),timings.end());
        NorthlightGI::WorldScene scene;assert(NorthlightGI::loadScene(argv[argument],scene,error));Model packed;assert(packModel(std::move(scene),"asset",packed,error));const auto packedDigest=digest(packed);
        std::printf("STATIC_GROUP_ASSET argument=%d digest=%016llx packDigest=%016llx vertices=%zu indices=%zu batches=%zu decodeMedianMs=%.6f\n",argument,(unsigned long long)exact,(unsigned long long)packedDigest,vertices,indices,batches,timings[timings.size()/2]);
    }
    std::puts("PASS exact static grouping, non-adjacent keys, 16/32-bit indices, alpha/cutoff/addressing, failure and job reset");
}
