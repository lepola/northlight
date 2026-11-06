#include "snapshot_archive.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
using namespace NorthlightStream;
namespace A=NorthlightStream::SnapshotArchive;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"snapshot archive check failed line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static unsigned groups=0;
static std::unique_ptr<GameSnapshot> sample(){
    auto s=std::make_unique<GameSnapshot>();const std::uint32_t a=111,b=222;
    CHECK(s->record(0xb00010,&a,4,true));CHECK(s->record(0xb00020,nullptr,8,false));
    CHECK(s->record(0xb00010,nullptr,4,false));CHECK(s->record(0xb00010,&b,4,true));
    CHECK(s->record(0xb00030,&a,0,true));s->triggerKind=2;s->triggerDraw=9876;return s;
}
static std::vector<unsigned char> encoded(const GameSnapshot& s){
    std::vector<unsigned char> bytes(A::MaxBytes);std::size_t written=999;A::Error error;
    CHECK(A::encode(s,bytes.data(),bytes.size(),written,error)&&error==A::Error::None);bytes.resize(written);return bytes;
}
static void roundTrip(){
    const auto source=sample();const auto bytes=encoded(*source);auto target=std::make_unique<GameSnapshot>();A::Error error;
    CHECK(A::decode(bytes.data(),bytes.size(),*target,error));CHECK(target->triggerKind==2&&target->triggerDraw==9876);
    CHECK(target->keys()==3&&target->copies()==5&&target->bytes()==8);CHECK(encoded(*target)==bytes);
    A::Reader read(*target);std::uint32_t value=0;
    CHECK(read(0xb00010,&value,4)&&value==111);value=333;CHECK(!read(0xb00010,&value,4)&&value==333&&read.recordedFailures==1);
    CHECK(read(0xb00010,&value,4)&&value==222);CHECK(!read(0xb00010,&value,4)&&read.missing==1);
    CHECK(!read(0xb00020,&value,8)&&read.recordedFailures==2);CHECK(read(0xb00030,nullptr,0));
    CHECK(!read(0x4f6650,&value,4)&&read.missing==2);CHECK(!read(0xb00010,nullptr,4)&&read.missing==3);
    CHECK(source->copies()==5&&source->mode==GameSnapshot::Mode::Idle);++groups;
}
static void malformed(){
    const auto source=sample();const auto bytes=encoded(*source);auto target=std::make_unique<GameSnapshot>();A::Error error;
    for(std::size_t i=0;i<bytes.size();++i){CHECK(!A::decode(bytes.data(),i,*target,error));CHECK(!target->usable()&&target->keys()==0&&target->mode==GameSnapshot::Mode::Idle);}
    for(std::size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;CHECK(!A::decode(bad.data(),bad.size(),*target,error));CHECK(!target->usable());}
    auto bad=bytes;bad.push_back(0);CHECK(!A::decode(bad.data(),bad.size(),*target,error));
    const auto change=[&](std::size_t at,std::uint32_t value){auto data=bytes;A::put32(data.data()+at,value);A::put32(data.data()+data.size()-4,A::checksum(data.data(),data.size()-4));return data;};
    for(auto data:{change(16,4),change(20,0),change(20,64),change(24,257),change(28,65537),change(48,65537),change(52,2)})
        CHECK(!A::decode(data.data(),data.size(),*target,error)&&!target->usable());
    CHECK(!A::decode(nullptr,bytes.size(),*target,error));
    std::size_t written=999;std::array<unsigned char,8> shortBuffer{};
    CHECK(!A::encode(*source,shortBuffer.data(),shortBuffer.size(),written,error)&&written==0&&error==A::Error::Buffer);
    CHECK(!A::encode(*source,nullptr,A::MaxBytes,written,error)&&written==0);
    auto empty=std::make_unique<GameSnapshot>();CHECK(!A::encode(*empty,nullptr,0,written,error)&&error==A::Error::Unusable);++groups;
}
static void capacity(){
    auto source=std::make_unique<GameSnapshot>();std::vector<unsigned char> payload(GameSnapshot::ArenaBytes,0xA5);
    CHECK(source->record(0xb00000,payload.data(),payload.size(),true));auto bytes=encoded(*source);auto target=std::make_unique<GameSnapshot>();A::Error error;
    CHECK(A::decode(bytes.data(),bytes.size(),*target,error));std::vector<unsigned char> out(payload.size());A::Reader read(*target);
    CHECK(read(0xb00000,out.data(),out.size())&&out==payload);
    source->clear();unsigned char dummy=0;
    for(unsigned i=0;i<GameSnapshot::MaxCopies;++i)CHECK(source->record(0xb00000+i%GameSnapshot::MaxKeys,&dummy,0,true));
    bytes=encoded(*source);CHECK(A::decode(bytes.data(),bytes.size(),*target,error)&&target->keys()==64&&target->copies()==256);
    CHECK(!source->record(0xb00000,&dummy,0,true)&&source->overflowed());std::size_t n=1;
    CHECK(!A::encode(*source,bytes.data(),bytes.size(),n,error)&&n==0&&error==A::Error::Unusable);
    source->clear();for(unsigned i=0;i<GameSnapshot::MaxKeys;++i)CHECK(source->record(0xb00000+i,&dummy,1,true));
    CHECK(!source->record(0xc00000,&dummy,1,true));
    std::vector<unsigned char> huge(A::MaxBytes+1);CHECK(!A::decode(huge.data(),huge.size(),*target,error)&&!target->usable());++groups;
}
static void productionFallback(){
    auto source=sample();source->mode=GameSnapshot::Mode::Playing;unsigned live=0;std::uint32_t value=0;
    auto liveRead=[&](std::uintptr_t,void* output,std::size_t size){++live;CHECK(size==sizeof value);const std::uint32_t v=999;std::memcpy(output,&v,size);return true;};
    CHECK(source->serve(0xb00010,&value,4)==GameSnapshot::Serve::Ok&&value==111);
    CHECK(source->serve(0xb00010,&value,4)==GameSnapshot::Serve::Failed);
    CHECK(source->serve(0xb00010,&value,4)==GameSnapshot::Serve::Ok&&value==222);
    CHECK(source->serve(0xb00010,&value,4)==GameSnapshot::Serve::Ok&&value==222); // Existing repeated-last policy.
    CHECK(snapshotRead(*source,0xd00000,&value,4,liveRead,CodeRange{})&&value==999&&live==1);
    CHECK(snapshotRead(*source,0x4f6650,&value,4,liveRead,CodeRange{})&&live==2); // Existing code-range bypass.
    A::Reader strict(*source);CHECK(!strict(0xd00000,&value,4)&&!strict(0x4f6650,&value,4)&&live==2&&strict.missing==2);++groups;
}
int main(int argc,char** argv){
    CHECK(argc==2);roundTrip();malformed();capacity();productionFallback();const auto source=sample();const auto bytes=encoded(*source);
    std::ofstream file(argv[1],std::ios::binary);CHECK(file.good());file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.close();CHECK(file.good());
    std::printf("SNAPSHOT_ARCHIVE_PASS groups=%u dataOnly=1 nativeFallbackUnchanged=1 noGame=1 noGPU=1\n",groups);
}
