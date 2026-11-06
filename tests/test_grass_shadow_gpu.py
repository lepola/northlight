#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Native DetailDoodad capture and shadow masks; correctness only, no game/cache/timings."""
import os,sys,subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants
out=fp.output_dir()/'grass-shadow-gpu';out.mkdir(parents=True,exist_ok=False)
assets=Assets(archives='stock')
try:
 for stage,folder in [('vs','vertex'),('ps','pixel')]:
  index=0
  for major in (2,3):
   for _,_,data in variants(assets.read(f'shaders\\{folder}\\{stage}_{major}_0\\DetailDoodad.bls')):
    if stage=='ps':
     h=14695981039346656037
     for b in data:h=((h^b)*1099511628211)&((1<<64)-1)
     assert f'0x{h:016x}ULL' in fp.src('detail_doodad_shadow.h').read_text()
    (out/f'{stage}-{index}.bin').write_bytes(data);index+=1
finally:assets.close()
sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
exe=out/'grass-shadow-gpu.exe'
command=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*fp.test_include_flags(),fp.TESTS/'grass_shadow_gpu.cpp',*(fp.src(s) for s in sources),'-luser32','-luuid','-o',exe]
subprocess.run(list(map(str,command)),check=True,timeout=240)
backends=[Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index,backend in enumerate(backends):
 settings=out/f'settings-{index}';settings.mkdir()
 env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
 p=subprocess.run([str(exe),str(backend),str(settings)+os.sep,str(out)+os.sep],cwd=out,env=env,capture_output=True,text=True,timeout=120)
 (out/f'gpu-{index}.log').write_text(p.stdout+p.stderr);print(p.stdout+p.stderr);assert p.returncode==0,p.returncode
