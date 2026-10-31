#!/usr/bin/env python3
"""Offline WORLD emitter evidence and Map.dbc/ADT/WMO placement census.

All listed world M2s are inspected, regardless of their filename. Additive
surface/particle radiance is an uncalibrated relative-energy proxy, not lumens.
Animated anchors/materials remain unsupported rather than frozen at frame zero.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import client_archives
import instance_maps
from build_equipment_emitters import lifetime_track, lifetime_colour
from world_scene_builder import Assets, array, chunks, decode_blp, srgb_to_linear, string, unpack
from world_lights_builder import constant_track, static_bone_offset, static_bone_point, extract_m2, extract_wmo
from world_light_placements import discover, discover_global, wmo_doodads, group_refs, group_blobs

DISABLED = re.compile(r'off\d*\.m2$|(?:^|[_ .-])off(?:[_ .-]|$)|broken|unlit|extinguish|unlight|burntout|firewood')
NON_SOURCE = re.compile(r'smoke|shadow|reflect|ground|footprint|blood|waterfall|splash')
FIRE = re.compile(r'flame|fire|torch|brazier|ember|lava')
CANDLE = re.compile(r'candle|candel')
STATUSES = ('native', 'addable', 'not_emissive', 'unsupported', 'missing')
CONTINENTS = {0: 'Eastern Kingdoms', 1: 'Kalimdor', 530: 'Outland', 571: 'Northrend'}
REFERENCES = [
    'https://github.com/wowdev/WoWDBDefs/blob/master/definitions/Map.dbd',
    'https://github.com/wowdev/pywowlib/blob/master/file_formats/m2_format.py',
    'https://github.com/wowdev/pywowlib/blob/master/enums/m2_enums.py',
]


def emission_kind(path, texture):
    name = path.rsplit('\\', 1)[-1] + ' ' + texture.rsplit('\\', 1)[-1]
    return 'candle' if CANDLE.search(name) else ('fire' if FIRE.search(name) else 'energy')


def texture_radiance(assets, name, alpha_blend, cache):
    """Mean absolute linear RGB × texture opacity; never normalise away brightness."""
    key = name, alpha_blend
    if key not in cache:
        _, _, rgba = decode_blp(assets.read(name), 64)
        rgb = [0., 0., 0.]
        for offset in range(0, len(rgba), 4):
            alpha = rgba[offset + 3] / 255. if alpha_blend else 1.
            for k in range(3):
                rgb[k] += srgb_to_linear(rgba[offset + k] / 255.) * alpha
        cache[key] = tuple(v / (len(rgba) // 4) for v in rgb)
    return cache[key]


def triangle_area(a, b, c):
    u, v = [b[k] - a[k] for k in range(3)], [c[k] - a[k] for k in range(3)]
    return math.sqrt(sum(x*x for x in (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]))) / 2


def surface_sources(assets, path, blob, cache, refusals):
    n, off = unpack('<II', blob, 112)
    flags = array(blob, off, n, '<2H')
    if not any(f & 1 and blend in (3, 4) for f, blend in flags):
        return []
    skin = assets.read(path[:-3] + '00.skin')
    if skin[:4] != b'SKIN':
        raise ValueError('Unsupported skin')
    n, off = unpack('<II', blob, 60)
    vertices = array(blob, off, n, '<3f4B4B3f4f')
    n, off = unpack('<II', skin, 4)
    lookup = [v[0] for v in array(skin, off, n, '<H')]
    n, off = unpack('<II', skin, 12)
    indices = [lookup[v[0]] for v in array(skin, off, n, '<H')]
    n, off = unpack('<II', skin, 28)
    subsets = array(skin, off, n, '<10H7f')
    n, off = unpack('<II', skin, 36)
    units = array(skin, off, n, '<2B11H')
    nt, ot = unpack('<II', blob, 80)
    textures = array(blob, ot, nt, '<4I')
    nl, ol = unpack('<II', blob, 128)
    texlookup = [v[0] for v in array(blob, ol, nl, '<H')]
    nc, oc = unpack('<II', blob, 72)
    no, oo = unpack('<II', blob, 88)
    nl, ol = unpack('<II', blob, 144)
    opacitylookup = [v[0] for v in array(blob, ol, nl, '<H')]
    result, transformed = [], {}
    for number, unit in enumerate(units):
        try:
            material, blend = flags[unit[6]]
            # M2Scene::s_blendModes maps 3 to NoAlphaAdd and 4 to Add.
            # Mod and Mod2X are 5/6; neither proves emitted light.
            if not material & 1 or blend not in (3, 4):
                continue
            if unit[8] != 1:
                raise ValueError('Multitexture luminous skin batch')
            texture = textures[texlookup[unit[9]]]
            if texture[0] != 0 or not texture[2]:
                raise ValueError('Dynamic luminous texture replacement')
            name = string(blob, texture[3])
            if NON_SOURCE.search(name):
                continue
            tint, opacity = (1., 1., 1.), 1.
            if unit[5] != 65535:
                if unit[5] >= nc:
                    raise ValueError('Luminous colour index outside M2')
                tint = constant_track(blob, oc + unit[5]*40, '<3f', tint)
                opacity *= constant_track(blob, oc + unit[5]*40 + 20, '<h', (32767,))[0] / 32767.
            if unit[11] != 65535:
                index = opacitylookup[unit[11]]
                if index != 65535:
                    if index >= no:
                        raise ValueError('Luminous opacity index outside M2')
                    opacity *= constant_track(blob, oo + index*20, '<h', (32767,))[0] / 32767.
            if opacity == 0:
                continue
            if not 0 < opacity <= 1 or not all(math.isfinite(v) and 0 <= v <= 4 for v in tint):
                raise ValueError('Luminous colour/opacity range')
            rgb = texture_radiance(assets, name, blend == 4, cache)
            radiance = tuple(rgb[k]*tint[k]*opacity for k in range(3))
            if max(radiance) <= .0001:
                continue
            sub = subsets[unit[3]]
            first, size = sub[4] + (sub[1] << 16), sub[5]
            if first + size > len(indices) or size % 3:
                raise ValueError('Luminous triangle range')
            triangles = [indices[i:i+3] for i in range(first, first+size, 3)]
            # Connected components separate the bulbs of a multi-fixture skin batch.
            components = []
            for tri in triangles:
                attached = [c for c in components if c[0].intersection(tri)]
                if attached:
                    component = attached[0]
                    component[0].update(tri); component[1].append(tri)
                    for other in attached[1:]:
                        component[0].update(other[0]); component[1].extend(other[1]); components.remove(other)
                else:
                    components.append((set(tri), [tri]))
            for ids, triangles in components:
                bones = set()
                for i in ids:
                    if i not in transformed:
                        v = vertices[i]
                        weights = list(zip(v[3:7], v[7:11]))
                        total = sum(w for w, _ in weights)
                        shift = [0., 0., 0.]
                        for weight, bone in weights:
                            if weight:
                                translation = static_bone_offset(blob, bone)
                                for k in range(3):
                                    shift[k] += translation[k] * weight / total
                        transformed[i] = tuple(v[k]+shift[k] for k in range(3))
                    bones.update(b for w, b in zip(vertices[i][3:7], vertices[i][7:11]) if w)
                area, center = 0., [0., 0., 0.]
                for tri in triangles:
                    points = [transformed[i] for i in tri]
                    weight = triangle_area(*points)
                    area += weight
                    for k in range(3):
                        center[k] += sum(p[k] for p in points) / 3 * weight
                if area <= 1e-8:
                    continue
                result.append({'position': tuple(v/area for v in center), 'flux': tuple(v*area for v in radiance),
                               'area': area, 'emission': emission_kind(path, name), 'bones': sorted(bones),
                               'position_basis': f'v264 unlit additive skin batch {number}: {name}; area-weighted triangle centroid',
                               'surface_batch': number, 'texture': name, 'opacity': opacity})
        except (ValueError, IndexError, FileNotFoundError, struct.error) as exc:
            refusals['surface: ' + str(exc)] += 1
    return result


def flame_height_centroid(blob, p, flags, bone, kind, life, colors, alpha, scales):
    """Offline proxy for a fixed vertical plane emitter's luminous volume.

    The nozzle can be inside a brazier. Use the brightness-weighted flame,
    without moving native particles or exempting its bowl from shadowing.
    Unsupported motion keeps the proven anchor instead of guessing an axis.
    """
    try:
        if kind != 1 or flags & (0x4000 | 0x40000 | 0x80000 | 0x800000):return None
        static_bone_offset(blob, bone) # Identity rotation/scale throughout the parent chain.
        if unpack('<f',blob,p+172)[0] or any(unpack('<2f',blob,p+308)) or any(unpack('<4f',blob,p+416)):return None
        if constant_track(blob,p+240,'<f',(0.,))[0]:return None
        speed=constant_track(blob,p+52,'<f',(0.,))[0]
        latitude=constant_track(blob,p+92,'<f',(0.,))[0]
        gravity=constant_track(blob,p+132,'<f',(0.,))[0];drag=unpack('<f',blob,p+372)[0]
        if not (0<=speed<=64 and 0<=latitude<=math.pi and -64<=gravity<=64 and 0<=drag<=16):return None
        speed *= math.sin(latitude)/latitude if latitude else 1.
        def sample(curve,t):
            if t<=curve[0][0]:return curve[0][1]
            for (a,x),(b,y) in zip(curve,curve[1:]):
                if t<=b:return tuple(u+(v-u)*(t-a)/(b-a) for u,v in zip(x,y))
            return curve[-1][1]
        energy=height=0.
        # Bounded midpoint integration is a relative light proxy, not a particle replay.
        for i in range(128):
            t=(i+.5)/128;age=t*life;x,y=sample(scales,t*32767)
            weight=max(sample(colors,t*32767))*sample(alpha,t*32767)[0]*x*y
            z=(speed+gravity/drag)*(-math.expm1(-drag*age))/drag-gravity*age/drag if drag>1e-6 else speed*age-.5*gravity*age*age
            energy+=weight;height+=weight*z
        value=height/energy if energy else 0.
        return value if math.isfinite(value) and 0<=value<=16 else None
    except (ValueError, IndexError, struct.error):return None


def particle_sources(assets, path, blob, cache, refusals):
    count, offset = unpack('<II', blob, 296)
    if count > 4096:
        raise ValueError('Particle count exceeds bounded policy')
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    records = array(blob, offset, count, f'<{stride}s')
    nt, ot = unpack('<II', blob, 80)
    textures = array(blob, ot, nt, '<4I')
    result = []
    for i, (particle,) in enumerate(records):
        try:
            p = offset + i*stride
            flags = unpack('<I', particle, 4)[0]
            bone, tex = unpack('<hH', particle, 20)
            blend, kind, replacement = unpack('<BBh', particle, 40)
            if blend not in (3, 4):
                continue
            if replacement not in (-1, 0) or flags & (0x8000 | 0x10000000):
                raise ValueError('Particle replacement colour, burst or multitexture')
            if kind not in (1, 2) or unpack('<I', particle, 24)[0] > 1 or unpack('<I', particle, 32)[0] > 1:
                raise ValueError('Model, child or noncompact particle emitter')
            texture = textures[tex]
            if texture[0] != 0 or not texture[2]:
                raise ValueError('Dynamic particle texture replacement')
            name = string(blob, texture[3])
            excluded = NON_SOURCE.search(name)
            # Scourge braziers tint this generic sprite into additive fire.
            # Keep smoke-only models and nonadditive particles excluded.
            model_name = path.rsplit('\\', 1)[-1]
            generic_fire_sprite = (excluded and excluded.group() == 'smoke' and
                                   re.fullmatch(r'toonsmoke\d+\.blp', name.rsplit('\\', 1)[-1]) and
                                   FIRE.search(model_name) and not NON_SOURCE.search(model_name))
            if excluded and not generic_fire_sprite:
                continue
            if constant_track(blob, p+456, '<B', (1,))[0] != 1:
                continue
            local = unpack('<3f', particle, 8)
            position = static_bone_point(blob, bone, local)
            rate = constant_track(blob, p+176, '<f')[0]
            life = constant_track(blob, p+152, '<f')[0]
            extent = [constant_track(blob, p+r, '<f', (0.,))[0] for r in (200, 220)]
            if not 0 < rate <= 100000 or not 0 < life <= 120 or not all(0 <= v <= 8 for v in extent):
                raise ValueError('Particle population/extent range')
            colors = lifetime_track(blob, p+260, '<3f')
            alpha = lifetime_track(blob, p+276, '<h') if blend == 4 else [(0, (32767,))]
            color, opacity = lifetime_colour(colors, alpha)
            scales = lifetime_track(blob, p+292, '<2f')
            if not all(math.isfinite(v) and 0 <= v <= 32 for _, pair in scales for v in pair):
                raise ValueError('Particle scale range')
            height = flame_height_centroid(blob,p,flags,bone,kind,life,colors,alpha,scales) if generic_fire_sprite else None
            if height is not None:position=(position[0],position[1],position[2]+height)
            # Integrate linearly interpolated sprite width × height over its lifetime.
            scales = [(0, scales[0][1]), *scales, (32767, scales[-1][1])]
            area = sum((b-a)*4*(x*y+(x*dy+y*dx)/2+dx*dy/3)/32767
                       for (a, (x,y)), (b, (u,v)) in zip(scales, scales[1:]) for dx,dy in [(u-x,v-y)]) * rate * life
            texrgb = texture_radiance(assets, name, blend == 4, cache)
            flux = tuple(color[k]*texrgb[k]*opacity*area for k in range(3))
            if max(flux) <= .0001:
                continue
            result.append({'position': position, 'local_position': local, 'bones': [bone], 'flux': flux,
                           'area': area, 'emission': emission_kind(path, name), 'particle': i, 'texture': name,
                           'position_basis': f'v264 additive particle {i}: {name}; proven fixed source point on bone {bone}' +
                                             (f'; vertical luminous centroid +{height:.6g}' if height is not None else ''),
                           'rate': rate, 'lifetime': life, 'opacity': opacity, 'flame_height_centroid': height})
        except (ValueError, IndexError, FileNotFoundError, struct.error) as exc:
            refusals['particle: ' + str(exc)] += 1
    return result


def calibrated_intensity(raw):
    """Relative calibration: exact weak response, smooth bounded strong response.

    This is an artistic energy mapping, not a claim of lux or lumen matching.
    Original RGB flux and precompression intensity remain in emitter evidence.
    """
    if not math.isfinite(raw) or raw < 0:
        raise ValueError('Nonfinite or negative source energy')
    return raw if raw <= 1 else 1-3*math.expm1(-(raw-1)/3)


def consolidate(sources, max_regions=16, position_limit=100):
    """Compact fixture groups sum RGB energy; separated flames remain independent."""
    if type(max_regions) is not int or not 1 <= max_regions <= 64:
        raise ValueError('Emitter region policy must stay within 1..64')
    if type(position_limit) not in (int,float) or not math.isfinite(position_limit) or not 0 < position_limit <= 1024:
        raise ValueError('Emitter local position policy must stay within 1024')
    for source in sources:
        if len(source['position']) != 3 or not all(math.isfinite(v) and abs(v) <= position_limit for v in source['position']):
            raise ValueError('Source outside bounded M2 position policy')
        if len(source['flux']) != 3 or not all(math.isfinite(v) and v >= 0 for v in source['flux']) or max(source['flux']) <= 0:
            raise ValueError('Nonfinite, negative or empty source flux')
    groups = []
    for source in sorted(sources, key=lambda s: (*s['position'], s['position_basis'])):
        radius = 1.5 if source['emission'] == 'candle' else 1.
        group = next((g for g in groups if all(s['emission'] == source['emission'] and
                     math.dist(s['position'], source['position']) <= radius for s in g)), None)
        if group is None:
            groups.append([source])
        else:
            group.append(source)
    if len(groups) > max_regions:
        raise ValueError(f'More than {max_regions} independent luminous regions')
    emitters = []
    for group in groups:
        flux = tuple(sum(s['flux'][k] for s in group) for k in range(3))
        power = max(flux)
        raw_intensity = power * .35
        intensity = calibrated_intensity(raw_intensity)
        weights = [max(s['flux']) for s in group]
        position = tuple(sum(s['position'][k]*w for s,w in zip(group, weights))/sum(weights) for k in range(3))
        if not all(math.isfinite(v) and abs(v) <= position_limit for v in position):
            raise ValueError('Source outside bounded M2 position policy')
        kind = group[0]['emission']
        radius = min(5. if kind == 'candle' else 24., max(2., 10.*math.sqrt(intensity)))
        emitters.append({'position': position, 'rgb': tuple(v/power for v in flux), 'intensity': intensity,
                         'start': min(1., radius/4), 'end': radius, 'emission': kind,
                         'position_basis': '; '.join(sorted({s['position_basis'] for s in group})),
                         'evidence': group, 'relative_flux': flux, 'raw_intensity': raw_intensity})
    return emitters


def inspect_model(assets, path, blob=None, cache=None, max_regions=16, position_limit=100):
    """Inspect genuine M2 geometry/particle anchors under an explicit local policy.

    Defaults match standalone native append. Metadata owners may opt into
    64 regions/1024 local units; source fields and static bones still provide
    every position. Unsupported bone animation never becomes a bind-pose light.
    """
    record = {'status': 'unsupported'}
    try:
        blob = assets.read(path) if blob is None else blob
        record['asset_sha256'] = hashlib.sha256(blob).hexdigest()
        record['archive'] = assets.origin(path)
        if not blob:
            return {**record, 'status': 'missing', 'reason': 'Archive deletion marker'}
        if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
            raise ValueError('Not MD20/v264')
        native = unpack('<I', blob, 264)[0]
        if native:
            stats = collections.Counter()
            lights = extract_m2(blob, stats)
            return {**record, 'status': 'native', 'native_records': native,
                    'supported_static_native_records': len(lights), 'native_track_statistics': dict(stats)}
        if DISABLED.search(path.rsplit('\\', 1)[-1]):
            return {**record, 'status': 'not_emissive', 'reason': 'Explicit disabled/broken variant'}
        refusals, cache = collections.Counter(), cache if cache is not None else {}
        sources = particle_sources(assets, path, blob, cache, refusals)
        try:
            surfaces = surface_sources(assets, path, blob, cache, refusals)
        except (ValueError, IndexError, FileNotFoundError, struct.error) as exc:
            surfaces = []; refusals['surface: ' + str(exc)] += 1
        # A flame sprite can also have a decorative glow shell. Prefer the
        # actual particle at the same source, retaining distant luminous parts.
        sources += [s for s in surfaces if not any(math.dist(s['position'], p['position']) <= .5 for p in sources)]
        record['refusals'] = dict(refusals)
        if not sources:
            return {**record, 'status': 'unsupported' if refusals else 'not_emissive',
                    'reason': 'Unsupported luminous evidence' if refusals else 'No supported additive luminous evidence'}
        emitters = consolidate(sources,max_regions=max_regions,position_limit=position_limit)
        return {**record, 'status': 'addable', 'emitters': emitters,
                'partial': bool(refusals), 'source_regions': len(sources)}
    except FileNotFoundError as exc:
        return {**record, 'status': 'missing', 'reason': str(exc)}
    except (ValueError, IndexError, KeyError, struct.error) as exc:
        return {**record, 'reason': str(exc)}


def map_records(assets):
    blob = assets.read('dbfilesclient\\map.dbc')
    magic, n, fields, size, strings = unpack('<4s4I', blob)
    if magic != b'WDBC' or fields != 66 or size != 264 or len(blob) != 20+n*size+strings:
        raise ValueError('Requires 3.3.5 Map.dbc (66 fields)')
    table = blob[20+n*size:]
    records = []
    canonical = {name.lower(): name for name in instance_maps.ALL_MAPS}
    canonical.update({name.lower(): name for name in ('Azeroth', 'Kalimdor', 'Expansion01', 'Northrend')})
    for row in array(blob, 20, n, '<66I'):
        name = string(table, row[1])
        if row[0] in CONTINENTS or row[2] in (1, 2) or name in canonical:
            records.append({'id': row[0], 'directory': canonical.get(name, name), 'instance_type': row[2],
                            'expansion': row[63], 'corpse_map_id': row[59],
                            'continent': CONTINENTS.get(row[0]), 'instance_group': instance_maps.group_of(canonical.get(name, name))})
    return records


def census(assets, catalog=None, with_placements=True, progress=None, max_regions=16, position_limit=100):
    """Complete listed-asset denominator plus actual placements in all target DBC maps."""
    models, maps, placements = {}, {}, collections.defaultdict(collections.Counter)
    wmos, read_cache = {}, {}
    def read_wmo(path):
        if path not in read_cache:
            blob = assets.read(path)
            groups = list(group_blobs(assets.read, path, blob))
            # The existing placement API deliberately skips missing WMO groups;
            # the census must expose the resulting limit on its denominator.
            expected = unpack('<I', dict(chunks(blob)).get('MOHD', b''), 4)[0]
            read_cache[path] = wmo_doodads(blob, group_refs(groups))
            stats = collections.Counter(); lights = extract_wmo(blob, stats)
            wmos[path] = {'native_point_lights': len(lights), 'statistics': dict(stats), 'archive': assets.origin(path),
                          'asset_sha256': hashlib.sha256(blob).hexdigest(), 'groups_expected': expected,
                          'groups_read': len(groups), 'missing_groups': expected-len(groups)}
        return read_cache[path]
    selected = map_records(assets) if with_placements else []
    if selected:
        assets.probe_maps([m['directory'] for m in selected])
    for metadata in selected:
        name = metadata['directory']; seen = set(); counts = collections.Counter(); placed_wmos = collections.Counter()
        entry = maps[name] = {**metadata, 'status': 'complete', 'errors': [], 'adt_expected': 0, 'adt_read': 0}
        def consume(items):
            for path, instance in items:
                if instance is None:
                    continue
                identity = instance[:3]
                if identity in seen:
                    continue
                seen.add(identity)
                if path.endswith('.m2'):
                    placements[path][name] += 1
                else:
                    placed_wmos[path] += 1
                counts['m2' if path.endswith('.m2') else 'wmo'] += 1
        try:
            wdt = instance_maps.read_wdt(assets.read(instance_maps.wdt_path(name)))
            if wdt['global_wmo']:
                consume(discover_global(wdt, read_wmo))
            else:
                entry['adt_expected'] = len(wdt['tiles'])
                for x,y in wdt['tiles']:
                    path = f'world\\maps\\{name}\\{name}_{x}_{y}.adt'.lower()
                    try:
                        consume(discover(assets.read(path), read_wmo, path)); entry['adt_read'] += 1
                    except (ValueError, FileNotFoundError, IndexError, KeyError, struct.error) as exc:
                        entry['errors'].append({'asset': path, 'error': str(exc)})
        except (ValueError, FileNotFoundError, IndexError, KeyError, struct.error) as exc:
            entry['errors'].append({'asset': instance_maps.wdt_path(name), 'error': str(exc)})
        for path in placed_wmos:
            if wmos.get(path, {}).get('missing_groups'):
                entry['errors'].append({'asset': path, 'error': 'Missing WMO groups', 'count': wmos[path]['missing_groups']})
        entry['native_wmo_light_placements'] = sum(wmos.get(p, {}).get('native_point_lights', 0)*n for p,n in placed_wmos.items())
        if entry['errors']:
            entry['status'] = 'incomplete'
        entry['placements'] = dict(counts)
        if progress:
            progress('map ' + name + ': ' + entry['status'])
    candidates = {p for p in assets.providers if p.startswith('world\\') and p.endswith('.m2')}
    candidates.update(p for p in placements if p.endswith('.m2'))
    candidates.update(catalog or {})
    cache = {}
    for i, path in enumerate(sorted(candidates)):
        models[path] = {**inspect_model(assets, path, cache=cache, max_regions=max_regions, position_limit=position_limit),
                        'placements': dict(placements[path])}
        if progress and i % 500 == 0:
            progress(f'models {i}/{len(candidates)}')
    for name, entry in maps.items():
        counts = collections.Counter()
        asset_counts = collections.Counter()
        for path, locations in placements.items():
            if name in locations:
                status = models[path]['status']; counts[status] += locations[name]; asset_counts[status] += 1
        entry['m2_asset_statuses'] = {s: asset_counts[s] for s in STATUSES}
        entry['m2_placement_statuses'] = {s: counts[s] for s in STATUSES}
    counts = collections.Counter(v['status'] for v in models.values())
    native_stats = collections.Counter()
    for model in models.values():
        native_stats.update(model.get('native_track_statistics', {}))
    return {'schema': 1, 'scope': 'All listed WORLD M2 plus referenced M2 and reviewed paths; four continents and every Map.dbc party/raid map',
            'archive_fingerprint': assets.fingerprint(), 'archive_warnings': assets.warnings(),
            'emitter_policy': {'max_regions':max_regions,'local_position_limit':position_limit,
                               'relative_energy':'x=.35*peak_flux; x<=1 unchanged; otherwise 1+3*(1-exp(-(x-1)/3)); maximum4'},
            'counts': {'model_denominator': len(models), **{s: counts[s] for s in STATUSES},
                       'maps': len(maps), 'maps_complete': sum(m['status'] == 'complete' for m in maps.values()),
                       'partial_addable_models': sum(v.get('partial', False) for v in models.values()),
                       'addable_emitters': sum(len(v.get('emitters', [])) for v in models.values()),
                       'native_m2_records': native_stats['m2_records'],
                       'native_m2_static_supported': sum(v.get('supported_static_native_records', 0) for v in models.values()),
                       'native_m2_unsupported': sum(v for k,v in native_stats.items() if k.startswith('unsupported_')),
                       'native_m2_disabled': native_stats['m2_disabled'],
                       'placed_wmo_assets': len(wmos),
                       'native_wmo_records': sum(v['statistics'].get('wmo_records', 0) for v in wmos.values()),
                       'native_wmo_supported_points': sum(v['native_point_lights'] for v in wmos.values())},
            'status_definitions': {'native': 'Original native light array preserved; unsupported tracks are counted separately.',
                                   'addable': 'At least one supported static source; partial marks additional unsupported evidence.',
                                   'not_emissive': 'Disabled/dark or no supported additive evidence; does not prove absence of shader-only/nonadditive glow.',
                                   'unsupported': 'Luminous evidence cannot be safely represented by this static bounded append.',
                                   'missing': 'Model unavailable or an active archive deletion marker.'},
            'maps': maps, 'models': models, 'placed_wmo_native_lights': wmos,
            'references': REFERENCES,
            'limitations': ['Completeness is bounded by readable archive listings and successfully parsed placements, not a claim of 100% visible lamp coverage.',
                            'Nonadditive/shader-only emission is not proof of emitted light; unsupported animated material/bone evidence is reported.',
                            'Mean linear texture radiance over the full texture × emitting triangle/sprite area is a relative proxy, not calibrated lumens or exact UV integration.',
                            'Native M2 arrays remain unchanged, including unsupported animated native tracks. Static extraction counts report their limits.',
                            'No game launch, installation or runtime visual verification.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    client_archives.add_arguments(parser)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--without-placements', action='store_true')
    parser.add_argument('--max-regions',type=int,choices=(16,64),default=16,help='64 is for renderer metadata; native standalone append stays at 16')
    parser.add_argument('--position-limit',type=int,choices=(100,1024),default=100,help='1024 requires metadata validators matching the proven local-source policy')
    args = parser.parse_args(); assets = Assets.from_args(args)
    try:
        result = census(assets, with_placements=not args.without_placements,
                        progress=lambda s: print(s, file=sys.stderr, flush=True),
                        max_regions=args.max_regions,position_limit=args.position_limit)
    finally:
        assets.close()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result['counts']))
