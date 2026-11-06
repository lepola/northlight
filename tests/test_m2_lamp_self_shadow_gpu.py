#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Production cube tests with real stock and active-mod lamps on D3D9 and DXVK."""
import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from test_m2_lamp_self_shadow import fixtures
import client_fixtures

def readback_metrics(out,case):
    def pixels(path):
        data=path.read_bytes();magic,w,h,fmt,stride=struct.unpack('<5I',data[:20])
        assert magic==0x31524746 and fmt==116 and stride==16 and len(data)==20+w*h*16
        values=array('f');values.frombytes(data[20:]);return values
    direct=pixels(out/(case.name+'-direct.fgr'))
    energy=[direct[i]+direct[i+1]+direct[i+2] for i in range(0,len(direct),4)]
    lo,hi=float('inf'),-float('inf')
    for phase in range(3):
        corrected=pixels(out/(case.name+f'-receiver-{phase}.fgr'))
        rgb=[v for i,v in enumerate(corrected) if i%4!=3]
        lo=min(lo,min(rgb));hi=max(hi,max(rgb))
    assert max(energy)-min(energy)>.05 and lo>=-.001
    return {'direct_rgb_sum_min':min(energy),'direct_rgb_sum_max':max(energy),'direct_rgb_sum_range':max(energy)-min(energy),
            'corrected_rgb_min':lo,'corrected_rgb_max':hi,'stand_image':str(out/(case.name+'-stand-0.fgr'))}

def character_fixture(out):
    """Actual stock body geometry/weights; the GPU stand supplies controlled poses."""
    fp.use_source_modules()
    from world_scene_builder import Assets, array, unpack
    name='character\\human\\male\\humanmale.m2'
    assets=Assets(archives='stock')
    try:
        model=assets.read(name);skin=assets.read(name[:-3]+'00.skin')
        nv,ov=unpack('<II',model,60);n,o=unpack('<II',skin,4);lookup=[x[0] for x in array(skin,o,n,'<H')]
        n,o=unpack('<II',skin,12);indices=[x[0] for x in array(skin,o,n,'<H')]
        n,o=unpack('<II',skin,20);skin_bones=array(skin,o,n,'<4B')
        n,o=unpack('<II',model,120);bone_combos=[x[0] for x in array(model,o,n,'<H')]
        n,o=unpack('<II',skin,28);subsets=array(skin,o,n,'<10H7f')
        subset=max((s for s in subsets if s[8]>1),key=lambda s:s[5])
        first=subset[4]+(subset[1]<<16);used=indices[first:first+subset[5]];vertices=sorted(set(used));assert max(lookup[i] for i in vertices)<nv
        raw=[bytearray(model[ov+lookup[i]*48:ov+(lookup[i]+1)*48]) for i in vertices]
        # CM2Shared::SetVertices supplies SKIN palette indices to the shader.
        # Resolve through this section's boneComboIndex before compacting its
        # global bones. Keep distinct SKIN vertices even when M2 lookup repeats.
        for v,skin_index in zip(raw,vertices):
            for k in range(4):v[16+k]=bone_combos[subset[7]+skin_bones[skin_index][k]] if v[12+k] else 0
        bones=sorted({v[16+k] for v in raw for k in range(4) if v[12+k]});assert 1<len(bones)<=75
        palette={bone:i for i,bone in enumerate(bones)}
        for v in raw:
            assert sum(v[12:16])==255
            for k in range(4):v[16+k]=palette[v[16+k]] if v[12+k] else 0
        influence=[sum(v[12+k] for v in raw for k in range(4) if v[16+k]==bone) for bone in range(len(bones))]
        pose_bone=max(range(len(bones)),key=lambda b:influence[b])
        remap={v:i for i,v in enumerate(vertices)}
        data=struct.pack('<5I',0x31414c4e,len(raw),len(used),len(bones),pose_bone)+b''.join(raw)+struct.pack('<%dI'%len(used),*(remap[i] for i in used))
        (out/'character.bin').write_bytes(data)
        shutil.copyfile(client_fixtures.four_bone_vs3(),out/'character-vs.bin')
        evidence={'model':name,'model_sha256':hashlib.sha256(model).hexdigest(),'skin_sha256':hashlib.sha256(skin).hexdigest(),
            'archive':assets.origin(name),'vertices':len(raw),'triangles':len(used)//3,'palette_bones':bones,'controlled_pose_bone':pose_bone,
            'geometry':'largest multi-influence stock skin section; original positions, weights, normals and UVs; SKIN indices + section boneComboIndex + M2 bone-combo table resolve the compact palette',
            'animation':'controlled placement and one-bone translation, not a captured gameplay animation'}
        (out/'character.json').write_text(json.dumps(evidence,indent=2)+'\n')
        return evidence
    finally:assets.close()

def live_fixture(case):
    """Actual local lamp triangles; the stand draws them only through replay."""
    fp.use_source_modules()
    from world_scene_builder import Assets,InstancedBuilder
    names={'dalaran-post':'dalaran_lamppost01','dalaran-wall':'dalaran_lampwall01'}
    name='world\\expansion02\\doodads\\dalaran\\'+names[case.name]+'.m2'
    assets=Assets()
    try:
        builder=InstancedBuilder(assets,case);mesh=builder.m2(name)
        assert all(material[1]<=0 for material in mesh[2]),'Live opaque fixture must retain every material without alpha approximation'
        triangles=[tuple(value for index in triangle[:3] for value in mesh[0][index][:3]) for triangle in mesh[1]]
        (case/'native-lamp.bin').write_bytes(struct.pack('<4sI',b'NLL1',len(triangles))+b''.join(struct.pack('<9f',*triangle) for triangle in triangles))
        # Upload storage is required by WorldRenderer. Every resulting batch
        # is removed before point rendering; none is a cached lamp caster.
        builder.add_mesh(mesh,((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'m2',1)
        builder.write_tile(case/'tile.fg3',builder.instances)
        (case/'native-lamp.json').write_text(json.dumps({'asset':name,'asset_sha256':hashlib.sha256(assets.read(name)).hexdigest(),'triangles':len(triangles),
            'cached_lamp_casters':0,'draw':'actual opaque M2 triangles, controlled one-bone SM3 replay; production patch, deformation and ownership helper; lamp remains fixed in world space'},indent=2)+'\n')
    finally:assets.close()


def compile_gpu(out,source_root=None):
    source_root=Path(source_root or fp.REPO).resolve()
    exe=out/'lamp-gpu.exe'
    sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
    root=Path(__file__).resolve().parents[1]
    includes=[str(source_root/Path(flag).relative_to(root)) if flag!='-I' and Path(flag).is_relative_to(root) and Path(flag).relative_to(root).parts[0]=='src' else flag for flag in fp.test_include_flags()]
    source=out/'lamp-gpu-stand.cpp'
    source.write_text((fp.TESTS/'m2_lamp_self_shadow_gpu.cpp').read_text().replace('../src/proxy/renderer.cpp',(source_root/'src/proxy/renderer.cpp').as_posix()))
    cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*includes,source,*(source_root/fp.src(s).relative_to(root) for s in sources),'-luser32','-luuid','-o',exe]
    with (out/'compile.log').open('w') as log:subprocess.run(list(map(str,cmd)),check=True,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,timeout=300)
    (out/'compile-command.json').write_text(json.dumps({'source_root':str(source_root),'command':list(map(str,cmd)),
        'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()},indent=2)+'\n')
    return exe

def run_actor(exe,backend,settings,case,mode,out,character_source,allow_running_game=False):
    from validate_native_scene_trace import processes
    processes(allow_running_game);out.mkdir(exist_ok=True)
    for name in ('character.bin','character-vs.bin','character.json'):
        if (out/name).resolve()!=(character_source/name).resolve():shutil.copyfile(character_source/name,out/name)
    command=[str(exe),str(backend),str(settings)+os.sep,str(case.resolve()),mode,str(out)]
    result=subprocess.run(command,capture_output=True,text=True,cwd=out,timeout=180,
        env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'})
    (out/'gpu.log').write_text(result.stdout+result.stderr);print(result.stdout+result.stderr,flush=True);assert result.returncode==0
    return {'backend':str(backend),'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'case':str(case),'mode':mode,'exit_code':result.returncode,
        'soft_consumer_verified':'soft' in mode or 'floor' in mode,'sunlit':'day' in mode,'legacy_sun_gate':'legacy' in mode,
        'physical_floor_negative_control':'floor' in mode,**readback_metrics(out,case)}

def benchmark(out,exe,args,backends,baseline_exe=None):
    """Balanced source A/B processes; timestamps exclude fixture/readback work."""
    from validate_native_scene_trace import processes
    baseline=out/'baseline';baseline.mkdir(exist_ok=True)
    executables={'baseline':baseline_exe or compile_gpu(baseline,args.baseline_source_root),'candidate':exe}
    records=[];native_cpu=[]
    for i,backend in enumerate(backends):
        settings=out/f'settings-{i}';settings.mkdir(exist_ok=True)
        for case in args.actor_case:
            for pair in range(4):
                for variant in (('baseline','candidate') if pair%2==0 else ('candidate','baseline')):
                    processes(False)
                    mode='actor-benchmark'+('-day' if args.sunlit else '')+('-hard' if args.benchmark_hard else '')
                    command=[str(executables[variant]),str(backend),str(settings)+os.sep,str(case.resolve()),mode,str(out)]
                    result=subprocess.run(command,capture_output=True,text=True,cwd=out,timeout=180,
                        env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'})
                    log=out/f'benchmark-{i}-{case.name}-{pair}-{variant}.log';log.write_text(result.stdout+result.stderr)
                    assert result.returncode==0,(log,result.stdout+result.stderr)
                    samples=[json.loads(line.removeprefix('LAMP_BENCH ')) for line in result.stdout.splitlines() if line.startswith('LAMP_BENCH ')]
                    native_cpu.extend(dict(json.loads(line.removeprefix('LAMP_NATIVE_CPU ')),backend=str(backend),variant=variant,pair=pair) for line in result.stdout.splitlines() if line.startswith('LAMP_NATIVE_CPU '))
                    assert len(samples)==32 and all(row['selected_lights']==row['point_shadow_limit']==1 and (row['facing'] or row['shadow_pixels']==0) for row in samples)
                    records.extend(dict(row,backend=str(backend),variant=variant,pair=pair) for row in samples)
    summary=[]
    for backend in backends:
        for case in args.actor_case:
            for facing in (True,False):
                for cache in ('warm-static','forced-refresh'):
                    selected=[row for row in records if row['backend']==str(backend) and row['case']==case.name and row['facing']==facing and row['cache']==cache]
                    row={'backend':str(backend),'case':case.name,'facing':facing,'cache':cache,'soft':not args.benchmark_hard,'sunlit':args.sunlit}
                    for metric in ('cpu_ms','gpu_ms'):
                        values={variant:[r[metric] for r in selected if r['variant']==variant] for variant in executables}
                        row.update({variant+'_'+metric:statistics.median(values[variant]) for variant in values})
                        deltas=[]
                        for pair in range(4):
                            pair_values={variant:statistics.median(r[metric] for r in selected if r['variant']==variant and r['pair']==pair) for variant in executables}
                            deltas.append(pair_values['candidate']-pair_values['baseline'])
                        row['paired_delta_'+metric]=statistics.median(deltas)
                        row['paired_delta_range_'+metric]=[min(deltas),max(deltas)]
                    summary.append(row)
                    print('LAMP_BENCH_SUMMARY',json.dumps(row),flush=True)
    evidence={'game_launched':False,'scope':'one selected actual lamp, independent box and real weighted character; 512x512 production hard/PCSS receiver; warm static cache and forced six-face static rebuild; front/back-facing normals; no readbacks in timestamp/QPC interval; not city frames or game FPS',
        'baseline_source_root':str(args.baseline_source_root.resolve()),'candidate_source_root':str((args.source_root or fp.REPO).resolve()),
        'command':sys.argv,'executable_sha256':{name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in executables.items()},
        'harness_source_sha256':{name:hashlib.sha256((path.parent/'lamp-gpu-stand.cpp').read_bytes()).hexdigest() for name,path in executables.items()},
        'backend_sha256':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in backends},
        'fixture_sha256':{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for case in args.actor_case for path in (case/'tile.fg3',case/'sources.fgl',case/'stand-origin.f32',case/'stand-basis.f32',case/'stand-ground.f32',case/'stand-pose-bone.u32',*sorted((case/'models').glob('*.fgs'))) if path.exists()},
        'summary':summary,'samples':records,'native_cpu':native_cpu}
    (out/'benchmark.json').write_text(json.dumps(evidence,indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path)
    parser.add_argument('--allow-running-game',action='store_true',help='Explicitly allow independent correctness stands alongside WoW; excludes benchmark runs')
    parser.add_argument('--city-regression',action='store_true',help='Generate all seven actual city fixtures and native bridge evidence, then run night/day/physical-controls integration coverage')
    parser.add_argument('--compile-only',action='store_true')
    parser.add_argument('--backend',type=Path,action='append')
    parser.add_argument('--actor-case',type=Path,action='append',default=[],help='Explicit FGS/FGL lamp fixture to audit with a real skinned character and independent box')
    parser.add_argument('--live-native-case',type=Path,action='append',default=[],help='Dalaran fixture with evaluated native FGL/snapshot and live opaque enclosure; no cached lamp caster')
    parser.add_argument('--blocked-floor-case',type=Path,action='append',default=[],help='Additional physical negative control: original floor/box/body remain entirely shadowed by the opaque lamp')
    parser.add_argument('--partial-floor-case',type=Path,action='append',default=[],help='Additional original-floor control: metal occludes the moved body/box while part of the initial body silhouette remains visible')
    parser.add_argument('--actor-only',action='store_true',help='Run only explicitly selected actor cases; keep the ordinary lamp matrix separate')
    parser.add_argument('--actor-soft',action='store_true',help='Require the production soft-shadow resolve and consumer for actor cases')
    parser.add_argument('--sunlit',action='store_true',help='Full sun baseline, unchanged 30% direct lamp gain, and sun-energy independence checks')
    parser.add_argument('--legacy-sun-gate',action='store_true',help='Require the pre-fix daylight failure: positive direct lamp light and zero shadow correction')
    parser.add_argument('--benchmark',action='store_true',help='Measure equal selected workloads in balanced baseline/candidate processes, separate from correctness cases')
    parser.add_argument('--baseline-source-root',type=Path,help='Pristine source tree for --benchmark')
    parser.add_argument('--benchmark-hard',action='store_true',help='Measure the hard-shadow consumer instead of PCSS')
    args=parser.parse_args()
    if args.allow_running_game and args.benchmark:parser.error('--allow-running-game is for correctness checks, not performance measurements')
    if args.city_regression:
        from city_lamp_fixtures import build
        city=build(fp.output_dir()/'city-fixtures')
        args.actor_case.extend(city['actor_cases']);args.live_native_case.extend(city['live_native_cases'])
        args.blocked_floor_case.extend(city['blocked_floor_cases']);args.partial_floor_case.extend(city['partial_floor_cases'])
        args.actor_only=args.actor_soft=True
    for case in args.live_native_case:
        live_fixture(case)
        if case not in args.actor_case:args.actor_case.append(case)
    if (args.actor_only or args.actor_soft) and not (args.actor_case or args.blocked_floor_case or args.partial_floor_case):parser.error('--actor-only/--actor-soft requires an actor or floor-control case')
    if args.benchmark and (not args.actor_case or not args.baseline_source_root):parser.error('--benchmark requires --actor-case and --baseline-source-root')
    if args.baseline_source_root and not args.benchmark:parser.error('--baseline-source-root requires --benchmark')
    if args.benchmark_hard and not args.benchmark:parser.error('--benchmark-hard requires --benchmark')
    if args.legacy_sun_gate and (not args.sunlit or args.benchmark):parser.error('--legacy-sun-gate requires functional --sunlit cases')
    out=fp.output_dir();cases=[] if args.actor_only or args.benchmark else fixtures(out/'fixtures');character_fixture(out);exe=compile_gpu(out,args.source_root)
    if args.compile_only:return
    from validate_native_scene_trace import processes
    processes(args.allow_running_game)
    backends=args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
    if args.benchmark:return benchmark(out,exe,args,backends)
    results=[]
    for i,backend in enumerate(backends):
        settings=out/f'settings-{i}';settings.mkdir(exist_ok=True)
        for case in cases:
            processes(args.allow_running_game)
            result=subprocess.run([str(exe),str(backend),str(settings)+os.sep,str(case),'fixture',str(out)],capture_output=True,text=True,env={**os.environ,'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'},timeout=180,cwd=out)
            (out/f'gpu-{i}-{case.name}.log').write_text(result.stdout+result.stderr)
            print(result.stdout+result.stderr,flush=True);assert result.returncode==0
            results.append({'backend':str(backend),'case':str(case),'mode':'fixture','exit_code':result.returncode})
        for case in args.actor_case:
            mode=('actor-day-'+('soft' if args.actor_soft else 'hard')+('-legacy' if args.legacy_sun_gate else '')) if args.sunlit else ('actor-soft' if args.actor_soft else 'actor')
            results.append(run_actor(exe,backend,settings,case,mode,out/f'actor-{i}-{case.name}',out,args.allow_running_game))
        for mode,controls_cases in (('actor-floor-blocked',args.blocked_floor_case),('actor-floor-partial',args.partial_floor_case)):
            for case in controls_cases:
                results.append(run_actor(exe,backend,settings,case,mode,out/f'{mode}-{i}-{case.name}',out,args.allow_running_game))
        if args.city_regression:
            stormwind=next(case for case in args.actor_case if case.name=='stormwind-street')
            for mode in ('actor-day-soft','actor-day-hard'):
                results.append(run_actor(exe,backend,settings,stormwind,mode,out/f'{mode}-{i}',out,args.allow_running_game))
    (out/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'allow_running_game':args.allow_running_game,'command':sys.argv,'source_root':str(args.source_root or fp.REPO),'results':results},indent=2)+'\n')

if __name__=='__main__':main()
