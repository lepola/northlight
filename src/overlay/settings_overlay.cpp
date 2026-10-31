// Settings overlay: the device window's subclass, the input queue, the ImGui context and drawing. See settings_overlay.h.
#include "settings_overlay.h"
#include "hdr_compiled_shaders.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "overlay_font_roboto.h"
#include "overlay_input.h"
#include "settings_panel.h"

namespace {
// The first failed ImGui assertion of the session. ImGui is used on the render thread only.
bool assertFailed=false;
char assertText[384];
}
// IM_ASSERT (northlight_imconfig.h): records the first failure and returns; never throws (ImGui asserts inside destructors
// and during cleanup too). The overlay checks the record after each ImGui step and turns itself off (Impl::checkpoint).
void northlightImGuiAssert(const char* expression,const char* file,int line){
    if(assertFailed)return;
    assertFailed=true;
    const char* name=std::strrchr(file,'/');const char* back=std::strrchr(file,'\\');if(back>name)name=back;
    std::snprintf(assertText,sizeof assertText,"ImGui assertion %s (%s:%d)",expression,name?name+1:file,line);
}

namespace NorthlightOverlay {
namespace {
namespace In=NorthlightOverlayInput;
static_assert(In::Msg::KillFocus==WM_KILLFOCUS&&In::Msg::ActivateApp==WM_ACTIVATEAPP&&In::Msg::KeyDown==WM_KEYDOWN&&In::Msg::KeyUp==WM_KEYUP&&In::Msg::Char==WM_CHAR&&
              In::Msg::SysKeyDown==WM_SYSKEYDOWN&&In::Msg::SysKeyUp==WM_SYSKEYUP&&In::Msg::SysChar==WM_SYSCHAR&&In::Msg::MouseMove==WM_MOUSEMOVE&&
              In::Msg::LButtonDown==WM_LBUTTONDOWN&&In::Msg::RButtonUp==WM_RBUTTONUP&&In::Msg::MButtonDblClk==WM_MBUTTONDBLCLK&&In::Msg::MouseWheel==WM_MOUSEWHEEL&&
              In::Msg::XButtonDown==WM_XBUTTONDOWN&&In::Msg::XButtonDblClk==WM_XBUTTONDBLCLK&&In::Msg::MouseHWheel==WM_MOUSEHWHEEL&&In::Msg::CaptureChanged==WM_CAPTURECHANGED&&
              In::VkF1==VK_F1&&In::VkF11==VK_F11,"overlay_input.h message numbers");

// Process lifetime, never freed: after the device is gone the procedure may still sit in another module's subclass
// chain (it then forwards everything). active=false is that pass-through state.
struct WindowHook {
    std::atomic<bool> active{false},installed{false},open{false},captureMouse{false},captureText{false};
    std::atomic<WNDPROC> previous{nullptr};
    std::atomic<HWND> window{nullptr};
    std::atomic<bool> unicode{false};
    std::atomic<unsigned> opens{0};   // every show: the render thread refreshes the panel and resets ImGui's key state
    std::atomic<unsigned> heldButtons{0};   // written by the window thread; others ask through forgetButtons
    std::atomic<bool> forgetButtons{false};   // the render thread closed the window: the next message clears heldButtons
    std::atomic<bool> captured{false};   // the overlay holds the mouse capture for heldButtons (window thread)
    std::atomic<unsigned> hotkey{0};   // packed In::Hotkey (vk | ctrl<<8 | shift<<9 | alt<<10), set before install()
    In::Queue queue;
};
WindowHook hook;
unsigned pack(const In::Hotkey& k){return k.vk|(k.mods.ctrl?1u<<8:0)|(k.mods.shift?1u<<9:0)|(k.mods.alt?1u<<10:0);}
In::Hotkey unpack(unsigned v){In::Hotkey k;k.vk=v&0xFF;k.mods.ctrl=(v>>8)&1;k.mods.shift=(v>>9)&1;k.mods.alt=(v>>10)&1;return k;}

LRESULT CALLBACK windowProcedure(HWND window,UINT msg,WPARAM wparam,LPARAM lparam){
    const WNDPROC previous=hook.previous.load(std::memory_order_acquire);
    const bool unicode=hook.unicode.load(std::memory_order_relaxed);
    auto forward=[&]()->LRESULT{
        if(!previous)return unicode?DefWindowProcW(window,msg,wparam,lparam):DefWindowProcA(window,msg,wparam,lparam);
        return unicode?CallWindowProcW(previous,window,msg,wparam,lparam):CallWindowProcA(previous,window,msg,wparam,lparam);
    };
    if(msg==WM_NCDESTROY){
        // The window is going away: unhook if nobody subclassed after us (the chain dies with the window anyway).
        hook.active.store(false);hook.open.store(false);
        const LONG_PTR current=unicode?GetWindowLongPtrW(window,GWLP_WNDPROC):GetWindowLongPtrA(window,GWLP_WNDPROC);
        if(current==reinterpret_cast<LONG_PTR>(&windowProcedure)&&previous){
            if(unicode)SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));else SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
            hook.installed.store(false);
        }
        return forward();
    }
    if(!hook.active.load(std::memory_order_relaxed))return forward();
    In::Modifiers mods;
    if(msg>=WM_KEYFIRST&&msg<=WM_KEYLAST){mods.ctrl=GetKeyState(VK_CONTROL)<0;mods.shift=GetKeyState(VK_SHIFT)<0;mods.alt=GetKeyState(VK_MENU)<0;}
    In::State state;state.open=hook.open.load(std::memory_order_relaxed);state.captureMouse=hook.captureMouse.load(std::memory_order_relaxed);
    state.captureText=hook.captureText.load(std::memory_order_relaxed);state.hotkey=unpack(hook.hotkey.load(std::memory_order_relaxed));
    state.self=reinterpret_cast<std::intptr_t>(window);
    bool releaseNow=false;
    if(hook.forgetButtons.exchange(false,std::memory_order_relaxed)){hook.heldButtons.store(0,std::memory_order_relaxed);releaseNow=hook.captured.exchange(false);}
    state.heldButtons=hook.heldButtons.load(std::memory_order_relaxed);state.captured=hook.captured.load(std::memory_order_relaxed);
    In::Decision d=In::route(msg,wparam,lparam,mods,state);
    RECT client={};
    auto queue=[&](unsigned m,std::uintptr_t w,std::intptr_t l)->bool{
        In::Event e;e.msg=m;e.wparam=w;e.lparam=l;e.mods=mods;
        if(client.right==0&&client.bottom==0)GetClientRect(window,&client);
        e.clientWidth=client.right-client.left;e.clientHeight=client.bottom-client.top;return hook.queue.push(e);
    };
    if(d.toggle){
        const bool open=!hook.open.load(std::memory_order_relaxed);
        if(open){
            // The render thread learns the cursor position before the first move.
            hook.queue.clear();POINT p={};
            if(GetCursorPos(&p)&&ScreenToClient(window,&p))queue(WM_MOUSEMOVE,0,std::intptr_t(MAKELPARAM(short(p.x),short(p.y))));   // the emptied queue takes it
        }else{hook.captureMouse.store(false);hook.captureText.store(false);}
        if(open)hook.opens.fetch_add(1,std::memory_order_relaxed);
        hook.open.store(open,std::memory_order_release);
    }
    if(d.queue&&!queue(msg,wparam,lparam))In::dropped(msg,wparam,d,state);   // ImGui will not see it: a press goes to the game
    // Publish before SetCapture/ReleaseCapture: both re-enter this procedure with WM_CAPTURECHANGED.
    hook.heldButtons.store(state.heldButtons,std::memory_order_relaxed);hook.captured.store(state.captured,std::memory_order_relaxed);
    if(d.capture)SetCapture(window);
    if((d.release||releaseNow)&&GetCapture()==window)ReleaseCapture();
    return d.swallow?0:forward();
}

bool install(HWND window,const char*& why){
    if(!window||!IsWindow(window)){why="no device window";return false;}
    if(hook.installed.load()){
        // Still chained from an earlier device (another module subclassed after us): reuse that link.
        if(hook.window.load()==window){hook.active.store(true);return true;}
        why="the procedure is still chained into an earlier window";return false;
    }
    const bool unicode=IsWindowUnicode(window)!=FALSE;
    const LONG_PTR current=unicode?GetWindowLongPtrW(window,GWLP_WNDPROC):GetWindowLongPtrA(window,GWLP_WNDPROC);
    if(!current){why="GetWindowLongPtr failed";return false;}
    hook.unicode.store(unicode);hook.window.store(window);hook.heldButtons.store(0);hook.queue.clear();
    hook.previous.store(reinterpret_cast<WNDPROC>(current),std::memory_order_release);
    SetLastError(0);
    const LONG_PTR replaced=unicode?SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&windowProcedure))
                                   :SetWindowLongPtrA(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&windowProcedure));
    if(!replaced&&GetLastError()){why="SetWindowLongPtr failed";return false;}
    if(replaced&&replaced!=current)hook.previous.store(reinterpret_cast<WNDPROC>(replaced),std::memory_order_release);
    hook.installed.store(true);hook.active.store(true);
    return true;
}
// Device destruction: restores the previous procedure only while ours is still the window's current one; otherwise
// another module subclassed after us and we stay in its chain as a pass-through (never break its chain).
void uninstall(){
    hook.active.store(false);hook.open.store(false);hook.captureMouse.store(false);hook.captureText.store(false);hook.queue.clear();
    hook.heldButtons.store(0);hook.captured.store(false);   // a capture we hold ends with the button's release (pass-through from now on)
    if(!hook.installed.load())return;
    const HWND window=hook.window.load();const bool unicode=hook.unicode.load();
    if(!IsWindow(window)){hook.installed.store(false);return;}
    const LONG_PTR current=unicode?GetWindowLongPtrW(window,GWLP_WNDPROC):GetWindowLongPtrA(window,GWLP_WNDPROC);
    if(current!=reinterpret_cast<LONG_PTR>(&windowProcedure))return;
    const LONG_PTR previous=reinterpret_cast<LONG_PTR>(hook.previous.load());
    if(unicode)SetWindowLongPtrW(window,GWLP_WNDPROC,previous);else SetWindowLongPtrA(window,GWLP_WNDPROC,previous);
    hook.installed.store(false);
}

HWND deviceWindow(IDirect3DDevice9* device){
    HWND window=nullptr;IDirect3DSwapChain9* chain=nullptr;
    if(SUCCEEDED(device->GetSwapChain(0,&chain))&&chain){D3DPRESENT_PARAMETERS pp={};if(SUCCEEDED(chain->GetPresentParameters(&pp)))window=pp.hDeviceWindow;chain->Release();}
    if(!window){D3DDEVICE_CREATION_PARAMETERS cp={};if(SUCCEEDED(device->GetCreationParameters(&cp)))window=cp.hFocusWindow;}
    return window;
}

ImGuiKey imguiKey(WPARAM vk){
    if(vk>='0'&&vk<='9')return ImGuiKey(ImGuiKey_0+int(vk-'0'));
    if(vk>='A'&&vk<='Z')return ImGuiKey(ImGuiKey_A+int(vk-'A'));
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return ImGuiKey(ImGuiKey_Keypad0+int(vk-VK_NUMPAD0));
    switch(vk){
    case VK_TAB:return ImGuiKey_Tab;case VK_LEFT:return ImGuiKey_LeftArrow;case VK_RIGHT:return ImGuiKey_RightArrow;case VK_UP:return ImGuiKey_UpArrow;case VK_DOWN:return ImGuiKey_DownArrow;
    case VK_PRIOR:return ImGuiKey_PageUp;case VK_NEXT:return ImGuiKey_PageDown;case VK_HOME:return ImGuiKey_Home;case VK_END:return ImGuiKey_End;case VK_INSERT:return ImGuiKey_Insert;
    case VK_DELETE:return ImGuiKey_Delete;case VK_BACK:return ImGuiKey_Backspace;case VK_SPACE:return ImGuiKey_Space;case VK_RETURN:return ImGuiKey_Enter;case VK_ESCAPE:return ImGuiKey_Escape;
    case VK_OEM_PERIOD:return ImGuiKey_Period;case VK_OEM_COMMA:return ImGuiKey_Comma;case VK_OEM_MINUS:return ImGuiKey_Minus;case VK_DECIMAL:return ImGuiKey_KeypadDecimal;
    case VK_SUBTRACT:return ImGuiKey_KeypadSubtract;case VK_ADD:return ImGuiKey_KeypadAdd;
    case VK_CONTROL:case VK_LCONTROL:return ImGuiKey_LeftCtrl;case VK_RCONTROL:return ImGuiKey_RightCtrl;
    case VK_SHIFT:case VK_LSHIFT:return ImGuiKey_LeftShift;case VK_RSHIFT:return ImGuiKey_RightShift;
    case VK_MENU:case VK_LMENU:return ImGuiKey_LeftAlt;case VK_RMENU:return ImGuiKey_RightAlt;
    default:return ImGuiKey_None;
    }
}

// The window's look: dark, compact, a little translucent so the scene behind stays readable.
void baseStyle(float scale){
    ImGuiStyle style;ImGui::StyleColorsDark(&style);
    style.WindowRounding=7;style.ChildRounding=5;style.FrameRounding=4;style.PopupRounding=5;style.GrabRounding=3;style.TabRounding=4;style.ScrollbarRounding=6;
    style.WindowPadding=ImVec2(12,10);style.FramePadding=ImVec2(7,4);style.ItemSpacing=ImVec2(8,5);style.ItemInnerSpacing=ImVec2(5,4);
    style.IndentSpacing=16;style.ScrollbarSize=13;style.GrabMinSize=10;style.WindowBorderSize=1;style.FrameBorderSize=0;style.SeparatorTextBorderSize=1;
    style.WindowTitleAlign=ImVec2(0.02f,0.5f);style.CellPadding=ImVec2(5,3);
    ImVec4* c=style.Colors;
    c[ImGuiCol_WindowBg]=ImVec4(0.075f,0.082f,0.098f,0.94f);c[ImGuiCol_ChildBg]=ImVec4(0,0,0,0);c[ImGuiCol_PopupBg]=ImVec4(0.09f,0.10f,0.12f,0.98f);
    c[ImGuiCol_Border]=ImVec4(0.28f,0.31f,0.36f,0.60f);c[ImGuiCol_TitleBg]=ImVec4(0.09f,0.10f,0.12f,1);c[ImGuiCol_TitleBgActive]=ImVec4(0.12f,0.17f,0.25f,1);
    c[ImGuiCol_FrameBg]=ImVec4(0.16f,0.18f,0.22f,1);c[ImGuiCol_FrameBgHovered]=ImVec4(0.21f,0.25f,0.31f,1);c[ImGuiCol_FrameBgActive]=ImVec4(0.24f,0.30f,0.38f,1);
    c[ImGuiCol_CheckMark]=ImVec4(0.45f,0.74f,1.00f,1);c[ImGuiCol_SliderGrab]=ImVec4(0.38f,0.62f,0.92f,1);c[ImGuiCol_SliderGrabActive]=ImVec4(0.50f,0.76f,1.00f,1);
    c[ImGuiCol_Button]=ImVec4(0.19f,0.22f,0.27f,1);c[ImGuiCol_ButtonHovered]=ImVec4(0.25f,0.31f,0.39f,1);c[ImGuiCol_ButtonActive]=ImVec4(0.29f,0.38f,0.50f,1);
    c[ImGuiCol_Header]=ImVec4(0.17f,0.21f,0.27f,1);c[ImGuiCol_HeaderHovered]=ImVec4(0.22f,0.28f,0.36f,1);c[ImGuiCol_HeaderActive]=ImVec4(0.26f,0.33f,0.43f,1);
    c[ImGuiCol_Tab]=ImVec4(0.13f,0.15f,0.19f,1);c[ImGuiCol_TabHovered]=ImVec4(0.25f,0.33f,0.45f,1);c[ImGuiCol_TabSelected]=ImVec4(0.20f,0.29f,0.42f,1);
    c[ImGuiCol_TabSelectedOverline]=ImVec4(0.45f,0.74f,1.00f,1);c[ImGuiCol_Separator]=ImVec4(0.26f,0.29f,0.34f,1);
    c[ImGuiCol_TableRowBg]=ImVec4(0,0,0,0);c[ImGuiCol_TableRowBgAlt]=ImVec4(1,1,1,0.025f);c[ImGuiCol_ModalWindowDimBg]=ImVec4(0,0,0,0.25f);
    c[ImGuiCol_TextDisabled]=ImVec4(0.46f,0.48f,0.52f,1);
    style.ScaleAllSizes(scale);
    ImGui::GetStyle()=style;
}

// 1 at 1080p, in quarter steps up to 4 (8K); never below 1 so text stays readable on small windows.
float uiScale(unsigned backBufferHeight){return std::clamp(std::round(float(backBufferHeight)/1080.f*4.f)/4.f,1.f,4.f);}

// A system TTF into ImGui-owned memory (the atlas frees it). Missing or unreadable: false (Wine may lack the font).
bool loadSystemFont(const wchar_t* name,float pixels,const ImWchar* ranges){
    wchar_t path[MAX_PATH];const UINT n=GetWindowsDirectoryW(path,MAX_PATH);
    if(!n||n>=MAX_PATH-40)return false;
    std::swprintf(path+n,MAX_PATH-n,L"\\Fonts\\%ls",name);
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size={};void* data=nullptr;DWORD read=0;
    const bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>1024&&size.QuadPart<(32ll<<20)&&(data=IM_ALLOC(size_t(size.QuadPart)))!=nullptr&&
                  ReadFile(file,data,DWORD(size.QuadPart),&read,nullptr)&&read==DWORD(size.QuadPart);
    CloseHandle(file);
    if(!ok){if(data)IM_FREE(data);return false;}
    ImFontConfig config;config.OversampleH=2;config.OversampleV=1;config.PixelSnapH=false;
    std::snprintf(config.Name,sizeof config.Name,"%ls",name);
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data,int(read),pixels,&config,ranges)!=nullptr;
}

class Impl final : public Overlay {
    IDirect3DDevice9* device;
    IDirect3DPixelShader9* hdrShader=nullptr;float hdrWhite=0;
    std::shared_ptr<NorthlightSettings::Service> service;
    Log log;
    std::string hotkeyName;
    std::mutex mutex;   // draw/invalidate/destroy (the device gate already serialises them; this keeps the overlay self-contained)
    ImGuiContext* context=nullptr;
    std::unique_ptr<SettingsPanel> panel;
    bool failed=false,backend=false,focusLost=false;
    std::atomic<bool> background{false};   // closed, but the panel still has service work pending or an unsaved draft to point at
    unsigned opens=0;
    float scale=0;
    LARGE_INTEGER frequency={},last={};
    std::vector<In::Event> events;

    void logf(const char* fmt,...)const{
        if(!log)return;char line[512];va_list args;va_start(args,fmt);std::vsnprintf(line,sizeof line,fmt,args);va_end(args);log(line);
    }
    // After every ImGui step: a recorded assertion ends the frame here (from our code, not from inside ImGui).
    static void checkpoint(){if(assertFailed)throw std::runtime_error(assertText);}
    // Ends the overlay for this session: input back to pure pass-through, D3DPOOL_DEFAULT objects released (Reset must not
    // find them). The context stays (a half-built frame may not survive DestroyContext) until the device goes.
    void fail(const char* why){
        failed=true;background.store(false);uninstall();
        logf("OVERLAY disabled for this session: %s",why);
        try{if(context&&backend){ImGui::SetCurrentContext(context);ImGui_ImplDX9_InvalidateDeviceObjects();}}catch(...){}
    }
    void ensureContext(){
        if(context)return;
        IMGUI_CHECKVERSION();
        context=ImGui::CreateContext();ImGui::SetCurrentContext(context);
        ImGuiIO& io=ImGui::GetIO();
        io.IniFilename=nullptr;io.LogFilename=nullptr;   // the window position lives for the session only
        io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;   // the game owns the cursor
        io.ConfigWindowsMoveFromTitleBarOnly=true;
        io.BackendPlatformName="northlight-overlay";
        backend=ImGui_ImplDX9_Init(device);
        if(!backend)throw std::runtime_error("ImGui_ImplDX9_Init failed");
        panel=std::make_unique<SettingsPanel>(service,hotkeyName);
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);
    }
    // Font atlas and style for a new scale only (first frame, resolution changes).
    void rescale(float next){
        ImGuiIO& io=ImGui::GetIO();
        ImGui_ImplDX9_InvalidateDeviceObjects();   // the font texture is rebuilt by the next NewFrame
        io.Fonts->Clear();
        static const ImWchar ranges[]={0x0020,0x00FF,0};
        const float pixels=std::round(16.f*next);
        // The embedded Roboto first (the same text on Windows and under Wine, whose font folder may be empty); system fonts
        // only if it cannot be added; ImGui's bitmap font last.
        const char* font="Roboto-Medium (embedded)";
        ImFontConfig embedded;embedded.OversampleH=2;embedded.OversampleV=1;std::snprintf(embedded.Name,sizeof embedded.Name,"Roboto-Medium");
        if(!io.Fonts->AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed,int(NorthlightOverlayFont::RobotoMediumCompressedSize),pixels,&embedded,ranges)){
            font="segoeui.ttf";
            if(!loadSystemFont(L"segoeui.ttf",pixels,ranges)){
                font="tahoma.ttf";
                if(!loadSystemFont(L"tahoma.ttf",pixels-1,ranges)){
                    font="ImGui default";ImFontConfig config;config.SizePixels=13.f*std::max(1.f,std::round(next));io.Fonts->AddFontDefault(&config);
                }
            }
        }
        baseStyle(next);
        if(scale!=next)logf("OVERLAY scale=%.2f font=%s pixels=%.0f",next,font,pixels);
        scale=next;
    }
    void lose(ImGuiIO& io){
        io.AddFocusEvent(false);for(int b=0;b<ImGuiMouseButton_COUNT;++b)io.AddMouseButtonEvent(b,false);focusLost=true;
    }
    void feed(const D3DSURFACE_DESC& desc){
        ImGuiIO& io=ImGui::GetIO();
        if(hook.queue.drain(events))lose(io);   // input was dropped: release everything ImGui may hold, then replay what arrived
        for(const auto& e:events){
            auto position=[&]{
                const short x=short(LOWORD(e.lparam)),y=short(HIWORD(e.lparam));
                io.AddMousePosEvent(In::mapCoordinate(x,e.clientWidth,desc.Width),In::mapCoordinate(y,e.clientHeight,desc.Height));
            };
            const bool lost=e.msg==WM_KILLFOCUS||e.msg==WM_ACTIVATEAPP||e.msg==WM_CAPTURECHANGED;
            if(focusLost&&!lost){io.AddFocusEvent(true);focusLost=false;}
            const int button=In::button(e.msg,e.wparam);
            if(button>=0){position();io.AddMouseButtonEvent(button,!In::buttonUp(e.msg));continue;}
            switch(e.msg){
            case WM_MOUSEMOVE:position();break;
            case WM_MOUSEWHEEL:io.AddMouseWheelEvent(0,float(short(HIWORD(e.wparam)))/float(WHEEL_DELTA));break;
            case WM_MOUSEHWHEEL:io.AddMouseWheelEvent(-float(short(HIWORD(e.wparam)))/float(WHEEL_DELTA),0);break;
            case WM_KEYDOWN:case WM_SYSKEYDOWN:case WM_KEYUP:case WM_SYSKEYUP:{
                const bool down=e.msg==WM_KEYDOWN||e.msg==WM_SYSKEYDOWN;
                io.AddKeyEvent(ImGuiMod_Ctrl,e.mods.ctrl);io.AddKeyEvent(ImGuiMod_Shift,e.mods.shift);io.AddKeyEvent(ImGuiMod_Alt,e.mods.alt);
                const ImGuiKey key=imguiKey(e.wparam);if(key!=ImGuiKey_None)io.AddKeyEvent(key,down);
                break;}
            case WM_CHAR:case WM_SYSCHAR:
                if(hook.unicode.load(std::memory_order_relaxed))io.AddInputCharacterUTF16(ImWchar16(e.wparam));
                else{const char byte=char(e.wparam);wchar_t wide=0;if(MultiByteToWideChar(CP_ACP,MB_PRECOMPOSED,&byte,1,&wide,1)==1)io.AddInputCharacterUTF16(ImWchar16(wide));}
                break;
            default:if(lost)lose(io);break;
            }
        }
    }
    // Outside the game's scene (Present comes after its EndScene), onto the implicit swap chain's back buffer, over
    // everything the game drew. The caller's SavedState restores all of this.
    static void hdrDrawState(const ImDrawList*,const ImDrawCmd* command){
        auto& self=*static_cast<Impl*>(command->UserCallbackData);
        const float color[]={self.hdrWhite/80,self.hdrWhite/80,self.hdrWhite/80,0};
        self.device->SetPixelShader(self.hdrShader);self.device->SetPixelShaderConstantF(0,color,1);
        self.device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    }
    void render(IDirect3DSurface9* back,const D3DSURFACE_DESC& desc){
        device->SetRenderTarget(0,back);
        for(DWORD i=1;i<4;++i)device->SetRenderTarget(i,nullptr);
        device->SetDepthStencilSurface(nullptr);
        const D3DVIEWPORT9 viewport={0,0,desc.Width,desc.Height,0.f,1.f};device->SetViewport(&viewport);
        // States imgui_impl_dx9 leaves as the game had them.
        const std::pair<D3DRENDERSTATETYPE,DWORD> states[]={{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_COLORWRITEENABLE,0xF},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_STENCILENABLE,FALSE},
            {D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD},{D3DRS_WRAP0,0},{D3DRS_VERTEXBLEND,D3DVBF_DISABLE},{D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE},{D3DRS_TWOSIDEDSTENCILMODE,FALSE},
            {D3DRS_MULTISAMPLEMASK,0xFFFFFFFF},{D3DRS_POINTSPRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0}};
        for(const auto& s:states)device->SetRenderState(s.first,s.second);
        device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);device->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);
        device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        device->SetTextureStageState(0,D3DTSS_RESULTARG,D3DTA_CURRENT);
        device->SetStreamSourceFreq(0,1);
        if(hdrWhite>0){
            if(!hdrShader&&FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrOverlay),&hdrShader))){fail("HDR overlay shader unavailable");return;}
            // The backend initializes fixed-function state before processing callbacks.
            // Insert our transfer after that setup for each command list.
            auto* data=ImGui::GetDrawData();
            for(int i=0;i<data->CmdListsCount;++i){ImDrawCmd command;command.UserCallback=&hdrDrawState;command.UserCallbackData=this;data->CmdLists[i]->CmdBuffer.insert(data->CmdLists[i]->CmdBuffer.begin(),command);}
        }
        if(FAILED(device->BeginScene()))return;
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
        checkpoint();
    }
    void frame(){
        if(device->TestCooperativeLevel()!=D3D_OK)return;   // lost: nothing to draw onto (Reset follows)
        const bool open=hook.open.load(std::memory_order_acquire);
        if(!open){
            // Closed: keep the service work moving; draw only the one-line reminder of an unsaved draft.
            if(!panel){background.store(false);return;}
            hook.queue.clear();
            panel->poll();
            const bool notice=panel->unsaved();
            background.store(notice||panel->pending());
            if(!notice)return;
        }
        struct Surface{IDirect3DSurface9* p=nullptr;~Surface(){if(p)p->Release();}} back;
        D3DSURFACE_DESC desc={};
        if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p))||!back.p||FAILED(back.p->GetDesc(&desc))||!desc.Width||!desc.Height)return;
        ensureContext();ImGui::SetCurrentContext(context);
        const float wanted=uiScale(desc.Height);
        if(wanted!=scale)rescale(wanted);
        checkpoint();
        ImGuiIO& io=ImGui::GetIO();
        if(open){
            if(const unsigned n=hook.opens.load(std::memory_order_relaxed);n!=opens){opens=n;panel->opened();io.AddFocusEvent(false);io.AddFocusEvent(true);}   // keys seen before the last close are released
            feed(desc);
        }
        LARGE_INTEGER now;QueryPerformanceCounter(&now);
        io.DeltaTime=std::clamp(frequency.QuadPart>0?float(double(now.QuadPart-last.QuadPart)/double(frequency.QuadPart)):1/60.f,1e-4f,0.25f);last=now;
        io.DisplaySize=ImVec2(float(desc.Width),float(desc.Height));
        ImGui_ImplDX9_NewFrame();
        ImGui::NewFrame();
        checkpoint();
        bool keep=true;
        if(open)keep=panel->draw(scale);else panel->notice(scale);
        checkpoint();
        ImGui::Render();
        checkpoint();
        render(back.p,desc);
        if(!open)return;
        hook.captureMouse.store(io.WantCaptureMouse&&keep,std::memory_order_relaxed);
        hook.captureText.store(io.WantTextInput&&keep,std::memory_order_relaxed);
        if(!keep){hook.open.store(false);hook.forgetButtons.store(true);}
        background.store(panel->unsaved()||panel->pending());
    }
public:
    Impl(IDirect3DDevice9* d,std::shared_ptr<NorthlightSettings::Service> s,Log l,std::string key):device(d),service(std::move(s)),log(l),hotkeyName(std::move(key)){device->AddRef();}
    ~Impl() override{
        uninstall();
        std::lock_guard<std::mutex> lock(mutex);
        try{
            panel.reset();
            if(context){ImGui::SetCurrentContext(context);if(backend)ImGui_ImplDX9_Shutdown();ImGui::DestroyContext(context);}
        }catch(...){}
        context=nullptr;
        if(hdrShader)hdrShader->Release();
        device->Release();
    }
    bool visible()const override{return hook.open.load(std::memory_order_relaxed)||background.load(std::memory_order_relaxed);}
    void draw(float uiWhite=0) override{
        std::lock_guard<std::mutex> lock(mutex);
        hdrWhite=uiWhite;
        if(failed)return;
        try{frame();}
        catch(const std::exception& e){fail(e.what());}
        catch(...){fail("unknown exception");}
    }
    void invalidate() override{
        std::lock_guard<std::mutex> lock(mutex);
        if(!context||!backend||failed)return;
        try{ImGui::SetCurrentContext(context);ImGui_ImplDX9_InvalidateDeviceObjects();checkpoint();}
        catch(const std::exception& e){fail(e.what());}
        catch(...){fail("unknown exception");}
    }
};
} // namespace

std::unique_ptr<Overlay> create(IDirect3DDevice9* device,bool enabled,const char* key,std::shared_ptr<NorthlightSettings::Service> service,Log log){
    char line[256];
    if(!enabled){if(log)log("OVERLAY off: [Renderer] SettingsOverlay=0 (no window hook)");return nullptr;}
    if(!device)return nullptr;
    if(hook.active.load()){if(log)log("OVERLAY unavailable for this device: another device owns the settings window");return nullptr;}
    In::Hotkey hotkey;
    if(key&&*key&&!In::parseHotkey(key,hotkey)){
        std::snprintf(line,sizeof line,"OVERLAY SettingsOverlayKey=%.64s not usable (F1..F12 with Ctrl/Shift/Alt, not Ctrl+Shift+F7..F10/F12); using %s",key,In::hotkeyName(hotkey).c_str());
        if(log)log(line);
    }
    hook.hotkey.store(pack(hotkey));
    const HWND window=deviceWindow(device);const char* why="";
    if(!install(window,why)){std::snprintf(line,sizeof line,"OVERLAY unavailable: %s",why);if(log)log(line);return nullptr;}
    try{
        auto overlay=std::make_unique<Impl>(device,std::move(service),log,In::hotkeyName(hotkey));
        std::snprintf(line,sizeof line,"OVERLAY ready: %s shows the Northlight settings (window=%p unicode=%d)",In::hotkeyName(hotkey).c_str(),static_cast<void*>(window),int(hook.unicode.load()));
        if(log)log(line);
        return overlay;
    }catch(...){uninstall();if(log)log("OVERLAY unavailable: allocation failure");return nullptr;}
}
} // namespace NorthlightOverlay
