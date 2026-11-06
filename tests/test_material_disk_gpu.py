#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,windows-gpu manual
"""Actual GPU maps survive separate processes without regeneration or blocking readback."""
import argparse,hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',default=str(Path(os.environ.get('SystemRoot','C:/Windows'))/'SysWOW64/d3d9.dll'));p.add_argument('--honor-hold',action='store_true');p.add_argument('--formats',nargs='+',default=['rgba']);p.add_argument('--extended',action='store_true');a=p.parse_args()
out=fp.output_dir()/'material-disk-gpu';out.mkdir(parents=True,exist_ok=True);exe=out/'maps.exe'
watched=[fp.src(n) for n in ['material_disk_cache.h','material_map_persistence.h','auto_normal_cache.h','auto_bump_compiled_shaders.h','auto_normal_memory.h','material_height.h','actor_texture.h','backend_loader.h','saved_state.h','dxvk_compatibility.h']]+[Path(__file__),fp.TESTS/'material_disk_gpu.cpp',fp.TESTS/'auto_normal_reuse_gpu.cpp']
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in watched}
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'material_disk_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
fixture=[]
if a.honor_hold:
    import struct
    sys.path.insert(0,str(fp.RENDERER))
    from world_scene_builder import Assets,decode_blp
    assets=Assets()
    try:w,h,rgba=decode_blp(assets.read(r'world\azeroth\elwynn\passivedoodads\monumentrock\statuebase.blp'),512)
    finally:assets.close()
    bgra=bytearray(rgba);bgra[0::4]=rgba[2::4];bgra[2::4]=rgba[0::4]
    path=out/'honor-hold.fgr';path.write_bytes(struct.pack('<5I',0x31524746,w,h,21,4)+bgra);fixture=[str(path)]
rows=[]
with tempfile.TemporaryDirectory(prefix='restart-',dir=out) as folder:
    for fmt in a.formats:
        destination=Path(folder)/fmt
        modes=['produce','consume']
        if a.extended and fmt=='rgba':modes+=['repair','consume','settings','settings-hit','equivalent']
        for index,mode in enumerate(modes):
            if mode=='repair':
                for file in (destination/'maps').glob('*.nlm'):file.write_bytes(b'interrupted or corrupt cache entry')
            r=subprocess.run([str(exe),str(Path(a.backend).resolve()),str(destination),mode,fixture[0] if fixture else '',fmt],cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=60)
            (out/(fmt+'-'+str(index)+'-'+mode+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
            assert r.returncode==0,r.stdout+r.stderr
            assert 'MATERIAL_DISK_GPU_PASS' in r.stdout,r.stdout
            print(fmt+' '+r.stdout.strip(),flush=True);rows.append(dict(format=fmt,mode=mode,stdout=r.stdout))
assert all(sha(Path(path))==digest for path,digest in inputs.items()),'Source changed during GPU validation'
(out/'validation.json').write_text(json.dumps(dict(inputs=inputs,executable_sha256=sha(exe),backend_sha256=sha(Path(a.backend)),backend=a.backend,honor_hold=a.honor_hold,rows=rows,game_launched=False),indent=2)+'\n')
