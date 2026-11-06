#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu
"""Projected shadow/glow visibility probes preserve colour, depth and UP side effects."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'projected-audit-gpu'
out.mkdir(parents=True, exist_ok=False)
spec = importlib.util.spec_from_file_location('projected_guard', fp.TESTS / 'validate_native_scene_trace.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)
guard.processes()
backend = Path(os.environ.get('NORTHLIGHT_TEST_D3D9', fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'))
env = fp.zig_env()
for key, name in [('TEMP', 'temp'), ('TMP', 'temp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'),
                  ('ZIG_GLOBAL_CACHE_DIR', 'zig-global'), ('DXVK_SHADER_CACHE_PATH', 'shader-cache'),
                  ('DXVK_STATE_CACHE_PATH', 'state-cache'), ('__GL_SHADER_DISK_CACHE_PATH', 'driver-cache')]:
    (out / name).mkdir(exist_ok=True)
    env[key] = str(out / name)
(out / 'dxvk.conf').write_bytes(b'dxvk.allowFse = False\nd3d9.presentInterval = 0\n')
env.update(DISABLE_VULKAN_OBS_CAPTURE='1', DXVK_CONFIG_FILE=str(out / 'dxvk.conf'), DXVK_LOG_PATH=str(out))
exe = out / 'projected-audit.exe'
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
           '-Wno-microsoft-exception-spec', *fp.test_include_flags(),
           str(fp.TESTS / 'projected_draw_audit_gpu.cpp'), '-luser32', '-o', str(exe)]
subprocess.run(command, env=env, check=True)
guard.processes()
result = subprocess.run([str(exe), str(backend)], cwd=out, env=env, capture_output=True, text=True, timeout=120)
(out / 'gpu.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
assert 'PROJECTED_AUDIT_GPU_PASS cases=96 ' in result.stdout, result.stdout
print(result.stdout.strip())
