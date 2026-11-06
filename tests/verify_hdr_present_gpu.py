"""Native-MSAA HDR presentation controls; never loads or changes the game client."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import northlight_paths as fp
from windows_gpu_guard import processes


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ('dxvk2', 'dxvk3', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--reference-ref', default='HEAD', help='Archive only hdr_output.h from this revision; Frame/shaders are identical in both variants.')
    parser.add_argument('--linear-capture', type=Path, help='Also benchmark post/UI/Present with a private HDR-linear capture; includes Present waits and CPU gaps.')
    args = parser.parse_args();output = args.output.resolve()
    if fp.is_client(output):
        raise SystemExit('Use an isolated presentation stand directory.')
    processes();output.mkdir(parents=True, exist_ok=True)
    reference = subprocess.check_output(['git', 'rev-parse', '--verify', args.reference_ref+'^{commit}'], cwd=fp.REPO, text=True).strip()
    headers = output/'headers';headers.mkdir(exist_ok=True)
    before = headers/'hdr_output_before.h'
    before.write_bytes(subprocess.check_output(['git', 'show', reference+':src/proxy/hdr_output.h'], cwd=fp.REPO))
    binary = output/'hdr_present_gpu.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti',
                    '-I', str(headers), *fp.include_flags(), str(fp.TESTS/'hdr_present_gpu.cpp'), '-luser32', '-o', str(binary)], env=fp.zig_env(), check=True)
    data=bytearray(binary.read_bytes());flags=struct.unpack_from('<I',data,0x3c)[0]+22
    struct.pack_into('<H',data,flags,struct.unpack_from('<H',data,flags)[0]|0x20);binary.write_bytes(data)
    report = dict(scope='Native-MSAA surface controls; optional post/UI/Present benchmark includes Present waits and CPU gaps, not pure GPU execution or client FPS.',
                  game_launched=False, client_modified=False, reference_commit=reference,
                  source_sha256={str(p.relative_to(fp.REPO)):sha(p) for p in [fp.TESTS/'hdr_present_gpu.cpp', fp.src('hdr_output.h'), fp.src('hdr_frame.h'), fp.src('hdr_color.h'), fp.src('hdr_compiled_shaders.h')]},
                  reference_output_header_sha256=sha(before), executable_sha256=sha(binary), runs=[])
    if args.linear_capture:
        report['linear_capture_sha256']=sha(args.linear_capture)
    for backend, source in [('dxvk2', args.dxvk2), ('dxvk3', args.dxvk3)]:
        processes();folder=output/backend;folder.mkdir(exist_ok=True)
        runtime=folder/'backend_d3d9.dll';shutil.copy2(source.resolve(),runtime)
        command=[str(binary),str(runtime),str(args.linear_capture.resolve()) if args.linear_capture else '-']
        if args.linear_capture:command.append('--benchmark')
        result=subprocess.run(command,cwd=folder,env=dict(os.environ,DXVK_LOG_PATH=str(folder)),capture_output=True,text=True,timeout=180)
        (folder/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        events=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        report['runs'].append(dict(backend=backend,backend_sha256=sha(runtime),exit_code=result.returncode,events=events,stdout=result.stdout,stderr=result.stderr))
        (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        if result.returncode or 'HDR_PRESENT_NUMERICAL_PASS' not in result.stdout:
            raise SystemExit(result.stderr or result.stdout or str(result.returncode))
        if args.linear_capture and len(events)!=5:raise SystemExit('Incomplete presentation benchmark')
        print(backend,'PASS',flush=True)
    print('PASS:',output/'validation.json')


if __name__=='__main__':main()
