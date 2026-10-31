#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace NorthlightGeometryMemory;
static unsigned GetTickCount(){return 1000;}
int main(){
 for(unsigned i=0;i<14;++i){
    EntryLift chunkLift;auto entryBudget=buildBudget(64*MiB);Sample memory{600*MiB,120*MiB,true};
    if(i==1)memory.largest=40*MiB;
    if(i==2)memory.available=500*MiB;
    if(i==3)memory.available=479*MiB;
    if(i==4)memory.largest=32*MiB;
    if(i==5)memory.valid=false;
    if(i==6){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,364*MiB,999,0);}
    if(i==7){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,384*MiB,999,50*MiB);}
    if(i==8){memory.available=480*MiB;memory.largest=33*MiB;}
    if(i==9){memory.available=395*MiB;memory.largest=183*MiB;}
    if(i==10){memory.available=391*MiB;memory.largest=183*MiB;}
    if(i==11){memory.available=395*MiB;memory.largest=32*MiB;}
    if(i==12){memory.available=395*MiB;memory.largest=183*MiB;}
    if(i==13){memory.available=391*MiB;memory.largest=183*MiB;}
    bool chunkedLocal=false,coldRecovery=false;unsigned samples=0;auto workerMemory=[&](Budget budget){++samples;assert(budget.valid);return memory;};
#include "chunked_entry.inl"
    if(i==0){assert(!chunkedLocal&&!samples&&admits(memory,entryBudget));}
    else if(i<9){assert(chunkedLocal&&samples>=1);assert(admits(memory,entryBudget)==(i==1||i==2||i==3||i==8));assert(entryBudget.available>=(coldRecovery?392:480)*MiB);}
    else{assert(chunkedLocal&&coldRecovery&&samples==2);assert(entryBudget.available==392*MiB);assert(admits(memory,entryBudget)==(i==9||i==12));}
    assert(entryBudget.largest>=33*MiB);
 }
 for(unsigned i=0;i<5;++i){
    bool localBuilt=i==4,localDeferred=false;Sample localSample;uint64_t localRequired=0,localLargest=0;std::string error;
    if(i==0)error="Incremental FGS3 model: Cannot allocate FGS2 geometry";
    if(i==1)error="Incremental FGS3 model: Geometry allocation deferred";
    if(i==2)error="Invalid model";
    if(i==3){error="Cannot allocate geometry BVH";localDeferred=true;}
    if(i==4)error="Cannot allocate geometry BVH";
    auto workerMemory=[&](Budget){return Sample{600*MiB,40*MiB,true};};auto geometryBudget=[](){return inBuildBudget();};
#include "chunked_failure.inl"
    noteLocalAllocationFailure();assert(localDeferred==(i<2||i==3));if(i<2)assert(localSample.valid&&localRequired&&localLargest);
 }
 std::puts("PASS chunked entry: normal 480 MiB, bounded cold/replacement 392 MiB, 256 MiB reserve, invalid and lifted samples refused");
}
