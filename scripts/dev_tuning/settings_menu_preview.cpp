// Draws the real settings window (src/overlay/settings_panel.cpp) to pictures without the game or a GPU: the
// production settings service on a scratch folder of INI files, Dear ImGui drawn by a small software rasteriser.
// Built and run by settings_menu_preview.py.
//
//     settings_menu_preview <config folder> <output folder> <back buffer height> <step>...
//
// Steps run in order:  click=<label>  drag=<label>:<dx>,<dy>  type=<text>  scroll=<wheel notches>  wait=<frames>  shot=<name>
// A label is what the item shows ("Water", "Save"); ImGui's test hooks report where each one was drawn.
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "overlay_theme.h"
#include "settings_panel.h"
#include "settings_service.h"

void northlightImGuiAssert(const char* expression,const char* file,int line){
    std::fprintf(stderr,"ImGui assertion %s (%s:%d)\n",expression,file,line);
    std::exit(3);
}

namespace {
std::map<ImGuiID,ImRect> boxes;            // every item drawn this frame
std::map<std::string,ImGuiID> labelled;    // the last item drawn with each label (some report their label before their box)
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& box,const ImGuiLastItemData*){boxes[id]=box;}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){
    if(!label)return;
    std::string name=label;if(const auto hidden=name.find("##");hidden!=name.npos)name.resize(hidden);
    if(!name.empty())labelled[name]=id;
}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return nullptr;}

namespace {
struct Picture {
    int width,height;std::vector<float> rgb;
    Picture(int w,int h):width(w),height(h),rgb(std::size_t(w)*h*3){}
    // A plain dusk-to-ground gradient stands in for the game scene behind the translucent window.
    void backdrop(){
        for(int y=0;y<height;++y){
            const float t=float(y)/float(height-1);
            const float top[3]={0.30f,0.36f,0.48f},bottom[3]={0.20f,0.24f,0.18f};
            for(int x=0;x<width;++x)for(int c=0;c<3;++c)rgb[(std::size_t(y)*width+x)*3+c]=top[c]+(bottom[c]-top[c])*t;
        }
    }
    void save(const std::filesystem::path& path)const{
        std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        for(float v:rgb)out.put(char(std::clamp(int(v*255.f+0.5f),0,255)));
    }
};

struct Texture { const unsigned char* pixels=nullptr;int width=0,height=0; };
// The font atlas is white with coverage in alpha; ImGui samples it bilinearly.
float coverage(const Texture& t,float u,float v){
    const float x=u*t.width-0.5f,y=v*t.height-0.5f;
    const int x0=int(std::floor(x)),y0=int(std::floor(y));const float fx=x-x0,fy=y-y0;
    auto at=[&](int px,int py){return t.pixels[(std::size_t(std::clamp(py,0,t.height-1))*t.width+std::clamp(px,0,t.width-1))*4+3]/255.f;};
    return (at(x0,y0)*(1-fx)+at(x0+1,y0)*fx)*(1-fy)+(at(x0,y0+1)*(1-fx)+at(x0+1,y0+1)*fx)*fy;
}
void triangle(Picture& p,const Texture& tex,const ImVec4& clip,const ImDrawVert& a,const ImDrawVert& b,const ImDrawVert& c){
    const float area=(b.pos.x-a.pos.x)*(c.pos.y-a.pos.y)-(b.pos.y-a.pos.y)*(c.pos.x-a.pos.x);
    if(area==0)return;
    const int x0=std::max({0,int(std::floor(clip.x)),int(std::floor(std::min({a.pos.x,b.pos.x,c.pos.x})))});
    const int y0=std::max({0,int(std::floor(clip.y)),int(std::floor(std::min({a.pos.y,b.pos.y,c.pos.y})))});
    const int x1=std::min({p.width,int(std::ceil(clip.z)),int(std::ceil(std::max({a.pos.x,b.pos.x,c.pos.x})))});
    const int y1=std::min({p.height,int(std::ceil(clip.w)),int(std::ceil(std::max({a.pos.y,b.pos.y,c.pos.y})))});
    const ImVec4 ca=ImGui::ColorConvertU32ToFloat4(a.col),cb=ImGui::ColorConvertU32ToFloat4(b.col),cc=ImGui::ColorConvertU32ToFloat4(c.col);
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
        const float px=x+0.5f,py=y+0.5f;
        const float wa=((b.pos.x-px)*(c.pos.y-py)-(b.pos.y-py)*(c.pos.x-px))/area;
        const float wb=((c.pos.x-px)*(a.pos.y-py)-(c.pos.y-py)*(a.pos.x-px))/area;
        const float wc=1-wa-wb;
        if(wa<0||wb<0||wc<0)continue;
        const float u=a.uv.x*wa+b.uv.x*wb+c.uv.x*wc,v=a.uv.y*wa+b.uv.y*wb+c.uv.y*wc;
        const float alpha=(ca.w*wa+cb.w*wb+cc.w*wc)*coverage(tex,u,v);
        const float colour[3]={ca.x*wa+cb.x*wb+cc.x*wc,ca.y*wa+cb.y*wb+cc.y*wc,ca.z*wa+cb.z*wb+cc.z*wc};
        float* out=&p.rgb[(std::size_t(y)*p.width+x)*3];
        for(int i=0;i<3;++i)out[i]=out[i]*(1-alpha)+colour[i]*alpha;
    }
}
void rasterise(Picture& p,const Texture& tex,const ImDrawData* data){
    for(int n=0;n<data->CmdListsCount;++n){
        const ImDrawList* list=data->CmdLists[n];
        for(const ImDrawCmd& cmd:list->CmdBuffer){
            if(cmd.UserCallback)continue;
            const ImDrawIdx* index=list->IdxBuffer.Data+cmd.IdxOffset;
            const ImDrawVert* vertex=list->VtxBuffer.Data+cmd.VtxOffset;
            for(unsigned i=0;i+2<cmd.ElemCount;i+=3)triangle(p,tex,cmd.ClipRect,vertex[index[i]],vertex[index[i+1]],vertex[index[i+2]]);
        }
    }
}

struct Preview {
    NorthlightOverlay::SettingsPanel panel;
    float scale;int width,height;Texture font;
    ImVec2 mouse{-1,-1};
    Preview(std::shared_ptr<NorthlightSettings::Service> service,float uiScale,int w,int h):panel(std::move(service),"Ctrl+Shift+F11"),scale(uiScale),width(w),height(h){
        ImGuiIO& io=ImGui::GetIO();
        io.IniFilename=nullptr;io.LogFilename=nullptr;io.ConfigWindowsMoveFromTitleBarOnly=true;
        io.DisplaySize=ImVec2(float(w),float(h));
        if(!NorthlightOverlayTheme::addEmbeddedFont(scale)){std::fprintf(stderr,"the embedded font did not load\n");std::exit(2);}
        NorthlightOverlayTheme::applyStyle(scale);
        unsigned char* pixels=nullptr;io.Fonts->GetTexDataAsRGBA32(&pixels,&font.width,&font.height);font.pixels=pixels;
        ImGui::GetCurrentContext()->TestEngineHookItems=true;
        panel.opened();
    }
    void frame(){
        ImGuiIO& io=ImGui::GetIO();io.DeltaTime=1/60.f;
        boxes.clear();labelled.clear();
        ImGui::NewFrame();
        panel.draw(scale);
        ImGui::Render();
    }
    // The service answers from its worker thread; real time has to pass between frames.
    void frames(int count){for(int i=0;i<count;++i){frame();std::this_thread::sleep_for(std::chrono::milliseconds(4));}}
    // Moves the pointer to the centre of the item with this label. False when no such item is on screen.
    bool pointAt(const std::string& label){
        frames(2);
        const auto found=labelled.find(label);
        if(found==labelled.end())return false;
        const auto box=boxes.find(found->second);
        if(box==boxes.end()||box->second.GetWidth()<=0)return false;
        mouse=box->second.GetCenter();
        ImGui::GetIO().AddMousePosEvent(mouse.x,mouse.y);frames(2);
        return true;
    }
    bool click(const std::string& label){
        if(!pointAt(label))return false;
        ImGuiIO& io=ImGui::GetIO();
        io.AddMouseButtonEvent(0,true);frames(2);
        io.AddMouseButtonEvent(0,false);frames(6);
        return true;
    }
    // Presses at the item's centre and releases after moving by (dx,dy) pixels at scale 1.
    bool drag(const std::string& label,float dx,float dy){
        if(!pointAt(label))return false;
        ImGuiIO& io=ImGui::GetIO();
        io.AddMouseButtonEvent(0,true);frames(2);
        for(int step=1;step<=8;++step){io.AddMousePosEvent(mouse.x+dx*scale*step/8,mouse.y+dy*scale*step/8);frames(2);}
        io.AddMouseButtonEvent(0,false);frames(12);
        return true;
    }
    void shot(const std::filesystem::path& path){
        frames(2);
        Picture picture(width,height);picture.backdrop();
        rasterise(picture,font,ImGui::GetDrawData());
        picture.save(path);
    }
};
} // namespace

int main(int argc,char** argv){
    if(argc<4){std::fprintf(stderr,"usage: settings_menu_preview <config folder> <output folder> <back buffer height> <step>...\n");return 2;}
    const std::filesystem::path config=argv[1],output=argv[2];
    const float scale=NorthlightOverlayTheme::uiScale(unsigned(std::atoi(argv[3])));
    ImGui::CreateContext();
    int status=0;
    {
        // The window opens at (48,96) and 820x700, scaled; leave a margin of scene around it.
        Preview preview(std::make_shared<NorthlightSettings::Service>(config,true),scale,int(920*scale),int(840*scale));
        preview.frames(120);
        for(int i=4;i<argc&&!status;++i){
            const std::string step=argv[i];const auto equals=step.find('=');
            const std::string verb=step.substr(0,equals),value=equals==step.npos?"":step.substr(equals+1);
            if(verb=="click"){if(!preview.click(value)){std::fprintf(stderr,"nothing labelled \"%s\" is drawn\n",value.c_str());status=1;}}
            else if(verb=="drag"){
                const auto colon=value.rfind(':');float dx=0,dy=0;
                if(colon==value.npos||std::sscanf(value.c_str()+colon+1,"%f,%f",&dx,&dy)!=2){std::fprintf(stderr,"drag needs <label>:<dx>,<dy>\n");status=2;}
                else if(!preview.drag(value.substr(0,colon),dx,dy)){std::fprintf(stderr,"nothing labelled \"%s\" is drawn\n",value.substr(0,colon).c_str());status=1;}
            }
            else if(verb=="type"){ImGui::GetIO().AddInputCharactersUTF8(value.c_str());preview.frames(4);}
            else if(verb=="scroll"){ImGui::GetIO().AddMouseWheelEvent(0,-float(std::atof(value.c_str())));preview.frames(4);}
            else if(verb=="wait")preview.frames(std::atoi(value.c_str()));
            else if(verb=="shot")preview.shot(output/(value+".ppm"));
            else{std::fprintf(stderr,"unknown step \"%s\"\n",step.c_str());status=2;}
        }
    }
    ImGui::DestroyContext();
    return status;
}
