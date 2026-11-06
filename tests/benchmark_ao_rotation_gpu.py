#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual timing
"""Paired ABBA GPU timings for changed Low AO kernels; not a game FPS test."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp
from windows_gpu_guard import processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True, help='Saved compiled shader directory')
    parser.add_argument('--after', type=Path, default=fp.COMPILED)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--size', default='1920x1080')
    args = parser.parse_args()
    processes()
    w,h = map(int, args.size.lower().split('x'))
    assert w>256 and h>=64
    out = args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    exe=out/'ao_kernel_timing.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    '-Wno-return-type',*fp.include_flags(),str(ROOT/'tests/gtao_footprint_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(game_launched=False,whole_frame_fps_measured=False,
                scope='Raw AO kernel only, FP16 half-resolution target. Batched elapsed GPU queries include submission gaps. Twelve ABBA samples per variant, 64 draws per batch, after 1024 warmup draws.',
                backend_sha256=digest(args.backend),cases=[],shaders={})
    for method,stage in ((m,s) for m in ('HBAOPlus','GTAO') for s in ('Only','ContactBloom')):
        entry=method+stage+'Low.bin'
        old,new=args.before/entry,args.after/entry
        report['shaders'][entry]=dict(before=digest(old),after=digest(new))
        processes()
        result=subprocess.run([str(exe),str(old.resolve()),str(new.resolve()),str(args.backend.resolve()),str(w),str(h),'16','--measure-changed-output'],cwd=out,capture_output=True,text=True,check=True,timeout=180)
        (out/(method+stage+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        assert len(rows)==9 and all(r['nonfinite']==0 and r['old_ms']>0 and r['new_ms']>0 for r in rows)
        assert any(r['changed_bytes'] for r in rows), 'Expected changed sampling did not reach the shader'
        report['adapter']=next(line.removeprefix('GPU_ADAPTER ') for line in result.stdout.splitlines() if line.startswith('GPU_ADAPTER '))
        report['cases'] += [dict(method=method,stage=stage,**r) for r in rows]
        print(method,stage,'median delta ms',round(statistics.median(r['new_ms']-r['old_ms'] for r in rows),6),flush=True)
    (out/'timings.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(out/'timings.json')


if __name__=='__main__':
    main()
