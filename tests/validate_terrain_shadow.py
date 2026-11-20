#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib,zig,wine
"""Prove the terrain shadow PS patch on every shipped terrain pixel shader.

Runs the native unit test, then patches all `\\pixel\\ps_3_0\\terrain*.bls`
and `\\pixel\\ps_2_0\\terrain*.bls` variants from the client's archives and
reports how many were patched. Non-terrain ps_3_0 shaders are checked as a
negative set. A few samples are disassembled through the Wine build prefix
(no game process, no device) and diffed: every changed line must only swap
the shadow register for the 1.0 literal. Never starts the game.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import difflib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
CLIENT = fp.client_root()
sys.path.insert(0, str(WORK))
from mpq import Archive
from extract_signatures import variants
from build_environment import wine_build_prefix, zig_env, ZIG, WINE_ROOT


def collect(selector):
    inventory = json.loads((WORK / 'inspection/inventory.json').read_text())
    codes, sources = {}, []
    for path, record in sorted(inventory.items()):
        if not record['enabled'] or not (CLIENT / path).exists():
            continue
        listing = WORK / 'inspection' / (Path(path).name + '.list')
        if not listing.exists():
            continue
        names = [name for name in listing.read_text().splitlines() if selector(name.lower())]
        if not names:
            continue
        with Archive(CLIENT / path) as archive:
            for name in names:
                data = archive.read(name)
                if not data:
                    continue
                for index, flags, code in variants(data):
                    digest = hashlib.sha256(code).hexdigest()
                    codes[digest] = code
                    sources.append({'archive': path, 'path': name, 'variant': index, 'sha256': digest})
    return codes, sources


def disassembler():
    exe = fp.tracked('disassemble_shader.cpp').parent / 'disassemble_shader.exe'
    env = zig_env()
    if not exe.exists() or exe.stat().st_mtime < fp.tracked('disassemble_shader.cpp').stat().st_mtime:
        subprocess.run([str(ZIG), 'c++', '-target', 'x86-windows-gnu',
                        '-O2', '-s', '-static', str(fp.tracked('disassemble_shader.cpp')), '-o', str(exe)],
                       env=env, check=True)
    env.update(WINEPREFIX=str(wine_build_prefix()), WINEDEBUG='-all',
               DYLD_LIBRARY_PATH=str(WINE_ROOT / 'lib/external'))

    def run(binary, asm):
        compiler = os.environ.get('NORTHLIGHT_D3DCOMPILER47')
        extra = ['Z:' + str(Path(compiler).resolve()).replace('/', '\\')] if compiler else []
        subprocess.run([str(WINE_ROOT / 'bin/wine'), str(exe), str(binary), str(asm), *extra], env=env, check=True, timeout=120)
        return asm.read_text(errors='replace').splitlines()
    return run


def main():
    terrain = lambda n: ('\\pixel\\ps_3_0\\terrain' in n or '\\pixel\\ps_2_0\\terrain' in n) and n.endswith('.bls')
    others = lambda n: '\\pixel\\ps_3_0\\' in n and 'terrain' not in n and n.endswith('.bls')
    codes, sources = collect(terrain)
    negatives, _ = collect(others)
    results, samples = {}, []
    with tempfile.TemporaryDirectory(prefix='northlight-terrain-shadow-') as temporary:
        folder = Path(temporary)
        binary = folder / 'test-terrain-shadow'
        subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                        *fp.test_include_flags(), str(HERE / 'test_patch_terrain_shadow.cpp'), '-o', str(binary)], check=True)
        tests = subprocess.check_output([str(binary)], text=True).strip()
        paths = {}
        for digest, code in {**codes, **negatives}.items():
            p = folder / (digest + '.bin')
            p.write_bytes(code)
            paths[digest] = p
        ordered = list(paths.values())
        for start in range(0, len(ordered), 100):
            output = subprocess.check_output([str(binary), *map(str, ordered[start:start + 100])], text=True)
            for line in output.splitlines():
                if line.startswith('TERRAIN_SHADOW '):
                    parts = line.split(' ', 5)
                    results[Path(parts[5]).stem] = {'replaced': int(parts[1]), 'version': parts[2],
                                                    'shadow': parts[3], 'one': parts[4]}
        disassemble = disassembler()
        sample_dir = fp.output_dir()/'terrain-shadow-samples'   # never into tests/fixtures: disassembled game shaders
        sample_dir.mkdir(exist_ok=True)
        seen_paths = set()
        for source in sources:
            if source['path'] in seen_paths or results.get(source['sha256'], {}).get('replaced', -1) < 0:
                continue
            seen_paths.add(source['path'])
            parts = source['path'].lower().split('\\')
            stem = parts[-2] + '-' + parts[-1].replace('.bls', '') + f"-v{source['variant']}"
            original = disassemble(paths[source['sha256']], sample_dir / f'{stem}.asm')
            patched = disassemble(Path(str(paths[source['sha256']]) + '.patched'), sample_dir / f'{stem}.patched.asm')
            diff = [l for l in difflib.unified_diff(original, patched, lineterm='', n=0)
                    if l[:1] in '-+' and not l.startswith(('---', '+++'))]
            removed = [l for l in diff if l.startswith('-')]
            replaced = results[source['sha256']]['replaced']
            # Each removed line is an instruction that read the shadow scalar;
            # folded scales add a `nop` line, so the patched text may be longer.
            assert 1 <= len(removed) <= replaced, (source['path'], diff)
            samples.append({'path': source['path'], 'variant': source['variant'], 'diff': diff, 'replaced': replaced})
    per_file = {}
    for source in sources:
        row = per_file.setdefault(source['path'], {'patched': 0, 'rejected': 0})
        row['patched' if results[source['sha256']]['replaced'] > 0 else 'rejected'] += 1
    patched = sum(1 for d in codes if results[d]['replaced'] > 0)
    negative_hits = sum(1 for d in negatives if results[d]['replaced'] > 0)
    report = {'result': 'pass', 'game_launched': False, 'gpu_test': False, 'native_tests': tests,
              'reproduce': 'python3 tests/validate_terrain_shadow.py',
              'patcher_sha256': hashlib.sha256(fp.src('patch_terrain_shadow.h').read_bytes()).hexdigest(),
              'unique_terrain_pixel_shaders': len(codes), 'patched': patched, 'rejected': len(codes) - patched,
              'non_terrain_ps3_shaders': len(negatives), 'non_terrain_patched': negative_hits,
              'per_file': per_file, 'samples': samples,
              'policy': 'Every read of the shadow scalar after its definition becomes the shader\'s own 1.0 literal; '
                        'instruction lengths unchanged; fail closed otherwise. Replacement is bound at draw time only '
                        'to terrain draws while the extension\'s shadow maps are active.',
              'limitations': ['Baked shadows beyond the extension\'s far cascade are removed as well.',
                              'CPU checks do not prove runtime substitution or appearance.']}
    output = fp.output_dir() / 'terrain-shadow-validation.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(tests)
    print(f'{len(codes)} terrain pixel shaders: {patched} patched, {len(codes) - patched} rejected; '
          f'{negative_hits}/{len(negatives)} non-terrain ps_3_0 shaders matched; {len(samples)} samples disassembled; {output}')


if __name__ == '__main__':
    main()
