#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

// DXVK compatibility workarounds. Only the selected DXVK module's GetProcAddress
// import is redirected; the system Vulkan loader stays untouched.
// These two Vulkan 1.0 structures retain their ABI through Vulkan 1.4.
namespace NorthlightDxvkEngineName {
struct ApplicationInfo {
    uint32_t type;const void* next;const char* application;uint32_t applicationVersion;
    const char* engine;uint32_t engineVersion,apiVersion;
};
struct InstanceCreateInfo {
    uint32_t type;const void* next;uint32_t flags;const ApplicationInfo* application;
    uint32_t layerCount;const char* const* layers;uint32_t extensionCount;const char* const* extensions;
};
using VulkanFunction=void(WINAPI*)();
using GetInstanceProc=VulkanFunction(WINAPI*)(void*,const char*);
using CreateInstance=int32_t(WINAPI*)(const InstanceCreateInfo*,const void*,void**);
struct FeatureHeader { uint32_t type; FeatureHeader* next; };
struct PresentWaitFeature { uint32_t type; FeatureHeader* next; uint32_t supported; };
using GetFeatures=void(WINAPI*)(void*,FeatureHeader*);
using GetProperties=void(WINAPI*)(void*,void*);
using GetProc=FARPROC(WINAPI*)(HMODULE,LPCSTR);
using Log=void(*)(const char*,...);
inline GetProc systemGetProc=&::GetProcAddress;
inline std::atomic<GetInstanceProc> nextInstanceProc{nullptr};
inline std::atomic<CreateInstance> nextCreateInstance{nullptr};
inline std::atomic<GetFeatures> nextFeatures{nullptr},nextFeaturesKhr{nullptr};
inline std::atomic<GetProperties> nextProperties{nullptr};
inline bool engineNameEnabled=true,presentWaitEnabled=false;
inline Log log=nullptr;
inline constexpr char Engine[]="Northlight";
template<class To,class From> To pointer(From value){
    static_assert(sizeof(To)==sizeof(From));To result;std::memcpy(&result,&value,sizeof result);return result;
}
inline int32_t WINAPI createInstance(const InstanceCreateInfo* info,const void* allocator,void** instance){
    const auto next=nextCreateInstance.load();if(!next)return -3; // VK_ERROR_INITIALIZATION_FAILED
    const bool change=engineNameEnabled&&info&&info->application&&info->application->engine&&
        !std::strcmp(info->application->engine,"DXVK")&&((info->application->engineVersion>>22)&127u)==3;
    if(!change)return next(info,allocator,instance);
    ApplicationInfo app=*info->application;app.engine=Engine;
    InstanceCreateInfo copy=*info;copy.application=&app;
    const int32_t result=next(&copy,allocator,instance);
    if(log)log("DXVK3 engine-name instance original=DXVK replacement=%s result=%ld",Engine,long(result));
    return result;
}
inline void filterPresentWait(void* physical,FeatureHeader* features,GetFeatures next){
    if(!next)return;
    next(physical,features);
    const auto properties=nextProperties.load();
    if(!presentWaitEnabled||!properties||!physical||!features)return;
    // VkPhysicalDeviceProperties starts with apiVersion, driverVersion, vendorID.
    // The complete Vulkan 1.x structure is written by the loader.
    alignas(8) uint32_t props[1024]={};properties(physical,props);
    if(props[2]!=0x10de)return;
    for(auto* node=features->next;node;node=node->next){
        if(node->type==1000248000u||node->type==1000480001u){
            auto* wait=reinterpret_cast<PresentWaitFeature*>(node);
            if(wait->supported){wait->supported=0;if(log)log("DXVK3 NVIDIA present-wait feature disabled type=%u",node->type);}
        }
    }
}
inline void WINAPI features(void* physical,FeatureHeader* out){filterPresentWait(physical,out,nextFeatures.load());}
inline void WINAPI featuresKhr(void* physical,FeatureHeader* out){filterPresentWait(physical,out,nextFeaturesKhr.load());}
inline VulkanFunction WINAPI instanceProc(void* instance,const char* name){
    const auto next=nextInstanceProc.load();if(!next)return nullptr;
    const auto result=next(instance,name);
    if(result&&name&&!std::strcmp(name,"vkCreateInstance")){
        nextCreateInstance.store(pointer<CreateInstance>(result));return pointer<VulkanFunction>(&createInstance);
    }
    if(result&&name&&presentWaitEnabled&&(!std::strcmp(name,"vkGetPhysicalDeviceFeatures2")||!std::strcmp(name,"vkGetPhysicalDeviceFeatures2KHR"))){
        const auto props=next(instance,"vkGetPhysicalDeviceProperties");
        if(!props)return result;
        nextProperties.store(pointer<GetProperties>(props));
        if(!std::strcmp(name,"vkGetPhysicalDeviceFeatures2")){nextFeatures.store(pointer<GetFeatures>(result));return pointer<VulkanFunction>(&features);}
        nextFeaturesKhr.store(pointer<GetFeatures>(result));return pointer<VulkanFunction>(&featuresKhr);
    }
    if(result&&name&&!std::strcmp(name,"vkGetInstanceProcAddr"))return pointer<VulkanFunction>(&instanceProc);
    return result;
}
inline FARPROC WINAPI getProc(HMODULE module,LPCSTR name){
    const auto result=systemGetProc(module,name);
    // GetProcAddress also accepts ordinal values in its name parameter.
    if(result&&reinterpret_cast<uintptr_t>(name)>65535&&!std::strcmp(name,"vkGetInstanceProcAddr")){
        nextInstanceProc.store(pointer<GetInstanceProc>(result));return pointer<FARPROC>(&instanceProc);
    }
    return result;
}
inline const char* install(HMODULE module,Log callback,bool engine=true,bool presentWait=false){
    auto* base=reinterpret_cast<unsigned char*>(module);if(!base)return "missing-module";
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||dos->e_lfanew>1048576)return "invalid-dos-header";
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return "requires-pe32";
    const auto size=nt->OptionalHeader.SizeOfImage;
    auto valid=[&](uint32_t rva,size_t bytes){return rva&&rva<size&&bytes<=size-rva;};
    if(nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT)return "missing-import-directory";
    const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!valid(imports.VirtualAddress,imports.Size))return "invalid-import-directory";
    for(uint32_t offset=0;offset+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=imports.Size;offset+=sizeof(IMAGE_IMPORT_DESCRIPTOR)){
        const auto* item=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+imports.VirtualAddress+offset);
        if(!item->Name)break;
        if(!item->OriginalFirstThunk||!item->FirstThunk)continue;
        for(uint32_t i=0;;i+=sizeof(IMAGE_THUNK_DATA32)){
            if(!valid(item->OriginalFirstThunk+i,sizeof(IMAGE_THUNK_DATA32))||!valid(item->FirstThunk+i,sizeof(IMAGE_THUNK_DATA32)))break;
            const auto name=reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+item->OriginalFirstThunk+i)->u1.AddressOfData;
            if(!name)break;if(name&IMAGE_ORDINAL_FLAG32)continue;
            if(!valid(name,sizeof(WORD)+sizeof("GetProcAddress")))continue;
            if(std::memcmp(base+name+sizeof(WORD),"GetProcAddress",sizeof("GetProcAddress")))continue;
            auto* slot=reinterpret_cast<void**>(base+item->FirstThunk+i);
            if(*slot!=pointer<void*>(systemGetProc))return "getproc-already-intercepted";
            // WoW unloads its probe DLL before loading it again for the device.
            // DXVK retains this import and cached Vulkan callbacks, so their owner
            // must outlive those pointers. Pin only when enabling this workaround.
            HMODULE owner=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                                  pointer<LPCWSTR>(&getProc),&owner))return "callback-module-pin-failed";
            DWORD old=0;if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old))return "import-protection-failed";
            log=callback;engineNameEnabled=engine;presentWaitEnabled=presentWait;
            const auto previous=InterlockedCompareExchangePointer(slot,pointer<void*>(&getProc),pointer<void*>(systemGetProc));
            DWORD ignored=0;const bool restored=VirtualProtect(slot,sizeof(*slot),old,&ignored)!=FALSE;
            if(previous!=pointer<void*>(systemGetProc))return "import-changed-concurrently";
            return restored?"active-module-pinned":"active-import-protection-not-restored";
        }
    }
    return "getproc-import-not-found";
}
}
