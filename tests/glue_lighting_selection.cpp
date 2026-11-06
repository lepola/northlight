#include <windows.h>
#include <cassert>
#include <array>
#include <cstring>
#include <fstream>
#include <vector>
#include "glue_lighting_selection.h"

template<class T> T& at(void* p,unsigned offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
using Block=std::array<unsigned char,1024>;
alignas(4096) static unsigned char clientReserve[0x700000];
extern "C" __attribute__((naked)) void invoke(void*,void*,void*,void*){
    __asm__ volatile("push %ebp; mov %esp,%ebp; push %esi; mov 8(%ebp),%esi; mov 12(%ebp),%ecx; push 16(%ebp); call *20(%ebp); pop %esi; pop %ebp; ret");
}
int main(int argc,char** argv){
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<unsigned char> image((std::istreambuf_iterator<char>(file)),{});assert(!image.empty());
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+reinterpret_cast<IMAGE_DOS_HEADER*>(image.data())->e_lfanew);
    assert(nt->Signature==IMAGE_NT_SIGNATURE&&nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386);
    auto copy=[&](uintptr_t address,unsigned size){
        auto* section=IMAGE_FIRST_SECTION(nt);unsigned rva=unsigned(address-0x400000);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(rva>=section[i].VirtualAddress&&rva+size<=section[i].VirtualAddress+section[i].SizeOfRawData){
            const auto offset=section[i].PointerToRawData+rva-section[i].VirtualAddress;assert(offset+size<=image.size());
            std::memcpy(reinterpret_cast<void*>(address),image.data()+offset,size);return;
        }
        assert(false);
    };
    const auto begin=reinterpret_cast<uintptr_t>(clientReserve);
    assert(begin<=0x810000&&begin+sizeof clientReserve>=0xa50000);
    DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(0x810000),0x30000,PAGE_EXECUTE_READWRITE,&old));
    copy(0x831af0,NorthlightGlueLighting::SetupSize);copy(0x81e400,400);copy(0x8333d0,166);copy(0x834dc0,0x2e0);copy(0xa45634,12);
    assert(FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(0x810000),0x30000));
    const char* status=NorthlightGlueLighting::install(reinterpret_cast<HMODULE>(0x400000));
    assert(std::strcmp(status,"active")==0);
    assert(std::strcmp(NorthlightGlueLighting::install(reinterpret_cast<HMODULE>(0x400000)),"already-active")==0);
    auto* call=reinterpret_cast<unsigned char*>(0x831b58);
    int32_t relative;std::memcpy(&relative,call+1,4);auto* thunk=call+5+relative;
    assert(!std::memcmp(thunk,NorthlightGlueLighting::Selection,sizeof NorthlightGlueLighting::Selection));
    Block scene{},model{},shared{},data{},lighting{};
    std::array<unsigned char,3*212> own{};
    std::array<std::array<unsigned char,108>,6> equipment{};
    char name[]="UI_Scourge";
    at<void*>(model.data(),0x2c)=shared.data();at<void*>(shared.data(),0x150)=data.data();
    at<unsigned>(model.data(),0x10)=1; // Native M2 data is available only after this bit is set.
    at<unsigned>(data.data(),8)=sizeof name;at<void*>(data.data(),12)=name;at<unsigned>(data.data(),264)=3;
    at<void*>(model.data(),0x1d0)=own.data();
    std::vector<void*> lights;
    for(unsigned i=0;i<3;++i){auto* light=own.data()+i*212+104;
        at<unsigned>(light,8)=i==1?0:1;at<unsigned>(light,96)=1;
        at<float>(light,12)=i==2?-6.f:2.f;
        at<float>(light,60)=.7f;at<float>(light,64)=.25f;at<float>(light,68)=1.f;
        lights.push_back(light);
    }
    for(unsigned i=0;i<equipment.size();++i){auto* light=equipment[i].data();
        at<unsigned>(light,8)=1;at<unsigned>(light,96)=1;at<float>(light,12)=.1f+float(i)*.1f;at<float>(light,60)=1.f;lights.push_back(light);
    }
    auto link=[&]{at<void*>(scene.data(),32)=lights[0];for(unsigned i=0;i<lights.size();++i)at<void*>(lights[i],104)=i+1<lights.size()?lights[i+1]:nullptr;};
    link();
    auto select=[&](void* function){lighting={};invoke(model.data(),scene.data(),lighting.data(),function);};
    auto contains=[&](void* light){for(unsigned i=0;i<at<unsigned>(lighting.data(),164);++i)if(at<void*>(lighting.data(),132+i*4)==light)return true;return false;};
    const auto ownBefore=own;const auto equipmentBefore=equipment;
    select(reinterpret_cast<void*>(0x81e400));assert(at<unsigned>(lighting.data(),164)==4);
    assert(!contains(lights[0])&&!contains(lights[2]));const auto native=lighting;
    select(thunk);assert(at<unsigned>(lighting.data(),164)==4&&contains(lights[0])&&contains(lights[2]));
    assert(own==ownBefore&&equipment==equipmentBefore);
    // Ambient/directional output remains the real native selector's output.
    assert(!std::memcmp(lighting.data()+20,native.data()+20,112));
    std::memcpy(name,"Creature",9);select(thunk);assert(lighting==native);
    // Both real Dalaran dumps had m_fFlags=0x8340 (not loaded), so
    // shared->data was not a usable M2. Even a plausible in-range name
    // pointer must never be inspected before the native loaded-bit gate.
    std::memcpy(name,"UI_Test",8);
    const auto* sharedBefore=at<void*>(model.data(),0x2c);
    for(unsigned flags:{0u,0x8340u}){
        at<unsigned>(model.data(),0x10)=flags;
        at<void*>(model.data(),0x2c)=reinterpret_cast<void*>(0x02000a01);
        select(thunk);assert(lighting==native);
    }
    at<void*>(model.data(),0x2c)=const_cast<void*>(sharedBefore);
    at<unsigned>(model.data(),0x10)=1;
    std::memcpy(name,"Creature",9);
    for(unsigned length:{0u,3u}){at<unsigned>(data.data(),8)=length;select(thunk);assert(lighting==native);}
    at<unsigned>(data.data(),8)=sizeof name;
    // The real in-world crash had name length >=4 and name pointer -1.
    // All absent/reserved names must retain the native selector's output.
    for(uintptr_t invalid:{uintptr_t(0),uintptr_t(1),uintptr_t(0xffff),uintptr_t(0xffffffff),uintptr_t(0xffff0000)}){
        at<void*>(data.data(),12)=reinterpret_cast<void*>(invalid);
        select(thunk);assert(lighting==native);
    }
    at<void*>(data.data(),12)=name;
    std::memcpy(name,"ui_Test",8);select(thunk);assert(contains(lights[2]));
    at<unsigned>(lights[2],96)=0;select(thunk);assert(!contains(lights[2])&&contains(lights[0]));
    at<unsigned>(lights[2],96)=1;for(unsigned k=0;k<3;++k)at<float>(lights[2],60+k*4)=0;
    select(thunk);assert(!contains(lights[2]));
    at<unsigned>(data.data(),264)=5;select(reinterpret_cast<void*>(0x81e400));const auto overBudget=lighting;
    select(thunk);assert(lighting==overBudget);
    // Already-active detection still validates the surrounding native code.
    auto* setup=reinterpret_cast<unsigned char*>(0x831af0);setup[0]^=1;
    assert(std::strcmp(NorthlightGlueLighting::install(reinterpret_cast<HMODULE>(0x400000)),"setup-signature-mismatch")==0);
    setup[0]^=1;
    puts("GLUE_LIGHTING_PASS native-12340-selector=1 eviction-control=1 own-sources=2 slots=4 positions-unchanged=1 non-glue-unchanged=1");
}
