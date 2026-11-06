#include "wall_unique.h"
#include "northlight_mem.h"
#include <random>
#include <chrono>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
using namespace NorthlightActorDeformation;using namespace NorthlightReplayBounds::detail;
bool DisplacementStateTestAccess::wallCall(const Program& p,const float* low,const float* high,const float* bank,float& out,bool fast){return NorthlightAutoDisplacement::nearestViewDepth(p,0,low,high,bank,out,fast?2:0);}
bool DisplacementStateTestAccess::wallMatch(const Program& p){return NorthlightAutoDisplacement::wallDepthProgram(p);}
volatile float sink;
int main(){Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0}};
 const unsigned code[]={9,9,1,9};const Word dest[]={0x80010000,0x80020000,0x80080000,0x80040000},a[]={0xa0e4001f,0xa0e40020,0xa0000000,0xa0e40021},b[]={0x90e40000,0x90e40000,0,0x90e40000};
 for(unsigned i=0;i<4;++i){Operation op;op.code=code[i];op.destination=dest[i];op.source[0]={a[i],0};op.source[1]={b[i],0};p.operations.push_back(op);}buildKernel(p);assert(DisplacementStateTestAccess::wallMatch(p));
 for(unsigned i=0;i<4;++i)for(unsigned field=0;field<8;++field){auto q=p;if(field==0)q.operations[i].code^=1;else if(field==1)q.operations[i].destination^=1;else if(field<5)q.operations[i].source[field-2].token^=1;else q.operations[i].source[field-5].address=1;assert(!DisplacementStateTestAccess::wallMatch(q));}
 {auto q=p;q.major=1;assert(!DisplacementStateTestAccess::wallMatch(q));q=p;q.major=2;assert(DisplacementStateTestAccess::wallMatch(q));q=p;q.inputs[0].reg=1;assert(!DisplacementStateTestAccess::wallMatch(q));q=p;q.inputs[0].usage=D3DDECLUSAGE_NORMAL;assert(!DisplacementStateTestAccess::wallMatch(q));q=p;q.textureCoordinates=true;assert(!DisplacementStateTestAccess::wallMatch(q));}

 std::mt19937 gen(17);float bank[1024]={},lo[3],hi[3];for(unsigned sample=0;sample<200000;++sample){for(unsigned k=0;k<3;++k){lo[k]=float(int(gen()%10000)-5000)*.01f;hi[k]=lo[k]+float(gen()%100)*.01f;}for(unsigned k=0;k<1024;++k)bank[k]=float(int(gen()%2000)-1000)*.001f;bank[0]=1;if(sample%17==0)bank[(31+gen()%3)*4+gen()%4]=std::numeric_limits<float>::quiet_NaN();if(sample%41==0){unsigned bits=gen();std::memcpy(&bank[(31+gen()%3)*4+gen()%4],&bits,4);}if(sample%23==0){unsigned bits=gen();std::memcpy(&lo[gen()%3],&bits,4);}p.definitions.clear();if(sample%7==0){Definition d;d.reg=(gen()%2)?0:31+gen()%3;for(auto &v:d.value){unsigned bits=gen();std::memcpy(&v,&bits,4);}p.definitions.push_back(d);if(sample%14==0)p.definitions.push_back(d);}if(sample%31==0)bank[(31+gen()%3)*4+gen()%4]=std::numeric_limits<float>::denorm_min();float a=0,b=0;bool x=DisplacementStateTestAccess::wallCall(p,lo,hi,bank,a,false),y=DisplacementStateTestAccess::wallCall(p,lo,hi,bank,b,true);if(x!=y||(x&&std::memcmp(&a,&b,4))){printf("MISMATCH %u %u %u %.9g %.9g\n",sample,x,y,a,b);return 1;}}
 std::puts("MATERIAL_WALL_BOUNDS_PASS exact_200000 current_constants definitions duplicate_defs nan subnormal random_bits program_mismatch");}
