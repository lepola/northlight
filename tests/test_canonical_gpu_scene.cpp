#define main previous_gpu_scene_main
#include "test_shared_gpu_residency.cpp"
#undef main
#include "scene_native_store.h"

static void sceneControls(){
 using namespace NorthlightGpuScene;NativeStore scene;NativeClient mat(scene,Consumer::Material),shadow(scene,Consumer::Shadow);Device d;auto m=authored(11);Binding first,second;
 scene.setFrame(1);assert(!mat.bind(&d,m,first.vb,first.ib,admit,first.at));assert(!shadow.bind(&d,m,second.vb,second.ib,admit,second.at));
 assert(mat.population().entries+shadow.population().entries==1&&!d.calls);
 scene.setFrame(2);assert(!mat.bind(&d,m,first.vb,first.ib,admit,first.at));assert(mat.commitPending());assert(mat.bind(&d,m,first.vb,first.ib,admit,first.at)&&exact(*m,first));
 auto rebuilt=std::make_shared<Mesh>(*m);m.reset();const auto calls=d.calls;
 scene.setFrame(3);assert(shadow.bind(&d,rebuilt,second.vb,second.ib,admit,second.at)&&exact(*rebuilt,second)&&d.calls==calls&&shadow.stats().reconstructedHits==1);
 mat.clear();assert(!mat.bytes()&&shadow.bytes()&&scene.resident(rebuilt));assert(shadow.bind(&d,rebuilt,second.vb,second.ib,admit,second.at)&&d.calls==calls);
 mat.setLimit(32u<<20);assert(mat.bind(&d,rebuilt,first.vb,first.ib,admit,first.at));shadow.clear();assert(!shadow.bytes()&&mat.bytes()&&scene.resident(rebuilt));
 // An independent device cannot consume this scene's buffers.
 Device other;assert(!mat.bind(&other,rebuilt,first.vb,first.ib,admit,first.at));assert(exact(*rebuilt,first));
 scene.clearAll();assert(exact(*rebuilt,first)&&exact(*rebuilt,second));
 std::puts("CANONICAL_SCENE_CONTROLS_PASS one_identity one_upload reconstructed_source cross_pass source_release consumer_off ownership_transfer held_views device_isolation");
}
static void faultsAndCollisions(){
 using namespace NorthlightGpuScene;
 for(unsigned failure=0;failure<4;++failure){NativeStore scene;NativeClient c(scene,Consumer::Material);Device d;auto m=authored(90);Binding b;
  scene.setFrame(1);assert(!c.bind(&d,m,b.vb,b.ib,admit,b.at));scene.setFrame(2);assert(!c.bind(&d,m,b.vb,b.ib,admit,b.at));
  if(failure<2)d.failAt=d.calls+failure+1;else if(failure==2)d.lockFail=true;else d.failUnlockAt=d.calls+1;
  c.commitPending();assert(!c.bytes()&&!alive&&c.population().probation==1&&c.chargedBytes()<=c.limit());
  d.failAt=d.failUnlockAt=0;d.lockFail=false;scene.setFrame(3);assert(!c.bind(&d,m,b.vb,b.ib,admit,b.at));assert(c.commitPending());assert(c.bind(&d,m,b.vb,b.ib,admit,b.at)&&exact(*m,b));
 }
 assert(!alive);
 NativeStore scene;NativeClient c(scene,Consumer::Material);Device d;auto a=std::const_pointer_cast<Mesh>(authored(201)),b=std::const_pointer_cast<Mesh>(authored(202));
 constexpr uint64_t prime=1099511628211ull,offset=1469598103934665603ull;uint64_t inverse=1;for(unsigned i=0;i<6;++i)inverse*=2-prime*inverse;
 a->sourceIdentity=NorthlightSourceIdentity::make({0x111,0x222});uint64_t prefix=(offset^0x333)*prime;
 b->sourceIdentity=NorthlightSourceIdentity::make({0x333,(a->sourceIdentity->hash*inverse)^prefix});
 assert(a->sourceIdentity->hash==b->sourceIdentity->hash&&!NorthlightSourceIdentity::same(a->sourceIdentity,b->sourceIdentity));
 Binding x,y;scene.setFrame(1);assert(!c.bind(&d,a,x.vb,x.ib,admit,x.at)&&!c.bind(&d,b,y.vb,y.ib,admit,y.at)&&c.population().entries==2);
 scene.setFrame(2);c.bind(&d,a,x.vb,x.ib,admit,x.at);c.bind(&d,b,y.vb,y.ib,admit,y.at);c.commitPending();
 assert(c.bind(&d,a,x.vb,x.ib,admit,x.at)&&c.bind(&d,b,y.vb,y.ib,admit,y.at)&&exact(*a,x)&&exact(*b,y));
 std::puts("CANONICAL_SCENE_FAULTS_PASS partial_create lock unlock retry full_64bit_hash_collision exact_words");
}
static void limitsAndClock(){
 using namespace NorthlightGpuScene;Device d;Binding b;
 {
  NativeStore scene;NativeClient m(scene,Consumer::Material),s(scene,Consumer::Shadow);scene.setFrame(1);
  auto unknown=std::const_pointer_cast<Mesh>(authored(60));unknown->sourceIdentity.reset();unknown->replayIdentity.reset();
  assert(!m.bind(&d,unknown,b.vb,b.ib,admit,b.at));unknown.reset();scene.setFrame(2);assert(!m.population().entries);
  m.setLimit(16384);s.setLimit(32768);std::vector<Owner> meshes;
  for(unsigned i=0;i<80;++i)meshes.push_back(authored(1000+i));
  for(unsigned frame=3;frame<20;++frame){scene.setFrame(frame);
   for(auto& mesh:meshes){m.bind(&d,mesh,b.vb,b.ib,admit,b.at);s.bind(&d,mesh,b.vb,b.ib,admit,b.at);}
   m.commitPending();s.commitPending();assert(m.chargedBytes()<=16384&&s.chargedBytes()<=32768);
  }
  m.trimTo(0);s.trimTo(0);assert(!scene.chargedBytes()&&!scene.bytes());
 }
 {
  NativeStore scene;NativeClient m(scene,Consumer::Material);scene.setFrame(1);std::vector<Owner> meshes;
  for(unsigned i=0;i<8300;++i){auto mesh=authored(10000+i);meshes.push_back(mesh);m.bind(&d,mesh,b.vb,b.ib,admit,b.at);}
  assert(m.population().entries==NativeStore::entryLimit()&&m.stats().roomRejected);
  scene.setFrame(2);m.bind(&d,meshes.back(),b.vb,b.ib,admit,b.at);assert(m.population().entries==NativeStore::entryLimit()&&m.stats().probationEvictions);
 }
 std::puts("CANONICAL_SCENE_LIMITS_PASS private_lifetime count_cap current_frame_protection exact_lane_caps simultaneous_trim no_budget_pingpong");
}
static void boundedCommits(){
 using namespace NorthlightGpuScene;
 for(unsigned lane=0;lane<2;++lane){
  NativeStore scene;NativeClient client(scene,Consumer(lane));Device device;Binding binding;std::vector<Owner> meshes;
  for(unsigned i=0;i<17;++i)meshes.push_back(authored(88000+i));
  scene.setFrame(1);for(auto& mesh:meshes)assert(!client.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));
  scene.setFrame(2);for(auto& mesh:meshes)assert(!client.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));
  assert(client.stats().attemptDeferred==1);assert(client.commitPending());
  for(unsigned i=0;i<16;++i)assert(scene.resident(meshes[i]));assert(!scene.resident(meshes.back()));
  assert(!client.commitPending());scene.setFrame(3);
  assert(!client.bind(&device,meshes.back(),binding.vb,binding.ib,admit,binding.at));assert(client.commitPending());
  for(auto& mesh:meshes)assert(client.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at)&&exact(*mesh,binding));
 }
 assert(!alive);std::puts("CANONICAL_SCENE_BATCH_LIMIT_PASS both_consumers sixteen_pending seventeenth_deferred next_frame_retry repeated_commit exact_bytes");
}
static void reentrantAdmission(){
 using namespace NorthlightGpuScene;
 for(bool byteLimit:{false,true}){
  NativeStore scene;NativeClient client(scene,Consumer::Material);Device device;Binding binding;std::vector<Owner> meshes;
  const unsigned count=byteLimit?2:17;for(unsigned i=0;i<count;++i)meshes.push_back(authored(99000+i));
  if(byteLimit){std::const_pointer_cast<Mesh>(meshes[0])->streams[0].bytes.resize(3u<<20);std::const_pointer_cast<Mesh>(meshes[1])->streams[0].bytes.resize(((2u<<20)/48)*48);}
  scene.setFrame(1);for(auto& mesh:meshes)assert(!client.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));
  scene.setFrame(2);auto admission=[&](size_t){for(unsigned i=1;i<count;++i)assert(!client.bind(&device,meshes[i],binding.vb,binding.ib,admit,binding.at));return true;};
  assert(!client.bind(&device,meshes[0],binding.vb,binding.ib,admission,binding.at));
  assert(byteLimit?client.stats().uploadByteDeferred==1:client.stats().attemptDeferred==1);
  assert(client.commitPending()&&!scene.resident(meshes[0]));for(unsigned i=1;i<count;++i)assert(scene.resident(meshes[i]));
 }
 assert(!alive);std::puts("CANONICAL_SCENE_ADMISSION_REENTRY_PASS attempt_and_byte_limits rechecked_after_nested_submissions");
}
static void batchPreservation(){
 using namespace NorthlightGpuScene;
 for(unsigned lane=0;lane<2;++lane)for(bool ordered:{false,true})for(bool deny:{false,true})for(unsigned count:{1u,17u,64u,512u}){
  NativeStore scene;NativeClient source(scene,Consumer(lane)),other(scene,Consumer(1-lane));Device device;Binding binding;
  std::vector<Owner> meshes;for(unsigned i=0;i<count;++i)meshes.push_back(authored(123000+i));
  scene.setFrame(1);for(auto& mesh:meshes)assert(!source.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));
  for(unsigned at=0;at<count;at+=16){scene.setFrame(2+at/16);
   for(unsigned i=at;i<std::min(at+16,count);++i)assert(!source.bind(&device,meshes[i],binding.vb,binding.ib,admit,binding.at));
   assert(source.commitPending());
  }
  for(unsigned i=0;i<count;++i)if(i%16==0||i%3==1)assert(other.bind(&device,meshes[i],binding.vb,binding.ib,admit,binding.at));
  // Put a shared entry first in each batch's LRU order, with unused siblings.
  if(ordered){scene.setFrame(1000);for(auto& mesh:meshes)assert(source.bind(&device,mesh,binding.vb,binding.ib,admit,binding.at));}
  if(deny)other.trimTo(0); // Preservation must still obey the other consumer's retained budget.
  const auto creates=device.calls;source.clear();assert(!source.chargedBytes()&&!source.bytes());unsigned retained=0;
  for(unsigned i=0;i<count;++i){const bool keep=!deny&&(i%16==0||i%3==1);assert(scene.resident(meshes[i])==keep);retained+=keep;
   if(keep)assert(other.bind(&device,meshes[i],binding.vb,binding.ib,admit,binding.at)&&exact(*meshes[i],binding));
  }
  assert(other.population().entries==retained&&other.chargedBytes()<=other.limit()&&device.calls==creates);
 }
 assert(!alive);std::puts("CANONICAL_SCENE_PRESERVATION_PASS both_consumers sparse_shared_batches unused_siblings exact_bytes no_reupload");
}
static Result unified(const std::vector<Owner>& meshes,bool audit,unsigned overlap,unsigned frames=96,bool reconstruct=false){
 using namespace NorthlightGpuScene;NativeStore scene;NativeClient mat(scene,Consumer::Material),world(scene,Consumer::Shadow);Device device;Result result;
 std::vector<Owner> material,actors;
 for(size_t i=0;i<meshes.size();++i){auto a=std::make_shared<Mesh>(*meshes[i]);actors.push_back(a);
  if(i*100/meshes.size()<overlap)material.push_back(a);else{auto b=std::make_shared<Mesh>(*a);b->sourceIdentity=NorthlightSourceIdentity::make({0x6975aa3,2000+i});material.push_back(b);}}
 auto clear=[](Binding& b){for(auto& p:b.vb){if(p)p->Release();p=nullptr;}if(b.ib)b.ib->Release();b.ib=nullptr;b.at={};};
 std::vector<std::unique_ptr<Binding>> materials,shadows;for(size_t i=0;i<meshes.size();++i){materials.push_back(std::make_unique<Binding>());shadows.push_back(std::make_unique<Binding>());}
 std::vector<bool> ready(meshes.size());const auto begin=std::chrono::steady_clock::now();
 for(unsigned frame=0;frame<frames;++frame){scene.setFrame(frame+1);
  if(reconstruct&&frame%5==4){for(auto& m:actors)m=std::make_shared<Mesh>(*m);for(size_t i=0;i<material.size();++i)material[i]=i*100/meshes.size()<overlap?actors[i]:std::make_shared<Mesh>(*material[i]);}
  for(size_t j=0;j<meshes.size();++j){const size_t i=(j+(frame/24)*13)%meshes.size();auto& b=*materials[i];clear(b);ready[i]=mat.bind(&device,material[i],b.vb,b.ib,admit,b.at);}
  mat.commitPending();
  for(size_t j=0;j<meshes.size();++j){size_t i=(j+(frame/24)*13)%meshes.size();auto& m=material[i];auto& b=*materials[i];
   if(!ready[i]&&mat.resident(m))ready[i]=mat.bind(&device,m,b.vb,b.ib,admit,b.at);
   if(ready[i]){if(audit)assert(exact(*m,b));}else result.up+=m->byteSize();result.digest=result.digest*33+uint64_t(m->vertexCount)*7+m->primitiveCount;}
  for(size_t i=0;i<meshes.size();++i){auto& b=*shadows[i];clear(b);ready[i]=world.bind(&device,actors[i],b.vb,b.ib,admit,b.at);}
  world.commitPending();
  for(size_t i=0;i<meshes.size();++i){auto& b=*shadows[i];if(!ready[i]&&world.resident(actors[i]))ready[i]=world.bind(&device,actors[i],b.vb,b.ib,admit,b.at);
   if(ready[i]){if(audit)assert(exact(*actors[i],b));}else result.up+=actors[i]->byteSize();}
  result.uploaded+=mat.uploaded()+world.uploaded();result.peak=std::max(result.peak,mat.bytes()+world.bytes());
  assert(mat.chargedBytes()<=mat.limit()&&world.chargedBytes()<=world.limit());
  if(frame%47==46){mat.trimTo(0);world.trimTo(0);}
 }
 result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();result.creates=device.calls;scene.clearAll();materials.clear();shadows.clear();assert(!alive);return result;
}
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc>=2);NorthlightReplayGPU::createBudgetMs()=1e6;auto meshes=load(argv[1]);
 if(argc>2){const auto overlap=argc>3?unsigned(std::stoul(argv[3])):100;const bool canonical=std::string(argv[2])=="canonical";
  auto r=canonical?unified(meshes,false,overlap):run(meshes,true,false,96,overlap);
  std::printf("CANONICAL_SCENE_TIMING canonical=%u overlap=%u cpuMs=%.6f uploads=%zu fallbackBytes=%zu peak=%zu creates=%u digest=%llu\n",canonical,overlap,r.ms,r.uploaded,r.up,r.peak,r.creates,(unsigned long long)r.digest);return 0;}
 sceneControls();assert(!alive);faultsAndCollisions();assert(!alive);limitsAndClock();assert(!alive);boundedCommits();reentrantAdmission();batchPreservation();
 for(unsigned overlap:{0u,50u,100u}){auto reference=run(meshes,true,true,96,overlap),current=unified(meshes,true,overlap),rebuilt=unified(meshes,true,overlap,96,true);
  std::printf("CANONICAL_SCENE_CHECK overlap=%u reference=%llu current=%llu reconstructed=%llu uploads=%zu/%zu fallback=%zu/%zu\n",overlap,(unsigned long long)reference.digest,(unsigned long long)current.digest,(unsigned long long)rebuilt.digest,reference.uploaded,current.uploaded,reference.up,current.up);
  assert(current.digest==reference.digest&&rebuilt.digest==reference.digest&&current.up<=reference.up&&current.uploaded<=reference.uploaded);
  assert(rebuilt.uploaded==current.uploaded&&rebuilt.peak==current.peak);
  std::printf("CANONICAL_SCENE_EXACT_PASS overlap=%u uploads=%zu->%zu fallback=%zu->%zu peak=%zu->%zu creates=%u->%u reconstructedUploads=%zu digest=%llu\n",overlap,reference.uploaded,current.uploaded,reference.up,current.up,reference.peak,current.peak,reference.creates,current.creates,rebuilt.uploaded,(unsigned long long)current.digest);
 }
}
