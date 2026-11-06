#include "snapshot_harness.h"
#include <chrono>
#include <random>
#include <cstdlib>
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
 using Key=Frame::CacheKey;
 static std::uint64_t hash(Frame& f,const Key& k){
#ifdef NORTHLIGHT_HASH_PREFIX_BASELINE
  (void)f;return Frame::hashKey(k);
#else
  return f.lookupHash(k);
#endif
 }
 static std::uint64_t original(const Key& key){
  const auto* b=reinterpret_cast<const unsigned char*>(&key);std::uint64_t h=1469598103934665603ull;size_t i=0;
  for(;i+8<=sizeof key;i+=8){std::uint64_t word;std::memcpy(&word,b+i,8);h^=word;h*=1099511628211ull;}
  for(;i<sizeof key;++i){h^=b[i];h*=1099511628211ull;}
  h^=h>>33;h*=0xff51afd7ed558ccdULL;h^=h>>33;h*=0xc4ceb9fe1a85ec53ULL;h^=h>>33;
  return NORTHLIGHT_SNAPSHOT_HASH_BITS?h&((std::uint64_t(1)<<NORTHLIGHT_SNAPSHOT_HASH_BITS)-1):h;
 }
};
}
using Access=NorthlightDrawSnapshot::SnapshotTestAccess;
static void exact(){Frame frame;std::mt19937 random(186);Access::Key key;std::size_t checks=0;
 for(unsigned round=0;round<1000;++round){auto* bytes=reinterpret_cast<unsigned char*>(&key);for(size_t i=0;i<sizeof key;++i)bytes[i]=static_cast<unsigned char>(random());
  assert(Access::hash(frame,key)==Access::original(key));++checks;
  for(size_t i=0;i<sizeof key;++i){const auto keep=bytes[i];bytes[i]^=1u<<(random()%8);assert(Access::hash(frame,key)==Access::original(key));++checks;bytes[i]=keep;assert(Access::hash(frame,key)==Access::original(key));++checks;}
  // Long submesh runs retain resource metadata but vary every draw field.
  for(unsigned draw=0;draw<256;++draw){key.base=int(random());key.minimum=random();key.vertices=random();key.start=random();key.primitives=random();key.topology=random();key.indexed=random();key.declaration=reinterpret_cast<void*>(std::uintptr_t(random()));key.tracking[0]=random();key.privateDomain=reinterpret_cast<void*>(std::uintptr_t(random()));assert(Access::hash(frame,key)==Access::original(key));++checks;}
 }
 std::printf("PASS snapshot hash prefix: %zu exact original FNV hashes, all bytes/revisions/padding/suffix fields, keyBytes=%zu frameBytes=%zu\n",checks,sizeof key,sizeof frame);
}
static void benchHash(unsigned pattern){Frame frame;Access::Key key;std::mt19937 random(3);auto* bytes=reinterpret_cast<unsigned char*>(&key);for(size_t i=0;i<sizeof key;++i)bytes[i]=static_cast<unsigned char>(random());std::uint64_t sink=0;const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<2000000;++i){key.start=i;key.primitives=i%401+1;if(pattern==1&&!(i%64))++key.version[0];if(pattern==2)++key.version[0];if(pattern==3)key.vb[0]^=1;sink^=Access::hash(frame,key);}
 auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();std::printf("HASH_BENCH pattern=%u us=%.3f sink=%llu\n",pattern,us,static_cast<unsigned long long>(sink));}
static std::uint64_t identity(void* p,bool){return std::uint64_t(reinterpret_cast<std::uintptr_t>(p));}
static std::uint64_t version(void*,bool){return 1;}
static Device* active=nullptr;
static void metadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* out,unsigned count){for(unsigned i=0;i<count;++i){out[i]={};void* p=requests[i].buffer;if(!p)continue;out[i].identity=identity(p,requests[i].index);out[i].revision=1;out[i].known=true;
 if(requests[i].index){out[i].size=UINT(active->ib.bytes.size());out[i].usage=active->ib.desc.Usage;out[i].format=active->ib.desc.Format;}
 else for(unsigned s=0;s<4;++s)if(p==static_cast<IDirect3DVertexBuffer9*>(&active->vb[s])){out[i].size=UINT(active->vb[s].bytes.size());out[i].usage=active->vb[s].desc.Usage;}}}
static void benchFrame(){Device d;active=&d;Frame f;f.setIdentityProvider(identity);f.setVersionProvider(version);f.setMetadataProvider(metadata);f.setChargeResidentHits(false);Mesh unused;std::shared_ptr<const Mesh> mesh;Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};assert(f.read(&d,&d.decl,draw,unused,nullptr,false,&mesh));std::uint64_t sink=0;
 auto start=std::chrono::steady_clock::now();for(unsigned frame=0;frame<80;++frame){f.clearFrame();for(unsigned i=0;i<3000;++i){const bool ok=f.read(&d,&d.decl,draw,unused,nullptr,false,&mesh);assert(ok);sink+=mesh->vertexCount;}}
 auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();std::printf("FRAME_BENCH draws=240000 us=%.3f sink=%llu\n",us,static_cast<unsigned long long>(sink));}
int main(int argc,char** argv){if(argc>1){if(std::atoi(argv[1])==4)benchFrame();else benchHash(unsigned(std::atoi(argv[1])));}else exact();}
