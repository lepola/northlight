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

OVERLAY = ['settings_overlay.h', 'settings_overlay.cpp', 'settings_panel.h', 'settings_panel.cpp', 'settings_draft.h', 'overlay_input.h', 'northlight_imconfig.h']
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

# Sidebar pages in the old menu's order (renaming or reordering one is a deliberate UI change: update this pin).
panel = uncomment(fp.src('settings_panel.cpp').read_text())
titles = re.findall(r'\{Page::\w+,"([^"]+)"', panel)
check(titles == ['Overview', 'Lighting & AO', 'Materials & parallax', 'Shadows', 'Atmosphere & weather', 'Sun & moon', 'Shadow range',
                 'Spells & names', 'Renderer & HDR', 'Performance', 'Diagnostics'], f'settings_panel.cpp: sidebar pages {titles}')

# The embedded font: generated header present and loaded first; its licence ships.
font_header = fp.GENERATED / 'overlay_font_roboto.h'
check(font_header.is_file() and 'RobotoMediumCompressed' in font_header.read_text()[:2000], 'src/generated/overlay_font_roboto.h (scripts/generate_overlay_font.py)')
check(overlay.index('AddFontFromMemoryCompressedTTF(NorthlightOverlayFont::RobotoMediumCompressed') < overlay.index('loadSystemFont(L"segoeui.ttf"') < overlay.index('AddFontDefault('),
      'rescale(): embedded Roboto first, system fonts as fallback, ImGui default last')
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

for key in ['AutoBumpModels','AutoHeightBalance','AutoDisplacementDistance','AutoDisplacementDetail','AutoDisplacementStrength','AutoModelDisplacementDistance','AutoModelDisplacementDetail','AutoModelDisplacementStrength']:
    check(key in panel,'Material menu must expose '+key)
renderer=fp.src('renderer.cpp').read_text()
boundary=renderer[renderer.index('if(settingsService)if(auto update=settingsService->after(settingsRevision))'):]
check(boundary.index('autoBump->configure(world->surfaceSettings())')<boundary.index('settingsService->acknowledge(*update)'),'Material preview must reach its renderer before acknowledgement')

if problems:
    print('\n'.join('FAIL ' + p for p in problems))
    sys.exit(1)
print('settings overlay: static checks OK')
