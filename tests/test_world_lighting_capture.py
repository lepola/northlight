#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Actual diagnostic depth/state/export roundtrip; no game or timing benchmark."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
folder = Path(tempfile.mkdtemp(prefix='world-lighting-capture-', dir=out))
backend = Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'
assert backend.is_file(), backend
inputs = [fp.REPO / name for name in (
    'src/world/world_lighting_capture.h', 'src/lights/local_light_selection.h',
    'src/lights/world_local_lights.h', 'src/lights/local_light_scissor.h',
    'src/generated/forwarders.h', 'tests/world_lighting_capture_gpu.cpp',
    'tests/test_world_lighting_capture.py', 'northlight_paths.py')]
guard = {str(p.relative_to(fp.REPO)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
backend_hash = hashlib.sha256(backend.read_bytes()).hexdigest()
adapter = int(os.environ.get('NORTHLIGHT_TEST_D3D9_ADAPTER', '0'))
assert 0 <= adapter < 16
def game_boundary():
    process = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Wow.exe', '/FO', 'CSV', '/NH'], capture_output=True, text=True, check=True)
    assert not any(line.startswith('"') and line.split(',')[0].strip('"').lower() == 'wow.exe' for line in process.stdout.splitlines()), 'Game must remain stopped for the standalone fixture'
    return process.stdout

before_game = game_boundary()
private_backend = folder / 'backend.dll'
shutil.copyfile(backend, private_backend)
assert hashlib.sha256(private_backend.read_bytes()).hexdigest() == backend_hash
temporary = folder / 'tmp'
temporary.mkdir()
config = folder / 'dxvk.conf'
config.write_text('dxvk.allowFse = False\nd3d9.presentInterval = 0\nd3d9.supportDFFormats = True\n', encoding='utf-8')
environment = dict(fp.zig_env(), TEMP=str(temporary), TMP=str(temporary),
                   ZIG_LOCAL_CACHE_DIR=str(folder / 'zig-local-cache'),
                   ZIG_GLOBAL_CACHE_DIR=str(folder / 'zig-global-cache'),
                   DXVK_LOG_PATH=str(folder), DXVK_CONFIG_FILE=str(config),
                   DXVK_SHADER_CACHE_PATH=str(folder / 'shader-cache'),
                   DXVK_STATE_CACHE_PATH=str(folder / 'state-cache'))
binary = folder / 'fixture-lighting-capture.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG',
           '-Wno-microsoft-exception-spec', '-std=c++17', *fp.test_include_flags(),
           str(fp.TESTS / 'world_lighting_capture_gpu.cpp'), '-luser32', '-o', str(binary)]
compiled = subprocess.run(command, env=environment, capture_output=True, text=True)
(folder / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
assert compiled.returncode == 0, compiled.stdout + compiled.stderr
run = subprocess.run([str(binary), str(private_backend), str(folder), str(adapter)], cwd=folder, env=environment,
                     capture_output=True, text=True, timeout=180)
(folder / 'gpu.log').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
assert 'LIGHT_CAPTURE_PASS depthFormats=2 exactDepthPixels=1702 apiFailureCases=22 constantRegisters=116 stateVS=256 statePS=224 timing=none' in run.stdout, run.stdout
complete = sorted(folder.glob('capture-*-lighting-inputs.jsonl'))
incomplete = sorted(folder.glob('capture-*-lighting-inputs-incomplete.jsonl'))
assert len(complete) == 3 and len(incomplete) == 22
for path in complete + incomplete:
    events = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]
    assert events[0]['event'] == 'begin' and events[-1]['event'] == 'end'
    assert events[0]['constant_register_count'] == 116 and not events[0]['depth_reconstructed']
    assert not events[0]['fps_eligible'] and not events[0]['whole_scene_replay']
    assert not events[0]['material_replay_complete'] and not events[0]['per_sample_color_exported']
    assert not events[0]['stencil_image_exported'] and not events[0]['scene_boundary_changed']
    assert events[-1]['export_complete'] == (path in complete)
    assert events[0]['renderer_frame_known'] and events[0]['renderer_frame'] and events[0]['world_frame']
assert guard == {relative: hashlib.sha256((fp.REPO / relative).read_bytes()).hexdigest() for relative in guard}, 'Source changed during focused acceptance'
assert backend_hash == hashlib.sha256(backend.read_bytes()).hexdigest(), 'Backend changed during acceptance'
assert backend_hash == hashlib.sha256(private_backend.read_bytes()).hexdigest(), 'Private backend changed during acceptance'
after_game = game_boundary()
files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.iterdir()) if p.is_file()}
(folder / 'validation.json').write_text(json.dumps(dict(status='pass', command=command, source_guard=guard,
    backend=str(backend), backend_sha256=backend_hash, adapter=adapter,
    dxvk_device_filter=environment.get('DXVK_FILTER_DEVICE_NAME'),
    files=files, captures=3, incomplete_controls=22,
    game_before=before_game, game_after=after_game,
    scope='Synthetic odd-size actual DF24/INTZ helper inputs, exact 0/1 pixel centers and state/constant transport',
    authentic_source_replay=False, whole_scene_replay=False, timing_benchmark=False), indent=2) + '\n', encoding='utf-8')
print(run.stdout.strip())
