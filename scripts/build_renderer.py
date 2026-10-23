"""Cross-compile the renderer DLL. Does not run the game or the DLL."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent                 # scripts/: the generators
sys.path.insert(0, str(ROOT.parent))
import northlight_paths  # noqa: E402
import check_layout  # noqa: E402  (scripts/ is this script's directory)
SHADERS = northlight_paths.SHADERS                         # *.hlsl and their *-shader-build.json manifests
DLL = northlight_paths.RENDERER / 'frd9.dll'
DEV_DLL = northlight_paths.RENDERER / 'frd9-dev-tuning.dll'  # the dev variant never overwrites frd9.dll

def main(argv=None):
    dev_tuning = '--dev-tuning' in (sys.argv[1:] if argv is None else argv)
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
    manifest = json.loads((SHADERS/'shader-build.json').read_text())
    # All shader modifications must pass the offline compile/SM3 gate first.
    source_hash = hashlib.sha256((SHADERS/'effects.hlsl').read_bytes()).hexdigest()
    if source_hash not in json.dumps(manifest):
        raise SystemExit('Shader source differs from the offline build; run scripts/shaders/compile_shaders.py.')
    world_manifest = json.loads((SHADERS/'world-shader-build.json').read_text())
    if hashlib.sha256((SHADERS/'world_effects.hlsl').read_bytes()).hexdigest() not in json.dumps(world_manifest):
        raise SystemExit('World shader source differs from offline build.')
    local_manifest = json.loads((SHADERS/'local-light-shader-build.json').read_text())
    if hashlib.sha256((SHADERS/'local_light_effects.hlsl').read_bytes()).hexdigest() != local_manifest['source_sha256']:
        raise SystemExit('Local light shader source differs from offline build.')
    celestial_manifest = json.loads((SHADERS/'celestial-disc-shader-build.json').read_text())
    if hashlib.sha256((SHADERS/'celestial_disc_effects.hlsl').read_bytes()).hexdigest() != celestial_manifest['source_sha256']:
        raise SystemExit('Celestial disc shader source differs from offline build.')
    static_manifest = json.loads((SHADERS/'static-shadow-shader-build.json').read_text())
    if hashlib.sha256((SHADERS/'static_shadow_effects.hlsl').read_bytes()).hexdigest() != static_manifest['source_sha256']:
        raise SystemExit('Static caster shader source differs from offline build.')
    bump_digest=hashlib.sha256((SHADERS/"auto_bump_normal.hlsl").read_bytes()).hexdigest()
    if any(m.get("bump_include_sha256")!=bump_digest for m in (world_manifest,)):
        raise SystemExit("Auto bump include differs from the offline build.")
    bump_manifest=json.loads((SHADERS/"auto-bump-shader-build.json").read_text())
    if bump_manifest["source_sha256"]!=hashlib.sha256((SHADERS/"auto_bump_capture.hlsl").read_bytes()).hexdigest():
        raise SystemExit("Auto bump capture differs from the offline build.")
    if bump_manifest.get("normal_source_sha256")!=hashlib.sha256((SHADERS/"auto_normal_generate.hlsl").read_bytes()).hexdigest():
        raise SystemExit("Automatic normal-map generator differs from the offline build.")
    if any(digest!=hashlib.sha256((SHADERS/name).read_bytes()).hexdigest() for name,digest in bump_manifest.get('parallax_source_sha256',{}).items()):
        raise SystemExit("Parallax shaders differ from the offline build.")
    env = northlight_paths.zig_env()
    command = [str(zig), 'c++', '-target', 'x86-windows-gnu', '-O2', '-shared',
               '-std=c++17', '-fno-rtti', '-Wall', '-Wextra', '-Wno-microsoft-exception-spec',
               *northlight_paths.include_flags(),
               *[str(northlight_paths.src(n)) for n in ('renderer.cpp', 'world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'frd9.def')],
               '-luser32', '-luuid', '-o', str(DEV_DLL if dev_tuning else DLL)]
    if dev_tuning:
        # Developer-only zone tuning (scripts/dev_tuning/). The DLL carries a marker that renderer/build_packages.py refuses.
        command.insert(command.index('-luser32'), '-DNORTHLIGHT_DEV_TUNING=1')
        print('Building the DEV TUNING variant (NORTHLIGHT DEV TUNING BUILD): not for release packages.')
    subprocess.run(command, env=env, check=True)
    print('Built', DEV_DLL if dev_tuning else DLL)

if __name__ == '__main__':
    main()
