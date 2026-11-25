#include "grade_lut_pack.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace NorthlightGradeLut::Pack;

static std::vector<uint8_t> makePack(const NorthlightTone::BakeKey& key){
    std::vector<uint8_t> table(TableBytes);
    for(size_t i=3;i<TableBytes/2;i+=4)table[i*2]=table[i*2+1]=0xff;
    const auto canonical=encodeKey(key);
    const uint64_t tablesOffset=HeaderBytes+RecordBytes;
    std::vector<uint8_t> index(RecordBytes);
    write64(index.data(),NorthlightTone::stableHash(key));
    std::copy(canonical.begin(),canonical.end(),index.begin()+8);
    write16(index.data()+82,0);
    write64(index.data()+84,tablesOffset);
    write32(index.data()+92,TableBytes);
    const auto tableSha=Sha256::digest(table.data(),table.size());
    std::copy(tableSha.begin(),tableSha.end(),index.begin()+96);
    std::vector<uint8_t> pack(HeaderBytes+index.size()+table.size());
    static constexpr uint8_t magic[8]={'N','L','L','U','T','P','K',0};
    std::copy(std::begin(magic),std::end(magic),pack.begin());
    write16(pack.data()+8,FormatVersion);
    write16(pack.data()+10,NorthlightTone::BakeMathSchemaVersion);
    write16(pack.data()+12,NorthlightTone::CubeSize);
    write16(pack.data()+14,VoxelRGBA16UnormLE);
    write16(pack.data()+16,KeyBytes);
    write16(pack.data()+18,RecordBytes);
    write32(pack.data()+20,1);
    write64(pack.data()+24,HeaderBytes);
    write64(pack.data()+32,tablesOffset);
    const auto sourceSha=Sha256::digest(reinterpret_cast<const uint8_t*>("vibrant"),7);
    std::copy(sourceSha.begin(),sourceSha.end(),pack.begin()+40);
    const auto indexSha=Sha256::digest(index.data(),index.size());
    std::copy(indexSha.begin(),indexSha.end(),pack.begin()+72);
    std::copy(index.begin(),index.end(),pack.begin()+HeaderBytes);
    std::copy(table.begin(),table.end(),pack.begin()+tablesOffset);
    return pack;
}

static void save(const std::filesystem::path& path,const std::vector<uint8_t>& bytes){
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    assert(out.good());
}

int main(int argc,char** argv){
    assert(argc==2);
    const std::filesystem::path path(argv[1]);
    const auto abc=Sha256::digest(reinterpret_cast<const uint8_t*>("abc"),3);
    static constexpr uint8_t expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(std::equal(abc.begin(),abc.end(),std::begin(expected)));

    NorthlightTone::Grade grade;grade.curves[1].levels[0]=.31f;grade.hueShift[1]=-12.f;grade.saturation[4]=.2f;
    const auto key=NorthlightTone::makeBakeKey(grade,.7f);
    const auto bytes=encodeKey(key);
    const auto decoded=decodeKey(bytes.data());
    assert(decoded==key&&stableHash(decoded)==stableHash(key));

    auto packed=makePack(key);save(path,packed);
    Index index;assert(index.load(path)&&index.records.size()==1&&index.status==Status::Ready);
    auto* record=index.find(key);assert(record&&record->canonical==bytes);
    std::vector<uint16_t> words;std::string error;
    assert(index.decode(*record,words,error)&&words.size()==TableBytes/2&&error.empty());
    for(size_t i=3;i<words.size();i+=4)assert(words[i]==65535);
    assert(words[0]==0&&words[1]==0&&words[2]==0);

    auto damagedTable=packed;damagedTable.back()^=1;save(path,damagedTable);
    Index lazy;assert(lazy.load(path));
    auto* lazyRecord=lazy.find(key);assert(lazyRecord);
    assert(!lazy.decode(*lazyRecord,words,error)&&error=="table digest mismatch");
    assert(!lazy.find(key));

    auto damagedIndex=packed;damagedIndex[HeaderBytes+8]^=1;save(path,damagedIndex);
    Index invalid;assert(!invalid.load(path)&&invalid.status==Status::Invalid&&invalid.error=="index digest mismatch");
    std::filesystem::remove(path);
}
