# northlight-test: requires=windows-gpu,zig
"""Color-only sky transfer and no-color depth/stencil work, on both DXVK versions."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes

processes()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
exe = out / 'hdr-diffuse.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.test_include_flags(), str(fp.TESTS / 'hdr_diffuse_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
for backend in ('dxvk/dxvk_d3d9.dll', 'dxvk2/dxvk2_d3d9.dll'):
    path = fp.client_root() / 'renderer-backends' / backend
    result = subprocess.run([str(exe), str(path)] + (['benchmark'] if '--benchmark' in sys.argv else []),
                            cwd=out, env=dict(os.environ, DXVK_LOG_PATH=str(out)),
                            capture_output=True, text=True, timeout=180)
    (out / (path.stem + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    assert result.returncode == 0, result.stdout + result.stderr
    print(backend, *[s for s in result.stdout.splitlines() if s.startswith('HDR_DIFFUSE')], sep='\n', flush=True)
