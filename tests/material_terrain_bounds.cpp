#include "terrain_unique.h"
#include "northlight_mem.h"
#include <random>
#include <chrono>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
using namespace NorthlightActorDeformation;using namespace NorthlightReplayBounds::detail;
bool DisplacementStateTestAccess::terrainCall(const Program& p,const float* low,const float* high,const float* bank,float& out,bool fast){return NorthlightAutoDisplacement::nearestViewDepth(p,0,low,high,bank,out,fast);}
bool DisplacementStateTestAccess::terrainMatch(const Program& p){return NorthlightAutoDisplacement::terrainDepthProgram(p);}
volatile float sink;
int main(){Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0}};
 const unsigned code[]={5,4,4,2};const Word a[]={0xa0e40001,0x90000000,0x90aa0000,0x80e40000},b[]={0x90550000,0xa0e40000,0xa0e40002,0xa0e40003},c[]={0,0x80e40000,0x80e40000,0};
 for(unsigned i=0;i<4;++i){Operation op;op.code=code[i];op.destination=0x800f0000;op.source[0]={a[i],0};op.source[1]={b[i],0};op.source[2]={c[i],0};p.operations.push_back(op);}buildKernel(p);assert(DisplacementStateTestAccess::terrainMatch(p));
 for(unsigned i=0;i<4;++i)for(unsigned field=0;field<8;++field){auto q=p;if(field==0)q.operations[i].code^=1;else if(field==1)q.operations[i].destination^=1;else if(field<5)q.operations[i].source[field-2].token^=1;else q.operations[i].source[field-5].address=1;assert(!DisplacementStateTestAccess::terrainMatch(q));}
 {auto q=p;q.major=2;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.inputs[0].reg=1;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.inputs[0].usage=D3DDECLUSAGE_NORMAL;assert(!DisplacementStateTestAccess::terrainMatch(q));q=p;q.textureCoordinates=true;assert(!DisplacementStateTestAccess::terrainMatch(q));}

 std::mt19937 gen(17);float bank[1024]={},lo[3],hi[3];for(unsigned sample=0;sample<200000;++sample){for(unsigned k=0;k<3;++k){lo[k]=float(int(gen()%10000)-5000)*.01f;hi[k]=lo[k]+float(gen()%100)*.01f;}for(unsigned k=0;k<16;++k)bank[k]=float(int(gen()%2000)-1000)*.001f;for(unsigned k=0;k<3;++k)bank[k*4+3]=0;bank[15]=1;if(sample%17==0)bank[gen()%16]=std::numeric_limits<float>::quiet_NaN();if(sample%41==0){unsigned bits=gen();std::memcpy(&bank[gen()%16],&bits,4);}if(sample%23==0){unsigned bits=gen();std::memcpy(&lo[gen()%3],&bits,4);}p.definitions.clear();if(sample%7==0){Definition d;d.reg=gen()%4;for(auto &v:d.value){unsigned bits=gen();std::memcpy(&v,&bits,4);}p.definitions.push_back(d);if(sample%14==0)p.definitions.push_back(d);}if(sample%31==0)bank[gen()%16]=std::numeric_limits<float>::denorm_min();float a=0,b=0;bool x=DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,a,false),y=DisplacementStateTestAccess::terrainCall(p,lo,hi,bank,b,true);if(x!=y||(x&&std::memcmp(&a,&b,4))){printf("MISMATCH %u %u %u %.9g %.9g\n",sample,x,y,a,b);return 1;}}
 std::puts("MATERIAL_TERRAIN_BOUNDS_PASS exact_200000 current_constants definitions duplicate_defs nan subnormal random_bits program_mismatch");}
