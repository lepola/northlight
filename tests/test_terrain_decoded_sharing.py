#!/usr/bin/env python3
# northlight-test: requires=cxx slow
"""Exact decoded sharing, mutable values, charged lifetimes and failure controls."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
helper_paths = [Path(__file__), repo / 'northlight_paths.py', repo / 'tests/test_terrain_classification.py',
                repo / 'tests/test_terrain_snapshot.py', repo / 'tests/fixtures/terrain_classification_reference.h']
helper_before = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in helper_paths}
sys.path.insert(0, str(repo))
import northlight_paths as fp
from test_terrain_classification import headers


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='terrain-sharing-') as tmp:
        out = args.output or (fp.output_dir() / 'terrain-decoded-sharing')
        assert not out.exists() or not any(out.iterdir()), 'Use a new/empty evidence directory'
        out.mkdir(parents=True, exist_ok=True)
        source = fp.TESTS / 'test_terrain_decoded_sharing.cpp'
        watched = [Path(__file__), source, fp.REPO / 'northlight_paths.py',
                   fp.TESTS / 'test_terrain_snapshot.py', fp.TESTS / 'test_terrain_classification.py',
                   fp.FIXTURES / 'terrain_classification_reference.h']
        watched += [path for path in fp.SRC.rglob('*') if path.is_file()]
        inputs = {str(path.resolve()): sha(path) for path in watched}
        assert all(inputs[path] == value for path, value in helper_before.items()), 'Helper input changed during import'
        headers(out)
        device = (out / 'rotation_device.h').read_text()
        device = device.replace('Declaration declaration;float view[16]={};',
                                'Declaration declaration;float view[16]={};UINT stride=24;')
        device = device.replace('16+actualIndex*24+4', '16+actualIndex*stride+4')
        device = device.replace('*offset=16;*stride=24;', '*offset=16;*stride=this->stride;')
        original = '++locks;*out=bytes.data()+offset;return D3D_OK;'
        assert device.count(original) == 1
        device = device.replace(original, '++locks;*out=bytes.data()+offset;if constexpr(std::is_same_v<Interface,IDirect3DVertexBuffer9>){vertexReadLocked=true;if(directNullLock)*out=nullptr;}return D3D_OK;')
        original = 'HRESULT Unlock()override{++unlocks;assert(unlocks<=locks);return D3D_OK;}'
        assert device.count(original) == 1
        device = device.replace(original, 'HRESULT Unlock()override{++unlocks;assert(unlocks<=locks);if constexpr(std::is_same_v<Interface,IDirect3DVertexBuffer9>){vertexReadLocked=false;vertexUnlocked();if(directUnlockFailure)return HRESULT(0x80004005u);}return D3D_OK;}')
        (out / 'decoded-device.h').write_bytes(device.encode())
        generated = {str(path.resolve()): sha(path) for path in out.iterdir() if path.is_file()}
        commands, results = [], []

        def unchanged(records):
            assert all(Path(path).is_file() and sha(Path(path)) == value
                       for path, value in records.items()), 'Frozen source/helper/generated input changed'

        def run(command):
            commands.append(command)
            (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
            result = subprocess.run(command, capture_output=True, text=True)
            return result

        for label, flags in [('O2', ['-O2']), ('O0', ['-O0']),
                             ('asan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
            unchanged(inputs);unchanged(generated)
            exe = out / label
            command = ['clang++', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
                       '-I', str(out), *fp.test_include_flags(), str(source), '-o', str(exe)]
            compiled = run(command)
            (out / (label + '-compile.log')).write_text(compiled.stdout + compiled.stderr)
            if compiled.returncode: print(compiled.stdout + compiled.stderr, file=sys.stderr, flush=True)
            compiled.check_returncode()
            result = run([str(exe)])
            (out / (label + '.log')).write_text(result.stdout + result.stderr)
            if result.returncode: print(result.stdout + result.stderr, file=sys.stderr, flush=True)
            result.check_returncode()
            results.append({'mode': label, 'exe_sha256': sha(exe), 'pass': True})
            print(label, result.stdout, end='', flush=True)

        controls = [
            ('decoded', '&&!std::memcmp(a,b,count*sizeof(*a))', '&&true', 'DECODED_XYZ_MISMATCH'),
            ('offsets', 'if(std::memcmp(stored+i,&value,sizeof value))return false;',
             '(void)value;', 'OFFSET_LAYOUT_REVALIDATION_MISMATCH'),
            ('direct-xyz', 'std::memcpy(&p,source+std::size_t(index-smallest)*detail.stride,sizeof(Position));',
             'std::memcpy(&p,source+std::size_t(index-smallest)*detail.stride,sizeof(Position));p.z+=0.5f;', 'DECODED_XYZ_MISMATCH'),
            ('direct-revision', 'if(!directCold||verified)rememberContent',
             'if(true)rememberContent', 'DIRECT_UNSTABLE_CACHE_PUBLISHED'),
            ('direct-lock', 'if(directCold)preparePositions();',
             'if(false)preparePositions();', 'TERRAIN_ALLOCATION_DURING_VERTEX_LOCK'),
        ]
        for label, original, replacement, marker in controls:
            mutant = out / ('negative-' + label);mutant.mkdir()
            text = fp.src('terrain_capture_bounds.h').read_text()
            assert text.count(original) == 1
            text = text.replace(original, replacement)
            if label == 'direct-lock':
                original = 'if(directCold){copyPositions'
                assert text.count(original) == 1
                text = text.replace(original, 'if(directCold){preparePositions();copyPositions')
            (mutant / 'terrain_capture_bounds.h').write_bytes(text.encode())
            exe = mutant / 'wrong'
            compiled = run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', '-I', str(mutant), '-I', str(out),
                            *fp.test_include_flags(), str(source), '-o', str(exe)])
            (out / ('negative-' + label + '-compile.log')).write_text(compiled.stdout + compiled.stderr)
            if compiled.returncode: print(compiled.stdout + compiled.stderr, file=sys.stderr, flush=True)
            compiled.check_returncode()
            result = run([str(exe)])
            (out / ('negative-' + label + '.log')).write_text(result.stdout + result.stderr)
            assert result.returncode != 0 and marker in result.stderr, result
        unchanged(inputs);unchanged(generated)
        (out / 'validation.json').write_text(json.dumps({'inputs': inputs, 'generated_inputs': generated,
            'inputs_unchanged': True, 'helper_inputs_before_import': helper_before, 'results': results, 'commands': commands,
            'compiler_version': subprocess.check_output(['clang++', '--version'], text=True),
            'unsafe_equality_control_rejected': True, 'unsafe_offset_layout_control_rejected': True,
            'unsafe_direct_xyz_revision_lock_controls_rejected': True,
            'game_launched': False}, indent=2) + '\n')
        print('PASS unsafe decoded and offset equality controls fail their independent geometry/revalidation oracles')


if __name__ == '__main__':
    main()
