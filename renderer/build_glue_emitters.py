"""Stage proven emitters for active login and character-screen model assets.

Native backdrop lights always win. A proven constant-RGB/dynamic-intensity
update bug can be repaired without adding a light or changing authored values.
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
                        patched, repairs = repair_native_flicker(blob)
                    except ValueError as error:
                        refused[path] = str(error)
                        repairs = []
                    if repairs:
                        payload[path] = patched
                        repaired[path] = {**models[path], 'before_sha256': hashlib.sha256(blob).hexdigest(),
                                          'after_sha256': hashlib.sha256(patched).hexdigest(),
                                          'native_flicker_repairs': repairs, 'emitters': [],
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
                  native_lights=added, native_lights_added=added, existing_native_lights=sum(v['native_lights'] for v in native.values()),
                  validated_native_models=len(payload), mpq_byte_roundtrips=len(payload), dbc_files=0, wdb_files=0)
    report = {**shared, 'format': 'native WotLK v264 glue scene lights', 'discovery': discovery,
              'models': {**shared.get('models', {}), **repaired}, 'skipped': skipped, 'counts': dict(counts),
              'existing_native_models_preserved': native, 'native_repair_refusals': refused,
              'excluded_existing_owners': sorted(excluded),
              'limitations': ['No duplicate source is added beside existing native lights; original count, bones and all original bytes remain.',
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
