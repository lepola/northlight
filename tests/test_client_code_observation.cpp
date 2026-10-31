#include "client_code_observation.h"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>
namespace C=NorthlightStream::ClientCodeObservation;
namespace I=NorthlightStream::ClientImage;
namespace A=NorthlightStream::SnapshotArchive;
static_assert(sizeof(C::Capture)<=96u*1024u&&sizeof(C::Proof)<=96u*1024u,"Fixed ownership bound");
static constexpr std::size_t Nt=0x80,Opt=Nt+24,Sections=Opt+224;
static void put16(std::vector<unsigned char>& b,std::size_t at,unsigned n){b[at]=n&255;b[at+1]=(n>>8)&255;}
static std::vector<unsigned char> image(){
    std::vector<unsigned char> b(0x20600);
    put16(b,0,0x5a4d);A::put32(b.data()+60,Nt);A::put32(b.data()+Nt,0x4550);
    put16(b,Nt+4,0x14c);put16(b,Nt+6,2);put16(b,Nt+20,224);put16(b,Nt+22,0x102);
    put16(b,Opt,0x10b);A::put32(b.data()+Opt+28,I::View::Base);A::put32(b.data()+Opt+56,0x25000);A::put32(b.data()+Opt+60,0x200);
    A::put32(b.data()+Sections+8,0x20200);A::put32(b.data()+Sections+12,0x1000);
    A::put32(b.data()+Sections+16,0x20000);A::put32(b.data()+Sections+20,0x200);A::put32(b.data()+Sections+36,0x60000020);
    const auto second=Sections+40;A::put32(b.data()+second+8,0x1000);A::put32(b.data()+second+12,0x24000);
    A::put32(b.data()+second+16,0x400);A::put32(b.data()+second+20,0x20200);A::put32(b.data()+second+36,0xc0000040);
    for(std::size_t i=0x200;i<b.size();++i)b[i]=static_cast<unsigned char>((i*17+i/257)&255);
    return b;
}
static std::vector<unsigned char> encode(const C::Capture& capture){
    std::vector<unsigned char> bytes(C::MaxEncodedBytes);std::size_t written=0;C::Error error;
    assert(C::encode(capture,bytes.data(),bytes.size(),written,error)&&error==C::Error::None);
    bytes.resize(written);return bytes;
}
static void observe(C::Capture& capture,const I::View& view,std::uintptr_t address,std::size_t size){
    std::array<unsigned char,C::MaxSpanBytes> bytes{};assert(size<=bytes.size()&&view.readStatic(address,bytes.data(),size));
    const auto original=bytes;capture.record(view,address,bytes.data(),size,true);assert(bytes==original);
}
static void synthetic(){
    auto bytes=image();I::View view;I::Error imageError;assert(view.open(bytes.data(),bytes.size(),imageError));
    auto capture=std::make_unique<C::Capture>();auto proof=std::make_unique<C::Proof>();C::Error error;
    const auto begin=[&](std::uint64_t prefix=0){capture=std::make_unique<C::Capture>();return capture->begin(prefix);};
    assert(begin()&&!capture->complete());
    observe(*capture,view,0x401000,32);observe(*capture,view,0x401000,32);observe(*capture,view,0x401010,16);
    auto encoded=encode(*capture);assert(C::decode(encoded.data(),encoded.size(),view,*proof,error));
    assert(proof->valid()&&proof->spans()==2&&proof->payloadBytes()==48&&proof->observations()==3);
    assert(proof->covers(0x401000,32)&&proof->covers(0x401010,16));
    std::array<unsigned char,64> output{};assert(proof->read(0x401000,output.data(),32));
    assert(!std::memcmp(output.data(),bytes.data()+0x200,32));output.fill(0xa5);
    for(const auto& key:std::array<std::pair<std::uintptr_t,std::size_t>,5>{{{0x401000,16},{0x401001,31},{0x401000,48},{0x401020,16},{0x401000,0}}}){
        assert(!proof->covers(key.first,key.second)&&!proof->read(key.first,output.data(),key.second));for(auto x:output)assert(x==0xa5);
    }
    assert(!proof->read(0x401000,nullptr,32));
    std::size_t written=99;assert(!C::encode(*capture,nullptr,encoded.size(),written,error)&&written==0&&error==C::Error::Buffer);
    assert(!C::encode(*capture,output.data(),1,written,error)&&error==C::Error::Buffer);
    assert(!capture->begin(0)&&!capture->complete()); // Even a successful capture cannot restart its proof.
    // Nonstatic runtime and outside reads are not code correspondence evidence.
    assert(begin());capture->record(view,0x424000,nullptr,4,false);capture->record(view,0x10000,nullptr,4,false);
    assert(!capture->complete());observe(*capture,view,0x401000,16);assert(capture->complete());
    auto rejectCapture=[&](std::uintptr_t address,std::size_t size,bool ok){
        assert(begin());capture->record(view,address,output.data(),size,ok);assert(!capture->complete());
        assert(!capture->begin(0)&&!capture->complete());
        observe(*capture,view,0x401000,16);assert(!capture->complete());
        assert(!C::encode(*capture,output.data(),output.size(),written,error)&&error==C::Error::State&&written==0);
    };
    rejectCapture(0x401000,16,false);rejectCapture(0x401000,513,true);rejectCapture(0x421000,16,true); // uninitialized tail
    rejectCapture(0x400010,16,true);rejectCapture(UINTPTR_MAX,16,true);rejectCapture(0x401000,0,true);
    assert(!begin(1));assert(!capture->begin(0));observe(*capture,view,0x401000,16);assert(!capture->complete());
    // A patched duplicate must be compared again despite its existing key.
    assert(begin());observe(*capture,view,0x401000,32);
    assert(view.readStatic(0x401000,output.data(),32));output[3]^=1;const auto patched=output;
    capture->record(view,0x401000,output.data(),32,true);assert(output==patched);
    if(capture->complete()){std::fputs("LIVE_PATCH_ACCEPTED\n",stderr);std::abort();}
    assert(!capture->begin(0)&&!capture->complete());
    assert(begin());capture->record(view,0x401000,nullptr,16,true);assert(!capture->complete());
    // Exactly full fixed storage succeeds; a 129th distinct span fails closed.
    assert(begin());for(std::size_t i=0;i<C::MaxSpans;++i)observe(*capture,view,0x401000+i*C::MaxSpanBytes,C::MaxSpanBytes);
    auto full=encode(*capture);assert(full.size()==C::MaxEncodedBytes&&C::decode(full.data(),full.size(),view,*proof,error));
    assert(proof->spans()==128&&proof->payloadBytes()==65536&&proof->observations()==128);
    observe(*capture,view,0x411000,1);assert(!capture->complete());
    assert(begin());for(std::size_t i=0;i<C::MaxSpans;++i)observe(*capture,view,0x401000+i,1);
    observe(*capture,view,0x401080,1);assert(!capture->complete());
    auto repair=[](std::vector<unsigned char>& b){A::put32(b.data()+b.size()-4,A::checksum(b.data(),b.size()-4));};
    auto reject=[&](auto edit,C::Error expected,bool crc=true){auto bad=encoded;edit(bad);if(crc)repair(bad);
        assert(C::decode(encoded.data(),encoded.size(),view,*proof,error));assert(!C::decode(bad.data(),bad.size(),view,*proof,error)&&error==expected);
        output.fill(0xa5);assert(!proof->valid()&&!proof->spans()&&!proof->observations()&&!proof->read(0x401000,output.data(),32));for(auto x:output)assert(x==0xa5);
    };
    reject([](auto& b){b[0]^=1;},C::Error::Magic);
    reject([](auto& b){A::put32(b.data()+8,2);},C::Error::Version);
    reject([](auto& b){A::put32(b.data()+12,std::uint32_t(b.size()+1));},C::Error::Bounds);
    for(unsigned at:{16u,20u})reject([&](auto& b){A::put32(b.data()+at,0);},C::Error::Bounds);
    reject([](auto& b){A::put32(b.data()+16,129);},C::Error::Bounds);
    reject([](auto& b){A::put32(b.data()+20,65537);},C::Error::Bounds);
    for(unsigned at:{32u,40u,44u})reject([&](auto& b){A::put32(b.data()+at,1);},C::Error::Record);
    reject([](auto& b){A::put64(b.data()+24,1);},C::Error::Counts);
    reject([](auto& b){A::put64(b.data()+24,4);},C::Error::Counts);
    reject([](auto& b){b.back()^=1;},C::Error::Checksum,false);
    reject([](auto& b){A::put64(b.data()+48,UINT64_C(0x100000000));},C::Error::Address);
    reject([](auto& b){A::put32(b.data()+56,0);},C::Error::Record);
    reject([](auto& b){A::put32(b.data()+56,513);},C::Error::Record);
    reject([](auto& b){A::put32(b.data()+60,0);},C::Error::Counts);
    reject([](auto& b){A::put64(b.data()+48,0x424000);},C::Error::Image);
    reject([](auto& b){A::put64(b.data()+48,0x421000);},C::Error::Image);
    reject([](auto& b){A::put32(b.data()+56,16);A::put64(b.data()+64,0x401000);},C::Error::Duplicate);
    reject([](auto& b){b[80]^=1;},C::Error::Correspondence);
    for(std::size_t end=0;end<encoded.size();++end)assert(!C::decode(encoded.data(),end,view,*proof,error)&&!proof->valid());
    assert(!C::decode(nullptr,encoded.size(),view,*proof,error));
    auto changed=bytes;changed[0x200]^=1;I::View other;assert(other.open(changed.data(),changed.size(),imageError));
    assert(!C::decode(encoded.data(),encoded.size(),other,*proof,error)&&error==C::Error::Correspondence);
    std::puts("PASS bounded static correspondence, exact-key coverage, prefix/patch/failure refusal, independent decode and malformed controls");
}
static void client(const char* path){
    std::ifstream stream(path,std::ios::binary);assert(stream);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
    I::View view;I::Error imageError;assert(view.open(bytes.data(),bytes.size(),imageError));
    auto capture=std::make_unique<C::Capture>();auto proof=std::make_unique<C::Proof>();C::Error error;
    assert(capture->begin(0));for(std::uintptr_t address:{0x4f6650u,0x7bfd1cu,0xa1ea54u,0xa3e860u})observe(*capture,view,address,16);
    auto encoded=encode(*capture);assert(C::decode(encoded.data(),encoded.size(),view,*proof,error));
    std::array<unsigned char,16> raw{};assert(view.readStatic(0x4f6650,raw.data(),raw.size()));raw[0]^=1;
    capture=std::make_unique<C::Capture>();assert(capture->begin(0));capture->record(view,0x4f6650,raw.data(),raw.size(),true);assert(!capture->complete());
    // Read actual PE directory metadata; simulate a loader-resolved IAT value,
    // never map/execute the client or claim these are observed live game bytes.
    const auto nt=A::u32(bytes.data()+60),opt=nt+24;assert(opt+200<=bytes.size());
    const auto iat=A::u32(bytes.data()+opt+192),iatBytes=A::u32(bytes.data()+opt+196);assert(iat&&iatBytes>=16);
    assert(view.readStatic(I::View::Base+iat,raw.data(),raw.size()));raw[0]^=1;
    capture=std::make_unique<C::Capture>();assert(capture->begin(0));capture->record(view,I::View::Base+iat,raw.data(),raw.size(),true);assert(!capture->complete());
    std::puts("PASS actual read-only Wow.exe ranges and independently simulated patched-text/loaded-IAT mismatch; client not executed");
}
int main(int argc,char** argv){assert(argc==1||argc==2);synthetic();if(argc==2)client(argv[1]);}
