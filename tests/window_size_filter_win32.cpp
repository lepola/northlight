// Real Win32 subclass chains and private D3D target; never loads the game.
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <thread>
#include <vector>
#include "settings_overlay.h"

static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static std::atomic<HWND> foregroundFact{nullptr};
static unsigned* currentCase=nullptr;
extern "C" HWND NorthlightWindowSizeTestForeground(){return foregroundFact.load(std::memory_order_acquire);}
static bool closingArmed=false;
static IDirect3DDevice9* closingOwner=nullptr;
static IDirect3DDevice9* closingReplacement=nullptr;
static HWND closingWindow=nullptr;
static unsigned closingCalls=0;
extern "C" void NorthlightWindowSizeTestClosing(IDirect3DDevice9* owner,HWND window){
    if(!closingArmed)return;closingArmed=false;++closingCalls;
    assert(owner==closingOwner&&window==closingWindow);
    NorthlightWindowHook::detach(owner); // A duplicate release cannot claim the closing owner.
    assert(!NorthlightWindowHook::attach(closingReplacement,window));
}
struct Game {
    HWND window=nullptr;IDirect3DDevice9* device=nullptr;IDirect3DSurface9* shadow=nullptr;
    unsigned sizes=0,resets=0,other=0,lastOther=0,chainCalls=0,width=320,height=180;
    bool gpuWrites=true,unicode=true;WNDPROC chainPrevious=nullptr;
};
static LRESULT CALLBACK gameProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto* game=reinterpret_cast<Game*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){game=static_cast<Game*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(game));game->window=window;}
    if(game&&message==WM_SIZE){
        ++game->sizes;const unsigned width=LOWORD(lparam),height=HIWORD(lparam);
        if(game->device&&wparam!=SIZE_MINIMIZED&&width&&height){
            // The real client synchronously enters Device::Reset here. Its
            // invalidation must not erase the post-forward size proof.
            ++game->resets;NorthlightWindowHook::invalidate(game->device);game->width=width;game->height=height;
            if(game->shadow&&game->gpuWrites)ok(game->device->ColorFill(game->shadow,nullptr,0));
        }
        return 0;
    }
    if(game){++game->other;game->lastOther=message;}
    return IsWindowUnicode(window)?DefWindowProcW(window,message,wparam,lparam):DefWindowProcA(window,message,wparam,lparam);
}
static LRESULT CALLBACK thirdPartyProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto* game=reinterpret_cast<Game*>(GetWindowLongPtrW(window,GWLP_USERDATA));assert(game&&game->chainPrevious);++game->chainCalls;
    return IsWindowUnicode(window)?CallWindowProcW(game->chainPrevious,window,message,wparam,lparam):CallWindowProcA(game->chainPrevious,window,message,wparam,lparam);
}
static HWND makeWindow(Game& game,bool unicode){
    game.unicode=unicode;
    RECT rectangle={0,0,320,180};assert(AdjustWindowRect(&rectangle,WS_OVERLAPPEDWINDOW,FALSE));
    HWND window=unicode?CreateWindowExW(0,L"NorthlightResizeOracleW",L"",WS_OVERLAPPEDWINDOW,0,0,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,GetModuleHandleW(nullptr),&game):
        CreateWindowExA(0,"NorthlightResizeOracleA","",WS_OVERLAPPEDWINDOW,0,0,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,GetModuleHandleW(nullptr),&game);
    assert(window&&bool(IsWindowUnicode(window))==unicode);return window;
}
static WNDPROC procedure(Game& game){return reinterpret_cast<WNDPROC>(game.unicode?GetWindowLongPtrW(game.window,GWLP_WNDPROC):GetWindowLongPtrA(game.window,GWLP_WNDPROC));}
static void procedure(Game& game,WNDPROC value){if(game.unicode)SetWindowLongPtrW(game.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(value));else SetWindowLongPtrA(game.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(value));}
static void size(Game& game,WPARAM state=SIZE_RESTORED,unsigned width=0,unsigned height=0){
    SendMessageW(game.window,WM_SIZE,state,MAKELPARAM(width?width:game.width,height?height:game.height));
}
static void forward(Game& game,WPARAM state=SIZE_RESTORED){const auto before=game.sizes;size(game,state);assert(game.sizes==before+1);}
static void suppressed(Game& game){const auto before=game.sizes;size(game);if(game.sizes!=before)std::fprintf(stderr,"UNEXPECTED_SIZE_FORWARD case=%u unicode=%u size=%ux%u lastOther=%u iconic=%u zoomed=%u\n",currentCase?*currentCase:0,unsigned(game.unicode),game.width,game.height,game.lastOther,unsigned(IsIconic(game.window)),unsigned(IsZoomed(game.window)));assert(game.sizes==before);}
static void publish(Game& game,bool windowed=true,HRESULT status=D3D_OK){NorthlightWindowHook::presented(game.device,game.window,game.width,game.height,windowed,status);}
static void healthy(Game& game){if(game.shadow)ok(game.device->ColorFill(game.shadow,nullptr,0xff72a4d6));publish(game);}
static std::vector<std::uint32_t> pixels(Game& game){
    IDirect3DSurface9* readback=nullptr;ok(game.device->CreateOffscreenPlainSurface(16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(game.device->GetRenderTargetData(game.shadow,readback));
    D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<std::uint32_t> result;
    for(unsigned y=0;y<16;++y){const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch);result.insert(result.end(),row,row+16);}ok(readback->UnlockRect());readback->Release();return result;
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    assert(argc==2);WNDCLASSW wide{};wide.lpfnWndProc=gameProcedure;wide.lpszClassName=L"NorthlightResizeOracleW";wide.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wide));
    WNDCLASSA narrow{};narrow.lpfnWndProc=gameProcedure;narrow.lpszClassName="NorthlightResizeOracleA";narrow.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassA(&narrow));
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    // Synthetic display/DPI messages target the counting game window only;
    // the private D3D device uses a separate hidden window.
    HWND gpuWindow=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(gpuWindow);
    D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.hDeviceWindow=gpuWindow;parameters.BackBufferWidth=16;parameters.BackBufferHeight=16;parameters.BackBufferFormat=D3DFMT_X8R8G8B8;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,gpuWindow,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&parameters,&device));
    unsigned cases=0;currentCase=&cases;
    for(bool unicode:{false,true}){
        Game game;makeWindow(game,unicode);game.device=device;foregroundFact.store(game.window,std::memory_order_release);
        ok(game.device->CreateRenderTarget(16,16,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&game.shadow,nullptr));
        healthy(game);const auto white=pixels(game);assert(white[0]==0xff72a4d6);
        const auto noGuard=game.resets;for(unsigned n=0;n<4;++n)forward(game);assert(game.resets==noGuard+4&&pixels(game)!=white);++cases;
        assert(NorthlightWindowHook::attach(game.device,game.window));
        // Overlay disabled must retain its existing nullptr result and leave
        // the independent resize owner installed.
        assert(!NorthlightOverlay::create(game.device,false,nullptr,{},nullptr));
        forward(game);healthy(game);const auto prime=game.resets;
        for(unsigned n=0;n<32;++n)suppressed(game);assert(game.resets==prime&&pixels(game)==white);++cases;
        for(HRESULT status:{D3DERR_DEVICELOST,D3DERR_DEVICENOTRESET,S_FALSE}){publish(game,true,status);forward(game);healthy(game);suppressed(game);++cases;}
        NorthlightWindowHook::invalidate(game.device);forward(game);healthy(game);suppressed(game);++cases;
        publish(game,false);forward(game);healthy(game);suppressed(game);++cases;
        for(WPARAM state:{WPARAM(SIZE_MINIMIZED),WPARAM(SIZE_MAXIMIZED)}){const auto style=GetWindowLongPtrW(game.window,GWL_STYLE);SetWindowLongPtrW(game.window,GWL_STYLE,style|(state==SIZE_MINIMIZED?WS_MINIMIZE:WS_MAXIMIZE));assert(state==SIZE_MINIMIZED?IsIconic(game.window):IsZoomed(game.window));forward(game,state);SetWindowLongPtrW(game.window,GWL_STYLE,style);healthy(game);forward(game);healthy(game);suppressed(game);++cases;}
        for(WPARAM state:{WPARAM(SIZE_MAXSHOW),WPARAM(SIZE_MAXHIDE)}){forward(game,state);healthy(game);forward(game);healthy(game);suppressed(game);++cases;}
        for(UINT message:{UINT(WM_DISPLAYCHANGE),UINT(WM_DPICHANGED)}){RECT proposed={0,0,320,180};const auto before=game.other;SendMessageW(game.window,message,message==WM_DPICHANGED?MAKELONG(120,120):32,message==WM_DPICHANGED?reinterpret_cast<LPARAM>(&proposed):MAKELPARAM(1920,1080));assert(game.other>before);healthy(game);forward(game);healthy(game);suppressed(game);++cases;}
        Game foreign;makeWindow(foreign,unicode);const auto unchanged=game.resets;forward(foreign);assert(game.resets==unchanged);++cases;
        foregroundFact.store(foreign.window,std::memory_order_release);forward(game);foregroundFact.store(game.window,std::memory_order_release);healthy(game);forward(game);healthy(game);suppressed(game);++cases;
        NorthlightWindowHook::invalidate(game.device);NorthlightWindowHook::presented(game.device,foreign.window,game.width,game.height,true,D3D_OK);forward(game);healthy(game);suppressed(game);++cases;
        auto* otherOwner=reinterpret_cast<IDirect3DDevice9*>(std::uintptr_t(1));NorthlightWindowHook::invalidate(otherOwner);NorthlightWindowHook::detach(otherOwner);suppressed(game);++cases;
        RECT rectangle={0,0,LONG(game.width+16),LONG(game.height+8)};assert(AdjustWindowRect(&rectangle,WS_OVERLAPPEDWINDOW,FALSE));const auto resized=game.resets;
        assert(SetWindowPos(game.window,nullptr,0,0,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE));assert(game.resets>resized);healthy(game);suppressed(game);++cases;
        const auto mismatch=game.sizes;size(game,SIZE_RESTORED,game.width-1,game.height);assert(game.sizes==mismatch+1);RECT client{};assert(GetClientRect(game.window,&client));game.width=unsigned(client.right-client.left);game.height=unsigned(client.bottom-client.top);forward(game);healthy(game);suppressed(game);++cases;
        // A publisher alternates two individually wrong sizes. Torn width/height
        // reads must never synthesize the real client dimensions and swallow.
        game.gpuWrites=false;NorthlightWindowHook::invalidate(game.device);const auto race=game.sizes;const unsigned w=game.width,h=game.height;std::atomic<bool> stop{false};
        std::thread publisher([&]{while(!stop.load(std::memory_order_relaxed)){NorthlightWindowHook::presented(game.device,game.window,w,h+1,true,D3D_OK);NorthlightWindowHook::presented(game.device,game.window,w+1,h,true,D3D_OK);}});
        for(unsigned n=0;n<4000;++n){game.width=w;game.height=h;forward(game);}stop.store(true,std::memory_order_relaxed);publisher.join();assert(game.sizes==race+4000);game.gpuWrites=true;
        // A racing snapshot may conservatively invalidate the final proof.
        // Re-establish it without a publisher, then verify normal suppression.
        forward(game);healthy(game);suppressed(game);++cases;
        game.chainPrevious=procedure(game);assert(game.chainPrevious);procedure(game,thirdPartyProcedure);const auto chain=game.chainCalls;suppressed(game);assert(game.chainCalls>chain);++cases;
        NorthlightWindowHook::detach(game.device);assert(procedure(game)==thirdPartyProcedure);forward(game);assert(NorthlightWindowHook::attach(game.device,game.window));forward(game);healthy(game);suppressed(game);++cases;
        procedure(game,game.chainPrevious);NorthlightWindowHook::detach(game.device);forward(game);++cases;
        assert(NorthlightWindowHook::attach(game.device,game.window));forward(game);healthy(game);
        closingOwner=game.device;closingReplacement=otherOwner;closingWindow=game.window;const auto closes=closingCalls;closingArmed=true;NorthlightWindowHook::detach(game.device);assert(!closingArmed&&closingCalls==closes+1);
        assert(NorthlightWindowHook::attach(otherOwner,game.window));auto* originalOwner=game.device;game.device=otherOwner;game.gpuWrites=false;forward(game);publish(game);suppressed(game);
        NorthlightWindowHook::presented(originalOwner,game.window,game.width,game.height,true,D3D_OK);NorthlightWindowHook::invalidate(originalOwner);NorthlightWindowHook::detach(originalOwner);suppressed(game);
        NorthlightWindowHook::detach(otherOwner);game.device=originalOwner;game.gpuWrites=true;++cases;
        assert(NorthlightWindowHook::attach(game.device,foreign.window));closingOwner=game.device;closingReplacement=otherOwner;closingWindow=foreign.window;const auto destroys=closingCalls;closingArmed=true;DestroyWindow(foreign.window);assert(!closingArmed&&closingCalls==destroys+1);NorthlightWindowHook::presented(game.device,foreign.window,320,180,true,D3D_OK);
        Game replacement;makeWindow(replacement,unicode);replacement.device=otherOwner;foregroundFact.store(replacement.window,std::memory_order_release);assert(NorthlightWindowHook::attach(otherOwner,replacement.window));forward(replacement);publish(replacement);suppressed(replacement);
        NorthlightWindowHook::presented(game.device,foreign.window,320,180,true,D3D_OK);NorthlightWindowHook::detach(game.device);suppressed(replacement);NorthlightWindowHook::detach(otherOwner);replacement.device=nullptr;DestroyWindow(replacement.window);++cases;
        game.shadow->Release();game.shadow=nullptr;game.device=nullptr;DestroyWindow(game.window);
    }
    // Keep an enabled old UI alive after its device hook has retired, then
    // replace it with another real device/UI before destroying the old UI.
    assert(NorthlightWindowHook::attach(device,gpuWindow));
    auto oldOverlay=NorthlightOverlay::create(device,true,nullptr,{},nullptr);assert(oldOverlay);
    bool pressed=false;assert(oldOverlay->masterHotkey(false,pressed));NorthlightWindowHook::detach(device);
    Game newGame;makeWindow(newGame,true);foregroundFact.store(newGame.window,std::memory_order_release);
    auto secondParameters=parameters;secondParameters.hDeviceWindow=newGame.window;secondParameters.BackBufferWidth=newGame.width;secondParameters.BackBufferHeight=newGame.height;IDirect3DDevice9* secondDevice=nullptr;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,newGame.window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&secondParameters,&secondDevice));newGame.device=secondDevice;
    assert(NorthlightWindowHook::attach(secondDevice,newGame.window));auto newOverlay=NorthlightOverlay::create(secondDevice,true,nullptr,{},nullptr);assert(newOverlay&&newOverlay->masterHotkey(false,pressed));
    forward(newGame);publish(newGame);suppressed(newGame);oldOverlay.reset();assert(newOverlay->masterHotkey(false,pressed));suppressed(newGame);
    newOverlay.reset();forward(newGame);publish(newGame);suppressed(newGame);
    NorthlightWindowHook::detach(secondDevice);newGame.device=nullptr;DestroyWindow(newGame.window);secondDevice->Release();
    device->Release();api->Release();FreeLibrary(library);DestroyWindow(gpuWindow);std::printf("WINDOW_SIZE_FILTER_WIN32_PASS cases=%u enabled_overlay_cases=2 ansi_unicode=1 real_subclass_chain=1 no_guard_negative=1 private_shadow_pixels_preserved=1 reentrant_reset=1 atomic_publication=1 reentrant_lifecycle=1 foreground_fact_seam=1 hidden_windows=1\n",cases);
}
