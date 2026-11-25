#include "grade_lut_worker.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <thread>

using namespace NorthlightTone;

static std::shared_ptr<const NorthlightGradeLut::Result> waitFor(
    NorthlightGradeLut::Worker& worker,const BakeKeys& keys){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<end){
        if(auto result=worker.ready(keys))return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}

int main(){
    NorthlightGradeLut::Worker worker;
    Timeline identity{};
    const BakeKeys neutral=makeBakeKeys(identity,.5f);
    assert(!neutral[0].strength&&!neutral[1].strength&&!neutral[2].strength);
    const auto neutralSerial=worker.request(neutral);
    auto readyNeutral=worker.ready(neutral);
    assert(neutralSerial&&readyNeutral&&readyNeutral->bakedVolumes==0&&readyNeutral->cacheHits==0);

    Timeline authored{};
    authored[0].curves[1].levels[0]=.31f;
    authored[1].vibrance=.22f;
    authored[1].hueShift[1]=12.f;
    authored[2].curves[0].levels[2]=.82f;
    authored[2].saturation[4]=-.18f;
    const BakeKeys keys=makeBakeKeys(authored,.7f);
    const auto lateDusk=timeWeights(.93f,true);
    const auto lateDuskMapped=transform(keys,lateDusk,{.62f,.28f,.44f});
    assert(lateDusk.dusk>0&&lateDusk.night>0&&lateDusk.day==0);
    assert(std::fabs(lateDuskMapped[0]-.62f)>1e-4f||std::fabs(lateDuskMapped[1]-.28f)>1e-4f||std::fabs(lateDuskMapped[2]-.44f)>1e-4f);
    Timeline duskToNight{};duskToNight[2]=authored[2];
    const auto duskNightKeys=makeBakeKeys(duskToNight,.7f);
    const auto neutralCrossfade=transform(duskNightKeys,lateDusk,{.62f,.28f,.44f});
    const auto nightOnly=transform(duskNightKeys[2],{.62f,.28f,.44f});
    const float base[3]={.62f,.28f,.44f};
    for(unsigned c=0;c<3;++c){
        assert(std::fabs(neutralCrossfade[c]-(base[c]+(nightOnly[c]-base[c])*lateDusk.night))<1e-6f);
    }
    const auto serial=worker.request(keys);
    auto result=waitFor(worker,keys);
    assert(serial&&result&&result->serial==serial);
    assert(result->bakedVolumes==3&&result->cacheHits==0);
    assert(worker.cachedVolumes()==3);
    constexpr unsigned n=CubeSize;
    const unsigned r=10,g=17,b=21;
    for(unsigned t=0;t<StopCount;++t){
        assert(result->volumes[t]);
        const auto expected=transform(keys[t],{r/float(n-1),g/float(n-1),b/float(n-1)});
        const size_t offset=((size_t(b)*n+g)*n+r)*4;
        for(unsigned c=0;c<3;++c){
            const float actual=result->volumes[t]->rgba[offset+c]/65535.f;
            assert(std::fabs(actual-expected[c])<2.f/65535.f);
        }
        assert(result->volumes[t]->rgba[offset+3]==65535);
    }

    const auto reusedSerial=worker.request(keys);
    auto reused=worker.ready(keys);
    assert(reusedSerial==serial&&reused==result&&reused->bakedVolumes==3);

    Timeline moved=authored;
    moved[0].curves[1].levels[0]=.39f;
    moved[1].hueShift[1]=-9.f;
    const BakeKeys movedKeys=makeBakeKeys(moved,.7f);
    const BakeKeys superseded=makeBakeKeys(identity,.8f);
    worker.request(superseded);
    const auto movedSerial=worker.request(movedKeys);
    auto movedResult=waitFor(worker,movedKeys);
    assert(movedSerial&&movedResult&&movedResult->serial==movedSerial);
    assert(movedResult->keys==movedKeys);
    assert(worker.cachedVolumes()<=NorthlightGradeLut::CpuCacheVolumes);
}
