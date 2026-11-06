#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Point-shadow caster coverage independent of native camera chunks; no game/cache/timings."""
import os,sys,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'point-terrain-gpu';out.mkdir(parents=True,exist_ok=False)
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
exe=out/'point-terrain-gpu.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'point_terrain_coverage_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
subprocess.run(list(map(str,command)),check=True,timeout=240)
backends=[Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index,backend in enumerate(backends):
 settings=out/f'settings-{index}';settings.mkdir()
 env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 p=subprocess.run([str(exe),str(backend),str(settings)+os.sep],cwd=out,env=env,capture_output=True,text=True,timeout=120)
 (out/f'gpu-{index}.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);assert p.returncode==0,p.returncode
