# northlight-test: requires=windows-exe,zig manual
"""Actual renderer: cold upload, fragmented VA, pool transitions and rollback.

--compile-only never loads a graphics backend. Prebuilt runs require the adjacent
.exe.provenance.json written by this runner and matching compilation inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def inputs():
    paths = [p for folder in (fp.SRC, fp.SHADERS) for p in folder.rglob('*') if p.is_file()]
    paths += [p for p in fp.TESTS.rglob('*') if p.is_file() and p.suffix in {'.cpp', '.h', '.bin', '.inc'}]
    paths += [fp.REPO / 'northlight_paths.py', fp.REPO / 'scripts/build_renderer.py']
    return {p.relative_to(fp.REPO).as_posix(): sha(p) for p in sorted(set(paths))}


def cache_inputs(root):
    # Conservative superset: every source referenced by the 5x5 screenshot tile
    # neighborhood, including records rejected by production crop predicates.
    center = (5139.84, -56.93)
    zero, tile = 17066.666666666666, 533.3333333333333
    import math
    tx, ty = math.floor((zero - center[1]) / tile), math.floor((zero - center[0]) / tile)
    paths, models = set(), set()
    for y in range(ty - 2, ty + 3):
        for x in range(tx - 2, tx + 3):
            path = root / 'Northrend' / f'{x}_{y}.fg3'
            if not path.is_file():
                continue
            data = path.read_bytes()
            assert data[:4] == b'FGS3' and struct.unpack_from('<I', data, 4)[0] == 3
            count = struct.unpack_from('<I', data, 8)[0]
            assert len(data) == 12 + count * 116, path
            paths.add(path)
            for index in range(count):
                models.add(data[12 + index * 116:44 + index * 116].hex())
    assert paths, 'Actual scene fixture has no source tiles'
    paths.update(root / 'models' / (key + '.fgs') for key in models)
    return {p.relative_to(root).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in sorted(paths)}


def pe_stack(path):
    data=path.read_bytes()
    assert data[:2]==b'MZ',path
    nt=struct.unpack_from('<I',data,0x3c)[0]
    assert data[nt:nt+4]==b'PE\0\0' and struct.unpack_from('<H',data,nt+24)[0]==0x10b,'An x86 PE32 host is required'
    reserve,commit=struct.unpack_from('<II',data,nt+24+72)
    assert reserve>=commit>0
    return {'path':str(path),'sha256':sha(path),'reserve':reserve,'commit':commit}


def host_stack(explicit):
    if explicit:return pe_stack(explicit)
    client=fp.client_root(False)
    if client and (client/'WoW.exe').is_file():return pe_stack(client/'WoW.exe')
    # Stock 3.3.5a host profile, not Zig's 16 MiB executable default. Thread
    # stacks inherit the host PE even when the thread is created by a DLL.
    return {'path':None,'sha256':None,'reserve':1500000,'commit':4096,'source':'WoW 3.3.5a PE32 profile'}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--compile-only', action='store_true')
    parser.add_argument('--backend', type=Path)
    parser.add_argument('--prebuilt', type=Path)
    parser.add_argument('--host-exe', type=Path, help='Read x86 stack reserve/commit from this game executable; never execute or modify it')
    parser.add_argument('--storage', choices=['spooled', 'resident', 'transitions', 'actual', 'fence-faults', 'probe-reuse', 'recovery-policy'], default='spooled')
    parser.add_argument('--available', nargs='+', type=int)
    args = parser.parse_args()
    out = fp.output_dir() / 'cold-upload'
    out.mkdir(parents=True, exist_ok=True)
    env = fp.zig_env()
    flags = {'creationflags': subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW}
    exe = out / 'cold-upload.exe'
    provenance_path = exe.with_suffix('.exe.provenance.json')
    stack = host_stack(args.host_exe)
    initial = inputs()
    runner_hash = sha(Path(__file__))
    sources = ['world_gi.cpp', 'world_mesh_plan.cpp', 'static_shadow_scene.cpp', 'settings_overlay.cpp', 'settings_panel.cpp',
               'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
    cmd = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-return-type',
           '-DIMGUI_USER_CONFIG="northlight_imconfig.h"', *fp.include_flags(), str(fp.TESTS / 'world_cold_upload_gpu.cpp'),
           *[str(fp.src(n)) for n in sources], '-luser32', '-luuid', '-o', str(exe)]
    if args.prebuilt:
        origin = json.loads(args.prebuilt.with_suffix('.exe.provenance.json').read_text(encoding='utf-8'))
        assert origin['inputs'] == initial, 'Prebuilt compilation inputs differ from current sources; rebuild'
        assert origin['binary_sha256'] == sha(args.prebuilt), 'Prebuilt binary provenance mismatch'
        if args.prebuilt.resolve() != exe.resolve():
            shutil.copy2(args.prebuilt, exe)
        build = origin['build']
    else:
        subprocess.run(cmd, env=env, check=True, **flags)
        build = {'command': cmd, 'compiler_sha256': sha(fp.zig()), 'source_root': str(fp.REPO)}
    assert inputs() == initial and sha(Path(__file__)) == runner_hash, 'Source changed during compilation'
    original_data = exe.read_bytes()
    data = bytearray(original_data)
    nt = struct.unpack_from('<I', data, 0x3c)[0]
    struct.pack_into('<H', data, nt + 22, struct.unpack_from('<H', data, nt + 22)[0] | 0x20)
    optional=nt+24
    old_stack=struct.unpack_from('<II',data,optional+72)
    struct.pack_into('<II',data,optional+72,stack['reserve'],stack['commit'])
    permitted={nt+22,nt+23,*range(optional+72,optional+80)}
    changed=[i for i,(old,new) in enumerate(zip(original_data,data)) if old!=new]
    assert set(changed)<=permitted,'Unexpected executable mutation'
    if args.prebuilt and args.prebuilt.resolve()==exe.resolve() and changed:
        raise RuntimeError('Use a fresh output directory to preserve the unmatched prebuilt control')
    exe.write_bytes(data)
    assert pe_stack(exe)['reserve']==stack['reserve'] and pe_stack(exe)['commit']==stack['commit']
    provenance = {'inputs': initial, 'binary_sha256': sha(exe), 'build': build,
                  'host_stack':stack,'pe_patch':{'original_stack_reserve':old_stack[0],'original_stack_commit':old_stack[1],'changed_offsets':changed,'code_bytes_unchanged':True}}
    provenance_path.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    if args.compile_only:
        print('COLD_UPLOAD_COMPILE_PASS', exe)
        return
    assert args.backend and args.backend.is_file()
    import windows_gpu_guard
    backend_hash = sha(args.backend)
    root = fp.world_cache() if args.storage == 'actual' else None
    cache = cache_inputs(root) if root else {}
    client = out / 'client'
    client.mkdir(exist_ok=True)
    reports = []
    for key, name in [('DXVK_LOG_PATH', 'logs'), ('DXVK_SHADER_CACHE_PATH', 'shaders'), ('DXVK_STATE_CACHE_PATH', 'state')]:
        folder = out / name
        folder.mkdir(exist_ok=True)
        env[key] = str(folder)
    for available in (args.available or ([1024] if args.storage in ('resident', 'transitions', 'fence-faults', 'probe-reuse', 'recovery-policy') else [1024, 434, 395, 307])):
        windows_gpu_guard.processes()
        start = time.perf_counter()
        command = [str(exe), str(args.backend), str(client), str(available), args.storage, *([str(root)] if root else [])]
        run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=180, **flags)
        (out / f'{available}.log').write_text(run.stdout + run.stderr, encoding='utf-8')
        print(run.stdout, flush=True)
        marker = 'COLD_RECOVERY_POLICY_PASS' if args.storage == 'recovery-policy' else 'COLD_PROBE_REUSE_PASS' if args.storage == 'probe-reuse' else 'COLD_FENCE_FAILURE_PASS' if args.storage == 'fence-faults' else 'ACTUAL_SCENE_PASS' if args.storage == 'actual' else 'COLD_TRANSITION_PASS' if args.storage == 'transitions' else 'COLD_UPLOAD_PASS'
        ok = run.returncode == 0 and run.stdout.count(marker) == (1 if args.storage in ('fence-faults', 'probe-reuse', 'recovery-policy') else 2)
        reports.append({'available_mib': available, 'storage': args.storage, 'wall_seconds': time.perf_counter() - start,
                        'command': command, 'exit_code': run.returncode, 'stdout': run.stdout, 'pass': ok})
        assert ok, run.stderr or run.stdout
    unchanged = inputs() == initial and sha(Path(__file__)) == runner_hash and sha(args.backend) == backend_hash
    cache_unchanged = not root or cache_inputs(root) == cache
    assert unchanged and cache_unchanged, 'Source/backend/cache changed during validation'
    host_unchanged=not stack['path'] or sha(Path(stack['path']))==stack['sha256']
    assert host_unchanged,'Host executable changed during validation'
    (out / 'validation.json').write_text(json.dumps({'pass': True, 'game_launched': False, 'actual_x86_va_pressure': True,
        'backend': str(args.backend), 'backend_sha256': backend_hash, 'runner_sha256': runner_hash,
        'binary': provenance, 'inputs_unchanged': unchanged, 'host_stack_unchanged':host_unchanged, 'cache_root': str(root) if root else None,
        'cache_inputs': cache, 'cache_unchanged': cache_unchanged, 'storage': args.storage, 'cases': reports}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
