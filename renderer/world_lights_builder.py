#!/usr/bin/env python3
"""Extract real static local-light records from FGS3 plus independent ADT/WMO placements.

Native sources plus explicitly reviewed model-local emitters; no game launch. Cache FGL1 is little endian:
header <4sIII = magic,version,recordBytes,count; record <8fQII (48 bytes) =
position XYZ, diffuse RGB*intensity, attenuation start/end, source ID, kind,flags.
Kind 1=WMO MOLT (potential baked contribution), 2=M2 default constant light, 3=authored fallback.
"""
from __future__ import annotations
import argparse,collections,hashlib,json,math,os,struct,time,tempfile
import sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]  # repo root, renderer/
from world_scene_builder import Assets,CLIENT,CONTINENTS,chunks,unpack,array,matvec,retry
import instance_maps
import client_archives
from world_light_placements import discover as discover_placements,discover_global,wmo_doodads,group_refs,group_blobs

import outdoor_light_profiles
import wmo_lamp_emission

RECORD=struct.Struct('<8fQII')
INSTANCE=struct.Struct('<32sQI18f')
MAPS=['Azeroth','Kalimdor','Expansion01','Northrend']
class Unsupported(ValueError):pass

def constant_track(b,offset,fmt,default=None):
    interpolation,seq,nt,ot,nk,ok=unpack('<hh4I',b,offset)
    if interpolation not in (0,1):raise Unsupported('nonlinear_track')
    times=array(b,ot,nt,'<II');keys=array(b,ok,nk,'<II')
    if len(times)!=len(keys):raise Unsupported('track_array_mismatch')
    values=[]
    for (tc,to),(kc,ko) in zip(times,keys):
        if tc!=kc:raise Unsupported('track_key_count_mismatch')
        stamps=array(b,to,tc,'<I');data=array(b,ko,kc,fmt)
        if any(stamps[i][0]>stamps[i+1][0] for i in range(len(stamps)-1)):
            raise Unsupported('unordered_track')
        values.extend(data)
    if not values:
        if default is None:raise Unsupported('missing_required_track')
        return default
    first=values[0]
    if any(value!=first for value in values):raise Unsupported('animated_track')
    if not all(math.isfinite(x) for x in first):raise Unsupported('nonfinite_track')
    return first

def static_bone_offset(b,bone):
    """Support inactive bones and constant translations; reject other transforms.

    This deliberately does not call a bind-pose position an animated position.
    Parent chain follows ModelHeaders.h and Model.cpp::Bone::calcMatrix.
    """
    count,offset=unpack('<II',b,44);seen=set();translation=[0.,0.,0.]
    while bone>=0:
        if bone>=count or bone in seen:raise Unsupported('bone_range_or_cycle')
        seen.add(bone);off=offset+bone*88
        _,flags,parent=unpack('<iIh',b,off)
        if flags&0x7f:raise Unsupported('bone_billboard_or_inheritance_flags')
        if flags&0x200:
            t=constant_track(b,off+16,'<3f',(0.,0.,0.))
            q=constant_track(b,off+36,'<4h',(32767,32767,32767,-1))
            q=tuple((v-32767 if v>0 else v+32767)/32767 for v in q)
            s=constant_track(b,off+56,'<3f',(1.,1.,1.))
            if any(abs(q[i])>1e-4 for i in range(3)) or abs(abs(q[3])-1)>1e-4 or s!=(1.,1.,1.):
                raise Unsupported('nonidentity_bone_rotation_or_scale')
            translation=[translation[i]+t[i] for i in range(3)]
        bone=parent
    return tuple(translation)

def static_bone_point(b,bone,position):
    """Prove an emitter point is fixed, including rotation about its own pivot.

    This does not freeze a moving bone: translation must be constant; a point
    away from the pivot still requires identity rotation and scale.
    """
    count,offset=unpack('<II',b,44);seen=set();point=tuple(position)
    while bone>=0:
        if bone>=count or bone in seen:raise Unsupported('bone_range_or_cycle')
        seen.add(bone);off=offset+bone*88
        _,flags,parent=unpack('<iIh',b,off)
        if flags&0x7f:raise Unsupported('bone_billboard_or_inheritance_flags')
        if flags&0x200:
            translation=constant_track(b,off+16,'<3f',(0.,0.,0.))
            pivot=unpack('<3f',b,off+76)
            if point==pivot:
                # Every linear rotation/scale fixes its pivot. Validate the
                # complete tracks, but do not mistake animation for motion here.
                for relative,fmt in ((36,'<4h'),(56,'<3f')):
                    interpolation,_,nt,ot,nk,ok=unpack('<hh4I',b,off+relative)
                    if interpolation not in (0,1):raise Unsupported('nonlinear_track')
                    stamps,keys=array(b,ot,nt,'<II'),array(b,ok,nk,'<II')
                    if len(stamps)!=len(keys):raise Unsupported('track_array_mismatch')
                    for (tc,to),(kc,ko) in zip(stamps,keys):
                        if tc!=kc or kc>16384:raise Unsupported('track_key_count_mismatch')
                        times=array(b,to,tc,'<I');values=array(b,ko,kc,fmt)
                        if any(x[0]>y[0] for x,y in zip(times,times[1:])):raise Unsupported('unordered_track')
                        if not all(math.isfinite(x) for row in values for x in row):raise Unsupported('nonfinite_track')
            else:
                rotation=constant_track(b,off+36,'<4h',(32767,32767,32767,-1))
                rotation=tuple((v-32767 if v>0 else v+32767)/32767 for v in rotation)
                scale=constant_track(b,off+56,'<3f',(1.,1.,1.))
                if any(abs(rotation[i])>1e-4 for i in range(3)) or abs(abs(rotation[3])-1)>1e-4 or scale!=(1.,1.,1.):
                    raise Unsupported('nonidentity_bone_rotation_or_scale')
            point=tuple(point[k]+translation[k] for k in range(3))
        bone=parent
    if not all(math.isfinite(x) for x in point):raise Unsupported('nonfinite_bone_point')
    return point

def valid_light(position,color,start,end):
    return (all(math.isfinite(x) for x in (*position,*color,start,end)) and
            all(abs(x)<100000 for x in position) and all(0<=x<=10000 for x in color) and
            max(color)>0 and 0<=start<end<=2000)

def extract_wmo(b,stats):
    root=dict(chunks(b));data=root.get('MOLT',b'')
    if len(data)%48:raise ValueError('MOLT record size')
    result=[]
    for index,r in enumerate(array(data,0,len(data)//48,'<4BI10f')):
        stats['wmo_records']+=1
        kind,_,use_atten,_,packed=r[:5]
        if kind!=0:stats['unsupported_wmo_nonpoint']+=1;continue
        if not use_atten:stats['unsupported_wmo_unbounded']+=1;continue
        position=r[5:8];intensity=r[8]
        # The four floats at24..39 are rotation, NOT attenuation. Actual legacy
        # assets have e.g. (0,0,-1,-.5) there. Ranges are offsets40/44.
        start,end=r[13:15]
        color=tuple(((packed>>shift)&255)/255*intensity for shift in (16,8,0))
        if not valid_light(position,color,start,end):stats['unsupported_wmo_value_range']+=1;continue
        result.append((position,color,start,end,index,1,3))
    return result

def extract_m2(b,stats):
    if b[:4]!=b'MD20' or unpack('<I',b,4)[0]!=264:raise Unsupported('m2_version')
    count,offset=unpack('<II',b,264)
    if count>4096 or offset+count*156>len(b):raise ValueError('M2 light array')
    result=[]
    for index in range(count):
        stats['m2_records']+=1;off=offset+index*156
        try:
            kind,bone,*position=unpack('<Hh3f',b,off)
            if kind!=1:raise Unsupported('m2_nonpoint')
            color=constant_track(b,off+56,'<3f')
            intensity=constant_track(b,off+76,'<f')[0]
            start=constant_track(b,off+96,'<f')[0];end=constant_track(b,off+116,'<f')[0]
            enabled=constant_track(b,off+136,'<B',(1,))[0]
            if enabled==0:stats['m2_disabled']+=1;continue
            if enabled!=1:raise Unsupported('m2_visibility_value')
            translation=static_bone_offset(b,bone)
            position=tuple(position[i]+translation[i] for i in range(3))
            color=tuple(c*intensity for c in color)
            if not valid_light(position,color,start,end):raise Unsupported('m2_value_range')
            result.append((position,color,start,end,index,2,2))
        except Unsupported as exc:stats['unsupported_'+str(exc)]+=1
    return result

def instance_lights(lights,record,path):
    key,uid,kind,*floats=record
    matrix=tuple(tuple(floats[6+i*3+j] for j in range(3)) for i in range(3))
    translation=floats[15:18]
    # FGS3 matrices are rigid with uniform placement scale. Reject shear and
    # nonuniform scale instead of assigning a guessed spherical light radius.
    lengths=[math.sqrt(sum(x*x for x in row)) for row in matrix]
    scale=sum(lengths)/3
    if not math.isfinite(scale) or scale<=0:return []
    if max(abs(x-scale) for x in lengths)>scale*1e-4:raise Unsupported('nonuniform_instance_scale')
    for i in range(3):
        for j in range(i):
            if abs(sum(matrix[i][k]*matrix[j][k] for k in range(3)))>scale*scale*1e-4:
                raise Unsupported('instance_shear')
    result=[]
    for position,color,start,end,index,source,flags in lights:
        p=matvec(matrix,position);p=tuple(p[i]+translation[i] for i in range(3))
        start*=scale;end*=scale
        if not valid_light(p,color,start,end):raise Unsupported('world_light_value_range')
        identity=hashlib.sha256(key+struct.pack('<QII',uid,kind,index)).digest()
        source_id=int.from_bytes(identity[:8],'little')
        result.append((*p,*color,start,end,source_id,source,flags))
    return result

def build(cache,output,maps,assets=None,manifest='manifest.json'):
    import world_emitter_index
    import point_transmission_builder
    import light_attachments
    catalog=outdoor_light_profiles.load()
    started=time.time();assets=assets or Assets();stats=collections.Counter();extracted={};provenance={};errors=[];reports=[]
    placement_cache={};placement_provenance={};unsupported_assets={};attachment_templates={}
    emitter_index=world_emitter_index.Index(assets)
    def read_placement_wmo(path):
        if path not in placement_cache:
            blob=assets.read(path);placement_cache[path]=wmo_doodads(blob,group_refs(group_blobs(assets.read,path,blob)))
            placement_provenance[path]={'archive':assets.origin(path),'asset_sha256':hashlib.sha256(blob).hexdigest()}
        return placement_cache[path]
    try:
        names={hashlib.sha256(('FGS3-v1:'+name).encode()).digest():name for name in assets.providers if name.endswith(('.m2','.wmo'))}
        for mapname in maps:
            records=[];attachments=[];owners={};profile_counts=collections.Counter();seen=set();tiles=sorted((cache/mapname).glob('*.fg3'));inputs=hashlib.sha256()
            if not tiles:raise ValueError('No FGS3 tiles for '+mapname)
            missing_mesh_lights=0;missing_mesh_instances=0;adt_count=0
            def consume(r,path=None,missing_mesh=False):
                nonlocal missing_mesh_lights,missing_mesh_instances
                key,uid,kind=r[:3]
                if kind==0:return
                identity=(key,uid,kind)
                if identity in seen:return
                seen.add(identity)
                path=path or names.get(key)
                if not path:stats['unmatched_asset_key']+=1;return
                if missing_mesh:missing_mesh_instances+=1
                if path not in extracted:
                    try:
                        blob=assets.read(path);before=stats.copy()
                        moving_native=light_attachments.native_sources(blob) if path.endswith('.m2') else []
                        extracted[path]=extract_wmo(blob,stats) if path.endswith('.wmo') else extract_m2(blob,stats)+moving_native
                        if path.endswith('.wmo'):
                            try:material_names=wmo_lamp_emission.candidates(blob)
                            except (ValueError,IndexError,KeyError,struct.error) as exc:
                                material_names={};errors.append({'asset':path,'lamp_evidence_error':str(exc)})
                            if material_names:
                                try:
                                    group_count=unpack('<I',dict(chunks(blob))['MOHD'],4)[0]
                                    if group_count>4096:raise ValueError('WMO group count')
                                except (ValueError,IndexError,KeyError,struct.error) as exc:
                                    group_count=0;errors.append({'asset':path,'lamp_evidence_error':str(exc)})
                                enclosed=[]
                                for group in range(group_count):
                                    group_path=path[:-4]+f'_{group:03d}.wmo'
                                    try:
                                        group_blob=assets.read(group_path)
                                        lamps,_=wmo_lamp_emission.analyze(blob,group_blob,assets.read,group,material_names)
                                        enclosed.extend(lamps)
                                    except FileNotFoundError:continue
                                    except (ValueError,IndexError,KeyError,struct.error) as exc:
                                        errors.append({'asset':group_path,'error':str(exc)})
                                extracted[path]=wmo_lamp_emission.merge_native(extracted[path],enclosed)
                                stats['wmo_glass_lamps']+=sum(bool(s[6]&wmo_lamp_emission.GLASS_EMITTER) for s in extracted[path])
                        authored=emitter_index.get(path,blob) if path.endswith('.m2') and not unpack('<I',blob,264)[0] else None
                        if authored is not None:
                            extracted[path]=world_emitter_index.records(authored)
                        else:
                            extracted[path]=outdoor_light_profiles.fallback(catalog,path,blob,extracted[path])
                        if path.endswith('.m2'):
                            try:attachment_templates[path]=light_attachments.templates(assets,path,blob,extracted[path]) if extracted[path] else []
                            except (ValueError,FileNotFoundError,IndexError,struct.error) as exc:
                                attachment_templates[path]=[];errors.append({'asset':path,'attachment_error':str(exc)})
                            # A moving native source without a proven palette
                            # attachment must not become a bind-pose light.
                            supported={t[0] for t in attachment_templates[path]}
                            unsupported={s[4] for s in moving_native}-supported
                            stats['unsupported_m2_attachment']+=len(unsupported)
                            extracted[path]=[s for s in extracted[path] if s[5]!=2 or s[4] not in unsupported]
                        rejected={k:v for k,v in (stats-before).items() if k.startswith('unsupported_')}
                        if rejected:unsupported_assets[path]=rejected
                        if extracted[path]:
                            provenance[path]={'archive':assets.origin(path),
                                'asset_sha256':hashlib.sha256(blob).hexdigest(),'light_records':[x[4] for x in extracted[path]],
                                'kind':('WMO MOLT and/or enclosed lamp' if path.endswith('.wmo') else ('Authored model emitter' if extracted[path][0][5]==3 else 'M2 constant default tracks'))}
                    except (ValueError,FileNotFoundError,IndexError,KeyError,struct.error) as exc:
                        extracted[path]=[];errors.append({'asset':path,'error':str(exc)})
                try:
                    placed=instance_lights(extracted[path],r,path);records.extend(placed)
                    attachments.extend(light_attachments.placed(attachment_templates.get(path,[]),r,
                        [(source[4],light) for source,light in zip(extracted[path],placed)]))
                    for light in placed:
                        owners[light[8]]=(kind,uid)
                        if light[9]==3:profile_counts[path]+=1
                    if missing_mesh:missing_mesh_lights+=len(placed)
                except Unsupported as exc:stats['unsupported_'+str(exc)]+=1
            # Preserve all existing FGS3 placements and their exact float32
            # transforms/IDs before recovering the mesh-independent placements.
            for tile in tiles:
                b=tile.read_bytes();inputs.update(tile.name.encode());inputs.update(b)
                magic,version,count=unpack('<4sII',b)
                if magic!=b'FGS3' or version!=3 or len(b)!=12+count*INSTANCE.size:raise ValueError('Invalid FGS3 '+str(tile))
                for r in INSTANCE.iter_unpack(b[12:]):consume(r)
            wdt=None
            if mapname not in CONTINENTS:   # an instance map: a global-WMO one has no ADTs, its WMO is in the WDT
                path=instance_maps.wdt_path(mapname)
                try:
                    blob=assets.read(path);wdt=instance_maps.read_wdt(blob)
                    if wdt['global_wmo']:
                        inputs.update(path.encode());inputs.update(blob)
                        for asset,r in discover_global(wdt,read_placement_wmo):consume(r,asset,True)
                        adt_count+=1
                except (ValueError,FileNotFoundError,IndexError,KeyError,struct.error) as exc:
                    errors.append({'placement_wdt':path,'error':str(exc)});wdt=None
            for tile in tiles if wdt is None or not wdt['global_wmo'] else ():
                x,y=map(int,tile.stem.split('_'))
                path=f'world\\maps\\{mapname.lower()}\\{mapname.lower()}_{x}_{y}.adt'
                try:
                    blob=assets.read(path);inputs.update(path.encode());inputs.update(blob)
                    if not blob:continue # highest-priority archive deletion marker
                    for asset,r in discover_placements(blob,read_placement_wmo,path):consume(r,asset,True)
                    adt_count+=1
                except (ValueError,FileNotFoundError,IndexError,KeyError,struct.error) as exc:
                    errors.append({'placement_adt':path,'error':str(exc)})
            records,suppressed=outdoor_light_profiles.deduplicate_parent_lights(records,owners)
            records.sort(key=lambda r:r[8])
            if len({r[8] for r in records})!=len(records):raise ValueError("Duplicate light source identity")
            output.mkdir(parents=True,exist_ok=True)
            destination=output/(mapname+'.fgl');temp=destination.with_suffix(f'.{os.getpid()}.tmp')
            with temp.open('wb') as f:
                f.write(struct.pack('<4sIII',b'FGL1',1,RECORD.size,len(records)))
                for r in records:f.write(RECORD.pack(*r))
            retry(temp.replace,destination)
            retained={r[8] for r in records}
            attachment_path=output/(mapname+'.fga');attachment_temp=attachment_path.with_suffix(f'.{os.getpid()}.tmp')
            light_attachments.write(attachment_temp,[a for a in attachments if a[0] in retained])
            retry(attachment_temp.replace,attachment_path)
            report={'map':mapname,'tiles':len(tiles),'unique_instances':len(seen),'lights':len(records),
                    'attached_sources':len({a[0] for a in attachments if a[0] in retained}),
                    'kinds':dict(collections.Counter(r[9] for r in records)),
                    'authored_profile_placements':dict(profile_counts),'parent_native_duplicates_suppressed':suppressed,
                    'cache_sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),
                    'scene_inputs_sha256':inputs.hexdigest(),'placement_adts':adt_count,
                    'additional_mesh_independent_instances':missing_mesh_instances,'additional_mesh_independent_lights':missing_mesh_lights}
            reports.append(report);print(json.dumps(report),flush=True)
        result={'format':'FGL1','maps':reports,'statistics':dict(stats),'errors':errors,'provenance':provenance,
                'emitter_catalog_sha256':hashlib.sha256(Path(__file__).with_name('outdoor-light-profiles.json').read_bytes()).hexdigest(),
                'emitter_policy_sha256':hashlib.sha256(Path(__file__).with_name('outdoor_light_profiles.py').read_bytes()).hexdigest(),
                'emitter_extension_sha256':hashlib.sha256(Path(__file__).with_name('wotlk-emitter-profiles.json').read_bytes()).hexdigest() if Path(__file__).with_name('wotlk-emitter-profiles.json').exists() else None,
                'builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'archive_fingerprint':assets.fingerprint(),
                'placement_builder_sha256':hashlib.sha256(Path(__file__).with_name('world_light_placements.py').read_bytes()).hexdigest(),
                'placement_sources':placement_provenance,'unsupported_assets':unsupported_assets,
                'source_id_recipe':'First 8 SHA256 bytes, little endian: FGS3 asset key + <QII>(instance UID, FGS3 kind, asset light index).',
                'elapsed_seconds':time.time()-started,'sources':[
                    'https://skarndev.github.io/wowlib/python/wmo/root-chunks/#wowlib.wmo.root_chunks.SMOLight',
                    'https://skarndev.github.io/wowlib/python/m2/records/',
                    'graphics-work/tools/noggit-red-master/src/noggit/ModelHeaders.h',
                    'graphics-work/tools/noggit-red-master/src/noggit/WMO.cpp'],
                'limitations':['World map tile coverage follows FGS3, but ADT/WMO placement discovery is independent of mesh/skin/opacity and includes authored light-only models.',
                    'WMO source colors may be baked into original vertex lighting: avoid applying direct light twice.',
                    'WMO diffuse color is packed RGB/255 times source intensity; source units are uncalibrated.',
                    'Unsupported native M2 tracks remain excluded. Reviewed exact models may supply a separate steady artistic emitter; no frame-zero animation freeze.',
                    'M2 ambient light tracks excluded; only supported diffuse point emitters exported.',
                    'No invented inverse-square attenuation: start/end are authored ranges; transport chooses documented attenuation policy.']}
        (output/'world-emitters-v1.json').write_bytes(emitter_index.payload())
        result['world_emitter_index']={'sha256':emitter_index.digest,'source':emitter_index.origin,'owner':'northlight-local-direct'}
        with tempfile.TemporaryDirectory(prefix='.point-transmission-',dir=output) as work:
            staged=Path(work)
            result['point_transmission']=point_transmission_builder.build(cache,staged,maps,assets)['counts']
            fresh=staged/'point-transmission';fresh.mkdir(exist_ok=True)
            destination=output/'point-transmission';previous=staged/'previous'
            if destination.exists():retry(destination.rename,previous)
            try:retry(fresh.rename,destination)
            except Exception:
                if previous.exists():retry(previous.rename,destination)
                raise
            retry(os.replace,staged/'point-transmission-report.json',output/'point-transmission-report.json')
        (output/manifest).write_text(json.dumps(result,indent=2)+'\n')
        return result
    finally:assets.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--cache',type=Path,help='default: <client>/world-cache');p.add_argument('--output',type=Path,help='default: <cache>/lights');p.add_argument('--maps',nargs='+',default=MAPS,choices=MAPS+instance_maps.ALL_MAPS)
    p.add_argument('--manifest',default='manifest.json',help='file name of the manifest in --output (instance maps use their own)')
    client_archives.add_arguments(p);args=p.parse_args();assets=Assets.from_args(args);cache=args.cache or assets.client/'world-cache'
    build(cache,args.output or cache/'lights',args.maps,assets,args.manifest)
