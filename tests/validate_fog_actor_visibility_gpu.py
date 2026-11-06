#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu manual
"""Native offscreen fog guard oracle and alternating old/new timings; no game.

The regional field, shadow maps and noise are authored controls, not a game replay.
The independent probe checks both zero and positive visibility and rejects a guard
that incorrectly skips positive visibility. Production comparisons retain every
sample and use the complete WorldFog/Clouds actor and non-actor entries.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1] / 'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler, validate_assembly


SETUP = r'''
 const int workload=std::atoi(std::getenv("NORTHLIGHT_FOG_VISIBILITY_CASE"));
 const bool probe=std::getenv("NORTHLIGHT_FOG_VISIBILITY_PROBE")!=nullptr;
 bank.count=79;c[65][3]=0;c[24][0]=.0001f;
 if(workload==5){c[16][0]=-.57735026f;c[16][1]=c[16][2]=.57735026f;c[17][0]=.15f;c[17][1]=.3f;c[17][2]=.8f;}
 NorthlightActorShadowMap::matrix(NorthlightGI::Vec3(0,8,1),NorthlightGI::Vec3(0,8,1),NorthlightGI::Vec3(c[16][0],c[16][1],c[16][2]),c[68]);
 c[66][0]=c[66][1]=-1;c[66][2]=c[66][3]=1;c[72][0]=workload==3?0.f:1.f;
 if(workload==4)c[17][0]=c[17][1]=c[17][2]=0;
 Image farMap{128,128,D3DFMT_R32F,4,std::vector<unsigned char>(128*128*4)};
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){float z=workload==1?0.f:workload==2?float((x/8+y/8)%2):1.f;std::memcpy(farMap.pixels.data()+4*(y*128+x),&z,4);}
 bind(3,farMap);c[2][3]=1.f/128;
 Image actorMap{1024,1024,D3DFMT_R32F,4,std::vector<unsigned char>(1024*1024*4)};
 for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){float z=probe?.25f:(x/29+y/37)%3?.35f:1.f;std::memcpy(actorMap.pixels.data()+4*(y*1024+x),&z,4);}
 bind(4,actorMap);
'''

REFERENCE = r'''
 uint64_t digest[2]={14695981039346656037ull,14695981039346656037ull};
 for(unsigned label=0;label<2;++label)for(unsigned char value:output[label]){digest[label]^=value;digest[label]*=1099511628211ull;}
 std::printf("DIGEST old=%llu new=%llu\n",(unsigned long long)digest[0],(unsigned long long)digest[1]);
 unsigned rgbNonzero=0;for(size_t at=0;at<size_t(W)*H*4;++at)if(at%4!=3){float value;if(bits==16){uint16_t raw;std::memcpy(&raw,output[1].data()+2*at,2);value=half(raw);}else std::memcpy(&value,output[1].data()+4*at,4);rgbNonzero+=value!=0;}
 std::printf("RGB_NONZERO count=%u\n",rgbNonzero);
 if(probe){unsigned errors=0,zero=0,positive=0;
  for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned mode=std::min(4u,unsigned((x+.5f)/W*5));const float world=mode==0?0.f:mode==1?.00000011920928955078125f:mode==2?.25f:mode==3?1.f:std::nextafter(1.f,2.f);
   const float expected[]={0,world,mode?1.f:0.f,1};
   for(unsigned channel=0;channel<4;++channel){const size_t at=(size_t(y)*W+x)*4+channel;float value;if(bits==16){uint16_t raw;std::memcpy(&raw,output[1].data()+2*at,2);value=half(raw);}else std::memcpy(&value,output[1].data()+4*at,4);const float tolerance=bits==16?.0006f:0.f;errors+=!std::isfinite(value)||std::abs(value-expected[channel])>tolerance;}
   mode?++positive:++zero;
  }
  assert(zero&&positive);std::printf("ORACLE errors=%u zero=%u positive=%u\n",errors,zero,positive);std::fflush(stdout);
 }
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--baseline-ref', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compile-only', action='store_true')
    parser.add_argument('--reuse-compiled', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('guard', fp.TESTS / 'validate_native_scene_trace.py')
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    guard.processes()
    report = {'baseline_ref': args.baseline_ref, 'game_launched': False,
              'authentic_source_replay': False, 'whole_scene_replay': False,
              'synthetic_shadow_maps': True, 'shaders': {}, 'checks': []}
    if not args.reuse_compiled:
        os.environ['NORTHLIGHT_OUT'] = str(output / 'compiler')
        compiler = Compiler(allow_compatible_binary=True)
        report['compiler'] = compiler.provenance
        for variant in ('old', 'new'):
            folder = output / variant
            folder.mkdir(exist_ok=True)
            for name in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
                data = (subprocess.check_output(['git', 'show', f'{args.baseline_ref}:shaders/{name}'], cwd=fp.REPO)
                        if variant == 'old' else (fp.SHADERS / name).read_bytes())
                (folder / name).write_bytes(data)
            for entry in ('WorldFog', 'WorldFogActor', 'FogClouds', 'FogCloudsActor'):
                binary = folder / (entry + '.bin')
                compiler.compile(folder / 'world_effects.hlsl', entry, 'ps_3_0', binary)
                report['shaders'][variant + '/' + entry] = {
                    'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                    **validate_assembly(Path(str(binary) + '.asm'), 'ps_3_0', slot_limit=600 if entry.endswith('Actor') else 512)}
        for entry in ('WorldFog', 'WorldFogActor', 'FogClouds', 'FogCloudsActor'):
            old, new = (report['shaders'][v + '/' + entry] for v in ('old', 'new'))
            assert old['samplers'] == new['samplers']
            for opcode in ('texld', 'texldl', 'texldb', 'texldd', 'texldp'):
                assert old['instructions'].get(opcode, 0) == new['instructions'].get(opcode, 0)
            if not entry.endswith('Actor'):
                assert old['sha256'] == new['sha256'], 'Non-actor bytecode changed'
        source = (fp.SHADERS / 'world_effects.hlsl').read_text()
        helper = source[source.index('sampler2D FogActorMap'):source.index('\nfloat2 probeUV')]
        condition = 'actorIntegration&&FogActorInfo.x>0&&visibility>0'
        assert source.count('if(' + condition + ')') == 2
        probe = '''
float4 WorldFog(float2 uv:TEXCOORD0):COLOR0{
 float mode=floor(uv.x*5),visibility=mode<1?0:mode<2?.00000011920928955078125:mode<3?.25:mode<4?1:1.00000011920928955078125;
 float world=visibility,called=0;bool actorIntegration=true;
 [branch]if(CONDITION){called=1;visibility=fogCombineActor(visibility,fogActorShadow(float3(0,0,.75),float3(.25,0,0)));}
 return float4(visibility,world,called,1);
}
'''
        for variant, guard_condition in [('probe', condition), ('negative', condition.replace('visibility>0', 'visibility==0'))]:
            fixture = output / (variant + '.hlsl')
            fixture.write_text('float4 PassInfo:register(c24);\n' + helper + probe.replace('CONDITION', guard_condition))
            compiler.compile(fixture, 'WorldFog', 'ps_3_0', output / (variant + '.bin'))
        cpp = (fp.TESTS / 'regional_fog_cache_gpu.cpp').read_text().replace(
            '#include "regional_fog.h"', '#include "regional_fog.h"\n#include "actor_shadow_map.h"')
        needle = ' IDirect3DVolumeTexture9* noise=nullptr;'
        assert cpp.count(needle) == 1
        cpp = cpp.replace(needle, SETUP + needle)
        needle = 'std::fflush(stdout);assert(!changed&&!nonfinite&&maximum==0);'
        assert cpp.count(needle) == 1
        cpp = cpp.replace(needle, needle + REFERENCE)
        (output / 'visibility.cpp').write_text(cpp)
        with (output / 'compile.log').open('w') as log:
            subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                            '-I', str(fp.TESTS), *fp.test_include_flags(), str(output / 'visibility.cpp'),
                            str(fp.src('world_gi.cpp')), '-luser32', '-o', str(output / 'visibility.exe')],
                           env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
        (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    else:
        report = json.loads((output / 'validation.json').read_text())
        report['checks'] = []
    # A reused stand must still correspond to these exact sources and binaries.
    report['source_sha256'] = {}
    for variant in ('old', 'new'):
        for name in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
            expected = (subprocess.check_output(['git', 'show', f'{args.baseline_ref}:shaders/{name}'], cwd=fp.REPO)
                        if variant == 'old' else (fp.SHADERS / name).read_bytes())
            assert (output / variant / name).read_bytes() == expected, 'Stale compiled source: ' + variant + '/' + name
            report['source_sha256'][variant + '/' + name] = hashlib.sha256(expected).hexdigest()
        for entry in ('WorldFog', 'WorldFogActor', 'FogClouds', 'FogCloudsActor'):
            assert hashlib.sha256((output / variant / (entry + '.bin')).read_bytes()).hexdigest() == report['shaders'][variant + '/' + entry]['sha256']
    (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    if args.compile_only:
        return
    backend = Path(os.environ['SystemRoot']) / 'SysWOW64/d3d9.dll'
    for probe in ('probe', 'negative'):
        for bits in (16, 32):
            folder = output / f'{probe}-{bits}'
            folder.mkdir(exist_ok=True)
            env = fp.zig_env()
            env.update(NORTHLIGHT_FOG_VISIBILITY_CASE='0', NORTHLIGHT_FOG_VISIBILITY_PROBE='1')
            guard.processes()
            command = [str(output / 'visibility.exe'), args.prefix, *[str(output / (probe + '.bin'))] * 2,
                       'WorldFog', str(backend), '7', str(bits), '160', '64', '1']
            run = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, timeout=120)
            (folder / 'gpu.log').write_text(run.stdout + run.stderr)
            assert run.returncode == 0, (folder, run.stderr[-1000:])
            oracle = next(line for line in run.stdout.splitlines() if line.startswith('ORACLE '))
            errors = int(oracle.split('errors=')[1].split()[0])
            assert (errors == 0) == (probe == 'probe'), oracle
            report['checks'].append({'probe': probe, 'bits': bits, 'oracle': oracle})
    for entry in ('WorldFog', 'FogClouds'):
        for bits in ((16,) if args.benchmark else (16, 32)):
            for workload in ((0, 1, 2) if args.benchmark else range(6)):
                for reverse in ((False, True) if args.benchmark else (False,)):
                    folder = output / f'{entry}-{bits}-{workload}-{int(reverse)}'
                    folder.mkdir(exist_ok=True)
                    env = fp.zig_env()
                    env.update(NORTHLIGHT_FOG_VISIBILITY_CASE=str(workload))
                    shaders = [output / variant / (entry + 'Actor.bin') for variant in ('old', 'new')]
                    if reverse:
                        shaders.reverse()
                    guard.processes()
                    command = [str(output / 'visibility.exe'), args.prefix, *map(str, shaders), entry, str(backend),
                               '7', str(bits), *(['960', '540', '8'] if args.benchmark else ['256', '128', '1'])]
                    run = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, timeout=120)
                    (folder / 'gpu.log').write_text(run.stdout + run.stderr)
                    assert run.returncode == 0, (folder, run.stderr[-1000:])
                    row = json.loads(next(line for line in run.stdout.splitlines() if line.startswith('{')))
                    assert row['changed'] == row['nonfinite'] == 0 and row['max_abs'] == 0, row
                    digest = next(line for line in run.stdout.splitlines() if line.startswith('DIGEST '))
                    row['output_digest'] = digest.split('old=')[1].split()[0]
                    assert row['output_digest'] == digest.split('new=')[1].split()[0]
                    row['nonzero_rgb_components'] = int(next(line for line in run.stdout.splitlines()
                                                           if line.startswith('RGB_NONZERO ')).split('count=')[1])
                    if workload == 4:
                        assert row['nonzero_rgb_components'] > 0, 'Zero-source ambient fog was blanked'
                    if reverse:
                        row['old_ms'], row['new_ms'] = row['new_ms'], row['old_ms']
                    row.update(workload=workload, reverse=reverse,
                               ratio=row['new_ms'] / row['old_ms'] if row['old_ms'] else None)
                    report['checks'].append(row)
                    (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
                    print(json.dumps(row), flush=True)
    for entry in ('WorldFog', 'FogClouds'):
        for bits in ((16,) if args.benchmark else (16, 32)):
            hashes = {row['output_digest'] for row in report['checks']
                      if row.get('shader') == entry and row['output_bits'] == bits and row['workload'] < 3}
            assert len(hashes) == 3, 'Lit/occluded/mixed production controls must differ'
    report['pass'] = True
    (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'FOG_VISIBILITY_PASS checks={len(report["checks"])} no game launched', flush=True)


if __name__ == '__main__':
    main()
