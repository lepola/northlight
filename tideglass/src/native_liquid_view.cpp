#include "native_liquid_view.h"
#include "client_call_patch.h"
#include "config.h"
#include "engine.h"
#include "log.h"
#include <atomic>
#include <cstring>

namespace {
std::atomic<unsigned> policy{0};
DWORD owner=0;
bool pinned=false,frameActive=false,installed=false;
uint32_t skyLiquid=0;
water::client12340::NativeCalls<2> lighting({engine::kDayNightLiquidSite,engine::kDayNightFogLiquidSite},engine::kCameraLiquidTarget);
#if defined(WATER_COOPERATIVE_BUILD)
water::client12340::NativeCalls<1> world({engine::kWorldRenderSite},engine::kWorldRenderTarget);
water::client12340::NativeCalls<1> ready({engine::kCameraLiquidReadySite},engine::kCameraLiquidUpdateTarget);
#endif
struct SkyRead {uintptr_t site;bool owned=false;};
SkyRead skies[]={{engine::kSkyClearLiquidRead},{engine::kSkyModelLiquidRead}};
bool Read(uintptr_t p,void* dst,size_t n){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),dst,n,&got)&&got==n;}
bool Write(uintptr_t p,const void* src,size_t n){
    DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(p),n,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(reinterpret_cast<void*>(p),src,n);DWORD ignored=0;
    const bool protectedAgain=VirtualProtect(reinterpret_cast<void*>(p),n,old,&ignored)!=0;
    return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p),n)&&protectedAgain;
}
bool Selected(){
    const auto p=policy.load(std::memory_order_acquire);
    return ((p&1)&&engine::CameraInWater())||((p&2)&&engine::CameraInSlime());
}
bool Daylight(){return (policy.load(std::memory_order_acquire)&4)&&Selected();}
bool SkyMatches(const SkyRead& sky,uint32_t address){
    unsigned char bytes[6]={};uint32_t operand=0;
    if(!Read(sky.site,bytes,sizeof bytes))return false;
    std::memcpy(&operand,bytes+2,4);
    return bytes[0]==0x39&&bytes[1]==0x1d&&operand==address;
}
bool SetSky(SkyRead& sky,bool install){
    const auto ours=uint32_t(reinterpret_cast<uintptr_t>(&skyLiquid));
    if(!SkyMatches(sky,sky.owned?ours:engine::kCameraLiquidGlobal))return false;
    const auto target=install?ours:uint32_t(engine::kCameraLiquidGlobal);
    // Mark ownership before a potentially partial write so restoration remains possible.
    if(install)sky.owned=true;
    const bool ok=Write(sky.site+2,&target,4);
    if(ok)sky.owned=install;
    return ok;
}
}

extern "C" __attribute__((force_align_arg_pointer)) uint32_t __cdecl water_native_view_liquid(float* depth){
    const auto liquid=reinterpret_cast<uint32_t(__cdecl*)(float*)>(engine::kCameraLiquidTarget)(depth);
    skyLiquid=Daylight()?0:liquid;
    return skyLiquid;
}

#if defined(WATER_COOPERATIVE_BUILD)
// These observers execute around native game work, never on the replay thread.
// Preserve the complete incoming/returned CPU state, including floating-point controls.
#define VIEW_SAVE "pushfl\n\tpushal\n\tmovl %esp,%eax\n\tsubl $528,%esp\n\tandl $-16,%esp\n\tmovl %eax,512(%esp)\n\tfxsave (%esp)\n\t"
#define VIEW_RESTORE "fxrstor (%esp)\n\tmovl 512(%esp),%esp\n\tpopal\n\tpopfl\n\t"
extern "C" {
uintptr_t g_nativeViewWorld=engine::kWorldRenderTarget,g_nativeViewReady=engine::kCameraLiquidUpdateTarget;
__attribute__((force_align_arg_pointer)) void water_native_view_ready(){engine::CaptureLiquidViewIdentity();}
__attribute__((force_align_arg_pointer)) void water_native_view_begin(){water::BeginNativeLiquidView();}
__attribute__((force_align_arg_pointer)) void water_native_view_end(){water::EndNativeLiquidView();}
__attribute__((naked)) void NativeViewReadyThunk(){asm("calll *_g_nativeViewReady\n\t" VIEW_SAVE "calll _water_native_view_ready\n\t" VIEW_RESTORE "retl\n\t");}
__attribute__((naked)) void NativeViewWorldThunk(){asm(VIEW_SAVE "calll _water_native_view_begin\n\t" VIEW_RESTORE
    "calll *_g_nativeViewWorld\n\t" VIEW_SAVE "calll _water_native_view_end\n\t" VIEW_RESTORE "retl\n\t");}
}
#undef VIEW_SAVE
#undef VIEW_RESTORE
#endif

namespace water {
// The game's wake lies flat on the water plane behind whatever moves. Our water covers it on the water and draws its own, but
// the wake's sheet reaches past the water's border and showed there, on the ground, as pale streaks. It returns with our water off.
static void KeepNativeRipples(bool keep){
    static bool hidden=false;static int before=1;
    if(keep!=hidden)return;
    const int was=engine::SwapNativeRipples(keep?before:0);
    if(!keep)before=was;
    hidden=!keep;
}
void ConfigureNativeLiquidView(const Config& cfg,bool rendererAvailable){
    policy.store(cfg.enable&&rendererAvailable?(unsigned(cfg.underwater)|(unsigned(cfg.slime)<<1)|(unsigned(cfg.underwaterDaylight)<<2)|(unsigned(cfg.traceLiquids)<<3)|16u):0,std::memory_order_release);
}
void DisableNativeLiquidView(){policy.store(0,std::memory_order_release);}
void BeginNativeLiquidView(){
    frameActive=true;
    KeepNativeRipples((policy.load(std::memory_order_acquire)&16)==0);
    skyLiquid=Daylight()?0:engine::CameraLiquidType();
    const bool suppress=Selected();
    if(suppress)engine::PushStockFogOutOfRange();
    if(policy.load(std::memory_order_acquire)&8){
        static int previous=-1;const int state=int(engine::CameraLiquidType()!=0)|(int(suppress)<<1)|(int(Daylight())<<2);
        if(state!=previous){WATER_LOG_INFO("native liquid environment wet=%u fog_suppressed=%u daylight=%u",state&1,(state>>1)&1,(state>>2)&1);previous=state;}
    }
}
void EndNativeLiquidView(){engine::RestoreStockFog();frameActive=false;}
bool RestoreNativeLiquidView(){
    DisableNativeLiquidView();
    if(owner&&GetCurrentThreadId()!=owner)return false;
    if(frameActive)return false;
    bool ok=true;
#if defined(WATER_COOPERATIVE_BUILD)
    ok=world.Restore(Read,Write)&&ok;ok=ready.Restore(Read,Write)&&ok;
#endif
    ok=lighting.Restore(Read,Write)&&ok;
    for(auto& sky:skies)if(sky.owned)ok=SetSky(sky,false)&&ok;
    KeepNativeRipples(true);
    if(ok)installed=false;
    return ok;
}
bool InstallNativeLiquidView(){
    if(!engine::IsSupportedClient())return false;
    const DWORD thread=GetCurrentThreadId();if(owner&&owner!=thread)return false;owner=thread;
    if(installed)return true;
    if(!pinned){HMODULE module=nullptr;
        pinned=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCSTR>(reinterpret_cast<uintptr_t>(&water_native_view_liquid)),&module)!=0;
        if(!pinned)return false;
    }
    for(const auto& sky:skies)if(!SkyMatches(sky,sky.owned?uint32_t(reinterpret_cast<uintptr_t>(&skyLiquid)):uint32_t(engine::kCameraLiquidGlobal)))return false;
    bool ok=lighting.Install(Read,Write,reinterpret_cast<uintptr_t>(&water_native_view_liquid));
    for(auto& sky:skies)if(ok)ok=SetSky(sky,true);
#if defined(WATER_COOPERATIVE_BUILD)
    if(ok)ok=ready.Install(Read,Write,reinterpret_cast<uintptr_t>(&NativeViewReadyThunk));
    if(ok)ok=world.Install(Read,Write,reinterpret_cast<uintptr_t>(&NativeViewWorldThunk));
#endif
    if(!ok){RestoreNativeLiquidView();return false;}
    installed=true;WATER_LOG_INFO("shared native liquid environment hooks installed");return true;
}
}
