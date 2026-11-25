#include "grade_lut_worker.h"
#include <cassert>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
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

static std::vector<uint8_t> makePack(const std::vector<BakeKey>& keys){
    using namespace NorthlightGradeLut::Pack;
    struct Packed {BakeKey key;std::array<uint8_t,KeyBytes> canonical;std::vector<uint8_t> table;};
    std::vector<Packed> rows;rows.reserve(keys.size());
    for(const auto& key:keys){
        Packed row;row.key=key;row.canonical=encodeKey(key);row.table.resize(TableBytes);
        std::vector<uint16_t> words(TableBytes/2);constexpr unsigned n=CubeSize;
        NorthlightTone::bakeColourVolume(words.data(),n*4*sizeof(uint16_t),n*n*4*sizeof(uint16_t),key);
        for(size_t i=0;i<words.size();++i)write16(row.table.data()+i*2,words[i]);
        rows.push_back(std::move(row));
    }
    std::sort(rows.begin(),rows.end(),[](const Packed& a,const Packed& b){
        const auto ah=NorthlightTone::stableHash(a.key),bh=NorthlightTone::stableHash(b.key);
        return ah<bh||(ah==bh&&a.canonical<b.canonical);
    });
    std::vector<uint8_t> index(rows.size()*RecordBytes),tables(rows.size()*TableBytes);
    const uint64_t tablesOffset=HeaderBytes+index.size();
    for(size_t i=0;i<rows.size();++i){
        auto* record=index.data()+i*RecordBytes;write64(record,NorthlightTone::stableHash(rows[i].key));
        std::copy(rows[i].canonical.begin(),rows[i].canonical.end(),record+8);write16(record+RecordReservedAt,0);
        write64(record+RecordOffsetAt,tablesOffset+i*TableBytes);write32(record+RecordLengthAt,TableBytes);
        std::copy(rows[i].table.begin(),rows[i].table.end(),tables.begin()+i*TableBytes);
        const auto digest=Sha256::digest(rows[i].table.data(),rows[i].table.size());std::copy(digest.begin(),digest.end(),record+RecordDigestAt);
    }
    std::vector<uint8_t> result(HeaderBytes+index.size()+tables.size());
    static constexpr uint8_t magic[8]={'N','L','L','U','T','P','K',0};std::copy(std::begin(magic),std::end(magic),result.begin());
    write16(result.data()+8,FormatVersion);write16(result.data()+10,BakeMathSchemaVersion);write16(result.data()+12,CubeSize);
    write16(result.data()+14,VoxelRGBA16UnormLE);write16(result.data()+16,KeyBytes);write16(result.data()+18,RecordBytes);
    write32(result.data()+20,uint32_t(rows.size()));write64(result.data()+24,HeaderBytes);write64(result.data()+32,tablesOffset);
    const auto source=Sha256::digest(reinterpret_cast<const uint8_t*>("vibrant"),7);std::copy(source.begin(),source.end(),result.begin()+40);
    const auto indexDigest=Sha256::digest(index.data(),index.size());std::copy(indexDigest.begin(),indexDigest.end(),result.begin()+72);
    std::copy(index.begin(),index.end(),result.begin()+HeaderBytes);std::copy(tables.begin(),tables.end(),result.begin()+tablesOffset);return result;
}
static void savePack(const std::filesystem::path& path,const std::vector<uint8_t>& bytes){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));assert(out.good());
}

int main(int argc,char** argv){
    assert(argc==2);
    NorthlightGradeLut::Worker worker;
    Timeline identity{};
    const BakeKeys neutral=makeBakeKeys(identity,.5f);
    assert(!neutral[0].strength&&!neutral[1].strength&&!neutral[2].strength);
    const auto neutralSerial=worker.request(neutral);
    auto readyNeutral=worker.ready(neutral);
    assert(neutralSerial&&readyNeutral&&readyNeutral->bakedVolumes==0&&readyNeutral->cacheHits==0);
    assert(!worker.packAttempted());

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

    const std::filesystem::path packPath(argv[1]);
    savePack(packPath,makePack({keys[0],keys[2]}));
    NorthlightGradeLut::Worker packWorker;assert(packWorker.setPackPath(packPath));
    const auto packSerial=packWorker.request(keys);auto packed=waitFor(packWorker,keys);
    assert(packSerial&&packed&&packed->serial==packSerial&&packed->packStatus==NorthlightGradeLut::Pack::Status::Ready);
    assert(packed->packDecodedVolumes==2&&packed->bakedVolumes==1&&packed->cacheHits==0);
    assert(packed->volumes[0]->origin==NorthlightGradeLut::Volume::Origin::Pack&&packed->volumes[1]->origin==NorthlightGradeLut::Volume::Origin::Fresh&&packed->volumes[2]->origin==NorthlightGradeLut::Volume::Origin::Pack);
    Timeline edited=authored;edited[0].curves[1].levels[0]=.39f;edited[1].hueShift[1]=-9.f;
    const auto editedKeys=makeBakeKeys(edited,.7f);
    const auto editedSerial=packWorker.request(editedKeys);auto editedResult=waitFor(packWorker,editedKeys);
    assert(editedSerial&&editedResult&&editedResult->serial==editedSerial);
    assert(editedResult->bakedVolumes==2&&editedResult->cacheHits==1&&editedResult->packCacheHits==1);
    assert(editedResult->volumes[2]->key==keys[2]&&editedResult->volumes[2]->origin==NorthlightGradeLut::Volume::Origin::Pack);

    auto damaged=makePack({keys[0],keys[2]});
    const uint64_t tablesOffset=NorthlightGradeLut::Pack::read64(damaged.data()+32);
    damaged[size_t(tablesOffset)]^=1;
    const auto badPackPath=packPath.string()+".bad";savePack(badPackPath,damaged);
    NorthlightGradeLut::Worker badPackWorker;assert(badPackWorker.setPackPath(badPackPath));
    const auto badSerial=badPackWorker.request(keys);auto recovered=waitFor(badPackWorker,keys);
    assert(badSerial&&recovered&&recovered->serial==badSerial);
    assert(recovered->packStatus==NorthlightGradeLut::Pack::Status::Ready&&recovered->packRejectedTables==1);
    assert(recovered->packDecodedVolumes==1&&recovered->bakedVolumes==2&&recovered->volumes[0]->rgba.size()==NorthlightGradeLut::VolumeWords);
    std::filesystem::remove(packPath);std::filesystem::remove(badPackPath);
}
