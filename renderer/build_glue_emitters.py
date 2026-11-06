"""Stage proven emitters for active login and character-screen model assets.

Native backdrop lights retain their colour and lifecycle. Frozen RGB updates
and displaced HD lamp anchors can be repaired without adding duplicate lights.
No Lua, skins or textures are staged.
"""
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import tempfile

from attached_emitter_sources import write_stage
from mpq import Archive
from world_scene_builder import array, unpack

EMPTY = 'No supported glue scene emitters'
REFERENCE = re.compile(r'''(?:interface|world|creature|item|spells)[\\/][^"'\r\n<>]*?\.(?:m2|mdx|mdl)(?=["'])''', re.I)
NATIVE_FIELDS = ((16, '<3f'), (36, '<f'), (56, '<3f'), (76, '<f'), (96, '<f'), (116, '<f'), (136, '<B'))
LAMP_WINDOW = re.compile(r'(?:lamp|lantern).*(?:window|glass)|(?:window|glass).*(?:lamp|lantern)', re.I)


def lamp_fixtures(assets, path, blob):
    """Locate compact, closed, rigid luminous glass fixtures.

    HD replacements can move the fixture while retaining the stock light bone.
    Read the winning SKIN palette, never the original vertex bone indices. The
    fixture's own bone keeps its scale/animation; its geometry is not rewritten.
    """
    from attached_emitter_sources import material_track
    from world_scene_builder import decode_blp, srgb_to_linear, string
    def rows(data, at, fmt):
        return array(data, *reversed(unpack('<II', data, at)), fmt)
    materials = rows(blob, 112, '<2H')
    textures = rows(blob, 80, '<4I')
    if not any(t[0] == 0 and t[2] and LAMP_WINDOW.search(string(blob, t[3])) for t in textures):
        return []
    skin = assets.read(path[:-3] + '00.skin')
    if skin[:4] != b'SKIN':
        raise ValueError('Unsupported lamp skin')
    vertices, lookup = rows(blob, 60, '<3f4B4B3f4f'), rows(skin, 4, '<H')
    indices, skin_bones = rows(skin, 12, '<H'), rows(skin, 20, '<4B')
    subsets, combos = rows(skin, 28, '<10H7f'), rows(blob, 120, '<H')
    texture_combos = rows(blob, 128, '<H')
    fixtures = []
    for batch_index, batch in enumerate(rows(skin, 36, '<2B11H')):
        if batch[8] != 1:
            continue
        flags, blend = materials[batch[6]]
        texture = textures[texture_combos[batch[9]][0]]
        name = string(blob, texture[3]) if texture[0] == 0 and texture[2] else ''
        if not flags & 1 or blend not in (0, 1) or not LAMP_WINDOW.search(name):
            continue
        # A display/opacity animation is not proof of a continuously lit bulb.
        if batch[5] != 65535:
            continue
        if batch[11] != 65535:
            weights = rows(blob, 144, '<h')
            weight = weights[batch[11]][0]
            if weight >= 0:
                at = unpack('<I', blob, 92)[0] + weight * 20
                if material_track(blob, at, '<h') != (32767,):
                    continue
        sub = subsets[batch[3]]
        first, size = sub[4] + (sub[1] << 16), sub[5]
        if not size or size % 3 or size > 96 or first + size > len(indices):
            continue
        triangles = {tuple(sorted(indices[i + k][0] for k in range(3))) for i in range(first, first + size, 3)}
        ids = {i for tri in triangles for i in tri}
        if not 8 <= len(ids) <= 64 or not 8 <= len(triangles) <= 64:
            continue
        points, bones = {}, set()
        for i in ids:
            vertex = vertices[lookup[i][0]]
            if sum(vertex[3:7]) != 255:
                raise ValueError('Invalid lamp vertex weights')
            points[i] = vertex[:3]
            for k, weight in enumerate(vertex[3:7]):
                if weight:
                    bones.add(combos[sub[7] + skin_bones[i][k]][0])
        if len(bones) != 1 or not all(math.isfinite(v) for p in points.values() for v in p):
            continue
        # A compact convex enclosure has supporting planes on opposite sides
        # of its center. This admits rectangular/octagonal glass, including
        # shared vertices and repeated backfaces, but rejects a flat glow card.
        center = tuple(sum(p[k] for p in points.values()) / len(points) for k in range(3))
        radius = max(math.dist(p, center) for p in points.values())
        if not .1 <= radius <= 1.5:
            continue
        normals = []
        for tri in triangles:
            p, q, r = (points[i] for i in tri)
            u, v = [tuple(y-x for x,y in zip(p,t)) for t in (q,r)]
            n = (u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0])
            length = math.sqrt(sum(x*x for x in n))
            if length < 1e-8:
                break
            distance = sum(n[k]*(p[k]-center[k]) for k in range(3)) / length
            if abs(distance) < .03:
                break
            normal = tuple(x / length * (1 if distance > 0 else -1) for x in n)
            if any(sum(normal[k]*(t[k]-p[k]) for k in range(3)) > .001 for t in points.values()):
                break
            normals.append(normal)
        if (len(normals) != len(triangles) or
                any(min(sum(x*y for x,y in zip(n,m)) for m in normals) > -.75 for n in normals)):
            continue
        _, _, pixels = decode_blp(assets.read(name), 64)
        rgb = tuple(sum(srgb_to_linear(pixels[i + k] / 255) for i in range(0, len(pixels), 4)) /
                    (len(pixels) // 4) for k in range(3))
        if max(rgb) < .05:
            continue
        fixtures.append(dict(bone=bones.pop(), position=center, texture=name,
                             mesh_batch=batch_index, radius=radius, texture_rgb=rgb))
    return fixtures


def repair_native_lamp_anchors(assets, path, blob):
    """Reattach a uniquely coloured native light to its original glass fixture."""
    count, offset = unpack('<II', blob, 264)
    fixtures = []
    for fixture in lamp_fixtures(assets, path, blob):
        rgb = fixture['texture_rgb']
        matches = []
        for i in range(count):
            record = offset + i * 156
            if unpack('<H', blob, record)[0] != 1:
                continue
            color = native_track(blob, record + 56, '<3f')
            colors = [v for _, _, values in color[2] for v in values]
            if not colors or any(v != colors[0] for v in colors) or min(colors[0]) < 0:
                continue
            norm = math.sqrt(sum(v*v for v in rgb) * sum(v*v for v in colors[0]))
            if not norm or sum(x*y for x,y in zip(rgb, colors[0])) / norm < .98:
                continue
            # Moving a per-bone animated lifecycle onto another bone could
            # change its schedule. Global/constant tracks retain their meaning.
            tracks = [native_track(blob, record + relative, fmt) for relative, fmt in NATIVE_FIELDS]
            if any(clock == -1 and len({v for _, _, values in curves for v in values}) > 1
                   for _, clock, curves in tracks):
                continue
            matches.append(i)
        if len(matches) == 1:
            fixtures.append(dict(light=matches[0], **fixture))
    selected = collections.Counter(f['light'] for f in fixtures)
    fixtures = [f for f in fixtures if selected[f['light']] == 1]
    records = bytearray(blob[offset:offset + count * 156])
    repairs = []
    for f in fixtures:
        at = f['light'] * 156
        before = unpack('<h3f', records, at + 2)
        if before[0] == f['bone'] and math.dist(before[1:], f['position']) < .01:
            continue
        struct.pack_into('<h3f', records, at + 2, f['bone'], *f['position'])
        repairs.append({**f, 'original_bone': before[0], 'original_position': before[1:]})
    if not repairs:
        return blob, []
    out = bytearray(blob)
    out.extend(b'\0' * (-len(out) % 4))
    struct.pack_into('<II', out, 264, count, len(out))
    out.extend(records)
    patched = bytes(out)
    verify_native_lamp_anchors(blob, patched, repairs)
    return patched, repairs


def verify_native_lamp_anchors(original, patched, repairs):
    count, before = unpack('<II', original, 264)
    new_count, after = unpack('<II', patched, 264)
    if (count != new_count or after < len(original) or after + count * 156 > len(patched) or
            original[:264] != patched[:264] or original[272:] != patched[272:len(original)]):
        raise ValueError('Lamp anchor repair changed original model arrays')
    selected = {r['light']: r for r in repairs}
    if len(selected) != len(repairs) or any(not 0 <= i < count for i in selected):
        raise ValueError('Invalid native lamp repair')
    bones = unpack('<I', original, 44)[0]
    for i in range(count):
        a, b = original[before+i*156:before+(i+1)*156], patched[after+i*156:after+(i+1)*156]
        if i not in selected:
            if a != b:
                raise ValueError('Unselected native light changed')
            continue
        repair = selected[i]
        if a[:2] != b[:2] or a[16:] != b[16:] or not 0 <= repair['bone'] < bones:
            raise ValueError('Lamp anchor repair changed light values or lifecycle')
        if b[2:16] != struct.pack('<h3f', repair['bone'], *repair['position']):
            raise ValueError('Lamp anchor does not match its emitting mesh')


def native_track(blob, offset, fmt):
    mode, clock, nt, ot, nk, ok = unpack('<hh4I', blob, offset)
    ng, og = unpack('<II', blob, 20)
    na, oa = unpack('<II', blob, 28)
    if ng >= 32767 or ng and (og < 304 or og + ng * 4 > len(blob)) or na and (oa < 304 or oa + na * 64 > len(blob)):
        raise ValueError('Invalid original native animation arrays')
    if mode not in (0, 1) or nt != nk or nt > 128 or not -1 <= clock < ng:
        raise ValueError('Unsupported native light track header')
    if clock >= 0 and (nt != 1 or not unpack('<I', blob, og + clock * 4)[0]):
        raise ValueError('Unsupported native light global clock')
    if clock == -1 and nt and na and nt != na:
        raise ValueError('Native track does not cover every original animation')
    curves = []
    for slot, ((tc, to), (kc, ko)) in enumerate(zip(array(blob, ot, nt, '<II'), array(blob, ok, nk, '<II'))):
        if tc != kc or tc > 4096 or tc and (to < 304 or ko < 304):
            raise ValueError('Invalid native light key arrays')
        if clock == -1 and na and tc and not unpack('<I', blob, oa + slot * 64 + 12)[0] & 0x20:
            raise ValueError('External native light animation is not inline proof')
        times = tuple(t for (t,) in array(blob, to, tc, '<I'))
        values = tuple(array(blob, ko, kc, fmt))
        if any(a >= b for a, b in zip(times, times[1:])) or any(not math.isfinite(v) for key in values for v in key):
            raise ValueError('Invalid native light times or values')
        curves.append((times, blob[ko:ko + kc * struct.calcsize(fmt)], values))
    return mode, clock, tuple(curves)


def verify_native_repair(original, patched, repairs):
    count, old_offset = unpack('<II', original, 264)
    new_count, new_offset = unpack('<II', patched, 264)
    if (new_count != count or new_offset < len(original) or new_offset + count * 156 > len(patched) or
            patched[:264] != original[:264] or patched[272:len(original)] != original[272:]):
        raise ValueError('Native repair changed original model, clock or light count')
    selected = {r['light'] for r in repairs}
    if not selected or len(selected) != len(repairs) or any(type(i) is not int or not 0 <= i < count for i in selected):
        raise ValueError('Invalid or repeated native light repair')
    for i in range(count):
        before, after = old_offset + i * 156, new_offset + i * 156
        old, new = original[before:before + 156], patched[after:after + 156]
        if i in selected:
            if old[:56] != new[:56] or old[76:] != new[76:]:
                raise ValueError('Native light values, bone or lifecycle changed')
        elif old != new:
            raise ValueError('Unselected native light changed')
        for relative, fmt in NATIVE_FIELDS:
            a, b = native_track(original, before + relative, fmt), native_track(patched, after + relative, fmt)
            if i not in selected or relative != 56:
                if a != b:
                    raise ValueError('Original native light track changed')
                continue
            if a[:2] != b[:2] or len(a[2]) != 1 or len(b[2]) != 1:
                raise ValueError('Native RGB clock changed')
            times, keys, values = a[2][0]
            nt, nk, nv = b[2][0]
            if len(times) != 1 or len(nt) != 2 or times[0] not in nt or nk != keys * 2 or nv != values * 2:
                raise ValueError('Native RGB repair is not semantically constant')


def repair_native_flicker(blob):
    """Make constant RGB refresh a varying native intensity; retain its clock.

    12340 CM2Model::AnimateMT (0x0082F0F0, light loop) updates intensity first,
    but commits intensity*RGB only in the RGB branch. With opaque alpha the
    AnimateST optimization flag m_uint90 becomes 1; one RGB key then stops
    committing. Two identical keys preserve the color function and keep that
    branch active. All original bytes stay in the prefix except array offset.
    """
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Requires MD20/v264')
    count, offset = unpack('<II', blob, 264)
    if count > 4096 or count and (offset < 304 or offset + count * 156 > len(blob)):
        raise ValueError('Invalid native light array')
    if not count:
        return blob, []
    tracks = [{relative: native_track(blob, offset + i * 156 + relative, fmt)
               for relative, fmt in NATIVE_FIELDS} for i in range(count)]
    out, records, repairs = bytearray(blob), [], []
    def add(data):
        out.extend(b'\0' * (-len(out) % 4))
        at = len(out)
        out.extend(data)
        return at
    for i, values in enumerate(tracks):
        record = bytearray(blob[offset + i * 156:offset + (i + 1) * 156])
        rgb, intensity = values[56], values[76]
        levels = [key[0] for _, _, keys in intensity[2] for key in keys]
        eligible = (unpack('<H', record)[0] == 1 and len(rgb[2]) == 1 and len(rgb[2][0][0]) == 1 and
                    len(set(levels)) > 1 and all(0 <= v <= 10000 for v in levels) and
                    (intensity[1] >= 0 or unpack('<I', blob, 28)[0] > 0))
        if eligible:
            times, keys, colors = rgb[2][0]
            if not all(0 <= v <= 10000 for v in colors[0]) or not max(colors[0]):
                records.append(bytes(record))
                continue
            bones, bone_offset = unpack('<II', blob, 44)
            bone = unpack('<h', record, 2)[0]
            if (not 0 <= bone < bones or bone_offset < 304 or bone_offset + bones * 88 > len(blob) or
                    not all(math.isfinite(v) for v in unpack('<3f', record, 4))):
                raise ValueError('Invalid original native source bone or position')
            stamp = times[0]
            stamps = (stamp, stamp + 1) if stamp < 0xffffffff else (stamp - 1, stamp)
            time_data, key_data = add(struct.pack('<2I', *stamps)), add(keys * 2)
            time_array, key_array = add(struct.pack('<II', 2, time_data)), add(struct.pack('<II', 2, key_data))
            record[56:76] = struct.pack('<hh4I', *rgb[:2], 1, time_array, 1, key_array)
            repairs.append({'light': i, 'rgb_clock': rgb[1], 'intensity_clock': intensity[1],
                            'original_rgb_time': stamp, 'new_rgb_times': stamps, 'rgb': colors[0],
                            'intensity_min': min(levels), 'intensity_max': max(levels)})
        records.append(bytes(record))
    if not repairs:
        return blob, []
    struct.pack_into('<II', out, 264, count, add(b''.join(records)))
    patched = bytes(out)
    verify_native_repair(blob, patched, repairs)
    return patched, repairs


def model_path(value):
    path = value.replace('/', '\\').lower()
    while '\\\\' in path:
        path = path.replace('\\\\', '\\')
    if path.endswith(('.mdx', '.mdl')):
        path = path[:-4] + '.m2'
    if (not path.startswith('interface\\') or not path.endswith('.m2') or
            any(p in ('', '.', '..') for p in path.split('\\')) or any(ord(c) < 32 for c in path)):
        raise ValueError('Noncanonical or shared non-Interface backdrop model')
    return path


def candidates(assets):
    models, references, refused = {}, {}, {}
    for name in sorted(assets.providers):
        path = name.lower().replace('/', '\\')
        if path.startswith('interface\\glues\\models\\') and path.endswith(('.m2', '.mdx', '.mdl')):
            try:
                models.setdefault(model_path(path), {'references': []})
            except ValueError as error:
                refused[name] = str(error)
        if not path.startswith('interface\\gluexml\\') or not path.endswith(('.lua', '.xml')):
            continue
        try:
            blob = assets.read(name)
            if len(blob) > 2 * 1024 * 1024:
                raise ValueError('Glue source exceeds 2 MiB bound')
            text = blob.decode('utf-8-sig')
        except (ValueError, OSError) as error:
            refused[name] = str(error)
            continue
        references[path] = hashlib.sha256(blob).hexdigest()
        for line, value in enumerate(text.splitlines(), 1):
            for match in REFERENCE.finditer(value):
                try:
                    model = model_path(match.group())
                    if not model.startswith('interface\\glues\\models\\') and not re.search(
                            r'\b(?:SetCharSelectBackground|SetCharCustomizeBackground|SetBackgroundModel|ModelFFX)\b', value):
                        raise ValueError('Interface widget reference is not a proven backdrop binding')
                    models.setdefault(model, {'references': []})['references'].append({'file': path, 'line': line})
                except ValueError as error:
                    refused[match.group()] = str(error)
    return models, {'glue_source_sha256': references, 'refused_references': refused,
                    'basis': 'Active Glues inventory plus literal Interface background-setter/ModelFFX references in active GlueXML/Lua.',
                    'limitations': ['Unlisted archive additions cannot be enumerated.',
                                    'Arbitrary computed Lua paths outside Glues are not executed or guessed.',
                                    'Shared World/Creature/Item/Spells references retain their existing owner.']}


def build(assets, output, excluded=()):
    output = Path(output)
    models, discovery = candidates(assets)
    excluded = {p.lower().replace('/', '\\') for p in excluded}
    models = {path: info for path, info in models.items() if path not in excluded}
    native, regular, payload, repaired, refused = {}, {}, {}, {}, {}
    for path in models:
        try:
            blob = assets.read(path)
            if len(blob) >= 304 and blob[:4] == b'MD20' and struct.unpack_from('<I', blob, 4)[0] == 264:
                count = struct.unpack_from('<I', blob, 264)[0]
                if count:
                    native[path] = {'sha256': hashlib.sha256(blob).hexdigest(), 'native_lights': count,
                                    'source_archive': assets.origin(path)}
                    try:
                        anchored, anchors = repair_native_lamp_anchors(assets, path, blob)
                    except (OSError, ValueError, IndexError, struct.error) as error:
                        refused[path] = 'Lamp anchor: ' + str(error)
                        anchored, anchors = blob, []
                    try:
                        patched, repairs = repair_native_flicker(anchored)
                    except ValueError as error:
                        refused[path] = str(error)
                        patched, repairs = anchored, []
                    if repairs or anchors:
                        payload[path] = patched
                        repaired[path] = {**models[path], 'before_sha256': hashlib.sha256(blob).hexdigest(),
                                          'after_sha256': hashlib.sha256(patched).hexdigest(),
                                          'native_flicker_repairs': repairs, 'native_anchor_repairs': anchors, 'emitters': [],
                                          'preserved_native_light_count': count, 'source_archive': assets.origin(path)}
                    continue
        except (OSError, ValueError):
            pass  # The shared writer records unavailable and unsupported models.
        regular[path] = models[path]
    output.mkdir(parents=True, exist_ok=True)
    shared = {}
    target = output / 'glue-lights-staged.mpq'
    with tempfile.TemporaryDirectory(prefix='.glue-', dir=output) as folder:
        work = Path(folder)
        try:
            write_stage(assets, work, regular, shared, 'glue', EMPTY)
        except ValueError as error:
            if str(error) != EMPTY:
                raise
        else:
            with Archive(work / 'glue-lights-staged.mpq') as archive:
                payload.update({name: archive.read(name) for name in archive.names() if not name.startswith('(')})
        if payload:
            staged, temporary = work / 'combined.mpq', work / 'model.tmp'
            with Archive(staged, create=True, capacity=max(4096, len(payload) * 2)) as archive:
                for path, blob in sorted(payload.items()):
                    temporary.write_bytes(blob)
                    archive.add(temporary, path)
            with Archive(staged) as archive:
                names = {p.lower() for p in archive.names() if not p.startswith('(')}
                if names != set(payload) or any(archive.read(p) != data for p, data in payload.items()):
                    raise ValueError('Glue model-only MPQ roundtrip failed')
            staged.replace(target)
        else:
            target.unlink(missing_ok=True)
    skipped = dict(shared.get('skipped', {}))
    skipped.update({p: {'category': 'native', 'reason': refused.get(p, 'Existing native lights preserved; no proven frozen RGB condition')}
                    for p in native if p not in repaired})
    added = sum(len(v['emitters']) for v in shared.get('models', {}).values())
    counts = collections.Counter(scanned=len(models), patched=len(payload), native=0, unsupported=0, not_emissive=0)
    counts.update(v['category'] for v in skipped.values())
    counts.update(native_repaired=len(repaired), rgb_tracks_repaired=sum(len(v['native_flicker_repairs']) for v in repaired.values()),
                  lamp_anchors_repaired=sum(len(v['native_anchor_repairs']) for v in repaired.values()),
                  native_lights=added, native_lights_added=added, existing_native_lights=sum(v['native_lights'] for v in native.values()),
                  validated_native_models=len(payload), mpq_byte_roundtrips=len(payload), dbc_files=0, wdb_files=0)
    report = {**shared, 'format': 'native WotLK v264 glue scene lights', 'discovery': discovery,
              'models': {**shared.get('models', {}), **repaired}, 'skipped': skipped, 'counts': dict(counts),
              'existing_native_models_preserved': native, 'native_repair_refusals': refused,
              'excluded_existing_owners': sorted(excluded),
              'limitations': ['No duplicate source is added beside existing native lights; original count and original mesh/bone arrays remain.',
                              'Only one-to-one colour-matched compact luminous lamp enclosures can reanchor an existing source.',
                              'Only proven constant RGB tracks gain an identical key; their original values, timestamps and clock remain.',
                              'This repairs intensity refresh, not native four-slot eviction or other scene consumption problems.',
                              'Only original inline lifecycle tracks, source bones and measured additive emission qualify for new sources.'],
              'archive_warnings': assets.warnings() if hasattr(assets, 'warnings') else []}
    if payload:
        report['mpq_sha256'] = hashlib.sha256(target.read_bytes()).hexdigest()
    (output / 'glue-lights-report.json').write_text(json.dumps(report, indent=2) + '\n')
    if not payload:
        raise ValueError(EMPTY)
    return report
