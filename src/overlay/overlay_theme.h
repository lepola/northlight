#pragma once
// The settings window's look: what each colour means, spacing, scale and the embedded font. Shared by the in-game
// overlay (settings_overlay.cpp), the window's contents (settings_panel.cpp) and the offline preview
// (scripts/dev_tuning/settings_menu_preview.cpp), so all three draw the same window.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "imgui.h"
#include "overlay_font_roboto.h"

namespace NorthlightOverlayTheme {
// One meaning per colour. Amber marks unsaved edits and nothing else, so headings stay neutral and the restart
// mark has a hue of its own.
namespace Color {
inline const ImVec4 Accent{0.40f,0.70f,1.00f,1.f},Edited{1.00f,0.74f,0.32f,1.f},Muted{0.62f,0.65f,0.70f,1.f},Good{0.48f,0.84f,0.54f,1.f},
    Bad{1.00f,0.48f,0.43f,1.f},Restart{0.76f,0.68f,1.00f,1.f},Heading{0.95f,0.96f,0.98f,1.f},
    Card{1.f,1.f,1.f,0.035f},Primary{0.20f,0.42f,0.70f,1.f},PrimaryHovered{0.26f,0.50f,0.80f,1.f};
}

// 1 at 1080p, in quarter steps up to 4 (8K); never below 1 so text stays readable on small windows.
inline float uiScale(unsigned backBufferHeight){return std::clamp(std::round(float(backBufferHeight)/1080.f*4.f)/4.f,1.f,4.f);}
inline float fontPixels(float scale){return std::round(16.f*scale);}
inline float titlePixels(float scale){return std::round(21.f*scale);}
// The larger face for page names; null when only a fallback font could be loaded.
inline ImFont* titleFont(){const auto& fonts=ImGui::GetIO().Fonts->Fonts;return fonts.Size>1?fonts[1]:nullptr;}

// Dark, compact, a little translucent so the scene behind stays readable.
inline void applyStyle(float scale){
    ImGuiStyle style;ImGui::StyleColorsDark(&style);
    style.WindowRounding=8;style.ChildRounding=7;style.FrameRounding=5;style.PopupRounding=6;style.GrabRounding=4;style.TabRounding=4;style.ScrollbarRounding=6;
    style.WindowPadding=ImVec2(12,10);style.FramePadding=ImVec2(8,4);style.ItemSpacing=ImVec2(8,6);style.ItemInnerSpacing=ImVec2(6,4);
    style.IndentSpacing=16;style.ScrollbarSize=12;style.GrabMinSize=10;style.WindowBorderSize=1;style.ChildBorderSize=0;style.FrameBorderSize=0;style.SeparatorTextBorderSize=1;
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
    c[ImGuiCol_TableRowBg]=ImVec4(0,0,0,0);c[ImGuiCol_TableRowBgAlt]=ImVec4(0,0,0,0);c[ImGuiCol_ModalWindowDimBg]=ImVec4(0,0,0,0.35f);
    c[ImGuiCol_TextDisabled]=ImVec4(0.46f,0.48f,0.52f,1);
    style.ScaleAllSizes(scale);
    ImGui::GetStyle()=style;
}

// The embedded Roboto at this scale, as the body face and then the title face: the same text on Windows and under
// Wine, whose font folder may be empty. False when the body face cannot be added; the caller then picks a fallback.
inline bool addEmbeddedFont(float scale){
    static const ImWchar ranges[]={0x0020,0x00FF,0};
    auto add=[&](float pixels){
        ImFontConfig config;config.OversampleH=2;config.OversampleV=1;std::snprintf(config.Name,sizeof config.Name,"Roboto-Medium");
        return ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed,int(NorthlightOverlayFont::RobotoMediumCompressedSize),
                                                                     pixels,&config,ranges)!=nullptr;
    };
    if(!add(fontPixels(scale)))return false;
    add(titlePixels(scale));
    return true;
}
} // namespace NorthlightOverlayTheme
