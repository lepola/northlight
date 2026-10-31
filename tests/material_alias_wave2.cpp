#include "alias_fixture.h"
#include "northlight_mem.h"
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
volatile uintptr_t sink;
void DisplacementStateTestAccess::aliasPerf(){using G=NorthlightAutoDisplacement;using M=NorthlightDrawSnapshot::Mesh;using E=G::Entry;cache();
 for(unsigned size:{4096u,65536u,1048576u}){G g(nullptr);auto mesh=std::make_shared<M>();mesh->vertexCount=size/16;mesh->primitiveCount=1;mesh->streams[0].stride=16;mesh->streams[0].bytes.resize(size);for(unsigned i=0;i<size;++i)mesh->streams[0].bytes[i]=i*31+i/7;mesh->sourceIdentity=NorthlightSourceIdentity::make({1,1});auto query=std::make_shared<M>(*mesh);query->sourceIdentity=NorthlightSourceIdentity::make({1,2});query->replayIdentity=mesh->sourceIdentity;auto e=std::make_unique<E>();e->source=mesh;e->program=1;e->domain=1;g.retain(std::move(e));assert(g.geometrySource(query,nullptr,1,1)==mesh);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<2000;++i){auto p=g.geometrySource(query,nullptr,1,1);sink=(uintptr_t)p.get();}printf("%u,%.6f\n",size,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}}
int main(){DisplacementStateTestAccess::aliasPerf();}
