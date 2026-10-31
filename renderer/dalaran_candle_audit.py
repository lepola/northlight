#!/usr/bin/env python3
"""Offline geometric oracle for actual Dalaran inn candle/table placements.

Reports source/mesh hashes and coordinates, never game payload bytes. This
proves tabletop geometry blocks the sampled segments; it does not measure the
renderer, shadow atlas resolution, bias or an in-game frame.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import client_archives
from world_scene_builder import Assets, Builder, array, chunks, doodad_indices, matvec, placement, quaternion_matrix, string, unpack, wmo_group
from world_light_placements import names, wmo_doodads
from world_emitter_coverage import inspect_model
from emitter_pin_report import batches
from m2_visibility import alpha_bound

CITY = r'world\wmo\northrend\dalaran\nd_dalaran.wmo'


def cross(a, b):
    return a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]


def dot(a, b):
    return sum(x*y for x,y in zip(a,b))


def segment_hits(start, end, vertices, triangles):
    """Independent double-sided segment/triangle test, with open endpoints."""
    direction = tuple(b-a for a,b in zip(start,end))
    result = []
    for index, tri in enumerate(triangles):
        a,b,c = (vertices[i][:3] for i in tri[:3])
        edge1, edge2 = tuple(y-x for x,y in zip(a,b)), tuple(y-x for x,y in zip(a,c))
        p = cross(direction, edge2); determinant = dot(edge1,p)
        if abs(determinant) < 1e-10:
            continue
        distance = tuple(x-y for x,y in zip(start,a))
        u = dot(distance,p)/determinant
        if not -1e-8 <= u <= 1+1e-8:
            continue
        q = cross(distance,edge1); v = dot(direction,q)/determinant
        if v < -1e-8 or u+v > 1+1e-8:
            continue
        t = dot(edge2,q)/determinant
        if 1e-7 < t < 1-1e-7:
            result.append({'triangle': index, 'fraction': t, 'point': tuple(start[k]+t*direction[k] for k in range(3))})
    return sorted(result, key=lambda hit: hit['fraction'])


def transform(point, matrix, translation):
    return tuple(x+y for x,y in zip(matvec(matrix,point),translation))


def inverse_point(point, matrix, translation):
    """Inverse of the placement API's rigid, uniformly scaled transform."""
    scale2 = sum(x*x for x in matrix[0])
    if not math.isfinite(scale2) or scale2 <= 0:
        raise ValueError('Invalid table transform')
    relative = tuple(x-y for x,y in zip(point,translation))
    return tuple(sum(matrix[j][k]*relative[j] for j in range(3))/scale2 for k in range(3))


def placed_city(assets):
    """Exact active ADT MODF identity, not a hardcoded game position."""
    instances = {}
    for path in sorted(assets.providers):
        if not path.startswith('world\\maps\\northrend\\') or not path.endswith('.adt'):
            continue
        blob = assets.read(path)
        if not blob:
            continue
        root = {tag:data for tag,data in chunks(blob) if tag in ('MWMO','MWID','MODF')}
        wmos = names(root,'MWID','MWMO')
        wanted = {i for i,p in enumerate(wmos) if p == CITY}
        if not wanted:
            continue
        data = root.get('MODF',b'')
        if len(data)%64:
            raise ValueError('Invalid Dalaran ADT MODF')
        for entry in array(data,0,len(data)//64,'<2I12f4H'):
            if entry[0] not in wanted:
                continue
            matrix, translation = placement(entry[2:5],entry[5:8],1.)
            value = instances.setdefault(entry[1], {'uid':entry[1], 'doodad_set':entry[15],
                                       'matrix':matrix, 'translation':translation, 'adts':{}})
            if value['doodad_set'] != entry[15] or value['matrix'] != matrix or value['translation'] != translation:
                raise ValueError('Inconsistent cross-tile Dalaran placement')
            value['adts'][path] = {'asset_sha256':hashlib.sha256(blob).hexdigest(), 'archive':assets.origin(path)}
    return list(instances.values())


def visible_mesh(assets, path):
    """All skin triangles, independent of the cache's opaque-caster selection."""
    blob = assets.read(path); skin = assets.read(path[:-3]+'00.skin')
    count, offset = unpack('<II',blob,60)
    vertices = array(blob,offset,count,'<3f4B4B3f4f')
    count, offset = unpack('<II',skin,4)
    lookup = [v[0] for v in array(skin,offset,count,'<H')]
    count, offset = unpack('<II',skin,12)
    indices = [lookup[v[0]] for v in array(skin,offset,count,'<H')]
    if len(indices)%3 or max(indices,default=0) >= len(vertices):
        raise ValueError('Invalid table skin triangle indices')
    return vertices, [indices[i:i+3] for i in range(0,len(indices),3)], []


def tabletop_proof(source, flame_points, table_doodad, mesh, caster_mesh):
    path, pos, quat, scale = table_doodad
    matrix = quaternion_matrix(quat,scale)
    vertices, triangles, _ = mesh
    lower = tuple(min(v[k] for v in vertices) for k in range(3))
    upper = tuple(max(v[k] for v in vertices) for k in range(3))
    local = inverse_point(source,matrix,pos)
    if not all(lower[k] <= local[k] <= upper[k] for k in (0,1)) or abs(local[2]-upper[2]) > 4:
        return None
    vertical = segment_hits((local[0],local[1],upper[2]+2), (local[0],local[1],lower[2]-1), vertices,triangles)
    if not vertical:
        return None
    tabletop = vertical[0]['point'][2]
    rays = []
    for x in (-.2,0.,.2):
        for y in (-.2,0.,.2):
            target = ((lower[0]+upper[0])/2+x*(upper[0]-lower[0]),
                      (lower[1]+upper[1])/2+y*(upper[1]-lower[1]), tabletop-.35)
            hits = segment_hits(local,target,vertices,triangles)
            caster_hits = segment_hits(local,target,caster_mesh[0],caster_mesh[1])
            rays.append({'target_table_local':target, 'target_wmo_local':transform(target,matrix,pos),
                         'blocked':bool(hits), 'first_hit':hits[0] if hits else None,
                         'opaque_cache_blocked':bool(caster_hits), 'opaque_cache_first_hit':caster_hits[0] if caster_hits else None})
    flame_local = [inverse_point(p,matrix,pos) for p in flame_points]
    return {'table':path, 'table_position':pos, 'table_quaternion':quat, 'table_scale':scale,
            'table_bounds_local':[lower,upper], 'table_triangles':len(triangles), 'tabletop_z_local':tabletop,
            'opaque_cache_triangles':len(caster_mesh[1]),
            'source_table_local':local, 'anchor_above_table_m':(local[2]-tabletop)*abs(scale),
            'minimum_flame_above_table_m':min((p[2]-tabletop)*abs(scale) for p in flame_local),
            'under_table_rays':rays, 'blocked_rays':sum(r['blocked'] for r in rays),
            'opaque_cache_blocked_rays':sum(r['opaque_cache_blocked'] for r in rays)}


def audit(assets):
    blob = assets.read(CITY); root = dict(chunks(blob))
    count = unpack('<I',root['MOHD'],4)[0]
    doodads, sets, _ = wmo_doodads(blob)
    placements = placed_city(assets)
    if not placements:
        raise ValueError('Dalaran WMO has no active Northrend MODF placement')
    builder = Builder(assets); measured = {}; source_cache = {}; visible = {}; cases = []; excluded = []; groups = []
    for group_index in range(count):
        group_path = CITY[:-4]+f'_{group_index:03d}.wmo'; group_blob = assets.read(group_path)
        group = wmo_group(group_blob)[0]['MOGP']
        name = string(root['MOGN'],unpack('<I',group)[0])
        if not re.search(r'(?:^|_)inn(?:_|$)|hordeinn|innentrance|cinn_',name):
            continue
        sub = dict(chunks(group,68)); data = sub.get('MODR',b'')
        refs = {r[0] for r in array(data,0,len(data)//2,'<H')}
        groups.append({'index':group_index, 'name':name, 'path':group_path,
                       'asset_sha256':hashlib.sha256(group_blob).hexdigest(), 'doodad_refs':len(refs)})
        for city in placements:
            active = doodad_indices(sets,len(doodads),refs,city['doodad_set'])
            tables = [(i,doodads[i]) for i in active if 'table' in doodads[i][0]]
            for candle_index in active:
                path, pos, quat, scale = doodads[candle_index]
                if not any(word in path for word in ('candle','candel')):
                    continue
                if path not in measured:
                    measured[path] = inspect_model(assets,path,cache=source_cache)
                proof = measured[path]
                if proof['status'] != 'addable':
                    excluded.append({'group':name,'doodad':candle_index,'asset':path,'status':proof['status'],
                                     'reason':proof.get('reason'), 'refusals':proof.get('refusals')})
                    continue
                matrix = quaternion_matrix(quat,scale)
                for emitter_index, emitter in enumerate(proof['emitters']):
                    source = transform(emitter['position'],matrix,pos)
                    flames = [transform(s['position'],matrix,pos) for s in emitter['evidence']]
                    matches = []
                    for table_index, table in tables:
                        try:
                            caster_mesh = builder.m2(table[0])
                            if table[0] not in visible:
                                visible[table[0]] = visible_mesh(assets,table[0])
                        except (ValueError,FileNotFoundError) as exc:
                            excluded.append({'group':name,'doodad':table_index,'asset':table[0],'reason':str(exc)})
                            continue
                        result = tabletop_proof(source,flames,table,visible[table[0]],caster_mesh)
                        if result:
                            matches.append((math.dist(source,table[1]),table_index,result))
                    if not matches:
                        excluded.append({'group':name,'doodad':candle_index,'asset':path,'reason':'No overlapping placed table mesh; no guessed pairing'})
                        continue
                    _, table_index, result = min(matches,key=lambda item:item[0])
                    table_blob = assets.read(result['table'])
                    skin_blob = assets.read(result['table'][:-3]+'00.skin')
                    units, offset = unpack('<II',skin_blob,36)
                    surface_batches = [{**b, 'opacity_bound':alpha_bound(table_blob,u)} for b,u in
                                       zip(batches(assets,result['table']),array(skin_blob,offset,units,'<2B11H'))]
                    cases.append({**result, 'group':name, 'group_index':group_index, 'city_uid':city['uid'],
                                  'table_doodad':table_index, 'candle_doodad':candle_index, 'emitter':emitter_index,
                                  'candle':path, 'candle_sha256':proof['asset_sha256'], 'candle_archive':proof['archive'],
                                  'table_sha256':hashlib.sha256(table_blob).hexdigest(), 'table_archive':assets.origin(result['table']),
                                  'table_skin_sha256':hashlib.sha256(skin_blob).hexdigest(),
                                  'table_surface_batches':surface_batches,
                                  'candle_position':pos, 'candle_quaternion':quat, 'candle_scale':scale,
                                  'source_model_local':emitter['position'], 'source_wmo_local':source,
                                  'source_world':transform(source,city['matrix'],city['translation']),
                                  'flame_bounds_wmo_local':[[min(p[k] for p in flames) for k in range(3)],
                                                            [max(p[k] for p in flames) for k in range(3)]],
                                  'flame_source_count':len(flames), 'emission':emitter['emission']})
    return {'schema':1, 'wmo':CITY, 'wmo_sha256':hashlib.sha256(blob).hexdigest(), 'wmo_archive':assets.origin(CITY),
            'placements':placements, 'groups':groups, 'cases':cases, 'excluded':excluded,
            'counts':{'paired_sources':len(cases), 'rays':sum(len(c['under_table_rays']) for c in cases),
                      'blocked_rays':sum(c['blocked_rays'] for c in cases),
                      'opaque_cache_blocked_rays':sum(c['opaque_cache_blocked_rays'] for c in cases),
                      'above_table_sources':sum(c['anchor_above_table_m'] > 0 and c['minimum_flame_above_table_m'] > 0 for c in cases)},
            'archive_fingerprint':assets.fingerprint(),
            'limitations':['Exact native placement; full skin triangles and the exported opaque caster mesh are measured separately.',
                           'The Dalaran table top has translucent blend2/Mod2X batches. Geometric intersection alone is not proof that it should be fully opaque.',
                           'No in-game visibility, shadow atlas, bias or runtime leakage validation.',
                           'Only candle fixtures with a proven source and overlapping placed table mesh are paired; every other inspected fixture is reported.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0]); client_archives.add_arguments(parser)
    parser.add_argument('--output',type=Path,required=True); args = parser.parse_args(); assets = Assets.from_args(args)
    try:
        result = audit(assets)
    finally:
        assets.close()
    args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['counts']))
