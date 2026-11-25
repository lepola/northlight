#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Ctrl+Shift+F11 settings overlay (src/overlay/, vendored Dear ImGui in src/imgui/).

Static: the DLL build compiles the overlay and ImGui translation units with the overlay's ImGui config;
the overlay never touches the game client (no process-memory or protection calls, no client address
literals, no CVar functions); [Renderer] SettingsOverlay=0 creates nothing and hooks no window; the
window procedure forwards everything it does not swallow and restores the previous procedure only while
it is still the current one; renderer.cpp draws once per presented frame from finishFrame (both Present
paths), inside a RawScope and SavedState, releases the overlay's device objects before Reset and the
overlay before the extension device; the ImGui licence ships in the packages.
Behaviour (native clang++, tests/test_settings_overlay.cpp): overlay_input.h routing (mouse moves and
key-ups always reach the game, hotkey swallowed, presses only over the window) and settings_draft.h.
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
HERE = Path(__file__).resolve().parent

problems = []
def check(ok, what):
    if not ok:
        problems.append(what)

def uncomment(text):
    return re.sub(r'/\*.*?\*/', '', re.sub(r'//[^\n]*', '', text), flags=re.S)

OVERLAY = ['settings_overlay.h', 'settings_overlay.cpp', 'settings_panel.h', 'settings_panel.cpp', 'settings_draft.h', 'overlay_input.h', 'overlay_theme.h',
           'overlay_widgets.h', 'northlight_imconfig.h']
IMGUI_TUS = ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']

# build_renderer.py: every overlay/ImGui TU is in the DLL, all built with northlight_imconfig.h.
build = (fp.SCRIPTS / 'build_renderer.py').read_text()
for name in ['settings_overlay.cpp', 'settings_panel.cpp', *IMGUI_TUS]:
    check(f"'{name}'" in build, f'build_renderer.py does not compile {name}')
check('-DIMGUI_USER_CONFIG="northlight_imconfig.h"' in build, 'build_renderer.py: IMGUI_USER_CONFIG missing')
check('imgui_impl_win32' not in build and '-limm32' not in build and '-ldwmapi' not in build, 'no Win32 platform backend or its libraries')
config = fp.src('northlight_imconfig.h').read_text()
for define in ['IMGUI_DISABLE_DEMO_WINDOWS', 'IMGUI_DISABLE_WIN32_FUNCTIONS', 'IMGUI_DISABLE_FILE_FUNCTIONS', 'IM_ASSERT']:
    check(f'#define {define}' in config, f'northlight_imconfig.h: {define}')

# Nothing in the overlay reads, writes, patches or calls the game client.
for name in OVERLAY:
    text = uncomment(fp.src(name).read_text())
    for banned in ['ReadProcessMemory', 'WriteProcessMemory', 'VirtualProtect', 'VirtualAlloc', 'GetModuleHandle', 'GetProcAddress', 'Detour']:
        check(banned not in text, f'{name}: {banned}')
    check(re.search(r'cvar', text, re.I) is None, f'{name}: refers to a CVar')
    addresses = re.findall(r'\b0x0*[4-9][0-9A-Fa-f]{5}\b', text)
    check(not addresses, f'{name}: client address literal(s) {addresses}')

# SettingsOverlay=0: read once at device creation; create() returns before hooking the window.
renderer = fp.src('renderer.cpp').read_text()
check(re.search(r'GetPrivateProfileIntW\(L"Renderer",L"SettingsOverlay",1,config\)', renderer) is not None, 'renderer.cpp: [Renderer] SettingsOverlay read')
check('overlay=NorthlightOverlay::create(ext,on,hotkey,settingsService,' in renderer, 'renderer.cpp: overlay created with the SettingsOverlay gate and key')
check('GetPrivateProfileStringW(L"Renderer",L"SettingsOverlayKey"' in renderer, 'renderer.cpp: [Renderer] SettingsOverlayKey read')
overlay = uncomment(fp.src('settings_overlay.cpp').read_text())
create = overlay[overlay.index('std::unique_ptr<Overlay> create('):]
check(create.index('if(!enabled)') < create.index('install('), 'create(): SettingsOverlay=0 must return before install()')

# Window procedure: forwards everything it does not swallow; the swallow decision is overlay_input.h route().
proc = overlay[overlay.index('LRESULT CALLBACK windowProcedure'):overlay.index('bool install(')]
check('In::route(msg,wparam,lparam,mods,state)' in proc, 'windowProcedure: decisions come from overlay_input.h route()')
check('return d.swallow?0:forward();' in proc, 'windowProcedure: unswallowed messages go to the previous procedure')
check('CallWindowProcW(previous' in proc and 'CallWindowProcA(previous' in proc, 'windowProcedure: chains via CallWindowProc (Unicode and ANSI)')
check(proc.count('ImGui') == 0, 'windowProcedure: never touches ImGui (render thread only)')
route = uncomment(fp.src('overlay_input.h').read_text())
route = route[route.index('inline Decision route('):route.index('inline float mapCoordinate')]
check(re.search(r'case Msg::MouseMove:case Msg::KeyUp:case Msg::SysKeyUp:d\.queue=true;break;', route) is not None,
      'route(): WM_MOUSEMOVE and WM_KEYUP are queued and never swallowed')
# Unhook only while ours is the current procedure (never cut another subclasser's chain).
uninstall = overlay[overlay.index('void uninstall()'):overlay.index('HWND deviceWindow(')]
check('if(current!=reinterpret_cast<LONG_PTR>(&windowProcedure))return;' in uninstall, 'uninstall(): restores only when ours is current')

# renderer.cpp hook points: drawn from finishFrame (Device::Present and SwapChain::Present both finish there) under RawScope+SavedState;
# invalidated before the real Reset; destroyed before the extension device.
draw = renderer[renderer.index('void drawOverlay(bool implicitChain){'):]
draw = draw[:draw.index('\n    }\n')]
check('if(!implicitChain||!overlay||!overlay->visible())return;' in draw and 'ExtensionDevice::RawScope raw(*ext);SavedState saved(ext,&stateBlocks);' in draw, 'drawOverlay(): closed check first, then RawScope and SavedState')
finish = renderer[renderer.index('    void finishFrame(bool implicitChain=true) {'):]
finish = finish[:finish.index('\n    }\n')]
check(finish.count('drawOverlay(implicitChain);') == 2 and finish.index('extensionWork("frame finish"') < finish.rindex('drawOverlay(implicitChain);'), 'finishFrame(): overlay after the frame finish, also on the fault path')
check(renderer.count('drawOverlay(') == 3, 'drawOverlay(): finishFrame is the only caller (one draw per presented frame)')
# Only the implicit swap chain (index 0) draws it; additional swap chains never do.
check('new SwapChain(*out,this,&mirrorResources,index==0)' in renderer and 'new SwapChain(*out,this,&mirrorResources,false)' in renderer
      and 'finishDeviceFrame(owner,implicitChain)' in renderer, 'SwapChain: the overlay draws only for the implicit swap chain')
# Outside the game's scene, in its own BeginScene/EndScene; a failed BeginScene draws nothing.
render = overlay[overlay.index('void render(IDirect3DSurface9* back'):]
render = render[:render.index('\n    }\n')]
check(render.index('if(FAILED(device->BeginScene()))return;') < render.index('ImGui_ImplDX9_RenderDrawData') < render.index('device->EndScene();'), 'render(): BeginScene/EndScene around the ImGui draw')
for state in ['D3DRS_BLENDOPALPHA', 'D3DRS_WRAP0', 'D3DRS_VERTEXBLEND', 'D3DRS_COLORWRITEENABLE', 'D3DRS_SRGBWRITEENABLE', 'D3DRS_STENCILENABLE', 'D3DSAMP_MAXMIPLEVEL', 'D3DSAMP_MIPMAPLODBIAS', 'D3DTSS_RESULTARG', 'SetViewport']:
    check(state in render, f'render(): sets {state}')
# IM_ASSERT never throws (it may fire inside destructors); the overlay checks the record from its own code.
assert_fn = overlay[overlay.index('void northlightImGuiAssert('):overlay.index('namespace NorthlightOverlay {')]
check('throw' not in assert_fn, 'northlightImGuiAssert(): must not throw')
check(overlay.count('checkpoint();') >= 4, 'frame(): checks for a recorded ImGui assertion after each step')
reset = renderer[renderer.index('HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override'):]
check(reset.index('overlay->invalidate();') < reset.index('ext->Reset(pp)'), 'Reset(): overlay invalidated before the real Reset')
dtor = renderer[renderer.index('    ~Device() {'):]
check(dtor.index('overlay.reset();') < dtor.index('ext->Release();'), '~Device(): overlay destroyed before the extension device')
check('Ctrl+Shift+F11 settings' in renderer and 'F11 unassigned' not in renderer, 'device wrapped line names the Ctrl+Shift+F11 settings')

# Licence: shipped like licenses/ao-methods.txt.
check((fp.REPO / 'licenses/imgui.txt').is_file() and 'Omar Cornut' in (fp.REPO / 'licenses/imgui.txt').read_text(), 'licenses/imgui.txt')
check("tree.add('LICENSES/Dear-ImGui.txt', fp.REPO / 'licenses/imgui.txt')" in (fp.RENDERER / 'build_packages.py').read_text(), 'build_packages.py ships the ImGui licence')
check('97428e8ac99e339ce05eee531cf55b77b29ea709' in (fp.SRC / 'imgui' / 'README.md').read_text(), 'src/imgui/README.md records the upstream commit')

# Sidebar pages in order, grouped by what a player comes to do: effects, water, one region's look, the system
# (renaming or reordering one is a deliberate UI change: update this pin and renderer/docs/settings-menu.md).
panel = uncomment(fp.src('settings_panel.cpp').read_text())
titles = re.findall(r'\{Page::\w+,"([^"]+)"', panel)
check(titles == ['Overview', 'Lighting & AO', 'Shadows', 'Materials & parallax', 'Atmosphere & weather', 'Spells & names', 'Water',
                 'Regional appearance', 'Colour grading', 'Sun & moon', 'Shadow range', 'Renderer & HDR', 'Performance', 'Diagnostics'],
      f'settings_panel.cpp: sidebar pages {titles}')
captions = re.findall(r'\{Page::(\w+),"[^"]+",(?:nullptr|"[^"]*"),"([^"]+)"', panel)
check(captions == [('Lighting', 'Northlight effects'), ('Water', 'Tideglass'), ('Regional', 'By region'), ('Renderer', 'System')],
      f'settings_panel.cpp: sidebar groups {captions}')
check((fp.SCRIPTS / 'dev_tuning' / 'settings_menu_preview.py').read_text().count("'" + "', '".join(titles[:7]) + "'") == 1,
      'settings_menu_preview.py renders the same pages in the same order')
sidebar = panel[panel.index('void sidebar(){'):panel.index('std::vector<std::string> sections(unsigned file)const{')]
check(sidebar.index('refreshPageFields();') < sidebar.index('fieldPageMask.find('),
      'sidebar: the per-page index is refreshed before its unsaved and restart marks read it')
check('if(appearancePresetModal){ImGui::OpenPopup("Save as preset");appearancePresetModal=false;}' in panel and
      'if(appearancePresetDeleteModal){ImGui::OpenPopup("Delete preset?");appearancePresetDeleteModal=false;}' in panel,
      'Preset popups: each opening request is consumed once and closing cannot reopen it')
check('ImGui::SetNextWindowSizeConstraints(ImVec2(440*scale,0),ImVec2(440*scale,FLT_MAX));' in panel and
      'ImGui::TextWrapped("%s",NorthlightAppearancePresets::NameRuleText);' in panel and
      'NameRuleText[]="Use 1 to 40 letters, numbers, spaces, underscores or hyphens; Default is reserved.";' in fp.src('appearance_presets.h').read_text(),
      'Save As popup: fixed width and wrapped ASCII validation text keep a stable layout')
check('noteColor=status.state==NorthlightSettings::Status::State::Saved?Color::Good' in panel and
      'ImGui::TextColored(noteColor,"%s",note.c_str())' in panel,
      'Save As status: successful exports use the normal success colour')
check('GradeTimes={NorthlightTone::TimeStop::Day,NorthlightTone::TimeStop::Dusk,NorthlightTone::TimeStop::Night}' in panel and
      'GradeTitles={"Daylight grade","Dusk grade","Night grade"}' in panel and
      'for(unsigned i=0;i<GradeTimes.size();++i)' in panel and 'gradeEditor(GradeTimes[i])' in panel,
      'Colour grading: three clearly named independent Daylight, Dusk and Night grade editors')
check(panel.index('ImGui::PushID("ranges");') < panel.index('ImGui::RadioButton(bandTitle(band).c_str()'),
      'grade editor: the colour ranges have their own ID scope (Red, Green and Blue are channel names too)')
check('NorthlightTone::gradeKey(time,curveChannel,i)' in panel and 'NorthlightTone::vibranceKey(time)' in panel,
      'grade editor: curves and vibrance use the native timeline key helpers')
check('for(const auto band:HueBands)' in panel and 'NorthlightTone::selectiveKey(time,band,SelectiveProperties[i])' in panel and
      'SmallButton("Inherit range")' in panel,
      'grade editor: each time exposes selective colour bands with a per-range inherit action')
check('celestialSectionSnapshot==snapshot.get()' in panel and 'celestialFields(region)' in panel and
      'fields(D::Celestial,&region)' not in panel,
      'Sun & Moon page: region fields are cached by snapshot instead of rescanning the full catalogue each frame')
check('fieldPageMask[f->id()]|=1u<<i' in panel and 'for(const auto& change:draft.changes())' in panel and
      'restartFieldCache[i]' in panel and 'for(const Field* f:pageFields(info.page))' not in panel,
      'sidebar: page membership is indexed once per snapshot and only draft edits/restart fields are checked per frame')
menu_docs=(fp.RENDERER / 'docs/settings-menu.md').read_text()
check('Dusk is the look at 19:30' in menu_docs and 'returns around sunrise at 06:00' in menu_docs and 'Night from 21:15' in menu_docs,
      'settings-menu.md: describes the Dusk anchor, its return at sunrise and the grade clock')
tone_h=fp.src('world_tone_curves.h').read_text()
check('DawnStart=4.5f,Sunrise=6.f,DayStart=8.f,DayEnd=18.f,DuskAnchor=19.5f,SunsetEnd=20.5f,NightStart=21.25f' in tone_h
      and 'NorthlightTone::clockWeights(' in fp.src('world_renderer.h').read_text(),
      'grade clock: the frame uses the hours the menu guide states')
check('dusk_vibrance' in menu_docs and 'dusk_red_hue_shift' in menu_docs and 'Inherit range' in menu_docs,
      'settings-menu.md: documents saved Dusk and selective colour controls')

# The embedded font: generated header present and loaded first; its licence ships.
font_header = fp.GENERATED / 'overlay_font_roboto.h'
check(font_header.is_file() and 'RobotoMediumCompressed' in font_header.read_text()[:2000], 'src/generated/overlay_font_roboto.h (scripts/generate_overlay_font.py)')
theme = uncomment(fp.src('overlay_theme.h').read_text())
check('AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed' in theme and
      overlay.index('if(!Theme::addEmbeddedFont(next))') < overlay.index('loadSystemFont(L"segoeui.ttf"') < overlay.index('AddFontDefault('),
      'rescale(): embedded Roboto first (overlay_theme.h), system fonts as fallback, ImGui default last')
# One theme for the game, the window's contents and the offline preview.
check('Theme::applyStyle(next)' in overlay and 'StyleColorsDark' not in overlay and 'NorthlightOverlayTheme::Color' in panel and
      'NorthlightOverlayTheme::applyStyle(scale)' in (fp.SCRIPTS / 'dev_tuning' / 'settings_menu_preview.cpp').read_text(),
      'overlay_theme.h is the only place the settings window gets its look')
check((fp.REPO / 'licenses/roboto.txt').is_file() and 'Apache License' in (fp.REPO / 'licenses/roboto.txt').read_text(), 'licenses/roboto.txt')
check("tree.add('LICENSES/Roboto.txt', fp.REPO / 'licenses/roboto.txt')" in (fp.RENDERER / 'build_packages.py').read_text(), 'build_packages.py ships the Roboto licence')

with tempfile.TemporaryDirectory(prefix='northlight-overlay-') as tmp:
    exe = Path(tmp) / 'overlay.exe'
    built = subprocess.run(['clang++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                            str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)])
    check(built.returncode == 0, 'test_settings_overlay.cpp did not build')
    if built.returncode == 0:
        check(subprocess.run([str(exe)]).returncode == 0, 'test_settings_overlay.cpp failed')
    # ImGui built natively with the overlay's config decodes and rasterizes the embedded font.
    font_exe = Path(tmp) / 'overlay_font.exe'
    built = subprocess.run(['clang++', '-std=c++17', '-O1', '-w', *fp.test_include_flags(), '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
                            str(HERE / 'test_settings_overlay_font.cpp'), *[str(fp.src(n)) for n in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')],
                            '-o', str(font_exe)])
    check(built.returncode == 0, 'test_settings_overlay_font.cpp did not build')
    if built.returncode == 0:
        check(subprocess.run([str(font_exe)]).returncode == 0, 'test_settings_overlay_font.cpp: the embedded font did not load')

if problems:
    print('\n'.join('FAIL ' + p for p in problems))
    sys.exit(1)
print('settings overlay: static checks OK')
