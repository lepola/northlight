#undef NDEBUG
#include "dxvk_engine_name.h"
#include "dxvk_compatibility.h"
#include "settings_store.h"
#include <cassert>
#include <cstdio>

namespace E=NorthlightDxvkEngineName;
static unsigned creates=0,logs=0;
static E::ApplicationInfo observed{};
static const E::InstanceCreateInfo* expectedInfo=nullptr;
static int32_t WINAPI fakeCreate(const E::InstanceCreateInfo* info,const void* allocator,void** instance){
    ++creates;assert(allocator==reinterpret_cast<void*>(33));assert(instance);
    if(info){
        assert(info->type==expectedInfo->type&&info->next==expectedInfo->next&&info->flags==expectedInfo->flags);
        assert(info->layers==expectedInfo->layers&&info->layerCount==expectedInfo->layerCount);
        assert(info->extensions==expectedInfo->extensions&&info->extensionCount==expectedInfo->extensionCount);
        if(info->application)observed=*info->application;
    }
    *instance=reinterpret_cast<void*>(42);return 0;
}
static void WINAPI sentinel(){}
static void WINAPI fakeProperties(void* device,void* data){static_cast<uint32_t*>(data)[2]=device==reinterpret_cast<void*>(1)?0x10de:0x1002;}
static void WINAPI fakeFeatures(void*,E::FeatureHeader* root){
    for(auto* p=root->next;p;p=p->next)if(p->type==1000248000u||p->type==1000480001u)reinterpret_cast<E::PresentWaitFeature*>(p)->supported=1;
}
static E::VulkanFunction WINAPI fakeInstanceProc(void*,const char* name){
    if(!std::strcmp(name,"vkCreateInstance"))return E::pointer<E::VulkanFunction>(&fakeCreate);
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties"))return E::pointer<E::VulkanFunction>(&fakeProperties);
    if(!std::strcmp(name,"vkGetPhysicalDeviceFeatures2")||!std::strcmp(name,"vkGetPhysicalDeviceFeatures2KHR"))return E::pointer<E::VulkanFunction>(&fakeFeatures);
    if(!std::strcmp(name,"missing"))return nullptr;
    return &sentinel;
}
static FARPROC WINAPI fakeGetProc(HMODULE,LPCSTR name){
    if(reinterpret_cast<uintptr_t>(name)>65535&&!std::strcmp(name,"vkGetInstanceProcAddr"))return E::pointer<FARPROC>(&fakeInstanceProc);
    return E::pointer<FARPROC>(&sentinel);
}
static void record(const char*,...){++logs;}
int main(){
    static_assert(sizeof(E::ApplicationInfo)==28&&sizeof(E::InstanceCreateInfo)==32,"x86 Vulkan ABI");
    const auto realGetProc=E::systemGetProc;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(image);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=128;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+128);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;nt->OptionalHeader.SizeOfImage=4096;
    nt->OptionalHeader.NumberOfRvaAndSizes=16;nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]={512,40};
    auto* imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+512);imports->Name=2000;imports->OriginalFirstThunk=1024;imports->FirstThunk=1200;
    reinterpret_cast<IMAGE_THUNK_DATA32*>(image+1024)->u1.AddressOfData=1500;
    std::memcpy(image+1502,"GetProcAddress",sizeof("GetProcAddress"));
    auto** slot=reinterpret_cast<void**>(image+1200);*slot=E::pointer<void*>(realGetProc);
    const auto systemBefore=::GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcAddress");
    assert(!std::strcmp(E::install(reinterpret_cast<HMODULE>(image),&record),"active-module-pinned"));
    assert(*slot==E::pointer<void*>(&E::getProc));
    assert(::GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcAddress")==systemBefore);
    assert(!std::strcmp(E::install(reinterpret_cast<HMODULE>(image),&record),"getproc-already-intercepted"));
    VirtualFree(image,0,MEM_RELEASE);
    E::systemGetProc=&fakeGetProc;
    assert(E::getProc(nullptr,reinterpret_cast<LPCSTR>(7))==E::pointer<FARPROC>(&sentinel));
    assert(E::getProc(nullptr,"other")==E::pointer<FARPROC>(&sentinel));
    const auto getInstance=E::pointer<E::GetInstanceProc>(E::getProc(nullptr,"vkGetInstanceProcAddr"));
    assert(getInstance==&E::instanceProc&&getInstance(nullptr,"missing")==nullptr);
    assert(getInstance(nullptr,"other")==&sentinel);
    const auto create=E::pointer<E::CreateInstance>(getInstance(nullptr,"vkCreateInstance"));assert(create==&E::createInstance);
    E::ApplicationInfo app{0,reinterpret_cast<void*>(11),"oracle",9,"DXVK",(3u<<22)|(1u<<12)|1u,(1u<<22)|(4u<<12)};
    const char* names[]={"unchanged"};
    E::InstanceCreateInfo info{1,reinterpret_cast<void*>(22),4,&app,1,names,1,names};expectedInfo=&info;
    void* instance=nullptr;
    assert(create(&info,reinterpret_cast<void*>(33),&instance)==0&&instance==reinterpret_cast<void*>(42));
    assert(!std::strcmp(observed.engine,"Northlight")&&!std::strcmp(app.engine,"DXVK")&&info.application==&app);
    assert(observed.next==app.next&&observed.application==app.application&&observed.applicationVersion==app.applicationVersion);
    assert(observed.engineVersion==app.engineVersion&&observed.apiVersion==app.apiVersion&&logs==1);
    app.engine="other";create(&info,reinterpret_cast<void*>(33),&instance);assert(observed.engine==app.engine&&logs==1);
    app.engine="DXVK";app.engineVersion=2u<<22;create(&info,reinterpret_cast<void*>(33),&instance);assert(observed.engine==app.engine&&logs==1);
    info.application=nullptr;create(&info,reinterpret_cast<void*>(33),&instance);
    create(nullptr,reinterpret_cast<void*>(33),&instance);assert(creates==5&&logs==1);
    E::presentWaitEnabled=true;
    E::PresentWaitFeature wait2{1000480001u,nullptr,0},wait1{1000248000u,reinterpret_cast<E::FeatureHeader*>(&wait2),0};
    E::FeatureHeader unrelated{123,reinterpret_cast<E::FeatureHeader*>(&wait1)},root{1000059000u,&unrelated};
    auto query=E::pointer<E::GetFeatures>(getInstance(nullptr,"vkGetPhysicalDeviceFeatures2"));
    query(reinterpret_cast<void*>(1),&root);assert(!wait1.supported&&!wait2.supported&&unrelated.type==123);
    query(reinterpret_cast<void*>(2),&root);assert(wait1.supported&&wait2.supported);
    auto queryKhr=E::pointer<E::GetFeatures>(getInstance(nullptr,"vkGetPhysicalDeviceFeatures2KHR"));
    queryKhr(reinterpret_cast<void*>(1),&root);assert(!wait1.supported&&!wait2.supported);
    E::presentWaitEnabled=false;query(reinterpret_cast<void*>(1),&root);assert(wait1.supported&&wait2.supported);
    E::engineNameEnabled=false;app.engineVersion=3u<<22;info.application=&app;create(&info,reinterpret_cast<void*>(33),&instance);assert(observed.engine==app.engine);
    E::systemGetProc=realGetProc;
    const std::string existing="dxvk.allowFse = True; d3d9.maxFrameRate = 120;";
    assert(NorthlightDxvkCompatibility::config(existing).find(existing)!=std::string::npos);
    const auto safe=NorthlightDxvkCompatibility::config(existing,true);
    assert(safe.rfind("dxvk.allowFse = False")>safe.rfind("dxvk.allowFse = True"));
    NorthlightSettings::Document ini("[Renderer]\nDxvkEngineNameWorkaround=1\nHDRPaperWhite=203\n");
    const std::string white="250";ini.set("Renderer","HDRPaperWhite",&white);
    assert(ini.text().find("DxvkEngineNameWorkaround=1")!=std::string::npos);
    assert(ini.text().find("HDRPaperWhite=250")!=std::string::npos);
    std::puts("PASS DXVK3 engine-name: import scope, Vulkan ABI/forwarding, DXVK2 exclusion, FSE precedence, INI preservation");
}
