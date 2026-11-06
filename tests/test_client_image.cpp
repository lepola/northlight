#include "client_image.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace NorthlightStream::ClientImage;
static void put16(std::vector<unsigned char>& b,std::size_t at,unsigned value){b[at]=value&255;b[at+1]=(value>>8)&255;}
static void put32(std::vector<unsigned char>& b,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)b[at+i]=(value>>(8*i))&255;}
static constexpr std::size_t Nt=0x80,Opt=Nt+24,Table=Opt+224;
static std::vector<unsigned char> fixture(){
    std::vector<unsigned char> b(0x800);
    put16(b,0,0x5a4d);put32(b,60,Nt);put32(b,Nt,0x4550);put16(b,Nt+4,0x14c);put16(b,Nt+6,3);put16(b,Nt+20,224);put16(b,Nt+22,0x102);
    put16(b,Opt,0x10b);put32(b,Opt+28,View::Base);put32(b,Opt+56,0x5000);put32(b,Opt+60,0x200);
    for(unsigned i=0;i<3;++i){const auto at=Table+i*40;
        put32(b,at+8,i==0?0x180:0x300);put32(b,at+12,(i+1)*0x1000);put32(b,at+16,0x200);put32(b,at+20,0x200+i*0x200);put32(b,at+36,i==2?0xc0000040u:i==0?0x60000020u:0x40000040u);
        for(unsigned byte=0;byte<0x200;++byte)b[0x200+i*0x200+byte]=static_cast<unsigned char>(byte*(7+6*i)+3+i);
    }
    return b;
}
static void synthetic(){
    auto b=fixture();View view;Error error;
    assert(view.open(b.data(),b.size(),error)&&error==Error::None&&view.valid()&&view.imageBase()==View::Base&&view.imageBytes()==0x5000);
    std::array<unsigned char,32> out{};assert(view.readStatic(0x401010,out.data(),out.size()));for(unsigned i=0;i<out.size();++i)assert(out[i]==static_cast<unsigned char>((i+16)*7+3));
    assert(view.readStatic(0x402010,out.data(),out.size()));for(unsigned i=0;i<out.size();++i)assert(out[i]==static_cast<unsigned char>((i+16)*13+4));
    assert(view.region(0x401000,1)==Region::Static&&view.region(0x402200,1)==Region::Static&&view.region(0x403200,1)==Region::Runtime);
    assert(view.region(0x800000,1)==Region::Outside&&view.region(0x3ffff0,16)==Region::Outside);
    assert(view.region(0x3ffff0,17)==Region::Missing&&view.region(0x400010,1)==Region::Missing&&view.region(0x401180,1)==Region::Missing&&view.region(0x40117f,2)==Region::Missing);
    assert(view.region(UINT32_MAX,2)==Region::Missing&&view.region(0x401000,0)==Region::Missing);
    out.fill(0xa5);
    for(std::uintptr_t address:{0x402200u,0x403010u,0x403200u,0x800000u,0x400010u,0x401180u}){
        assert(!view.readStatic(address,out.data(),out.size()));for(auto byte:out)assert(byte==0xa5);
    }
    assert(!view.readStatic(0x401000,nullptr,1)&&!view.readStatic(0x401000,out.data(),0));
    // A writable executable section is runtime state, never a file-code proof.
    auto writable=b;put32(writable,Table+36,0xe0000020u);assert(view.open(writable.data(),writable.size(),error)&&view.region(0x401000,1)==Region::Runtime&&!view.readStatic(0x401000,out.data(),1));
    auto noRead=b;put32(noRead,Table+36,0x20000020u);assert(view.open(noRead.data(),noRead.size(),error)&&view.region(0x401000,1)==Region::Missing);
    for(std::size_t end=0;end<b.size();++end){assert(!view.open(b.data(),end,error)&&!view.valid()&&!view.imageBase()&&!view.imageBytes());}
    auto reject=[&](auto mutate,Error expected){auto bad=b;mutate(bad);assert(view.open(b.data(),b.size(),error));assert(!view.open(bad.data(),bad.size(),error)&&error==expected&&!view.valid()&&!view.imageBase()&&!view.imageBytes());out.fill(0xa5);assert(!view.readStatic(0x401000,out.data(),1)&&out[0]==0xa5&&view.region(0x401000,1)==Region::Missing);};
    reject([](auto& x){x[0]=0;},Error::Dos);
    reject([](auto& x){put32(x,60,UINT32_MAX);},Error::Pe);
    reject([](auto& x){put32(x,Nt,0);},Error::Pe);
    reject([](auto& x){put16(x,Nt+4,0x8664);},Error::Machine);
    reject([](auto& x){put16(x,Nt+22,0x2102);},Error::Machine);
    reject([](auto& x){put16(x,Nt+20,95);},Error::Optional);
    reject([](auto& x){put16(x,Opt,0x20b);},Error::Optional);
    reject([](auto& x){put32(x,Opt+28,0x500000);},Error::Base);
    reject([](auto& x){put16(x,Nt+6,0);},Error::Sections);
    reject([](auto& x){put16(x,Nt+6,97);},Error::Sections);
    reject([](auto& x){put32(x,Opt+56,0);},Error::Bounds);
    reject([](auto& x){put32(x,Opt+56,UINT32_MAX);},Error::Bounds);
    reject([](auto& x){put32(x,Opt+60,Table+119);},Error::Bounds);
    reject([](auto& x){put32(x,Table+8,0);},Error::Sections);
    reject([](auto& x){put32(x,Table+12,0x100);},Error::Sections);
    reject([](auto& x){put32(x,Table+12,UINT32_MAX-16);},Error::Sections);
    reject([](auto& x){put32(x,Table+20,0x100);},Error::Sections);
    reject([](auto& x){put32(x,Table+16,UINT32_MAX);},Error::Sections);
    reject([](auto& x){put32(x,Table+40+12,0x1100);},Error::Overlap);
    reject([](auto& x){put32(x,Table+40+20,0x300);},Error::Overlap);
    auto uninitialized=b;put32(uninitialized,Table+40+16,0);put32(uninitialized,Table+40+20,0);
    assert(view.open(uninitialized.data(),uninitialized.size(),error)&&view.region(0x402010,1)==Region::Static&&!view.readStatic(0x402010,out.data(),1));
    view.clear();assert(!view.valid()&&view.region(0x401000,1)==Region::Missing);
    std::puts("PASS fixed-base PE32 raw-file static translation, readonly/runtime split, exact backing, failure publication, overlap/truncation/overflow/uninitialized guards");
}
static void client(const char* path){
    std::ifstream stream(path,std::ios::binary);assert(stream);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());View view;Error error;
    assert(view.open(bytes.data(),bytes.size(),error)&&view.imageBase()==View::Base);
    // Addresses/ranges only; expected client bytes/hashes remain in the existing
    // supported-client/camera/sky/weather proof providers, not duplicated here.
    for(std::uintptr_t address:{0x4f6650u,0x7bfd1cu,0x4f5960u,0xa1ea54u,0xa2e7b8u,0xa3e860u}){
        std::array<unsigned char,16> output{};assert(view.region(address,output.size())==Region::Static&&view.readStatic(address,output.data(),output.size()));
    }
    for(std::uintptr_t address:{0xb7436cu,0xc5df88u,0xcd7544u,0xd38b00u}){
        std::array<unsigned char,16> output{};output.fill(0xa5);assert(view.region(address,output.size())==Region::Runtime&&!view.readStatic(address,output.data(),output.size())&&output[0]==0xa5);
    }
    std::puts("PASS optional local client file: fixed PE32 proof ranges accessible, writable game globals denied, no image execution or embedded client bytes");
}
int main(int argc,char** argv){assert(argc==1||argc==2);synthetic();if(argc==2)client(argv[1]);}
