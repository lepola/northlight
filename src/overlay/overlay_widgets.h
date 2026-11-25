#pragma once
// The pieces the settings window is assembled from. They only draw: none of them knows about settings, drafts or
// the service (settings_panel.cpp supplies those). Sizes follow the current font and style, so they scale with the UI.
#include <algorithm>
#include "imgui.h"
#include "overlay_theme.h"

namespace NorthlightOverlayWidgets {
namespace Color=NorthlightOverlayTheme::Color;
inline ImU32 u32(const ImVec4& c){return ImGui::ColorConvertFloat4ToU32(c);}
inline float buttonWidth(const char* label){return ImGui::CalcTextSize(label).x+ImGui::GetStyle().FramePadding.x*2;}
// Right-aligns the next item of the given width on the current line.
inline void alignRight(float width){
    const float x=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-width;
    ImGui::SameLine(0,0);ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),x));
}
// Explanatory text: muted and wrapped, so it never competes with the controls.
inline void hint(const char* text){
    ImGui::PushTextWrapPos(0);ImGui::PushStyleColor(ImGuiCol_Text,Color::Muted);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();ImGui::PopTextWrapPos();
}
// The top of a page: its name in the title font and one line saying what the page is for.
inline void pageHeader(const char* title,const char* about){
    ImFont* font=NorthlightOverlayTheme::titleFont();
    if(font)ImGui::PushFont(font);
    ImGui::TextUnformatted(title);
    if(font)ImGui::PopFont();
    if(about&&*about)hint(about);
    ImGui::Spacing();
}
// A card holds one group of related settings on a slightly raised panel. Always pair with endCard(); draw the
// contents only when beginCard() returns true (a card scrolled out of view keeps its size and skips its contents).
inline bool beginCard(const char* id){
    ImGui::PushStyleColor(ImGuiCol_ChildBg,Color::Card);
    return ImGui::BeginChild(id,ImVec2(0,0),ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,
                             ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
}
inline void endCard(){ImGui::EndChild();ImGui::PopStyleColor();}
inline void cardTitle(const char* title){ImGui::AlignTextToFramePadding();ImGui::TextColored(Color::Heading,"%s",title);}
// The state mark before a row's label: filled for a value set here, hollow for an inherited one.
inline void dot(const ImVec4& colour,bool filled){
    const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.15f);
    const ImVec2 p=ImGui::GetCursorScreenPos(),c(p.x+r+1,p.y+h*0.5f);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    if(filled)dl->AddCircleFilled(c,r,u32(colour));else dl->AddCircle(c,r,u32(colour),0,std::max(1.f,h/24.f));
    ImGui::Dummy(ImVec2(2*r+2,h));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
}
// The room a dot takes, for a label that has none but must line up with rows that do.
inline void dotSpace(){
    const float h=ImGui::GetFrameHeight(),r=std::max(2.5f,h*0.15f);
    ImGui::Dummy(ImVec2(2*r+2,h));ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
}
// A short tag on a rounded tile, such as the "R" on settings that need a restart.
inline void badge(const char* text,const ImVec4& colour){
    const ImVec2 size=ImGui::CalcTextSize(text),pad(ImGui::GetFontSize()*0.3f,ImGui::GetFontSize()*0.08f);
    const float h=ImGui::GetFrameHeight();
    const ImVec2 p=ImGui::GetCursorScreenPos(),a(p.x,p.y+(h-size.y)*0.5f-pad.y),b(p.x+size.x+2*pad.x,p.y+(h+size.y)*0.5f+pad.y);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(a,b,u32(ImVec4(colour.x,colour.y,colour.z,0.16f)),ImGui::GetStyle().FrameRounding);
    dl->AddText(ImVec2(a.x+pad.x,p.y+(h-size.y)*0.5f),u32(colour),text);
    ImGui::Dummy(ImVec2(b.x-a.x,h));
}
// A caption between groups of sidebar pages.
inline void sidebarCaption(const char* text){
    ImGui::Dummy(ImVec2(0,ImGui::GetFontSize()*0.35f));
    ImGui::TextColored(Color::Muted,"%s",text);
}
// The one action a window most wants pressed. Highlighted only while it has something to do.
inline bool primaryButton(const char* label,bool ready){
    if(ready){ImGui::PushStyleColor(ImGuiCol_Button,Color::Primary);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,Color::PrimaryHovered);}
    const bool pressed=ImGui::Button(label);
    if(ready)ImGui::PopStyleColor(2);
    return pressed;
}
} // namespace NorthlightOverlayWidgets
