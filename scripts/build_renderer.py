"""Cross-compile the renderer DLL. Does not run the game or the DLL.

    python3 scripts/build_renderer.py                 # renderer/frd9.dll, with the Tideglass bridge
    python3 scripts/build_renderer.py --no-tideglass  # renderer/frd9-no-tideglass.dll, Northlight alone
    python3 scripts/build_renderer.py --dev-tuning    # renderer/frd9-dev-tuning.dll, never for release

Every Tideglass call in the renderer sits behind NORTHLIGHT_WATER. --no-tideglass builds with it off and without the
tideglass/ sources: for a client Tideglass cannot run in (it reads the 32-bit 12340 client's memory), or to check
that the renderer still stands alone. The usual build can also run without Tideglass: it only looks for
water-core.dll when water-host.ini asks for it.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent                 # scripts/: the generators
sys.path.insert(0, str(ROOT.parent))
import northlight_paths  # noqa: E402
import check_layout  # noqa: E402  (scripts/ is this script's directory)
sys.path.insert(0, str(ROOT/'shaders'))
import shader_manifest
SHADERS = northlight_paths.SHADERS                         # *.hlsl and their *-shader-build.json manifests
DLL = northlight_paths.RENDERER / 'frd9.dll'
DEV_DLL = northlight_paths.RENDERER / 'frd9-dev-tuning.dll'  # the dev variant never overwrites frd9.dll
ALONE_DLL = northlight_paths.RENDERER / 'frd9-no-tideglass.dll'  # nor does the build without the Tideglass bridge
OVERLAY_SOURCES = ('settings_overlay.cpp', 'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp')

def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    dev_tuning = '--dev-tuning' in argv
    tideglass = '--no-tideglass' not in argv
    if dev_tuning and not tideglass:
        raise SystemExit('build_renderer.py: --dev-tuning and --no-tideglass are separate variants: give one')
    output = DEV_DLL if dev_tuning else DLL if tideglass else ALONE_DLL
    # A source recreated at its old renderer/ path would silently fork.
    problems = check_layout.layout_problems()
    if problems:
        raise SystemExit('build_renderer.py: layout check failed:\n' + '\n'.join(problems))
    try:
        zig = northlight_paths.zig()
    except northlight_paths.Missing as e:
        raise SystemExit(f'build_renderer.py: {e}\nInstall zig {northlight_paths.ZIG_VERSION} (https://ziglang.org/download/) and point NORTHLIGHT_ZIG at it.')
    subprocess.run([sys.executable, str(ROOT/'generate_forwarders.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'generate_mirror_guarded_device.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'generate_mirror_resource_forwarders.py')], check=True)
    subprocess.run([sys.executable, str(ROOT/'generate_extension_raw_methods.py')], check=True)
    # Every entry, include and embedded array must describe the same build.
    shader_inputs = shader_manifest.validate_all(northlight_paths.REPO)
    env = northlight_paths.zig_env()
    command = [str(zig), 'c++', '-target', 'x86-windows-gnu', '-O2', '-shared', '-s',
               f'-ffile-prefix-map={northlight_paths.REPO}=.', f'-ffile-prefix-map={zig.parent}=zig',
               '-std=c++17', '-fno-rtti', '-Wall', '-Wextra', '-Wno-microsoft-exception-spec',
               *northlight_paths.include_flags(),
               *[str(northlight_paths.src(n)) for n in ('renderer.cpp', 'world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'frd9.def')],
               # Settings overlay (src/overlay/) and the vendored Dear ImGui it draws with (src/imgui/, configured by northlight_imconfig.h).
               '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
               *[str(northlight_paths.src(n)) for n in OVERLAY_SOURCES],
               '-luser32', '-luuid', '-o', str(output)]
    if tideglass:
        water = northlight_paths.REPO / 'tideglass'
        from tideglass_source import verify
        verify(water)
        command[command.index('-luser32'):command.index('-luser32')] = [
            '-DNORTHLIGHT_WATER=1', '-I' + str(water / 'src'), '-I' + str(water / 'integrations'),
            '-I' + str(water / 'generated'), str(water / 'src/surface_depth.cpp'), str(water / 'src/screen_pass.cpp')]
    if dev_tuning:
        # Developer-only zone tuning (scripts/dev_tuning/). The DLL carries a marker that renderer/build_packages.py refuses.
        command.insert(command.index('-luser32'), '-DNORTHLIGHT_DEV_TUNING=1')
        print('Building the DEV TUNING variant (NORTHLIGHT DEV TUNING BUILD): not for release packages.')
    subprocess.run(command, env=env, check=True)
    if shader_manifest.validate_all(northlight_paths.REPO) != shader_inputs:
        raise RuntimeError('Shader inputs changed during host compilation')
    print('Built', output)

if __name__ == '__main__':
    main()
