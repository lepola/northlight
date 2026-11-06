// Standalone host of the production precipitation shader and lens simulation.
#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "overlay_font_roboto.h"
#include "saved_state.h"
template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
static bool surfaceError=false;
static bool errorDialog=true;
static void logf(const char* text,...){surfaceError=true;std::fprintf(stderr,"%s\n",text);}
struct NameMaskCompositeBind {NameMaskCompositeBind(IDirect3DDevice9*,IDirect3DSurface9*){}};
#include "surface_renderer.h"
#include "weather_effects.h"

namespace fs=std::filesystem;
static unsigned W=960,H=640;
static void checked(HRESULT hr,int line){if(FAILED(hr)){char message[128];std::snprintf(message,sizeof message,"D3D9 call failed: 0x%08lx (weather_lab.cpp:%d)",static_cast<unsigned long>(hr),line);throw std::runtime_error(message);}}
#define check(hr) checked((hr),__LINE__)
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    if(ImGui::GetCurrentContext()){
        auto& io=ImGui::GetIO();
        switch(message){
        case WM_MOUSEMOVE:io.AddMousePosEvent(float(GET_X_LPARAM(l)),float(GET_Y_LPARAM(l)));return 0;
        case WM_LBUTTONDOWN:SetCapture(window);io.AddMouseButtonEvent(0,true);return 0;
        case WM_LBUTTONUP:ReleaseCapture();io.AddMouseButtonEvent(0,false);return 0;
        case WM_RBUTTONDOWN:io.AddMouseButtonEvent(1,true);return 0;
        case WM_RBUTTONUP:io.AddMouseButtonEvent(1,false);return 0;
        case WM_MOUSEWHEEL:io.AddMouseWheelEvent(0,float(GET_WHEEL_DELTA_WPARAM(w))/WHEEL_DELTA);return 0;
        case WM_CHAR:io.AddInputCharacter(unsigned(w));return 0;
        case WM_KEYDOWN:case WM_KEYUP:{
            ImGuiKey key=ImGuiKey_None;
            switch(w){case VK_TAB:key=ImGuiKey_Tab;break;case VK_LEFT:key=ImGuiKey_LeftArrow;break;case VK_RIGHT:key=ImGuiKey_RightArrow;break;
            case VK_UP:key=ImGuiKey_UpArrow;break;case VK_DOWN:key=ImGuiKey_DownArrow;break;case VK_RETURN:key=ImGuiKey_Enter;break;
            case VK_SPACE:key=ImGuiKey_Space;break;case VK_ESCAPE:key=ImGuiKey_Escape;break;case VK_BACK:key=ImGuiKey_Backspace;break;
            case VK_DELETE:key=ImGuiKey_Delete;break;case VK_HOME:key=ImGuiKey_Home;break;case VK_END:key=ImGuiKey_End;break;}
            if(key!=ImGuiKey_None)io.AddKeyEvent(key,message==WM_KEYDOWN);
            io.AddKeyEvent(ImGuiMod_Ctrl,(GetKeyState(VK_CONTROL)&0x8000)!=0);
            io.AddKeyEvent(ImGuiMod_Shift,(GetKeyState(VK_SHIFT)&0x8000)!=0);return 0;}
        case WM_SETFOCUS:io.AddFocusEvent(true);break;
        case WM_KILLFOCUS:io.AddFocusEvent(false);io.ClearInputMouse();io.ClearInputKeys();break;
        }
    }
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}

static IDirect3DTexture9* texture(IDirect3DDevice9* d,D3DFORMAT format,const void* bytes,unsigned stride){
    IDirect3DTexture9* t=nullptr;check(d->CreateTexture(W,H,1,0,format,D3DPOOL_MANAGED,&t,nullptr));
    D3DLOCKED_RECT lock{};check(t->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<H;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,static_cast<const char*>(bytes)+y*W*stride,W*stride);
    check(t->UnlockRect(0));return t;
}
static std::vector<unsigned char> pixels(IDirect3DDevice9* d,IDirect3DSurface9* target,unsigned width,unsigned height){
    IDirect3DSurface9* staging=nullptr;check(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    check(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};check(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<unsigned char> data(width*height*4);
    for(unsigned y=0;y<height;++y)std::memcpy(data.data()+y*width*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*4);
    check(staging->UnlockRect());drop(staging);return data;
}
static void capture(const char* name,const std::vector<unsigned char>& data,unsigned width,unsigned height){
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof file+sizeof info;file.bfSize=file.bfOffBits+DWORD(data.size());
    info.biSize=sizeof info;info.biWidth=LONG(width);info.biHeight=-LONG(height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(name,std::ios::binary);out.write(reinterpret_cast<const char*>(&file),sizeof file);out.write(reinterpret_cast<const char*>(&info),sizeof info);
    out.write(reinterpret_cast<const char*>(data.data()),data.size());if(!out)throw std::runtime_error("Could not save capture");
}
static void slider(const char* title,unsigned& value,int low,int high){ImGui::TextUnformatted(title);ImGui::SetNextItemWidth(-1);int v=int(value);ImGui::PushID(title);if(ImGui::SliderInt("##value",&v,low,high))value=unsigned(v);ImGui::PopID();}
static void toggle(const char* title,unsigned& value){bool v=value!=0;if(ImGui::Checkbox(title,&v))value=v;}
static void turnView(NorthlightSurfaceFrame& frame,float yaw,float pitch){
    const float cy=std::cos(yaw*.01745329252f),sy=std::sin(yaw*.01745329252f),cp=std::cos(pitch*.01745329252f),sp=std::sin(pitch*.01745329252f);
    const float sign=frame.c[1][2]<0?-1.f:1.f;
    for(unsigned i=0;i<3;++i){const float r=frame.c[4][i],u=frame.c[5][i],f=frame.c[6][i]*sign,heading=f*cy+r*sy;
        frame.c[4][i]=r*cy-f*sy;frame.c[5][i]=u*cp-heading*sp;frame.c[6][i]=(heading*cp+u*sp)*sign;}
}
static void syntheticScene(const NorthlightSurfaceFrame& frame,std::vector<DWORD>& scene,std::vector<float>& depth){
    const float* eye=frame.c[7];
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const float sx=((x+.5f)*2/W-1)/frame.c[1][0],sy=(1-(y+.5f)*2/H)/frame.c[1][1];float ray[3];
        for(unsigned i=0;i<3;++i)ray[i]=-frame.c[6][i]+sx*frame.c[4][i]+sy*frame.c[5][i];
        float z=1000;DWORD colour=0xff9daeb7;
        if(ray[2]<-.0001f){const float t=-eye[2]/ray[2];if(t>.1f&&t<z){z=t;const float px=eye[0]+ray[0]*t,py=eye[1]+ray[1]*t;colour=(int(std::floor(px/2))+int(std::floor(py/2)))%2?0xff555e59:0xff79827a;}}
        // Upright cylinder and a distant rounded hill provide near/far occlusion witnesses.
        const float ox=eye[0]-6,oy=eye[1]-1.8f,a=ray[0]*ray[0]+ray[1]*ray[1],b=ox*ray[0]+oy*ray[1],disc=b*b-a*(ox*ox+oy*oy-.65f*.65f);
        if(a>.00001f&&disc>0){const float t=(-b-std::sqrt(disc))/a,pz=eye[2]+ray[2]*t;if(t>.1f&&t<z&&pz>0&&pz<5){z=t;colour=0xffa18d6b;}}
        const float hx=eye[0]-38,hy=eye[1]+6,hz=eye[2]+9,qa=ray[0]*ray[0]+ray[1]*ray[1]+ray[2]*ray[2],qb=hx*ray[0]+hy*ray[1]+hz*ray[2],qd=qb*qb-qa*(hx*hx+hy*hy+hz*hz-16*16);
        if(qd>0){const float t=(-qb-std::sqrt(qd))/qa;if(t>.1f&&t<z){z=t;colour=0xff586a74;}}
        scene[y*W+x]=colour;depth[y*W+x]=1000.f/999.9f*(1-.1f/z);
    }
}

struct Replay {
    bool loaded=false;unsigned mode=0,flags=0;NorthlightSurfaceFrame context{};NorthlightQuality::Settings quality;
    float intensity=1,blend=1,gate=1,heat=0,depthLow=0,depthHigh=1;
    std::vector<DWORD> scene,restore;std::vector<float> depth;
    void load(const fs::path& path){
        std::ifstream input(path,std::ios::binary);auto read=[&](void* p,size_t bytes){if(!input.read(static_cast<char*>(p),bytes))throw std::runtime_error("Truncated weather replay");};
        unsigned header[6];read(header,sizeof header);
        if((header[0]!=0x32504357&&header[0]!=0x33504357)||!header[1]||!header[2]||header[1]>8192||header[2]>8192||uint64_t(header[1])*header[2]*8>64*1024*1024||header[3]>3||header[4]>2||header[5]>63)throw std::runtime_error("Invalid weather replay header");
        W=header[1];H=header[2];mode=header[3];flags=header[5];quality.preset=NorthlightQuality::Preset(header[4]);read(&context,sizeof context);
        for(const auto& row:context.c)for(float v:row)if(!std::isfinite(v))throw std::runtime_error("Invalid replay constants");
        const unsigned settingCount=header[0]==0x33504357?19:16;unsigned values[19];read(values,settingCount*4);unsigned* fields[]={&quality.weather,&quality.rainLens,&quality.snowLens,&quality.rainLensStrength,&quality.snowLensStrength,&quality.rainDropSize,&quality.snowFlakeSize,&quality.rainLensSize,&quality.snowLensSize,&quality.rainFog,&quality.rainRivulets,&quality.snowFrost,&quality.snowFrostStrength,&quality.sandLens,&quality.sandLensStrength,&quality.sandLensSize,&quality.weatherWindDirection,&quality.weatherWindStrength,&quality.weatherCameraMotion};
        const unsigned lows[]={0,0,0,0,0,25,25,25,25,0,0,0,0,0,0,25,0,0,0},highs[]={1,1,1,100,100,300,300,300,300,2,1,1,100,1,100,300,359,300,1};
        for(unsigned i=0;i<settingCount;++i){if(values[i]<lows[i]||values[i]>highs[i])throw std::runtime_error("Invalid replay setting");*fields[i]=values[i];}
        float state[4];read(state,sizeof state);for(float v:state)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid replay weather state");
        intensity=state[0];blend=state[1];gate=state[2];heat=state[3];scene.resize(W*H);depth.resize(W*H);restore.resize(W*H);
        read(scene.data(),scene.size()*4);read(depth.data(),depth.size()*4);read(restore.data(),restore.size()*4);
        if(input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Unexpected weather replay data");
        for(float v:depth)if(!std::isfinite(v)||v<0||v>1.00001f)throw std::runtime_error("Invalid replay depth");
        const auto range=std::minmax_element(depth.begin(),depth.end());depthLow=*range.first;depthHigh=*range.second;loaded=true;
    }
};
static void restoreNameMask(IDirect3DDevice9* d,IDirect3DTexture9* restore){
    check(d->SetVertexShader(nullptr));check(d->SetPixelShader(nullptr));check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    check(d->SetDepthStencilSurface(nullptr));for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_LIGHTING,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})check(d->SetRenderState(state,FALSE));
    check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));check(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
    check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    check(d->SetTexture(0,restore));for(auto state:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})check(d->SetTextureStageState(0,state,D3DTOP_SELECTARG1));
    for(auto state:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})check(d->SetTextureStageState(0,state,D3DTA_TEXTURE));
    check(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));check(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));check(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})check(d->SetSamplerState(0,state,D3DTEXF_POINT));
    const D3DVIEWPORT9 viewport{0,0,W,H,0,1};check(d->SetViewport(&viewport));
    struct Vertex{float x,y,z,w,u,v;};const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));check(d->SetTexture(0,nullptr));
}

int main(int argc,char** argv){try{
    bool smoke=false;fs::path backend,background,capturePath;
    for(int i=1;i<argc;++i){if(std::string(argv[i])=="--smoke"){smoke=true;errorDialog=false;}
        else if(i+1<argc&&std::string(argv[i])=="--backend")backend=fs::u8path(argv[++i]);
        else if(i+1<argc&&std::string(argv[i])=="--background")background=fs::u8path(argv[++i]);
        else if(i+1<argc&&std::string(argv[i])=="--capture-bin")capturePath=fs::u8path(argv[++i]);
        else throw std::runtime_error("Unknown or incomplete argument");}
    wchar_t executable[32768];const DWORD pathLength=GetModuleFileNameW(nullptr,executable,32768);
    if(!pathLength||pathLength==32768)throw std::runtime_error("Cannot locate the lab output directory");
    fs::current_path(fs::path(executable).parent_path());
    Replay replay;if(!capturePath.empty())replay.load(capturePath);
    if(backend.empty()){wchar_t system[MAX_PATH];if(!GetSystemDirectoryW(system,MAX_PATH))throw std::runtime_error("System directory unavailable");backend=fs::path(system)/L"d3d9.dll";}
    auto module=LoadLibraryW(backend.c_str());if(!module)throw std::runtime_error("Cannot load x86 D3D9 backend");
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));
    if(!create)throw std::runtime_error("Backend has no Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);if(!api)throw std::runtime_error("D3D9 unavailable");
    WNDCLASSW wc{};wc.lpfnWndProc=procedure;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightWeatherLab";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    if(!RegisterClassW(&wc))throw std::runtime_error("Window registration failed");
    HWND window=CreateWindowW(wc.lpszClassName,L"Northlight Weather Lab",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1380,820,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)throw std::runtime_error("Window creation failed");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=1600;pp.BackBufferHeight=1000;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9* d=nullptr;check(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9* back=nullptr;check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
    IDirect3DTexture9 *target=nullptr,*scratch=nullptr;IDirect3DSurface9 *targetSurface=nullptr,*scratchSurface=nullptr;
    check(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&target,nullptr));check(target->GetSurfaceLevel(0,&targetSurface));
    check(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scratch,nullptr));check(scratch->GetSurfaceLevel(0,&scratchSurface));
    std::vector<DWORD> scene(W*H);std::vector<float> depths(W*H,1.f),clearDepth(W*H,1.f);
    // Synthetic colour/depth fixture: distant silhouettes, tiled ground, near pillar.
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const bool ground=y>H/2,pillar=x>W*.70&&x<W*.83&&y>H*.25;
        float z=ground?std::min(200.f,2.f*H/std::max(1.f,float(y)-H/2)):1000.f;
        DWORD colour=ground?((x/60+y/40)%2?0xff555e59:0xff79827a):0xff9daeb7;
        if(!ground&&y>H*.39+std::sin(float(x)*.012f)*35)colour=0xff586a74;
        if(pillar){colour=(x/12)%2?0xffac9978:0xff8a795f;z=1.5f;}
        scene[y*W+x]=colour;depths[y*W+x]=1000.f/999.9f*(1-.1f/z);
    }
    if(replay.loaded){scene=replay.scene;depths=replay.depth;}
    if(!background.empty()){
        auto bitmap=static_cast<HBITMAP>(LoadImageW(nullptr,background.c_str(),IMAGE_BITMAP,W,H,LR_LOADFROMFILE|LR_CREATEDIBSECTION));
        if(!bitmap)throw std::runtime_error("Cannot read background BMP");
        BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=W;bi.bmiHeader.biHeight=-int(H);bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        auto dc=GetDC(nullptr);const int rows=GetDIBits(dc,bitmap,0,H,scene.data(),&bi,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);DeleteObject(bitmap);
        if(rows!=H)throw std::runtime_error("Incomplete background BMP");
    }
    // A game target's alpha (or X8 padding) is not the preview image's opacity.
    for(auto& pixel:scene)pixel|=0xff000000u;
    IDirect3DSurface9* sourceSurface=nullptr;check(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sourceSurface,nullptr));
    D3DLOCKED_RECT upload{};check(sourceSurface->LockRect(&upload,nullptr,0));
    for(unsigned y=0;y<H;++y)std::memcpy(static_cast<char*>(upload.pBits)+y*upload.Pitch,scene.data()+y*W,W*4);
    check(sourceSurface->UnlockRect());
    auto* depth=texture(d,D3DFMT_R32F,depths.data(),4);auto* noDepth=texture(d,D3DFMT_R32F,clearDepth.data(),4);
    IDirect3DTexture9* maskRestore=replay.loaded&&(replay.flags&1)?texture(d,D3DFMT_A8R8G8B8,replay.restore.data(),4):nullptr;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed,int(NorthlightOverlayFont::RobotoMediumCompressedSize),17);
    ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowPadding={16,16};style.ItemSpacing={10,9};style.FramePadding={7,5};
    style.WindowRounding=style.ChildRounding=0;style.FrameRounding=4;style.Colors[ImGuiCol_WindowBg]={.075f,.082f,.098f,1};
    style.Colors[ImGuiCol_Text]={.93f,.94f,.96f,1};style.Colors[ImGuiCol_TextDisabled]={.65f,.69f,.74f,1};
    if(!ImGui_ImplDX9_Init(d))throw std::runtime_error("ImGui D3D9 initialization failed");
    NorthlightQuality::Settings q;std::vector<std::string> warnings;
    std::ifstream config("weather-lab.ini");if(config)q=NorthlightQuality::load(&config,nullptr,warnings);
    int mode=1;float intensity=.8f,heat=0,cameraYaw=0,cameraPitch=0;bool orbit=false;float orbitSpeed=30;
    float sceneYaw=10000,scenePitch=10000;
    bool paused=false,shelter=false,indoors=false,occlusion=background.empty(),precipitation=true,smooth=false;
    const fs::path profile=fs::absolute("weather-lab.ini");
    mode=std::clamp(int(GetPrivateProfileIntW(L"Preview",L"Mode",1,profile.c_str())),0,3);
    intensity=std::min(100u,GetPrivateProfileIntW(L"Preview",L"Intensity",80,profile.c_str()))*.01f;
    heat=std::min(100u,GetPrivateProfileIntW(L"Preview",L"Heat",0,profile.c_str()))*.01f;
    cameraYaw=std::clamp(float(int(GetPrivateProfileIntW(L"Preview",L"CameraYaw",0,profile.c_str()))),-180.f,180.f);
    cameraPitch=std::clamp(float(int(GetPrivateProfileIntW(L"Preview",L"CameraPitch",0,profile.c_str()))),-85.f,85.f);
    bool respectMask=true;
    if(replay.loaded){q=replay.quality;mode=int(replay.mode);intensity=replay.intensity;heat=replay.heat;paused=true;occlusion=true;indoors=!(replay.flags&16);shelter=(replay.flags&32)!=0;cameraYaw=cameraPitch=0;}
    std::string status=warnings.empty()?"Changes are applied immediately.":"Some invalid preset values were ignored.";
    auto save=[&](const char* filename){std::ofstream out(filename);const char* presets[]={"Quality","Balanced","Performance"};out<<"[Quality]\nPreset="<<presets[unsigned(q.preset)]<<'\n';
        for(const auto& k:NorthlightQuality::Keys)if(std::strstr(k.name,"Rain")==k.name||std::strstr(k.name,"Snow")==k.name||std::strstr(k.name,"Sand")==k.name||std::strstr(k.name,"Weather")==k.name)out<<k.name<<'='<<q.*k.field<<'\n';
        out<<"\n[Preview]\nMode="<<mode<<"\nIntensity="<<std::lround(intensity*100)<<"\nHeat="<<std::lround(heat*100)<<"\nCameraYaw="<<std::lround(cameraYaw)<<"\nCameraPitch="<<std::lround(cameraPitch)<<'\n';
        out.close();status=out?"Saved weather-lab.ini. See the guide to copy [Quality] keys.":"Save failed. Check folder permissions.";};
    NorthlightWeather::Tracker tracker;double seconds=replay.loaded?replay.context.c[1][3]:0;float gate=replay.loaded?replay.gate:0;int previousMode=-1;bool running=true;
    std::vector<unsigned char> baseline,rainPixels,snowPixels;unsigned uiFrames=0;
    auto weatherState=[&](){auto state=tracker.state();if(replay.loaded&&!smooth){state.kind=NorthlightWeather::Kind(mode);state.intensity=intensity;state.blend=mode==int(replay.mode)?replay.blend:1;}return state;};
    {
    NorthlightSurfaceRenderer surface(d);

    auto render=[&](){
        check(d->SetRenderTarget(0,targetSurface));
        auto state=weatherState();const bool snow=state.kind==NorthlightWeather::Kind::Snow,sand=state.kind==NorthlightWeather::Kind::Sand;
        const bool active=q.weather&&!indoors&&(state.kind==NorthlightWeather::Kind::Rain||snow||sand)&&state.blend>0;
        if(!active)gate=0;else if(!paused)gate+=(1-gate)*.1f;
        NorthlightSurfaceFrame c{};c.c[0][0]=1.f/W;c.c[0][1]=1.f/H;c.c[0][2]=.1f;c.c[0][3]=1000;
        c.c[1][0]=1.3f;c.c[1][1]=1.3f*float(W)/H;c.c[1][2]=-1;c.c[1][3]=float(seconds);c.c[2][1]=1;
        c.c[3][0]=active?gate*state.blend*(.35f+.65f*state.intensity):0;c.c[3][1]=snow?1:0;
        c.c[3][2]=(float(snow?q.snowFlakeSize:q.rainDropSize)-100)*.01f;
        c.c[4][1]=1;c.c[5][2]=1;c.c[6][0]=-1;c.c[7][2]=3;c.c[7][3]=1;
        c.c[8][0]=.3f;c.c[8][1]=.35f;c.c[8][2]=.4f;c.c[9][0]=c.c[9][1]=c.c[9][2]=.7f;
        if(replay.loaded){const float amount=c.c[3][0],kind=c.c[3][1],size=c.c[3][2];c=replay.context;c.c[1][3]=float(seconds);c.c[3][0]=amount;c.c[3][1]=kind;c.c[3][2]=size;
            if(mode==int(replay.mode))c.c[3][0]=active?replay.context.c[3][0]*(.35f+.65f*intensity)/(.35f+.65f*replay.intensity):0;}
        turnView(c,cameraYaw,cameraPitch);
        if(!replay.loaded&&background.empty()&&(sceneYaw!=cameraYaw||scenePitch!=cameraPitch)){
            syntheticScene(c,scene,depths);sceneYaw=cameraYaw;scenePitch=cameraPitch;
            check(sourceSurface->LockRect(&upload,nullptr,0));for(unsigned y=0;y<H;++y)std::memcpy(static_cast<char*>(upload.pBits)+y*upload.Pitch,scene.data()+y*W,W*4);check(sourceSurface->UnlockRect());
            check(depth->LockRect(0,&upload,nullptr,0));for(unsigned y=0;y<H;++y)std::memcpy(static_cast<char*>(upload.pBits)+y*upload.Pitch,depths.data()+y*W,W*4);check(depth->UnlockRect(0));
        }
        check(d->UpdateSurface(sourceSurface,nullptr,targetSurface,nullptr));
        surface.setMotion(c,unsigned(state.kind),q,seconds);
        const bool rotatedCapture=replay.loaded&&(cameraYaw!=0||cameraPitch!=0);
        auto* weatherDepth=occlusion&&!rotatedCapture?depth:noDepth;
        check(d->BeginScene());
        if(precipitation&&!sand)surface.precipitation(targetSurface,weatherDepth,W,H,c,q.preset,nullptr);
        if(active&&!shelter){
            if(sand&&q.sandLens)surface.sandLens(targetSurface,W,H,seconds,c.c[3][0]*float(q.sandLensStrength)*.01f,nullptr,state.intensity,float(q.sandLensSize)*.01f);
            if(!sand){const bool on=snow?q.snowLens:q.rainLens;const float frost=snow&&q.snowFrost?c.c[3][0]*float(q.snowFrostStrength)*.01f:0;
                if(on||frost>0)surface.lens(targetSurface,scratch,scratchSurface,W,H,snow,seconds,on?c.c[3][0]*float(snow?q.snowLensStrength:q.rainLensStrength)*.01f:0,nullptr,state.intensity,snow?heat:0,float(snow?q.snowLensSize:q.rainLensSize)*.01f,snow||q.rainRivulets!=0,frost);}}
        if(maskRestore&&respectMask)restoreNameMask(d,maskRestore);
        check(d->EndScene());surface.endFrame();if(surfaceError)throw std::runtime_error("Production weather pass failed; see console");
    };
    if(smoke&&replay.loaded){render();const auto data=pixels(d,targetSurface,W,H);
        for(unsigned i=0;i<W*H;++i){if(data[i*4+3]!=255)throw std::runtime_error("Replay background became transparent");
            if((replay.flags&1)&&(replay.restore[i]>>24)==255){DWORD pixel;std::memcpy(&pixel,data.data()+i*4,4);if((pixel&0xffffff)!=(scene[i]&0xffffff))throw std::runtime_error("Replay name mask changed protected RGB");}}
        capture("replay.bmp",data,W,H);std::puts("PASS weather lab: captured raw depth, projection, scene, opaque display and protected RGB loaded on GPU");}
    if(smoke&&!replay.loaded){
        std::vector<unsigned char> heavyFrost,turnOn,sandPixels;
        for(int test=0;test<29;++test){mode=1;q={};shelter=false;indoors=false;precipitation=true;heat=0;intensity=.8f;cameraYaw=cameraPitch=0;
            switch(test){case 0:mode=0;break;case 2:mode=2;break;case 3:mode=3;break;case 4:shelter=true;break;
            case 5:mode=2;heat=1;break;case 6:q.rainDropSize=q.rainLensSize=250;break;case 7:mode=2;q.snowFlakeSize=q.snowLensSize=250;break;
            case 8:q.weather=0;break;case 9:indoors=true;break;case 10:q.rainRivulets=0;break;
            case 11:mode=3;q.sandLens=0;break;case 12:mode=3;shelter=true;break;
            case 13:case 14:case 15:mode=2;q.snowLens=0;precipitation=false;q.snowFrostStrength=100;if(test==14)intensity=.15f;if(test==15)q.snowFrost=0;break;
            case 16:precipitation=false;intensity=1;q.rainLensStrength=100;break;
            case 17:q.weatherWindDirection=270;break;case 18:mode=2;cameraPitch=60;q.weatherWindDirection=270;break;
            case 19:mode=3;q.weatherWindDirection=270;break;case 20:mode=3;cameraYaw=30;cameraPitch=10;break;
            case 21:case 22:case 23:case 24:case 25:case 26:mode=test<23?1:test<25?2:3;q.weatherCameraMotion=test%2?1:0;break;
            case 27:q.weatherWindStrength=0;break;case 28:mode=3;occlusion=false;break;}
            tracker={};surface.reset();seconds=0;gate=0;
            const unsigned frames=test>=13&&test<=27?360:100;
            for(unsigned frame=0;frame<frames;++frame){tracker.frame({NorthlightWeather::Kind::None,0,0,true,NorthlightWeather::Kind(mode),intensity},.1f);seconds+=1./60;if(test>=21&&test<=26)cameraYaw=float(frame)*.25f;render();}
            auto data=pixels(d,targetSurface,W,H);if(test==0)baseline=data;else if(test==8||test==9||test==11||test==12||test==15){if(data!=baseline)throw std::runtime_error("Disabled camera coating or indoors changed pixels");}
            else if(data==baseline)throw std::runtime_error("Active precipitation produced no pixels");
            if(test==1)rainPixels=data;if(test==2)snowPixels=data;
            if(test==6&&data==rainPixels)throw std::runtime_error("Rain size did not change pixels");
            if((test==5||test==7)&&data==snowPixels)throw std::runtime_error("Snow size/heat did not change pixels");
            if(test==10&&data==rainPixels)throw std::runtime_error("Rivulet option did not change pixels");
            if(test==13)heavyFrost=data;
            if(test==3)sandPixels=data;
            if(test==28&&data!=sandPixels)throw std::runtime_error("Lens sand changed when scene depth occlusion was toggled");
            if(test==21||test==23||test==25)turnOn=data;
            if((test==22||test==24||test==26)&&turnOn==data)throw std::runtime_error("Camera response toggle produced identical weather");
            if(test==14){unsigned heavy=0,light=0;for(unsigned i=0;i<W*H;++i){heavy+=std::memcmp(heavyFrost.data()+i*4,baseline.data()+i*4,3)!=0;light+=std::memcmp(data.data()+i*4,baseline.data()+i*4,3)!=0;}
                if(heavy<=light)throw std::runtime_error("Blizzard did not grow the frost vignette");}
            if(test==13)for(unsigned y=H*2/5;y<H*3/5;++y)for(unsigned x=W*2/5;x<W*3/5;++x)if(std::memcmp(data.data()+(y*W+x)*4,baseline.data()+(y*W+x)*4,4))throw std::runtime_error("Frost obscured the clear centre");
            capture(("smoke-"+std::to_string(test)+".bmp").c_str(),data,W,H);
        }
        std::puts("PASS weather lab: rain rivulets, snow crystals, sand coating, intensity-driven frost with clear centre, shelter/heat/sizes/disabled");
        for(auto preset:{NorthlightQuality::Preset::Quality,NorthlightQuality::Preset::Balanced,NorthlightQuality::Preset::Performance}){
            q.preset=preset;q.rainDropSize=125;q.snowLensSize=175;q.rainRivulets=0;q.snowFrostStrength=85;q.sandLensStrength=45;q.weatherWindDirection=270;q.weatherWindStrength=175;q.weatherCameraMotion=0;save("smoke-settings.ini");
            std::ifstream saved("smoke-settings.ini");std::vector<std::string> issues;auto loaded=NorthlightQuality::load(&saved,nullptr,issues);
            if(!issues.empty()||loaded.preset!=preset||loaded.rainDropSize!=125||loaded.snowLensSize!=175||loaded.rainRivulets||loaded.snowFrostStrength!=85||loaded.sandLensStrength!=45||loaded.weatherWindDirection!=270||loaded.weatherWindStrength!=175||loaded.weatherCameraMotion)throw std::runtime_error("Preset round trip failed");
        }
        std::puts("PASS weather lab: all quality presets and weather sizes survive save/reload");
        status="Changes are applied immediately.";
        mode=3;q={};shelter=indoors=false;precipitation=true;occlusion=background.empty();heat=0;intensity=.8f;tracker={};seconds=0;gate=0;cameraYaw=cameraPitch=0;surface.reset();
    }else if(!smoke)ShowWindow(window,SW_SHOW);
    auto last=std::chrono::steady_clock::now();
    while(running){
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)running=false;TranslateMessage(&message);DispatchMessageW(&message);}if(!running)break;
        if(IsIconic(window)){Sleep(30);last=std::chrono::steady_clock::now();continue;}
        if(d->TestCooperativeLevel()!=D3D_OK)throw std::runtime_error("Graphics device lost. Restart Weather Lab.");
        const auto now=std::chrono::steady_clock::now();const float dt=smoke?1.f/60:std::clamp(std::chrono::duration<float>(now-last).count(),.0001f,.1f);last=now;
        RECT client{};GetClientRect(window,&client);const float width=float(client.right),height=float(client.bottom);if(width<1||height<1)continue;
        if(pp.BackBufferWidth!=unsigned(width)||pp.BackBufferHeight!=unsigned(height)){
            surface.reset();ImGui_ImplDX9_InvalidateDeviceObjects();drop(back);drop(targetSurface);drop(target);drop(scratchSurface);drop(scratch);
            pp.BackBufferWidth=unsigned(width);pp.BackBufferHeight=unsigned(height);check(d->Reset(&pp));check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
            check(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&target,nullptr));check(target->GetSurfaceLevel(0,&targetSurface));
            check(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scratch,nullptr));check(scratch->GetSurfaceLevel(0,&scratchSurface));
        }
        io.DisplaySize={width,height};io.DeltaTime=dt;ImGui_ImplDX9_NewFrame();ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Weather lab",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        const float panel=width<850?std::max(245.f,width*.42f):340.f;
        ImGui::BeginChild("Controls",{panel,0});ImGui::SeparatorText("Northlight Weather Lab");
        ImGui::TextWrapped("Production weather passes. No game process or client assets required.");
        ImGui::Spacing();ImGui::SetNextItemWidth(-1);ImGui::Combo("##Weather",&mode,"Clear\0Rain\0Snow\0Sandstorm\0");
        ImGui::TextUnformatted("Intensity");ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##Intensity",&intensity,0,1,"%.2f");
        if(ImGui::Button("Save preset"))save("weather-lab.ini");ImGui::SameLine();if(ImGui::Button("Defaults")){q={};intensity=.8f;heat=0;surface.reset();}
        ImGui::TextWrapped("%s",status.c_str());
        toggle("Weather enabled",q.weather);ImGui::Checkbox("Smooth transitions",&smooth);
        ImGui::Checkbox("Camera under shelter",&shelter);ImGui::Checkbox("Fully indoors (no sky)",&indoors);ImGui::Checkbox("Pause",&paused);
        if(ImGui::Button("Restart simulation")){surface.reset();tracker={};previousMode=-1;seconds=0;gate=0;}
        ImGui::SeparatorText("Wind and camera");
        slider("Wind heading (world degrees)",q.weatherWindDirection,0,359);
        slider("Wind strength (%)",q.weatherWindStrength,0,300);
        toggle("React to camera turns",q.weatherCameraMotion);
        if(ImGui::CollapsingHeader("Camera controls")){
        ImGui::TextUnformatted("Camera yaw / pitch (degrees)");ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##Yaw",&cameraYaw,-180,180,"Yaw %.1f");
        ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##Pitch",&cameraPitch,-85,85,"Pitch %.1f");
        ImGui::Checkbox("Auto rotate camera",&orbit);
        if(orbit){ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##OrbitSpeed",&orbitSpeed,-90,90,"Turn %.0f deg/s");}
        if(ImGui::Button("Reset view")){cameraYaw=cameraPitch=0;orbit=false;}}
        ImGui::TextWrapped("Drag the preview to turn. Disabling turn response removes camera inertia; wind still projects from the viewing direction.");
        ImGui::SeparatorText("Particles in the scene");ImGui::Checkbox("Show precipitation",&precipitation);ImGui::Checkbox(replay.loaded?"Captured depth occlusion":"Synthetic depth occlusion",&occlusion);
        if(maskRestore)ImGui::Checkbox("Respect captured name mask",&respectMask);
        ImGui::SetNextItemWidth(-1);int preset=int(q.preset);if(ImGui::Combo("##Quality",&preset,"Quality (3 layers)\0Balanced (3 layers)\0Performance (2 layers)\0"))q.preset=NorthlightQuality::Preset(preset);
        if(mode==1)slider("Rain particle size (%)",q.rainDropSize,25,300);if(mode==2)slider("Snow particle size (%)",q.snowFlakeSize,25,300);
        if(mode==1||mode==2){ImGui::SeparatorText("Weather on the camera");
            if(mode==1){toggle("Rain on camera",q.rainLens);slider("Rain strength (%)",q.rainLensStrength,0,100);slider("Droplet size (%)",q.rainLensSize,25,300);toggle("Merge drops into rivulets",q.rainRivulets);}
            else{toggle("Snow on camera",q.snowLens);slider("Snow strength (%)",q.snowLensStrength,0,100);slider("Clump size (%)",q.snowLensSize,25,300);
                toggle("Frost at screen edges",q.snowFrost);slider("Frost strength (%)",q.snowFrostStrength,0,100);
                ImGui::TextUnformatted("Fire heat");ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##Heat",&heat,0,1,"%.2f");}}
        if(mode==3){ImGui::SeparatorText("Sand on the lens");toggle("Sand on lens",q.sandLens);slider("Sand strength (%)",q.sandLensStrength,0,100);slider("Grain size (%)",q.sandLensSize,25,300);}
        if(ImGui::CollapsingHeader("World fog (readout only)"))slider("Weather fog",q.rainFog,0,2);
        ImGui::EndChild();ImGui::SameLine();ImGui::BeginChild("Preview",{0,0});
        const char* names[]={"Clear","Rain","Snow","Sand"};ImGui::SeparatorText(names[mode]);
        const auto available=ImGui::GetContentRegionAvail();const float scale=std::max(.01f,std::min(available.x/W,std::max(1.f,available.y-145)/H));
        ImGui::Image(reinterpret_cast<ImTextureID>(target),{W*scale,H*scale});
        if(ImGui::IsItemHovered()&&ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
            cameraYaw=std::remainder(cameraYaw+io.MouseDelta.x*.25f,360.f);cameraPitch=std::clamp(cameraPitch-io.MouseDelta.y*.25f,-85.f,85.f);orbit=false;
        }
        if(replay.loaded&&(cameraYaw!=0||cameraPitch!=0))ImGui::TextWrapped("Captured frame stays fixed. Depth occlusion is suspended while the preview camera is rotated; Reset view restores the captured projection.");
        if(replay.loaded){ImGui::TextWrapped("Captured scene, raw depth and projection. Camera optics restart from fresh masses. Name mask uses resolved coverage; MSAA edges are approximate.");
            ImGui::TextWrapped("Raw depth %.6f..%.6f; viewport %.3f..%.3f; near/far %.2f / %.1f",replay.depthLow,replay.depthHigh,replay.context.c[2][0],replay.context.c[2][0]+1/replay.context.c[2][1],replay.context.c[0][2],replay.context.c[0][3]);
            if(replay.flags&4)ImGui::TextWrapped("Captured HDR scene converted to SDR for this preview.");
            if(!(replay.flags&8)||((replay.flags&2)&&!(replay.flags&1)))ImGui::TextWrapped("Incomplete capture: some production inputs were not exported. See the capture JSON.");}
        else ImGui::TextWrapped("%s. Preview: supplemental particles + camera optics. Native WoW particles and world lighting are not rendered here.",background.empty()?"Synthetic background and depth":"Custom background; depth is synthetic");
        const auto shown=weatherState();const auto effects=NorthlightWeatherEffects::derive(shown,q.weather,q.rainFog);
        ImGui::Text("State: %s  |  blend %.2f  |  %.1f s  |  %.0f FPS",NorthlightWeather::kindName(shown.kind),shown.blend,seconds,io.Framerate);
        if(mode==1||mode==2)ImGui::TextWrapped("Camera particles: %u | merges since restart: %u | frost %.0f%%",surface.lensParticleCount(),surface.lensMergeCount(),surface.frostCoverage()*100);
        ImGui::TextWrapped("Camera effects sit on the lens; scene geometry does not clip them.");
        const auto& flow=surface.weatherFlow();ImGui::TextWrapped("Lens wind: right %.2f | down %.2f | into glass %.2f. Turn response: %s",flow.x,flow.y,-flow.z,q.weatherCameraMotion?"on":"off");
        ImGui::TextWrapped("World coefficients (readout only): haze x%.2f, sun x%.2f, shafts x%.2f, shadow soften %.2f",effects.hazeTauScale(),effects.discGain(),effects.shaftGain(),effects.shadowSoften());
        if(ImGui::Button("Save preview BMP")){capture("preview.bmp",pixels(d,targetSurface,W,H),W,H);status="Saved preview.bmp next to the preset.";}
        ImGui::EndChild();ImGui::End();
        NorthlightWeather::Sample sample{};sample.memory=true;sample.memoryKind=NorthlightWeather::Kind(mode);sample.memoryIntensity=intensity;
        if(!paused){seconds+=dt;if(orbit)cameraYaw=std::remainder(cameraYaw+orbitSpeed*dt,360.f);if(!smooth&&previousMode!=mode){tracker={};surface.reset();gate=0;for(unsigned i=0;i<41;++i)tracker.frame(sample,.1f);}else tracker.frame(sample,dt);previousMode=mode;}
        render();ImGui::Render();check(d->SetRenderTarget(0,back));check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff131519,1,0));
        check(d->BeginScene());ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());check(d->EndScene());
        if(smoke&&++uiFrames==60){capture("desktop.bmp",pixels(d,back,unsigned(width),unsigned(height)),unsigned(width),unsigned(height));SetWindowPos(window,nullptr,0,0,760,960,SWP_NOMOVE|SWP_NOZORDER);}
        if(smoke&&uiFrames==64){capture("compact.bmp",pixels(d,back,unsigned(width),unsigned(height)),unsigned(width),unsigned(height));running=false;}
        RECT sourceRect{0,0,LONG(width),LONG(height)};check(d->Present(&sourceRect,nullptr,nullptr,nullptr));
    }
    }
    ImGui_ImplDX9_Shutdown();ImGui::DestroyContext();
    drop(maskRestore);drop(sourceSurface);drop(depth);drop(noDepth);drop(targetSurface);drop(target);drop(scratchSurface);drop(scratch);drop(back);drop(d);drop(api);
    FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Weather lab: %s\n",error.what());if(errorDialog)MessageBoxA(nullptr,error.what(),"Northlight Weather Lab",MB_OK|MB_ICONERROR);return 1;}}
