#include "memory_admission_probe.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace NorthlightGeometryMemory;
using namespace NorthlightMemoryAdmission;
constexpr std::uint64_t AddressMaximum=0xffffffffull;
struct Space {
    // Each cell models one page. Query deliberately returns the suffix from
    // the requested page, as documented for Win32 VirtualQuery.
    std::vector<unsigned char> free=std::vector<unsigned char>(4096,0);
    unsigned statusCalls=0,queries=0;
    bool statusOK=true,queryOK=true;
    std::uint64_t failAt=std::numeric_limits<std::uint64_t>::max();
    void set(unsigned first,unsigned end,bool available){assert(first<=end&&end<=free.size());std::fill(free.begin()+first,free.begin()+end,unsigned(available));}
    bool status(std::uint64_t& available){++statusCalls;if(!statusOK)return false;available=0;for(auto cell:free)if(cell)available+=MiB;return true;}
    bool query(std::uint64_t address,Region& out){
        ++queries;if(!queryOK||address>=free.size()*MiB||address==failAt)return false;
        const auto cell=unsigned(address/MiB);unsigned end=cell+1;while(end<free.size()&&free[end]==free[cell])++end;
        out={cell*MiB,(end-cell)*MiB,free[cell]!=0};return true;
    }
    Observation sample(Probe& probe,Budget budget){return probe.sample(budget,[&](auto& n){return status(n);},[&](auto address,auto& region){return query(address,region);},AddressMaximum);}
    Sample original(){
        Sample sample;if(!status(sample.available))return sample;
        std::uint64_t address=0;Region region;
        while(query(address,region)){
            if(region.free)sample.largest=std::max(sample.largest,region.size);
            const auto next=region.base+region.size;if(next<=address||next>AddressMaximum)break;address=next;
        }
        sample.valid=sample.largest>0;return sample;
    }
};
void parity(Space& space,Probe& probe,Budget budget){
    const auto before=space.statusCalls;const auto actual=space.sample(probe,budget);
    assert(space.statusCalls==before+1); // every fast or slow request is fresh
    const auto expected=space.original();assert(admits(actual.memory,budget)==admits(expected,budget));
    assert(actual.memory.available==expected.available&&actual.memory.largest<=expected.largest);
    if(actual.witnessOnly)assert(actual.memory.valid&&!actual.fullScan&&actual.regionQueries>=1&&actual.regionQueries<=2&&actual.memory.largest>=budget.largest);
    if(actual.fullScan)assert(!actual.witnessOnly&&actual.memory.largest==expected.largest);
}
std::uint64_t randomState=0x4096a64fffull;
unsigned randomWord(){randomState=randomState*6364136223846793005ull+1;return unsigned(randomState>>32);}
int main(){
    Probe probe;Space space;space.set(32,2048,true);const auto budget=dynamicBudget(64*MiB);
    auto cold=space.sample(probe,budget);assert(cold.fullScan&&!cold.witnessOnly&&cold.regionQueries==3&&cold.memory.largest==2016*MiB&&admits(cold.memory,budget));
    auto warm=space.sample(probe,budget);assert(warm.witnessOnly&&!warm.fullScan&&warm.regionQueries==1&&warm.memory.largest==cold.memory.largest);
    // Occupying the old base cannot preserve stale size. A fresh tail-window
    // query proves only 144 MiB while the actual process maximum is much larger.
    space.set(32,36,false);auto tail=space.sample(probe,budget);
    assert(tail.witnessOnly&&tail.regionQueries==2&&tail.memory.largest==budget.largest&&admits(tail.memory,budget));
    assert(tail.memory.largest<space.original().largest);
    // A later larger transaction asks farther back from the remembered end.
    // The previous lower bound cannot authorize it; a fresh region must do so.
    const auto larger=dynamicBudget(128*MiB);assert(!admits(tail.memory,larger));
    auto escalation=space.sample(probe,larger);assert(escalation.witnessOnly&&escalation.regionQueries==2&&admits(escalation.memory,larger));
    // Going beyond the original region is harmless: the fresh query fails
    // closed and the complete walk still finds the actual process maximum.
    auto excessive=space.sample(probe,Budget{MiB,3000*MiB,true});
    assert(excessive.fullScan&&!admits(excessive.memory,Budget{MiB,3000*MiB,true}));
    // A tiny interior split can halve the largest free range. Subtracting its
    // bytes from a frame-old size would be unsafe; fresh queries reject it.
    space.set(1023,1024,false);Budget splitBudget{768*MiB,1500*MiB,true};
    auto split=space.sample(probe,splitBudget);assert(split.fullScan&&!admits(split.memory,splitBudget)&&split.memory.largest==1024*MiB);
    space.set(1023,1024,true);parity(space,probe,splitBudget); // merge
    space.set(36,2048,false);space.set(2100,3500,true);parity(space,probe,dynamicBudget(256*MiB)); // stale addresses, new region
    space.set(2100,3500,false);parity(space,probe,budget); // no free block
    space.set(4000,4096,true);auto last=space.sample(probe,dynamicBudget(1*MiB));assert(last.memory.largest==96*MiB&&last.fullScan);
    auto lastWarm=space.sample(probe,dynamicBudget(1*MiB));assert(lastWarm.witnessOnly); // region ends exactly at 2^32
    probe.invalidate();auto forgotten=space.sample(probe,dynamicBudget(1*MiB));assert(forgotten.fullScan);
    // Fresh aggregate reserve can decline while the witnessed hole remains.
    space.set(0,4096,false);space.set(32,2000,true);space.sample(probe,budget);
    const auto belowReserve=unsigned(budget.available/MiB)-1;
    space.set(32+belowReserve,2000,false);parity(space,probe,budget);assert(!admits(space.original(),budget));
    std::puts("PASS fresh address witness: unchanged state, base allocation, interior split, merge, relocation, 32-bit final byte, aggregate reserve and lower-bound escalation");

    const auto beforeQueries=space.queries;space.statusOK=false;auto failed=space.sample(probe,budget);
    assert(!failed.memory.valid&&!failed.fullScan&&!failed.witnessOnly&&failed.regionQueries==0&&space.queries==beforeQueries);
    space.statusOK=true;space.queryOK=false;failed=space.sample(probe,budget);assert(failed.fullScan&&!failed.memory.valid&&failed.regionQueries==1);
    space.queryOK=true;space.set(32,2000,true);auto recovered=space.sample(probe,budget);assert(recovered.fullScan&&admits(recovered.memory,budget));
    // An individual witness query failure falls back to the same observed full
    // sweep. It never authorizes from cached bytes. A sweep that stops after a
    // real free region preserves the original walker's valid partial result.
    space.failAt=32*MiB;failed=space.sample(probe,Budget{512*MiB,2000*MiB,true});assert(failed.fullScan&&!failed.memory.valid);
    space.failAt=2000*MiB;probe.invalidate();parity(space,probe,budget);
    space.failAt=std::numeric_limits<std::uint64_t>::max();
    std::puts("PASS API failures: GlobalMemoryStatusEx failure skips queries; failed witness never uses old size; full-query failure and recovery retain original observed-scan behavior");

    // Malformed fresh answers cannot prove anything. Exhaust the old witness
    // and full-scan paths with ranges missing the queried address or wrapping.
    for(unsigned fault=0;fault<5;++fault){
        Probe broken;unsigned calls=0;
        auto observation=broken.sample(budget,[](auto& available){available=4*1024*MiB;return true;},[&](auto address,Region& r){
            ++calls;switch(fault){
                case 0:r={address,0,true};break;
                case 1:r={address+1,64*MiB,true};break;
                case 2:r={0,0x100000001ull,true};break;
                case 3:r={AddressMaximum,2,true};break;
                case 4:r={std::numeric_limits<std::uint64_t>::max(),std::numeric_limits<std::uint64_t>::max(),true};break;
            }return true;
        },AddressMaximum);
        assert(observation.fullScan&&!observation.memory.valid&&calls==1&&broken.stats().malformedRegions==1);
    }
    Probe upper;unsigned queries=0;
    auto upperSample=upper.sample(Budget{1,1,true},[](auto& n){n=10;return true;},[&](auto address,Region& r){
        ++queries;if(!address)r={0,std::numeric_limits<std::uint64_t>::max(),false};
        else r={address,1,true};return true;
    },std::numeric_limits<std::uint64_t>::max());
    assert(upperSample.memory.valid&&upperSample.memory.largest==1&&queries==2);
    std::puts("PASS fail-closed malformed ranges and 64-bit arithmetic edges");

    Probe randomProbe;Space randomSpace;randomSpace.set(16,4090,true);
    constexpr unsigned Iterations=10000;
    for(unsigned iteration=0;iteration<Iterations;++iteration){
        unsigned first=randomWord()%4096,end=std::min(4096u,first+(randomWord()%400));
        randomSpace.set(first,end,(randomWord()&1)!=0);
        Budget b;switch(iteration%4){
            case 0:b=dynamicBudget((randomWord()%256)*MiB);break;
            case 1:b=buildBudget((randomWord()%256)*MiB);break;
            case 2:b=uploadBudget((randomWord()%32)*MiB,(randomWord()%32)*MiB,64*MiB,(randomWord()%64)*MiB);break;
            default:b={ProcessReserve+64*MiB+(randomWord()%256)*MiB,(16+randomWord()%192)*MiB,true};break;
        }
        parity(randomSpace,randomProbe,b);
    }
    assert(randomProbe.stats().requests==Iterations&&randomProbe.stats().witnessHits>0&&randomProbe.stats().fullScans>0);
    std::printf("PASS %u differential allocation/fragmentation mutations: full-sweep admission parity, fresh aggregate status every request, witnessHits=%llu fullScans=%llu\n",Iterations,
        (unsigned long long)randomProbe.stats().witnessHits,(unsigned long long)randomProbe.stats().fullScans);
}
