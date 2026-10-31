// The runner generates the existing fake-D3D fixture and stock ADT geometry.
#include "terrain_index_reference.h"
#include <chrono>
#include <fstream>
#include <string>

struct Chunk {std::vector<Position> positions;std::vector<std::uint16_t> indices;};
static std::uint64_t identities[]={11,12},revisions[]={21,22};
static bool racing=false;static unsigned indexStamps=0;
static std::uint64_t identity(void*,bool index){return identities[index];}
static std::uint64_t version(void*,bool index){return revisions[index]+(index&&racing&&indexStamps++>0?1:0);}
static void metadata(const NorthlightCaptureMetadata::Request*,NorthlightCaptureMetadata::Info*,unsigned);
static std::uint64_t digest=1469598103934665603ull;
static std::ofstream audit;
static void bytes(const void* p,size_t n){
    const auto* data=static_cast<const unsigned char*>(p);
    for(size_t i=0;i<n;++i)digest=(digest^data[i])*1099511628211ull;
    if(audit.is_open())audit.write(static_cast<const char*>(p),std::streamsize(n));
}
static void word(std::uint64_t value){bytes(&value,sizeof value);}
static void snapshot(const MeshSnapshot& mesh){
    word(mesh.positions.size());bytes(mesh.positions.data(),mesh.positions.size()*sizeof(Position));
    word(mesh.indices.size());for(auto i:mesh.indices)word(i);
    bytes(mesh.bounds.low,sizeof mesh.bounds.low);bytes(mesh.bounds.high,sizeof mesh.bounds.high);
    word(mesh.bounds.chunks.size());for(const auto& c:mesh.bounds.chunks){word(unsigned(c.x));word(unsigned(c.y));bytes(&c.minX,4*sizeof(float));}
    word(bool(mesh.sourceIdentity));if(mesh.sourceIdentity){const auto& words=mesh.sourceIdentity->words;assert(words.size()==15);
        assert(words[1]==reinterpret_cast<std::uintptr_t>(identity)&&words[2]==reinterpret_cast<std::uintptr_t>(version));
        assert(!words[3]||words[3]==reinterpret_cast<std::uintptr_t>(metadata));
        // ASLR changes provider addresses between binaries. Check the exact
        // function pointers above, then serialize their stable identities.
        for(size_t i=0;i<words.size();++i)word(i>=1&&i<=3?std::uint64_t(bool(words[i])):words[i]);
    }
}
static void balanced(const Device& d){
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
    assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
}
struct FlexibleDevice:Device {
    UINT stride=24;
    HRESULT GetStreamSource(UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* size)override{
        assert(stream==0);*out=&vb;vb.AddRef();*offset=16;*size=stride;return 0;
    }
    void put(unsigned index,const Position& p){std::memcpy(vb.bytes.data()+20+index*stride,&p,12);}
};
struct Scene {
    FlexibleDevice device;std::vector<UINT> starts,primitives;
    Scene(const std::vector<Chunk>& chunks,unsigned draws,bool bank=true,bool wide=false,unsigned stride=24,bool single=false){
        device.stride=stride;device.vb.bytes.resize(16+size_t(draws)*290*stride);starts.resize(draws);primitives.resize(draws);
        std::vector<std::uint16_t> indices;std::vector<std::pair<UINT,UINT>> ranges;
        for(unsigned draw=0;draw<draws;++draw){
            std::vector<std::uint16_t> input;
            for(unsigned j=0;j<2;++j){const auto& chunk=chunks[(draw*2+j)%chunks.size()];assert(chunk.positions.size()==145);
                for(unsigned i=0;i<145;++i)device.put(draw*290+j*145+i,chunk.positions[i]);
                for(auto i:chunk.indices)input.push_back(std::uint16_t(i+j*145));}
            if(single)input.resize(3);
            bool found=false;
            if(bank)for(const auto& range:ranges)if(range.second==input.size()&&std::equal(input.begin(),input.end(),indices.begin()+range.first)){
                starts[draw]=range.first;found=true;break;}
            if(!found){starts[draw]=UINT(indices.size());ranges.push_back({starts[draw],UINT(input.size())});indices.insert(indices.end(),input.begin(),input.end());}
            primitives[draw]=UINT(input.size()/3);
        }
        device.ib.desc.Format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;
        device.ib.bytes.resize((indices.size()+8)*(wide?4:2));
        if(wide)for(size_t i=0;i<indices.size();++i){std::uint32_t value=indices[i];std::memcpy(device.ib.bytes.data()+i*4,&value,4);}
        else std::memcpy(device.ib.bytes.data(),indices.data(),indices.size()*2);
    }
    bool read(FrameCache& cache,unsigned draw,std::shared_ptr<const MeshSnapshot>& mesh,Diagnostics* detail=nullptr){
        return cache.readMeshShared(&device,D3DPT_TRIANGLELIST,INT(draw*290),0,290,starts[draw],primitives[draw],device.view,mesh,detail);
    }
};
static void configure(FrameCache& cache){cache.setIdentityProvider(identity);cache.setVersionProvider(version);}
static void metadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* output,unsigned count){
    for(unsigned i=0;i<count;++i){auto& info=output[i];info={};const bool index=requests[i].index;
        if(index){D3DINDEXBUFFER_DESC desc;assert(static_cast<IDirect3DIndexBuffer9*>(requests[i].buffer)->GetDesc(&desc)==D3D_OK);info.size=desc.Size;info.usage=desc.Usage;info.format=desc.Format;}
        else{D3DVERTEXBUFFER_DESC desc;assert(static_cast<IDirect3DVertexBuffer9*>(requests[i].buffer)->GetDesc(&desc)==D3D_OK);info.size=desc.Size;info.usage=desc.Usage;}
        info.identity=identity(requests[i].buffer,index);info.revision=version(requests[i].buffer,index);info.known=true;
    }
}
static void resetTokens(){identities[0]=11;identities[1]=12;revisions[0]=21;revisions[1]=22;racing=false;indexStamps=0;}
static void controls(const std::vector<Chunk>& chunks){
    unsigned accepted=0,refused=0;
    for(bool wide:{false,true})for(bool bank:{false,true})for(unsigned stride:{24u,48u}){
        resetTokens();Scene scene(chunks,UINT(chunks.size()),bank,wide,stride);FrameCache cache;configure(cache);if(bank)cache.setMetadataProvider(metadata);cache.profileCapture(true);
        std::shared_ptr<const MeshSnapshot> held;
        for(unsigned draw=0;draw<scene.starts.size();++draw){std::shared_ptr<const MeshSnapshot> mesh;Diagnostics detail;
            assert(scene.read(cache,draw,mesh,&detail));snapshot(*mesh);if(!held)held=mesh;++accepted;
            assert(cache.persistentBytes()<=32u*1024u*1024u&&cache.persistentEntries()<=8192);
        }
#ifdef INDEX_READ_CURRENT
        if(bank)assert(cache.maintenance().indexPlanTokenHits>0);else assert(!cache.maintenance().indexPlanTokenHits);
#endif
        const auto saved=held->positions;cache.clearFrame();
        std::shared_ptr<const MeshSnapshot> warm;assert(scene.read(cache,0,warm));assert(warm==held);snapshot(*warm);
        // A vertex write cannot replace any previously published immutable mesh.
        UINT source=0;if(wide)std::memcpy(&source,scene.device.ib.bytes.data()+scene.starts[0]*4,4);
        else{std::uint16_t value;std::memcpy(&value,scene.device.ib.bytes.data()+scene.starts[0]*2,2);source=value;}
        Position changed=held->positions[0];changed.z+=37;++revisions[0];scene.device.put(source,changed);cache.clearFrame();
        std::shared_ptr<const MeshSnapshot> altered;assert(scene.read(cache,0,altered));assert(altered!=held);snapshot(*altered);
        assert(!std::memcmp(saved.data(),held->positions.data(),saved.size()*sizeof(Position)));
        cache.clearPersistent();cache.clearFrame();assert(scene.read(cache,0,altered));snapshot(*altered);
        assert(cache.persistentEntries()==1&&scene.device.ib.locks>0);balanced(scene.device);
    }
    // Repeat exact bytes at new bases so an independently decoded second match
    // establishes the original weak index plan before each negative control.
    std::vector<Chunk> repeated(2,chunks.front());
    for(unsigned variation=0;variation<16;++variation){
        resetTokens();Scene scene(repeated,8);FrameCache cache;configure(cache);cache.profileCapture(true);
        std::shared_ptr<const MeshSnapshot> mesh;
        assert(scene.read(cache,0,mesh)&&scene.read(cache,1,mesh));
        if(variation==1)++revisions[1];
        if(variation==2)++identities[1];
        if(variation==3)revisions[1]=0;
        if(variation==4)revisions[0]=0;
        if(variation==5)scene.starts[2]+=3;
        if(variation==6)--scene.primitives[2];
        if(variation==7){std::uint16_t bad=65000;std::memcpy(scene.device.ib.bytes.data(),&bad,2);++revisions[1];}
        if(variation==8){std::uint16_t a,b;std::memcpy(&a,scene.device.ib.bytes.data(),2);std::memcpy(&b,scene.device.ib.bytes.data()+2,2);std::memcpy(scene.device.ib.bytes.data(),&b,2);std::memcpy(scene.device.ib.bytes.data()+2,&a,2);++revisions[1];}
        if(variation==9){cache.clearPersistent();cache.clearFrame();}
        if(variation==10){scene.device.ib.failLock=true;++revisions[1];}
        if(variation==11){scene.device.vb.failLock=true;++revisions[1];}
        if(variation==12){float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(scene.device.vb.bytes.data()+20+2*290*24,&nan,4);}
        if(variation==13){racing=true;indexStamps=0;}
        if(variation==14){const float zero=-0.f;std::memcpy(scene.device.vb.bytes.data()+28+2*290*24,&zero,4);}
        if(variation==15){std::fill(scene.device.ib.bytes.begin(),scene.device.ib.bytes.end(),0);++revisions[1];}
        Diagnostics detail;const bool ok=scene.read(cache,2,mesh,&detail);word(variation);word(ok);word(unsigned(detail.reason));word(std::uint32_t(detail.hr));
        if(ok){snapshot(*mesh);++accepted;}else ++refused;
        if(variation==0)assert(ok);
        if(variation==1||variation==2||variation==3||variation==4||variation==5||variation==6||variation==7||variation==8||variation==9||variation==10||variation==11||variation==15){
#ifdef INDEX_READ_CURRENT
            assert(!cache.maintenance().indexPlanTokenHits);
#endif
        }
        if(variation==13)assert(!mesh->sourceIdentity&&cache.persistentEntries()==2);
        scene.device.ib.failLock=scene.device.vb.failLock=false;racing=false;balanced(scene.device);
    }
    for(bool strip:{false,true}){
        resetTokens();Scene scene(repeated,8);FrameCache cache;configure(cache);cache.profileCapture(true);
        std::shared_ptr<const MeshSnapshot> mesh;assert(scene.read(cache,0,mesh)&&scene.read(cache,1,mesh));
        bool ok;Diagnostics detail;
        if(strip){
            const auto indices=scene.primitives[2]*3;
            ok=cache.readMeshShared(&scene.device,D3DPT_TRIANGLESTRIP,2*290,0,290,scene.starts[2],indices-2,scene.device.view,mesh,&detail);
        }else{
            scene.device.stride=48;++revisions[0];
            for(unsigned j=0;j<2;++j)for(unsigned i=0;i<145;++i)scene.device.put(2*290+j*145+i,repeated[j].positions[i]);
            ok=scene.read(cache,2,mesh,&detail);
        }
        word(strip);word(ok);word(unsigned(detail.reason));if(ok){snapshot(*mesh);++accepted;}else ++refused;
#ifdef INDEX_READ_CURRENT
        assert(!cache.maintenance().indexPlanTokenHits);
#endif
        balanced(scene.device);
    }
    assert(accepted&&refused);std::printf("CONTROLS accepted=%u refused=%u\n",accepted,refused);
}
static void pressure(const std::vector<Chunk>& chunks,bool single){
    resetTokens();Scene scene(chunks,9800,true,false,24,single);FrameCache cache;configure(cache);
    std::shared_ptr<const MeshSnapshot> held;std::uint64_t evictions=0,reads=0;size_t maximum=0;
    for(unsigned frame=0;frame<28;++frame){cache.clearFrame();
        for(unsigned n=0;n<700;++n){const unsigned draw=(frame%14)*700+n;std::shared_ptr<const MeshSnapshot> mesh;
            assert(scene.read(cache,draw,mesh));if(!held)held=mesh;word(draw);snapshot(*mesh);
        }
        evictions+=cache.maintenance().evictions;reads+=cache.bytesRead();maximum=std::max(maximum,cache.persistentEntries());
        assert(cache.persistentEntries()<=8192&&cache.persistentBytes()<=32u*1024u*1024u);
    }
    assert(evictions>0);if(single)assert(maximum==8192);
    const auto saved=held->positions;cache.clearPersistent();cache.clearFrame();++identities[0];++identities[1];
    std::shared_ptr<const MeshSnapshot> after;assert(scene.read(cache,0,after));snapshot(*after);
    assert(held!=after&&!std::memcmp(saved.data(),held->positions.data(),saved.size()*sizeof(Position)));balanced(scene.device);
    std::printf("PRESSURE single=%u captures=19600 evictions=%llu reads=%llu maximum=%zu\n",single,(unsigned long long)evictions,(unsigned long long)reads,maximum);
}
static void timing(const std::vector<Chunk>& chunks,const std::string& mode){
    resetTokens();const bool bank=mode!="unique",warm=mode=="warm";Scene scene(chunks,700,bank);FrameCache cache;configure(cache);
    std::uint64_t reads=0,reuses=0;const auto begin=std::chrono::steady_clock::now();
    for(unsigned repeat=0;repeat<30;++repeat){if(!warm)cache.clearPersistent();cache.clearFrame();
        for(unsigned draw=0;draw<700;++draw){std::shared_ptr<const MeshSnapshot> mesh;assert(scene.read(cache,draw,mesh));word(mesh->positions.size());word(mesh->indices.size());}
        reads+=cache.bytesRead();reuses+=cache.maintenance().trackedHits;
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    std::printf("TIMING mode=%s ms=%.6f reads=%llu reuses=%llu digest=%016llx\n",mode.c_str(),ms,(unsigned long long)reads,(unsigned long long)reuses,(unsigned long long)digest);
}
int main(int argc,char** argv){
    assert(argc>=3);std::ifstream file(argv[1],std::ios::binary);std::uint32_t count;file.read(reinterpret_cast<char*>(&count),4);assert(count>0&&count<=10000);
    std::vector<Chunk> chunks(count);
    for(auto& chunk:chunks){std::uint32_t positions,indices;file.read(reinterpret_cast<char*>(&positions),4);file.read(reinterpret_cast<char*>(&indices),4);assert(positions==145&&indices&&indices<=768&&indices%3==0);
        chunk.positions.resize(positions);chunk.indices.resize(indices);file.read(reinterpret_cast<char*>(chunk.positions.data()),positions*12);file.read(reinterpret_cast<char*>(chunk.indices.data()),indices*2);}
    assert(file);const std::string mode=argv[2];
    if(mode=="checks"){assert(argc==4);audit.open(argv[3],std::ios::binary);assert(audit);controls(chunks);audit.close();pressure(chunks,false);pressure(chunks,true);
        std::printf("TERRAIN_INDEX_READ_PASS chunks=%u digest=%016llx capMiB=32 entryLimit=8192\n",count,(unsigned long long)digest);}
    else timing(chunks,mode);
}
