#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Consumer-free terrain capture skips D3D/CPU reads and retains warm shadow geometry."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from validate_native_scene_trace import processes

out = fp.output_dir()/'terrain-capture-demand'
out.mkdir(parents=True, exist_ok=False)
sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp',
           'settings_panel.cpp', 'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
test = fp.TESTS/'terrain_capture_demand_gpu.cpp'
header = fp.src('world_renderer.h')
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
inputs = {str(p): digest(p) for p in [test, Path(__file__), *fp.SRC.rglob('*.h'), *fp.SRC.rglob('*.cpp'), *backends]}
report = {'pass': False, 'game_launched': False, 'timings_claimed': False, 'inputs': inputs, 'cases': []}
env = fp.zig_env()
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_LOG_PATH=str(out))

def compile_test(label, includes):
    exe = out/(label+'.exe')
    command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
               '-Wno-microsoft-exception-spec', '-DIMGUI_USER_CONFIG="northlight_imconfig.h"',
               *includes, test, *(fp.src(s) for s in sources), '-luser32', '-luuid', '-o', exe]
    with (out/('compile-'+label+'.log')).open('w') as log:
        subprocess.run(list(map(str, command)), env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
    return exe

def run(exe, backend, label):
    settings = out/label
    settings.mkdir()
    (settings/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n', encoding='utf-8')
    processes(False)
    result = subprocess.run([str(exe), str(backend), str(settings)+os.sep], cwd=settings, env=env,
                            capture_output=True, text=True, timeout=120)
    (out/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
    print(label, result.returncode, (result.stdout+result.stderr)[-2000:], flush=True)
    return result

try:
    exe = compile_test('current', fp.test_include_flags())
    for i, backend in enumerate(backends):
        result = run(exe, backend, 'current-'+str(i))
        assert result.returncode == 0 and 'TERRAIN_CAPTURE_DEMAND_PASS' in result.stdout
        report['cases'].append({'backend': str(backend), 'pass': True})
    # Only remove the two new demand gates in an artifact copy of the header.
    # All production code and every positive assertion remain identical.
    overlay = out/'baseline-access'
    overlay.mkdir()
    text = header.read_text(encoding='utf-8')
    gates = ['        if(!effects.shadows)return; // Live terrain is consumed only by shadow passes; UP has no material bounds handoff.\n',
             '            if(!effects.shadows&&!positions)return; // AutoBump may still need this draw\'s position bounds.\n']
    for gate in gates:
        assert text.count(gate) == 1
        text = text.replace(gate, '', 1)
    (overlay/'world_renderer.h').write_text(text, encoding='utf-8')
    baseline = compile_test('baseline', ['-I', str(overlay), *fp.test_include_flags()])
    result = run(baseline, backends[0], 'baseline-negative')
    assert result.returncode != 0 and 'd.reads==0' in result.stderr, 'Old ungated entry points must fail the zero-work assertion'
    report['baseline_failed_zero_work_assertion'] = True
    report['pass'] = True
finally:
    processes(False)
    report['inputs_unchanged'] = all(digest(Path(p)) == sha for p, sha in inputs.items())
    report['pass'] &= report['inputs_unchanged']
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
assert report['pass']
