#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production flash shadow maps and native model replay; no game or benchmarks."""
import os,sys,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'flash-shadow-gpu';out.mkdir(parents=True,exist_ok=True)
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
exe=out/'flash-shadow-gpu.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'flash_shadow_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
subprocess.run(list(map(str,command)),check=True,timeout=240)
if '--compile-only' in sys.argv:raise SystemExit(0)
for index,backend in enumerate([Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']):
    settings=out/f'settings-{index}';settings.mkdir(exist_ok=True)
    env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
    p=subprocess.run([str(exe),str(backend),str(settings)+os.sep],cwd=out,env=env,capture_output=True,text=True,timeout=90)
    (out/f'gpu-{index}.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);assert p.returncode==0,p.returncode
