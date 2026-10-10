#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Legacy fog hold: native tests of the real CPU helper (legacy_fog.h Hold, clang++ plain and
ASan/UBSan) and source wiring checks. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
WORLD = (fp.REPO/'src/world/world_renderer.h').read_text()
RENDERER = (fp.REPO/'src/proxy/renderer.cpp').read_text()


def native():
    with tempfile.TemporaryDirectory(prefix='nl-fog-hold-') as temp:
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            binary = Path(temp)/'test'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(HERE/'test_legacy_fog_hold.cpp'), '-o', str(binary)], check=True)
            out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
            assert 'legacy fog hold ok' in out, out


def body(text, start):
    a = text.index(start)
    b = text.index('{', a)
    depth, end = 1, b+1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[a:end]


def wiring():
    terrain = body(WORLD, 'void terrainContext()')
    assert 'if(valid||failed)return;' in terrain
    for gone in ('terrainFogTries', 'TerrainFogMaxTries', 'noteTerrainDraw', 'lateFogFrameEnd', 'LEGACYFOG late', 'fogAttempt', 'lastLateAttempt',
                 'lastTerrainOkAttempt', 'frameTDraw', 'frameTReach', 'frameLate', 'tdraw=', 'treach=', 'late terrain'):
        assert gone not in WORLD and gone not in RENDERER, gone
    assert 'readOriginalFog(12,true);if(legacyFogKnown)traceFog=1;' in terrain[terrain.index('valid=true;projection[0]'):]
    assert 'legacyFogKnown=fogKnown;' in body(WORLD, 'void readOriginalFog(')
    reset = 'valid=false;traceContext=0;traceFog=0;legacyFogKnown=false;'
    assert reset in WORLD and 'legacyFog=NorthlightLegacyFog::Constants{}' in WORLD[WORLD.index(reset):WORLD.index(reset)+200]
    assert 'NorthlightLegacyFog::Hold legacyFogHold;' in WORLD
    assert 'legacyFogHold.update(active->map,legacyFog,legacyFogKnown,now)' in WORLD
    assert 'memcpy(c[25],uploadedFog.parameters,16);memcpy(c[26],uploadedFog.color,16);' in WORLD
    assert 'memcpy(c[25],legacyFog' not in WORLD
    assert 'horizonHazeState.update(active->map,legacyFog.parameters,legacyFog.color,' in WORLD
    assert WORLD.count('legacyFogHold.update(') == 1
    assert 'wmoFogCheck' not in RENDERER and 'wmoFogCheck' not in WORLD and 'wmoCheck' not in WORLD
    assert 'context=%u fog=%u skip=%s' in RENDERER and 'frameTraceFog()' in RENDERER and 'unsigned frameTraceFog()' in WORLD


if __name__ == '__main__':
    native()
    wiring()
    print('legacy fog hold: ok')
