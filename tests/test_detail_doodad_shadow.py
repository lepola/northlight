#!/usr/bin/env python3
# northlight-test: requires=zig
"""Bounded native grass shadow replay; optionally audits the tester's stock BLS."""
import ast
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

HERE = Path(__file__).resolve().parent
stub = next(ast.literal_eval(n.value) for n in ast.parse((HERE / 'test_terrain_snapshot.py').read_text()).body
            if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'stub' for t in n.targets))

def pixel_alpha(code):
    """Trace the native PS alpha chain; no rendering state is inferred from BLS."""
    from extract_world_signatures import instructions
    from extract_signatures import register
    registers = {}
    colors = {1: 1} if code[1] == 2 else {}
    expected = '*'.join(sorted(('texture0.a', 'color1.x')))
    def source(token, lane):
        kind, index = register(token)
        component = token >> (16 + 2 * lane) & 3
        if token >> 24 & 15:
            return '?'
        if kind == 1 and index in colors:
            return f'color{colors[index]}.{"xyzw"[component]}'
        return registers.get((kind, index, component), '?')
    for op, args in instructions(code):
        assert op != 65, 'native DetailDoodad shader unexpectedly discards'
        if op == 31:
            if code[1] == 3 and args[0] & 15 == 10:
                colors[register(args[1])[1]] = args[0] >> 16 & 15
            continue
        if not args or op == 81:
            continue
        kind, index = register(args[0])
        if kind not in (0, 8):
            continue
        mask = args[0] >> 16 & 15
        for lane in range(4):
            if not mask >> lane & 1:
                continue
            if op in (66, 95):
                value = f'texture{register(args[-1])[1]}.{"rgba"[lane]}'
            elif op == 1:
                value = source(args[1], lane)
            elif op == 5:
                value = '*'.join(sorted((source(args[1], lane), source(args[2], lane))))
            else:
                value = '?'
            registers[kind, index, lane] = value
    assert registers.get((8, 0, 3)) == expected, registers.get((8, 0, 3))

with tempfile.TemporaryDirectory(prefix='northlight-detail-shadow-') as tmp:
    out = Path(tmp)
    local_stub=stub
    if sys.platform == 'win32':
        local_stub = stub[:stub.index('using UINT')] + '#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n' + stub[stub.index('constexpr unsigned D3DUSAGE'):]
    (out / 'd3d9.h').write_text(local_stub)
    fixtures = []
    if client_fixtures.shaders_available():
        fp.use_source_modules()
        from world_scene_builder import Assets
        from extract_signatures import variants
        assets = Assets(archives='stock')
        try:
            for major in (2, 3):
                name = f'shaders\\vertex\\vs_{major}_0\\DetailDoodad.bls'
                codes = list(variants(assets.read(name)))
                assert len(codes) == 6
                for variant, _, code in codes:
                    fixture = out / f'detail-{major}-{variant}.bin'
                    fixture.write_bytes(code)
                    fixtures.append(str(fixture))
                pixels = list(variants(assets.read(f'shaders\\pixel\\ps_{major}_0\\DetailDoodad.bls')))
                assert len(pixels) == 8
                for _, _, code in pixels:
                    pixel_alpha(code)
            print('PASS 16 native pixel variants: alpha=texture0.a*COLOR1.x; no TEXKILL; BLS does not establish draw order/ZWRITE')
        finally:
            assets.close()
    else:
        print('SKIP native BLS subcase: set NORTHLIGHT_CLIENT and NORTHLIGHT_STORMLIB')
    for flags in (['-O2'], ['-O0', '-g']):
        target = out / ('grass-shadow-test.exe' if os.name == 'nt' else 'grass-shadow-test')
        subprocess.run([str(fp.zig()), 'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-UNDEBUG',
                        '-I', str(out), *fp.test_include_flags(), str(HERE / 'test_detail_doodad_shadow.cpp'),
                        '-o', str(target)], env=fp.zig_env(), check=True)
        subprocess.run([str(target), *fixtures], check=True)
