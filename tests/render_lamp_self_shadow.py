#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib,world-cache manual
"""Replay installed/rebuilt point cubes on captured receivers without launching WoW.

The paired shader delta preserves the captured base. Animated casters, stencil
and the HDR presentation pass are not reconstructed.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import numpy as np
from PIL import Image
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets,InstancedBuilder
from world_lights_builder import RECORD,extract_m2,m2_sources,instance_lights
from world_emitter_index import Index
from test_m2_lamp_self_shadow_gpu import compile_gpu


def header(path):
    with path.open('rb') as f:magic,w,h,fmt,stride=struct.unpack('<5I',f.read(20))
    assert magic==0x31524746 and path.stat().st_size==20+w*h*stride
    return w,h,fmt,stride


def load(path):
    w,h,fmt,stride=header(path)
    if fmt==113:return np.fromfile(path,'<f2',offset=20).reshape(h,w,4).astype('f4')
    assert fmt in (21,22) and stride==4
    return np.fromfile(path,'u1',offset=20).reshape(h,w,4)[...,[2,1,0,3]].astype('f4')/255


def save(path,a,fmt=113):
    h,w,_=a.shape
    b=a.astype('<f2').tobytes() if fmt==113 else np.rint(np.clip(a,0,1)*255).astype('u1')[...,[2,1,0,3]].tobytes()
    path.write_bytes(struct.pack('<5I',0x31524746,w,h,fmt,8 if fmt==113 else 4)+b)


def capture_plan(records):
    material=next(r for r in records if r['event']=='pass' and r['entry']=='MaterialComposite')
    direct=next(r for r in records if r['event']=='pass' and r['entry'].startswith('LocalDirect'))
    shader=next(r['shader'] for r in records if r['event']=='shader-binding' and r['pass']==material['index'])
    target=next(r for r in records if r['event']=='target' and r['pass']==material['index'] and r['slot']==0)
    sources=[r for r in records if r['event']=='source' and r['kind']==2 and r['flags']&2]
    assert sources
    return dict(direct_pass=direct['index'],material_pass=material['index'],material_shader=shader,
                material_group=material['input_group'],hdr=target['format']==113,sources=sources)


def replay_contract(records, prefix, plan):
    """Reject incomplete exports; only older journals may omit final masks."""
    if not records or records[-1].get('event') != 'end' or not all(
            records[-1].get(key) is True for key in ('render_completed', 'export_complete')):
        raise ValueError('Capture did not complete its render and exports')
    if any(row.get('event') == 'unavailable' for row in records):
        raise ValueError('Capture contains unavailable inputs')
    if any(row.get('event') == 'file' and row.get('saved') is not True for row in records):
        raise ValueError('Capture contains a failed file export')
    masks = {}
    for name, group, slot in (('effect', plan['material_group'], 7),
                              ('rain', plan['material_group'], 13), ('water', 0, 11)):
        expected = f'lighting-input-g{group}-s{slot}'
        path = Path(prefix+expected+'.fgr')
        bindings = [row for row in records if row.get('event') == 'input-binding'
                    and row.get('group') == group and row.get('slot') == slot]
        if not bindings:
            if name == 'water' or path.exists() or any(row.get('event') == 'input-binding'
                    and row.get('slot') == slot or row.get('event') in ('file', 'image')
                    and row.get('name') in (expected, expected+'.fgr') for row in records):
                raise ValueError(f'{name} input has no journal binding')
            masks[name] = 'historical-omission'
            continue
        states = {(row.get('null'), row.get('file_base'), row.get('snapshot_group')) for row in bindings}
        if len(states) != 1:
            raise ValueError(f'{name} binding changed during the capture')
        null, base, snapshot = states.pop()
        if null is True:
            if base or path.exists():
                raise ValueError(f'{name} null binding has an unexpected image')
            masks[name] = 'null'
        elif null is False and base == expected and snapshot == group:
            if not path.is_file():
                raise ValueError(f'{name} image promised by the journal is missing')
            header(path)  # Verify the exported FGR, including its exact byte count.
            masks[name] = 'captured'
        else:
            raise ValueError(f'Unsupported {name} snapshot layout')
    if not plan['hdr']:
        return {'hdr_base_srgb': False, 'masks': masks}
    fmt = header(Path(prefix+'hdr-base.fgr'))[2]
    if fmt not in (21, 22, 113):
        raise ValueError('HDR base format has no supported color-transfer contract')
    # Production passes either the encoded UNORM scene or Frame's linear FP16 base.
    srgb = fmt in (21, 22)
    world_passes = {row['index'] for row in records if row.get('event') == 'pass'
                    and row.get('entry') in ('WorldCompositeHdr', 'WorldUnlitCompositeHdr',
                                            'WorldPrelitCompositeHdr', 'WorldMaterialCompositeHdr')}
    transfers = {row.get('value') for row in records if row.get('event') == 'sampler-state'
                 and row.get('pass') in world_passes and row.get('slot') == 2 and row.get('state') == 11}
    if transfers and transfers != {int(srgb)}:
        raise ValueError('Recorded HDR-base sRGB state conflicts with its format')
    return {'hdr_base_srgb': srgb, 'masks': masks}


def display_rgb(a,hdr):
    rgb=np.clip(a[...,:3],0,1)
    if hdr:rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*np.power(rgb,1/2.4)-.055)
    return np.rint(rgb*255).astype('u1')


def prepare_caches(source,out,center,sources):
    current=out/'cache-current';rebuilt=out/'cache-rebuilt'
    for folder in ('Azeroth','models','lights'):(current/folder).mkdir(parents=True,exist_ok=True)
    tx=math.floor((17066.66666666667-float(center[1]))/533.33333333333);ty=math.floor((17066.66666666667-float(center[0]))/533.33333333333)
    placements=[]
    for y in range(ty-1,ty+2):
        for x in range(tx-1,tx+2):
            tile=current/'Azeroth'/f'{x}_{y}.fg3'
            if not tile.exists():
                original=source/'Azeroth'/tile.name
                if not original.exists():continue
                shutil.copy2(original,tile)
            b=tile.read_bytes();assert struct.unpack_from('<4sI',b)==(b'FGS3',3)
            placements.extend(struct.iter_unpack('<32sQI18f',b[12:]))
    for key in {r[0].hex() for r in placements}:
        target=current/'models'/(key+'.fgs')
        if not target.exists():shutil.copy2(source/'models'/target.name,target)
    original_lights=current/'lights/Azeroth.fgl'
    if not original_lights.exists():shutil.copy2(source/'lights/Azeroth.fgl',original_lights)
    shutil.copytree(current,rebuilt,dirs_exist_ok=True)
    selected={int(r['source_id']) for r in sources};owners={}
    # These selected lamps have source index zero, as verified against the
    # complete emitter rows below; do not infer IDs from a renderer log tail.
    for r in placements:
        if r[2] not in (1,3):continue
        sid=int.from_bytes(hashlib.sha256(r[0]+struct.pack('<QII',r[1],r[2],0)).digest()[:8],'little')
        if sid in selected:owners[sid]=r
    assert set(owners)==selected,selected-set(owners)
    assets=Assets();builder=InstancedBuilder(assets,rebuilt);rows={};models=[]
    try:
        names={hashlib.sha256(('FGS3-v1:'+n).encode()).hexdigest():n for n in assets.providers if n.endswith('.m2')}
        index=Index(assets,lazy=True)
        for key in {r[0].hex() for r in owners.values()}:
            name=names[key];blob=assets.read(name);lights=m2_sources(name,blob,extract_m2(blob,assets.stats),index)
            for r in owners.values():
                if r[0].hex()==key:rows.update((row[8],row) for row in instance_lights(lights,r,name) if row[8] in selected)
            mesh=builder.m2(name);assert any(len(m)==4 for m in mesh[2]),name
            builder.add_mesh(mesh,((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'m2',0)
            models.append(dict(name=name,key=key,installed_sha256=hashlib.sha256((current/'models'/(key+'.fgs')).read_bytes()).hexdigest(),
                               rebuilt_sha256=hashlib.sha256((rebuilt/'models'/(key+'.fgs')).read_bytes()).hexdigest()))
    finally:assets.close()
    assert set(rows)==selected
    for captured in sources:
        row=rows[int(captured['source_id'])];assert np.allclose(row[:3],captured['position'],atol=.002,rtol=0)
        # Preserve captured fade/radiance. v2 avoids applying the native fit twice.
        rows[row[8]]=(*captured['position'],*captured['rgb'],*captured['range'],row[8],captured['kind'],captured['flags'])
    fgl=struct.pack('<4sIII',b'FGL1',2,RECORD.size,len(rows))+b''.join(RECORD.pack(*rows[s]) for s in sorted(rows))
    for cache in (current,rebuilt):(cache/'sources.fgl').write_bytes(fgl)
    return current,rebuilt,models


def missing_owner_sources(data):
    magic,version,stride,count=struct.unpack_from('<4sIII',data);assert (magic,version,stride)==(b'FGL1',2,RECORD.size) and count>0
    rows=[list(r) for r in RECORD.iter_unpack(data[16:])];ids={r[8] for r in rows};changed=rows[0][8]^1
    while changed in ids:changed+=1
    rows[0][8]=changed;rows.sort(key=lambda r:r[8])
    return data[:16]+b''.join(RECORD.pack(*r) for r in rows)


def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('--capture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--reuse-exe',type=Path);p.add_argument('--source-cache',type=Path)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);prefix=str(a.capture.resolve()/'capture-1-')
    records=[json.loads(l) for l in Path(prefix+'lighting-inputs.jsonl').read_text().splitlines()];plan=capture_plan(records);contract=replay_contract(records,prefix,plan)
    bank=Path(prefix+f"lighting-pass-{plan['direct_pass']}-constants.f32");c=np.fromfile(bank,'<f4').reshape(-1,4);center=c[6,:3]
    current,rebuilt,models=prepare_caches(a.source_cache or fp.world_cache(),out,center,plan['sources'])
    exe=a.reuse_exe.resolve() if a.reuse_exe else compile_gpu(out);backend=fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll'
    settings=out/'settings';settings.mkdir(exist_ok=True);env={**fp.zig_env(),'DXVK_LOG_PATH':str(out),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
    for name,cache in (('current',current),('rebuilt',rebuilt)):
        result=out/name;result.mkdir(exist_ok=True);shutil.copy2(bank,result/'direct-constants.f32')
        cmd=[exe,backend,str(settings)+os.sep,cache,prefix,result]+(['untagged'] if name=='current' else [])
        with (result/'gpu.log').open('w') as log:subprocess.run(list(map(str,cmd)),check=True,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300,cwd=out)
    # Remove one source-to-shell association while retaining its physical light.
    # The default rebuilt mode must reject this partial-ownership regression.
    source_file=rebuilt/'sources.fgl';original_sources=source_file.read_bytes();missing=out/'missing-owner';missing.mkdir(exist_ok=True);shutil.copy2(bank,missing/'direct-constants.f32')
    mutated=missing_owner_sources(original_sources)
    try:
        source_file.write_bytes(mutated)
        failure=subprocess.run(list(map(str,[exe,backend,str(settings)+os.sep,rebuilt,prefix,missing])),env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=300,cwd=out)
    finally:source_file.write_bytes(original_sources)
    failure_log=failure.stdout.decode(errors='replace');(missing/'gpu.log').write_text(failure_log)
    assert failure.returncode!=0 and f"mode=1 sources={len(plan['sources'])} owners={len(plan['sources'])-1}" in failure_log,failure_log
    before=load(out/'current/correction-after.fgr');after=load(out/'rebuilt/correction-after.fgr');unowned=load(out/'rebuilt/correction-before.fgr')
    delta=after-before;original=load(Path(prefix+'current-local-light.fgr'));fixed=original.copy();fixed[...,:3]=np.maximum(0,original[...,:3]+delta[...,:3]);save(out/'current-local-fixed.fgr',fixed)
    composite=out/'composite.exe';cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),fp.TESTS/'lamp_capture_composite_gpu.cpp','-luser32','-o',composite]
    subprocess.run(list(map(str,cmd)),check=True,env=env,timeout=180)
    composite_args=[composite,backend,prefix,out,'world',plan['material_pass'],plan['material_shader'],plan['material_group'],int(plan['hdr']),int(contract['hdr_base_srgb'])]
    subprocess.run(list(map(str,composite_args)),check=True,env=env,timeout=120,cwd=out)
    worldBefore,worldAfter=(load(out/('world-'+s+'.fgr')) for s in ('before','after'))
    material_path=Path(prefix+f"lighting-input-g{plan['material_group']}-s0.fgr");materialInput=load(material_path)
    save(out/'material-input-fixed.fgr',materialInput+worldAfter-worldBefore,header(material_path)[2])
    composite_args[4]='material';subprocess.run(list(map(str,composite_args)),check=True,env=env,timeout=120,cwd=out)
    materialBefore,materialAfter=(load(out/('material-'+s+'.fgr')) for s in ('before','after'))
    final=load(Path(prefix+'lighting-final-scene.fgr'));result=np.maximum(final+materialAfter-materialBefore,0);save(out/'after.fgr',result)
    for name,pixels in [('before',final),('after',result)]:Image.fromarray(display_rgb(pixels,plan['hdr'])).save(out/(name+'.png'))
    changed=np.max(np.abs(result[...,:3]-final[...,:3]),axis=2)
    report=dict(replay_contract=contract,sources=len(plan['sources']),source_ids=sorted(int(r['source_id']) for r in plan['sources']),camera=center.tolist(),models=models,game_launched=False,whole_scene_replay=False,animated_casters_replayed=False,stencil_replayed=False,hard_pcf=True,hdr=plan['hdr'],
                material_pass=plan['material_pass'],material_shader=plan['material_shader'],preview_transform='linear to sRGB, clip at 1; HDR presentation/exposure is not replayed' if plan['hdr'] else 'captured SDR RGB',
                missing_owner_control_rejected=True,missing_owner_control_exit_code=failure.returncode,
                source_only_control_equal_to_unowned=bool(np.array_equal(before,unowned)),
                remaining_shadowed_receiver_pixels=int((np.min(after[...,:3],axis=2)<-1e-5).sum()),
                correction_delta_mean=delta[...,:3].mean(axis=(0,1)).tolist(),correction_improved_pixels=int((np.max(delta[...,:3],axis=2)>1e-5).sum()),changed_pixels_over_1_255=int((changed>1/255).sum()),composite_baseline_mean_absolute_error=float(np.abs(materialBefore[...,:3]-final[...,:3]).mean()),
                limitations=['Azeroth static cache in a 144-unit camera region; selected native M2 emitters must map to source index zero','Hard PCF; original animated casters, stencil, MSAA samples and HDR presentation are absent'],
                method='Identical captured v2 FGL in both caches, installed FGS vs rebuilt selected M2 FGS; production cubes/LocalLighting; paired WorldComposite delta on recorded input; captured MaterialComposite shader; paired final delta preserves captured base.')
    assert report['correction_improved_pixels']>0 and report['remaining_shadowed_receiver_pixels']>0
    assert report['source_only_control_equal_to_unowned']
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
