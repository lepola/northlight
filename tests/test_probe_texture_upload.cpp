// CPU atlas upload regression: no D3D device, DLL or game.
#include "probe_texture_upload.h"
#include "fixtures/probe_texture_upload_20261030.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace NorthlightGI;
static void reference(unsigned channel,void* bits,size_t pitch,unsigned n,
 const std::vector<ProbeAtlasEntry>& atlas,const NorthlightProbeBlend::Mirror& blend,
 NorthlightProbeActivation& activation,float now,bool previous){
 for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
  const auto& entry=atlas[x+y*n+z*n*n];const auto& p=entry.probe;
  float* dst=(float*)((char*)bits+y*pitch)+(x+z*n)*4;
  if(channel<3){for(int k=0;k<4;++k)dst[k]=channel==0?p.sh[k].x:channel==1?p.sh[k].y:p.sh[k].z;}
  else if(channel==3)for(unsigned axis=0;axis<6;++axis){
   float* moment=(float*)((char*)bits+(y+axis*n)*pitch)+(x+z*n)*4;
   moment[0]=p.moments[axis].mean;moment[1]=p.moments[axis].meanSquare;
   moment[2]=entry.occupied&&p.valid?1.f:0.f;moment[3]=previous?blend.start(x+y*n+z*n*n):NorthlightProbeBlend::None;
  }else{dst[0]=float(entry.key.x);dst[1]=float(entry.key.y);dst[2]=float(entry.key.z);dst[3]=activation.update(x+y*n+z*n*n,entry,now);}
 }
}
static void referencePrevious(void* bits,size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
 for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
  const float* prev=blend.previous(x+y*n+z*n*n);float* row=(float*)((char*)bits+y*pitch)+(x+z*n)*4;
  for(int c=0;c<3;++c)for(int k=0;k<4;++k)row[c*n*n*4+k]=prev[c*4+k];
 }
}
static uint32_t randomState=42;
static uint32_t next(){return randomState=randomState*1664525u+1013904223u;}
static void mutate(std::vector<ProbeAtlasEntry>& atlas,unsigned round){
 for(size_t i=0;i<atlas.size();++i){
  auto& e=atlas[i];if(round==0||next()%37==0)e.key={int32_t(next()),int32_t(next()),int32_t(next())};
  e.occupied=next()%5!=0;e.probe.valid=next()%7!=0;
  if(round==0||next()%3==0){
   for(auto& sh:e.probe.sh)sh={float(int(next()%2048)-1024)/128,float(int(next()%2048)-1024)/128,float(int(next()%2048)-1024)/128};
   for(auto& m:e.probe.moments){m.mean=float(next()%4096)/8;m.meanSquare=float(next()%4096)/4;}
  }
 }
}
int main(){
 unsigned cases=0;uint64_t checkedBytes=0;
 for(unsigned n:{16u,22u,24u}){
  assert(configureProbeLayout(probeLayoutFor(n-2)));
  std::vector<ProbeAtlasEntry> atlas(probeLayout().atlasSize());NorthlightProbeBlend::Mirror blend;
  NorthlightProbeActivation a,b,c;blend.begin("A");a.begin("A");b.begin("A");c.begin("A");
  for(unsigned round=0;round<12;++round){
   const char* map=round<6?"A":"Northrend";blend.begin(map);a.begin(map);b.begin(map);c.begin(map);
   mutate(atlas,round);
   if(round==3)for(size_t i=0;i<atlas.size();i+=31){
    static const uint32_t bits[]={0x80000000u,0x7fc12345u,0x7f800000u,0x00000001u};
    for(unsigned k=0;k<4;++k){std::memcpy(&atlas[i].probe.sh[k].x,&bits[k],4);std::memcpy(&atlas[i].probe.moments[k].mean,&bits[3-k],4);}
   }
   const float now=10+float(round)*.075f;blend.publish(atlas,now);
   for(unsigned channel=0;channel<6;++channel){
    const size_t pitch=(n*n*(channel==5?3:1)*4+4*(round%5))*sizeof(float);
    const size_t bytes=pitch*n*(channel==3?6:1);
    std::vector<unsigned char> old(bytes+8,0xa5),current=old,independent=old;
    if(channel==5){referencePrevious(old.data()+4,pitch,n,blend);NorthlightProbeUpload::previous(current.data()+4,pitch,n,blend);NorthlightProbeUpload::previous(independent.data()+4,pitch,n,blend);}
    else{
     reference(channel,old.data()+4,pitch,n,atlas,blend,a,now,round%2);
     NorthlightProbeUpload::write(channel,current.data()+4,pitch,n,atlas,blend,b,now,round%2);
     NorthlightProbeUpload::write(channel,independent.data()+4,pitch,n,atlas,blend,c,now,round%2,true);
    }
    assert(old==current&&old==independent);++cases;checkedBytes+=bytes;
   }
   for(size_t i=0;i<atlas.size();++i)assert(a.update(i,atlas[i],now+1)==b.update(i,atlas[i],now+1));
   for(size_t i=0;i<atlas.size();++i)assert(b.update(i,atlas[i],now+1)==c.update(i,atlas[i],now+1));
  }
 }
 // An undersized mirror must throw at the same texel and leave identical partial writes.
 assert(configureProbeLayout(probeLayoutFor(14)));
 NorthlightProbeBlend::Mirror shortBlend;shortBlend.begin("A");
 assert(configureProbeLayout(probeLayoutFor(20)));
 {
  const unsigned n=probeLayout().atlas;const size_t pitch=size_t(n)*n*4*sizeof(float);
  std::vector<ProbeAtlasEntry> atlas(probeLayout().atlasSize());NorthlightProbeActivation a,b;
  std::vector<unsigned char> old(pitch*n*6,0xa5),current=old;bool oldThrew=false,newThrew=false;
  try{NorthlightProbeUpload::write<3,false,1,false>(old.data(),pitch,n,atlas,shortBlend,a,0,true);}catch(const std::out_of_range&){oldThrew=true;}
  try{NorthlightProbeUpload::write(3,current.data(),pitch,n,atlas,shortBlend,b,0,true,true);}catch(const std::out_of_range&){newThrew=true;}
  assert(oldThrew&&newThrew&&old==current);
  std::fill(old.begin(),old.end(),0xa5);current=old;oldThrew=newThrew=false;
  try{NorthlightProbeUpload::previousImpl<false>(old.data(),pitch,n,shortBlend);}catch(const std::out_of_range&){oldThrew=true;}
  try{NorthlightProbeUpload::previous(current.data(),pitch,n,shortBlend);}catch(const std::out_of_range&){newThrew=true;}
  assert(oldThrew&&newThrew&&old==current);
 }
 // A possibly aliased target retains the original load/store order.
 for(unsigned channel=0;channel<3;++channel){
  std::vector<ProbeAtlasEntry> expected(1),current(1);for(unsigned k=0;k<4;++k)expected[0].probe.sh[k]={float(k*3+1),float(k*3+2),float(k*3+3)};current=expected;
  NorthlightProbeBlend::Mirror mirror;NorthlightProbeActivation activation;
  FrozenProbeUpload::write(channel,&expected[0].probe.sh[1].x,16,1,expected,mirror,activation,0,false,false);
  NorthlightProbeUpload::write(channel,&current[0].probe.sh[1].x,16,1,current,mirror,activation,0,false,false);
  assert(!std::memcmp(expected[0].probe.sh,current[0].probe.sh,sizeof current[0].probe.sh));
 }
 assert(configureProbeLayout(probeLayoutFor(20)));const unsigned n=probeLayout().atlas;
 std::vector<ProbeAtlasEntry> atlas(probeLayout().atlasSize());mutate(atlas,0);
 NorthlightProbeBlend::Mirror blend;NorthlightProbeActivation activation;blend.begin("A");activation.begin("A");blend.publish(atlas,10);
 std::vector<unsigned char> buffers[6];size_t pitches[6];
 for(unsigned c=0;c<6;++c){pitches[c]=size_t(n)*n*4*(c==5?3:1)*sizeof(float);buffers[c].resize(pitches[c]*n*(c==3?6:1));}
 double elapsed[2][8];unsigned retained[2]={};volatile float sink=0;
 for(unsigned block=0;block<20;++block){
  const unsigned mode=(block/2)%2?1-block%2:block%2;
  const auto begin=std::chrono::steady_clock::now();
  for(unsigned iteration=0;iteration<200;++iteration){
   for(unsigned c=0;c<5;++c){
    if(mode)NorthlightProbeUpload::write(c,buffers[c].data(),pitches[c],n,atlas,blend,activation,11,true,true);
    else FrozenProbeUpload::write(c,buffers[c].data(),pitches[c],n,atlas,blend,activation,11,true,true);
   }
   if(mode)NorthlightProbeUpload::previous(buffers[5].data(),pitches[5],n,blend);
   else FrozenProbeUpload::previous(buffers[5].data(),pitches[5],n,blend);
   float sample;std::memcpy(&sample,buffers[iteration%6].data(),sizeof sample);sink=sample;
  }
  if(block>=4)elapsed[mode][retained[mode]++]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/200;
 }
 assert(retained[0]==8&&retained[1]==8);std::sort(elapsed[0],elapsed[0]+8);std::sort(elapsed[1],elapsed[1]+8);(void)sink;
 std::printf("{\"pass\":true,\"cases\":%u,\"bytes_exact\":%llu,\"old_ms\":%.8f,\"new_ms\":%.8f,\"ratio\":%.4f}\n",cases,(unsigned long long)checkedBytes,(elapsed[0][3]+elapsed[0][4])*.5,(elapsed[1][3]+elapsed[1][4])*.5,(elapsed[1][3]+elapsed[1][4])/(elapsed[0][3]+elapsed[0][4]));
}
