#include <cassert>
#include <cstdio>
#include <chrono>
#include <cfloat>
#include "actor_deformation.h"
using namespace NorthlightActorDeformation;
static Program load(const char* path){FILE* f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);const auto bytes=ftell(f);rewind(f);std::vector<Word> words(bytes/4);assert(fread(words.data(),1,bytes,f)==size_t(bytes));fclose(f);Program p;assert(compile(words.data(),words.size(),p));return p;}
static bool fast(const Program& p,const Four* in,const float* bank,Four& out){return p.kernel.evaluator==Kernel::Weighted?evaluateSkin<false,false,true>(p,in,bank,out):evaluateSkin<false,true,true>(p,in,bank,out);}
static D3DVERTEXELEMENT9 layout[]={{0,0,2,0,0,0},{0,12,3,0,1,0},{0,28,5,0,2,0},{0xff,0,17,0,0,0}};
__attribute__((noinline)) static bool bounds(bool optimized,const Program& p,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* bank,float* low,float* high){
 const bool byte=optimized&&weightedByteIndices(p,prepared,bank);
 for(unsigned i=0;i<mesh.vertexCount;++i){Four in[16],out;for(const auto& b:prepared.bindings){const auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+i*s.stride+b.element.Offset,b.element.Type,in[b.input.reg]))return false;}
  if(byte?!fast(p,in,bank,out):!evaluateFast(p,in,bank,out))return false;
  for(unsigned k=0;k<3;++k){if(!std::isfinite(out[k]))return false;low[k]=std::min(low[k],out[k]);high[k]=std::max(high[k],out[k]);}}
 return true;
}
int main(int argc,char** argv){assert(argc==3);unsigned checks=0;
 for(unsigned fixture=1;fixture<3;++fixture){auto p=load(argv[fixture]);if(p.kernel.commutedSkin()){std::swap(p.operations[0].source[0],p.operations[0].source[1]);buildKernel(p);}assert(p.kernel.evaluator==Kernel::Weighted||p.kernel.evaluator==Kernel::WeightedR0);
  NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=8192;mesh.streams[0].stride=32;mesh.streams[0].bytes.resize(8192*32);
  for(unsigned i=0;i<8192;++i){float v[7]={float(i%17),float(i%23),float(i%7),.1f,.2f,.3f,.4f};auto* b=mesh.streams[0].bytes.data()+i*32;memcpy(b,v,28);for(unsigned k=0;k<4;++k)b[28+k]=(i+k)%75;}
  PreparedBindings prepared;assert(prepareBindings(p,mesh,layout,4,prepared));float bank[1024];for(unsigned i=0;i<1024;++i)bank[i]=float(int(i%37)-18)*.13f;bank[0]=3;bank[1]=1;assert(weightedByteIndices(p,prepared,bank));
  const uint32_t exceptional[]={0,0x80000000,1,0x80000001,0x7f800000,0xff800000,0x7fc12345,0x7f7fffff};
  for(unsigned def=0;def<3;++def)for(unsigned lane=0;lane<4;++lane)for(unsigned index=0;index<256;++index)for(unsigned special=0;special<8;++special){auto q=p;if(def)q.definitions.push_back({32,{.5f,-.2f,.3f,.4f}});if(def==2)q.definitions.push_back({0,{3,0,0,0}});
   Four in[16]={};in[0]={1,-2,3,1};in[1]={.1f,.2f,.3f,.4f};in[2]={0,1,2,3};in[2][lane]=float(index);memcpy(&in[0][special%3],exceptional+special,4);Four a={17,19,23,29},b=a;const bool x=evaluateFast(q,in,bank,a),y=fast(q,in,bank,b);assert(x==y&&!memcmp(a.data(),b.data(),16));++checks;
  }
  for(unsigned alias=0;alias<3;++alias)for(unsigned bad:{0u,74u,75u,255u})for(bool extraDef:{false,true}){
   auto q=p;if(extraDef)q.definitions.push_back({32,{.5f,-.2f,.3f,.4f}});
   Four a[16]={},b[16];a[0]={1,-2,3,1};a[1]={.1f,.2f,.3f,.4f};a[2]={float(bad),1,2,3};std::copy(std::begin(a),std::end(a),b);
   const bool x=evaluateFast(q,a,bank,a[alias]),y=fast(q,b,bank,b[alias]);assert(x==y&&!memcmp(a,b,sizeof a));++checks;
  }
  auto altered=prepared;altered.bindings[2].element.Type=D3DDECLTYPE_UBYTE4N;assert(!weightedByteIndices(p,altered,bank));
  for(float control:{0.f,-3.f,1.5f,std::numeric_limits<float>::infinity()}){auto q=p;q.definitions.push_back({0,{control,1,0,0}});assert(!weightedByteIndices(q,prepared,bank));}
  for(unsigned phase=0;phase<4;++phase){auto q=p;if(phase==1)q.definitions.push_back({31,{.3f,-.7f,1,99}});if(phase==2)mesh.streams[0].bytes[32*3+30]=255;if(phase==3){mesh.streams[0].bytes[32*3+30]=74;bank[31*4]=std::numeric_limits<float>::quiet_NaN();}
   float a[3]={FLT_MAX,FLT_MAX,FLT_MAX},b[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX},c[3],d[3];memcpy(c,a,12);memcpy(d,b,12);bool x=bounds(false,q,mesh,prepared,bank,a,b),y=bounds(true,q,mesh,prepared,bank,c,d);assert(x==y&&!memcmp(a,c,12)&&!memcmp(b,d,12));++checks;}
  bank[31*4]=1;std::vector<double> old,newer;volatile float sum=0;
  for(unsigned round=0;round<9;++round)for(unsigned order=0;order<2;++order){bool next=(round+order)&1;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<32;++n){float low[3]={FLT_MAX,FLT_MAX,FLT_MAX},high[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};assert(bounds(next,p,mesh,prepared,bank,low,high));sum=sum+low[0]+high[0];}double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32;(next?newer:old).push_back(ms);}
  assert(std::isfinite(sum));std::sort(old.begin(),old.end());std::sort(newer.begin(),newer.end());printf("WEIGHTED_BYTE_BENCH fixture=%u oldMs=%.6f newMs=%.6f changePercent=%.3f vertices=8192\n",fixture,old[4],newer[4],100*(newer[4]/old[4]-1));
 }
 printf("WEIGHTED_BYTE_PASS checks=%u exact_output failure_output current_defs mutation bounds\n",checks);
}
