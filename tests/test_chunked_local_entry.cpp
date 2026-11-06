#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace NorthlightGeometryMemory;
static unsigned GetTickCount(){return 1000;}
int main(){
 for(unsigned i=0;i<17;++i){
    EntryLift chunkLift,coldLift;auto entryBudget=buildBudget(64*MiB);Sample memory{600*MiB,120*MiB,true};
    if(i==1)memory.largest=40*MiB;
    if(i==2)memory.available=500*MiB;
    if(i==3)memory.available=479*MiB;
    if(i==4)memory.largest=32*MiB;
    if(i==5)memory.valid=false;
    if(i==6){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,364*MiB,999,0);}
    if(i==7){memory.largest=40*MiB;chunkLift.refused(600*MiB,384*MiB,384*MiB,999,50*MiB);}
    if(i==8){memory.available=480*MiB;memory.largest=33*MiB;}
    if(i==9){memory.available=395*MiB;memory.largest=183*MiB;}
    if(i==10){memory.available=303*MiB;memory.largest=183*MiB;}
    if(i==11){memory.available=395*MiB;memory.largest=32*MiB;}
    if(i==12){memory.available=395*MiB;memory.largest=183*MiB;}
    if(i==13){memory.available=303*MiB;memory.largest=183*MiB;}
    if(i==14){memory.available=395*MiB;coldLift.refused(395*MiB,258*MiB,240*MiB,999);}
    if(i==15){memory.available=395*MiB;memory.largest=40*MiB;coldLift.refused(395*MiB,258*MiB,258*MiB,999,50*MiB);}
    // QA: a failed warm chunk build raised its floor to 556 MiB, while a
    // bounded candidate still fits. Its reserve and per-allocation gates remain.
    if(i==16){memory.available=466*MiB;memory.largest=113*MiB;chunkLift.refused(553*MiB,384*MiB,380*MiB,999);}
    bool chunkedLocal=false,coldRecovery=false;unsigned samples=0;auto workerMemory=[&](Budget budget){++samples;assert(budget.valid);return memory;};
#include "chunked_entry.inl"
    if(i==0){assert(!chunkedLocal&&!samples&&admits(memory,entryBudget));}
    else if(i<9){assert(chunkedLocal&&samples>=1);assert(admits(memory,entryBudget)==(i==1||i==2||i==3||i==6||i==7||i==8));assert(entryBudget.available>=(coldRecovery?304:480)*MiB);}
    else{assert(chunkedLocal&&coldRecovery&&samples==2);assert(entryBudget.available>=(304*MiB));assert(admits(memory,entryBudget)==(i==9||i==12||i==16));}
    assert(entryBudget.largest>=33*MiB);
 }
 for(unsigned mode=0;mode<3;++mode){
    EntryLift entryLift,chunkLift,coldLift;const bool coldRecovery=mode==2,chunkedLocal=mode!=0;
    const uint64_t entryAvailable=395*MiB,required=258*MiB,observed=240*MiB,requiredLargest=40*MiB;
#include "chunked_lift.inl"
    assert(entryLift.armed==(mode==0)&&chunkLift.armed==(mode==1)&&coldLift.armed==(mode==2));
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
 std::puts("PASS chunked entry: 480/304 MiB budgets, 256 MiB reserve, warm/cold refusal history isolated, cold aggregate/contiguous refusals retained");
}
