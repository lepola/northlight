"""Numerical GPU acceptance for HDR composition output, exposure and legacy expansion."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes


def main():
    parser=argparse.ArgumentParser(__doc__)
    for name in ('dxvk2','dxvk3','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--benchmark',action='store_true',help='Measure only HDR post passes with GPU timestamps; this is not client frame time.')
    parser.add_argument('--reference-ref',help='For --benchmark, build a second executable using HDR headers from this Git commit.')
    parser.add_argument('--linear-capture',type=Path,help='For --benchmark, also replay a local HDR-linear-scene.fgr through the post path; private capture/output buffers remain in the isolated stand.')
    parser.add_argument('--legacy-before',type=Path,help='Optional archived pre-fix HdrUi ps_3_0 transfer for the native SDR overshoot negative control.')
    args=parser.parse_args();output=args.output.resolve()
    if args.reference_ref and not args.benchmark:parser.error('--reference-ref requires --benchmark')
    if args.linear_capture and not args.benchmark:parser.error('--linear-capture requires --benchmark')
    if fp.is_client(output):raise SystemExit('Use an isolated GPU stand directory.')
    processes();output.mkdir(parents=True,exist_ok=True)
    source='hdr_pipeline_benchmark_gpu.cpp' if args.benchmark else 'hdr_frame_gpu.cpp'
    binaries={}
    report={'game_launched':False,'client_modified':False,'runs':[],'headers':{},'harness_sha256':hashlib.sha256((fp.TESTS/source).read_bytes()).hexdigest()}
    if args.linear_capture:
        report['linear_capture_sha256']=hashlib.sha256(args.linear_capture.read_bytes()).hexdigest()
    reference=None
    if args.reference_ref:
        reference=subprocess.check_output(['git','rev-parse','--verify',args.reference_ref+'^{commit}'],cwd=fp.REPO,text=True).strip()
        report['reference_commit']=reference
    for variant in (['reference','candidate'] if reference else ['candidate']):
        folder=output/variant;folder.mkdir(exist_ok=True);include=folder/'headers';include.mkdir(exist_ok=True)
        hashes={}
        for name in ('hdr_frame.h','hdr_quad.h','hdr_color.h','hdr_presentation.h','saved_state.h','hdr_compiled_shaders.h'):
            path=fp.src(name)
            data=subprocess.check_output(['git','show',reference+':'+path.relative_to(fp.REPO).as_posix()],cwd=fp.REPO) if variant=='reference' else path.read_bytes()
            (include/name).write_bytes(data);hashes[name]=hashlib.sha256(data).hexdigest()
        report['headers'][variant]=hashes
        binary=folder/(Path(source).stem+'.exe');binaries[variant]=binary
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-fno-rtti','-I',str(include),*fp.include_flags(),str(fp.TESTS/source),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
    for backend,dll in [('dxvk2',args.dxvk2),('dxvk3',args.dxvk3)]:
        folder=output/backend;folder.mkdir(exist_ok=True);runtime=folder/'backend_d3d9.dll';shutil.copy2(dll.resolve(),runtime)
        for variant,binary in binaries.items():
            command=[str(binary),str(runtime)]+([str(args.linear_capture.resolve())] if args.linear_capture else [str(args.legacy_before.resolve())] if args.legacy_before and not args.benchmark else [])
            run_folder=folder/variant;run_folder.mkdir(exist_ok=True)
            if args.linear_capture:
                for old in run_folder.glob('capture-output-*.rgba16f'):old.unlink()
            result=subprocess.run(command,cwd=run_folder,env=dict(os.environ,DXVK_LOG_PATH=str(run_folder)),capture_output=True,text=True,timeout=90)
            outputs={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in run_folder.glob('capture-output-*.rgba16f')} if args.linear_capture else {}
            report['runs'].append(dict(backend=backend,variant=variant,exit_code=result.returncode,backend_sha256=hashlib.sha256(runtime.read_bytes()).hexdigest(),executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),capture_outputs_sha256=outputs,stdout=result.stdout,stderr=result.stderr))
            (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print(backend,variant,result.stdout.strip(),flush=True)
            if result.returncode or (not args.benchmark and 'HDR_FRAME_GPU_PASS' not in result.stdout):raise SystemExit(result.stderr or result.stdout or str(result.returncode))
            if args.linear_capture and set(outputs)!={'capture-output-0.rgba16f','capture-output-8.rgba16f'}:raise SystemExit('Incomplete captured post outputs')
    print('PASS:',output/'validation.json')


if __name__=='__main__':main()
