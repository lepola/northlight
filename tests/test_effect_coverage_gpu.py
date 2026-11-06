#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Native alpha layers retain coverage through early drains and world composition."""
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.REPO/'scripts/shaders'))
from shader_compiler import Compiler

renderer = fp.src('renderer.cpp').read_text()
world = fp.src('world_renderer.h').read_text()
assert renderer.count('effectCoverageDraw([&]{return ext->Draw') == 4
assert 'owner.effectCoverageDraw([&]{return args.submit(device);},true)' in renderer
assert 'void spellSync(){effectCoverage.endBatch();' in renderer
assert 'effectCoverage.texture(ext,saved.targets[0])' in renderer
assert 'world->setEffectCoverage(coverage,effectCoverage.hasEmission());' in renderer
assert 'NorthlightEffectCoverage::Capture::bind(ext,coverage,effectCoverage.hasEmission());' in renderer
assert 'effectCoverage.colorWrite(rawDestination)' in renderer and 'effectCoverage.colorWrite(rawSurface)' in renderer
coverage = renderer[renderer.index('HRESULT effectCoverageDraw('):renderer.index('    template<class Draw> HRESULT projectedDraw(')]
assert 'ext->peekVertexShader(vertex)' in coverage and 'ext->GetVertexShader(&vertex)' in coverage
assert 'vsTags.find(vertex)' in coverage and 'if(releaseVertex)drop(vertex);' in coverage
assert '(entry&kUnlitTag)' in coverage and 'NorthlightSpellLayer::Family::Particle)' in coverage
assert 'NorthlightSpellLayer::Family::ParticleUnlit)' in coverage
assert 'COLORWRITEENABLE),nativeOverlay)' in coverage
assert 'lastVs' not in coverage and 'replay)' in coverage
# c79 is reused by the normals pass: bind it afresh for the final stage.
assert world.index('NorthlightEffectCoverage::Capture::bind(d,effectCoverage,effectEmission);') < world.index('bool compositeOk=false;')
assert 'effectCoverage=nullptr;' in world

out = fp.output_dir()
source = out/'native.hlsl'
source.write_text('float4 Color:register(c0); float4 Native():COLOR0 {return Color;}\n'
                  'struct O {float4 c:COLOR0;float4 m:COLOR1;}; O NativeMRT(){O o;o.c=Color;o.m=Color;return o;}\n')
compiler = Compiler(allow_compatible_binary=True)
for sm in (2, 3):
    compiler.compile(source, 'Native', f'ps_{sm}_0', out/f'native{sm}.bin')
    compiler.compile(source, 'NativeMRT', f'ps_{sm}_0', out/f'native-mrt{sm}.bin')
exe = out/'effect-coverage.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', *fp.include_flags(), str(fp.TESTS/'effect_coverage_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'))
run = subprocess.run([str(exe), str(fp.COMPILED), backend, str(out), os.environ.get('NORTHLIGHT_TEST_MRT_MSAA', '0')], cwd=out, text=True, capture_output=True, timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr, end='')
run.check_returncode()
assert 'EFFECT_COVERAGE_PASS' in run.stdout
