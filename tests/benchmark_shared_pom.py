#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Warm capture + native POM timing, including MRT writes and state changes; no game.

Prepare fixtures with validate_parallax_msaa.py --output-subdir shared-pom-msaa.
Timestamp intervals can include submission starvation; this is not game FPS.
"""
from pathlib import Path
import argparse,json,re,shutil,statistics,struct,subprocess,sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend',required=True)
p.add_argument('--width',type=int,default=3840)
p.add_argument('--height',type=int,default=2160)
p.add_argument('--samples',type=int,default=2)
p.add_argument('--repeats',type=int,default=5)
p.add_argument('--families',nargs='+',default=['terrain','wall','composite','grass'])
p.add_argument('--output-subdir',default='shared-pom-timing')
a=p.parse_args()
out=fp.out()/a.output_subdir;out.mkdir(exist_ok=True)
exe=out/'shared-pom-timing.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',f'-DNORTHLIGHT_OBLIQUE_WIDTH={a.width}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={a.height}','-DNORTHLIGHT_MATERIAL_MIRROR',*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
pe=bytearray(exe.read_bytes());flags=struct.unpack_from('<I',pe,0x3c)[0]+22
struct.pack_into('<H',pe,flags,struct.unpack_from('<H',pe,flags)[0]|0x20);exe.write_bytes(pe)
records=[]
for family in a.families:
    folder=out/family;folder.mkdir(exist_ok=True)
    for f in (fp.out()/'shared-pom-msaa'/family).iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):shutil.copy2(f,folder/f.name)
    times={'legacy':[],'shared':[]}
    for repeat in range(a.repeats):
        for label in (['legacy','shared'] if repeat%2==0 else ['shared','legacy']):
            env=fp.zig_env()
            for key in list(env):
                if key.startswith(('NORTHLIGHT_TEST_SHARED_','NORTHLIGHT_TEST_POM_ONLY')) or key=='NORTHLIGHT_TEST_NO_SHARED_POM':env.pop(key)
            env.update(NORTHLIGHT_TEST_MSAA=str(a.samples),NORTHLIGHT_TEST_SHARED_TIMING='1',NORTHLIGHT_EXPERIMENTAL_SHARED_POM='1')
            if label=='legacy':env['NORTHLIGHT_TEST_NO_SHARED_POM']='1'
            result=subprocess.run([str(exe),a.backend,str(folder)+'/', '12','.7']+([family] if family!='terrain' else []),env=env,cwd=out,capture_output=True,text=True,timeout=120)
            (out/f'{family}-{repeat}-{label}.log').write_text(result.stdout+result.stderr)
            assert result.returncode==0,(family,label,result.stdout[-1500:],result.stderr[-1500:])
            m=re.search(r'SHARED_POM_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',result.stdout);assert m
            times[label].append(dict(gpu_ms=float(m[1]),submission_ms=float(m[2])))
    record=dict(family=family,backend=a.backend,width=a.width,height=a.height,samples=a.samples,runs=times)
    for label in times:record[label+'_median_ms']=statistics.median(t['gpu_ms'] for t in times[label])
    record['gpu_interval_reduction_percent']=100*(1-record['shared_median_ms']/record['legacy_median_ms'])
    records.append(record);print(json.dumps(record),flush=True)
(out/'timing.json').write_text(json.dumps(records,indent=2)+'\n')
