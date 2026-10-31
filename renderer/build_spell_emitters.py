#!/usr/bin/env python3
"""Stage optional native spell lights; read client assets, never install or edit DBCs."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
import client_archives
from world_scene_builder import Assets, array, string, unpack
from world_lights_builder import constant_track
from build_equipment_emitters import NATIVE_REFERENCE, REFERENCES, lifetime_track, validate_rgb, sources as particle_sources
from equipment_emitter_patch import append_bone_lights
from mpq import Archive
from emitter_tracks import pulse_for


def lifecycle_track(blob, offset, fmt, default):
    """Read inline native linear/step tracks, including empty sequence defaults."""
    interpolation, sequence, nt, ot, nk, ok = unpack('<hh4I', blob, offset)
    globals, global_offset = unpack('<II', blob, 20)
    if interpolation not in (0, 1) or not -1 <= sequence < globals or nt != nk or nt > 128:
        raise ValueError('Unsupported lifecycle track header')
    if sequence >= 0 and (nt != 1 or not unpack('<I', blob, global_offset + sequence * 4)[0]):
        raise ValueError('Invalid lifecycle global sequence')
    curves = []
    animations, animation_offset = unpack('<II',blob,28)
    if sequence == -1 and animations and nt > animations:
        raise ValueError('Lifecycle has more curves than original animations')
    for slot,((tc, to), (kc, ko)) in enumerate(zip(array(blob, ot, nt, '<II'), array(blob, ok, nk, '<II'))):
        if tc != kc or tc > 4096:
            raise ValueError('Invalid lifecycle key counts')
        if sequence == -1 and animations and tc and not unpack('<I',blob,animation_offset+slot*64+12)[0] & 0x20:
            raise ValueError('External animation lifecycle is not inline emission proof')
        times = [v for (v,) in array(blob, to, tc, '<I')]
        values = [v for (v,) in array(blob, ko, kc, fmt)]
        if any(a >= b for a, b in zip(times, times[1:])) or not all(math.isfinite(v) for v in values):
            raise ValueError('Invalid lifecycle times/values')
        if not values:
            times, values = [0], [default]
        curves.append({'times': times, 'values': values})
    return {'interpolation': interpolation, 'sequence': sequence,
            'curves': curves or [{'times': [0], 'values': [default]}]}


def add_bytes(out, data):
    out.extend(bytes(-len(out) % 4))
    offset = len(out)
    out.extend(data)
    return offset


def write_track(out, track, fmt, scale=1):
    times, keys = [], []
    for curve in track['curves']:
        count = len(curve['times'])
        times.append((count, add_bytes(out, struct.pack('<%dI' % count, *curve['times']))))
        values = [v * scale for v in curve['values']]
        keys.append((count, add_bytes(out, b''.join(struct.pack(fmt, v) for v in values))))
    ot = add_bytes(out, b''.join(struct.pack('<II', *v) for v in times))
    ok = add_bytes(out, b''.join(struct.pack('<II', *v) for v in keys))
    return struct.pack('<hh4I', track['interpolation'], track['sequence'], len(times), ot, len(keys), ok)


def spell_sources(assets, blob, max_sources=16):
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Not WotLK v264')
    if unpack('<I', blob, 264)[0]:
        raise ValueError('Existing native lights preserved')
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    if count > 4096 or offset + count * stride > len(blob):
        raise ValueError('Invalid particle array')
    result, rejected = [], {}
    for index in range(count):
        try:
            p = offset + index * stride
            rate = lifecycle_track(blob, p + 176, '<f', 0.)
            enabled = lifecycle_track(blob, p + 456, '<B', 1)
            life = lifecycle_track(blob,p+152,'<f',0.)
            lifetimes = [v for c in life['curves'] for v in c['values']]
            scale = lifetime_track(blob,p+292,'<2f')
            if not all(0 <= v <= 120 for v in lifetimes) or not max(lifetimes) or not any(min(v) > 0 for _,v in scale) or not all(math.isfinite(v) and v >= 0 for _,s in scale for v in s):
                raise ValueError('No visible bounded particle lifetime/scale')
            rates = [v for c in rate['curves'] for v in c['values']]
            states = [v for c in enabled['curves'] for v in c['values']]
            peak = max(rates)
            if not all(0 <= v < 100000 for v in rates) or not peak or not all(v in (0, 1) for v in states) or not max(states):
                raise ValueError('No supported positive emission/enable lifecycle')
            # Reuse the strict actual additive texture/lifetime qualification on
            # one particle. Only this disposable qualification copy is neutralized.
            probe = bytearray(blob)
            struct.pack_into('<II', probe, 296, 1, p)
            probe[p + 176:p + 196] = write_track(probe, {'interpolation': 0, 'sequence': -1,
                'curves': [{'times': [0], 'values': [peak]}]}, '<f')
            probe[p + 456:p + 476] = struct.pack('<hh4I', 0, -1, 0, 0, 0, 0)
            source = particle_sources(assets, bytes(probe))[0]
            source.update(particles=[index], rate_track=rate, enable_track=enabled,
                          lifecycle='original bone + sequence/global time; rate-scaled intensity and original enabled-state')
            # Only identical schedules can share a source; unlike unrelated
            # particles, they always switch off together.
            neighbour = next((s for s in result if s['bone'] == source['bone'] and s['rate_track'] == rate
                and s['enable_track'] == enabled and s.get('emission') == source.get('emission')
                and s['intensity']+source['intensity'] <= 4
                and sum((a-b)**2 for a,b in zip(s['position'], source['position'])) < .25**2), None)
            if neighbour:
                rgb = [neighbour['rgb'][k]*neighbour['intensity'] + source['rgb'][k]*source['intensity'] for k in range(3)]
                power = max(rgb)
                a,b = neighbour['intensity'],source['intensity']
                neighbour['position'] = tuple((x*a+y*b)/(a+b) for x,y in zip(neighbour['position'],source['position']))
                neighbour['rgb'], neighbour['intensity'] = tuple(v/power for v in rgb), power
                neighbour['particles'].append(index)
                neighbour['particle_blends'] += source['particle_blends']
                neighbour['particle_flags'] += source['particle_flags']
                neighbour['textures'] = sorted(set(neighbour['textures'] + source['textures']))
            else:
                result.append(source)
        except (ValueError, FileNotFoundError, struct.error) as exc:
            rejected[index] = str(exc)
    if not result:
        raise ValueError('No proven additive source: ' + json.dumps(collections.Counter(rejected.values()), sort_keys=True))
    if max_sources is not None and len(result) > max_sources:
        raise ValueError('Source budget exceeded: %d independent sources (limit %d); none silently omitted' % (len(result), max_sources))
    return result, rejected


def append_spell_lights(original, emitters):
    out = bytearray(append_bone_lights(original, emitters))
    _, records = unpack('<II', out, 264)
    global_count, _ = unpack('<II', original, 20)
    for index, source in enumerate(emitters):
        record = records + index * 156
        if 'rate_track' not in source:
            continue
        out[record + 76:record + 96] = write_track(out, source['rate_track'], '<f', source['intensity'] / source['rate'])
        out[record + 136:record + 156] = write_track(out, source['enable_track'], '<B')
        # CM2Model::AnimateMT commits diffuseIntensity * RGB only when the RGB
        # track updates. Two equal keys keep that native branch running every
        # frame (count > m_uint90); a single constant RGB key freezes intensity.
        if pulse_for(source, index):
            continue  # Pulsed RGB already updates every native frame.
        timestamp = add_bytes(out, struct.pack('<2I', 0, 1))
        rgb = add_bytes(out, struct.pack('<6f', *source['rgb'], *source['rgb']))
        times = add_bytes(out, struct.pack('<II', 2, timestamp))
        keys = add_bytes(out, struct.pack('<II', 2, rgb))
        struct.pack_into('<hh4I', out, record + 56, 1, global_count, 1, times, 1, keys)
    validate_spell_lights(original, bytes(out), emitters)
    return bytes(out)


def validate_spell_lights(original, patched, emitters):
    preserved = bytearray(patched[:len(original)])
    for start, end in ((20, 28), (264, 272)):
        preserved[start:end] = original[start:end]
    if preserved != original:
        raise ValueError('Original model bytes changed')
    count, offset = unpack('<II', patched, 264)
    if count != len(emitters):
        raise ValueError('Native light count mismatch')
    for index, source in enumerate(emitters):
        record = offset + index * 156
        if unpack('<HH', patched, record) != (1, source['bone']):
            raise ValueError('Original source bone mismatch')
        for actual, wanted in zip(unpack('<3f', patched, record+4), source['position']):
            if not math.isclose(actual, wanted, rel_tol=1e-6, abs_tol=1e-6):
                raise ValueError('Original source position mismatch')
        validate_rgb(patched, record+56, source, index)
        if 'rate_track' not in source:
            for relative, wanted in ((76, source['intensity']), (96, source['start']), (116, source['end'])):
                if not math.isclose(constant_track(patched, record+relative, '<f')[0], wanted, rel_tol=1e-6):
                    raise ValueError('Steady mesh energy/range mismatch')
            continue
        for relative, fmt, key, scale in ((76, '<f', 'rate_track', source['intensity']/source['rate']), (136, '<B', 'enable_track', 1)):
            actual = lifecycle_track(patched, record+relative, fmt, 0 if fmt == '<f' else 1)
            wanted = source[key]
            if (actual['interpolation'], actual['sequence']) != (wanted['interpolation'], wanted['sequence']):
                raise ValueError('Lifecycle clock/interpolation mismatch')
            if len(actual['curves']) != len(wanted['curves']):
                raise ValueError('Lifecycle sequence count mismatch')
            for a, b in zip(actual['curves'], wanted['curves']):
                if a['times'] != b['times'] or not all(math.isclose(x,y*scale,rel_tol=1e-6,abs_tol=1e-7) for x,y in zip(a['values'],b['values'])):
                    raise ValueError('Lifecycle samples mismatch')
        _, _, _, times, _, _ = unpack('<hh4I', patched, record+56)
        if unpack('<I', patched, times)[0] < 2:
            raise ValueError('Animated intensity requires per-frame RGB update')


def dbc_table(assets, name):
    blob = assets.read(name)
    magic, count, fields, size, length = unpack('<4s4I', blob)
    if magic != b'WDBC' or size != fields * 4 or len(blob) != 20 + count * size + length:
        raise ValueError('Unsupported DBC schema: ' + name)
    return blob, array(blob, 20, count, '<%dI' % fields), blob[20 + count * size:]


def effect_models(assets):
    blob, rows, strings = dbc_table(assets, 'dbfilesclient\\spellvisualeffectname.dbc')
    if unpack('<I', blob, 8)[0] != 7:
        raise ValueError('Unsupported SpellVisualEffectName schema')
    models = collections.defaultdict(list)
    for row in rows:
        path = string(strings, row[2])
        if path.endswith('.mdx'):
            path = path[:-4] + '.m2'
        if path.startswith('spells\\') and path.endswith('.m2'):
            models[path].append(row[0])
    return dict(models)


def missile_models(assets, models):
    """Reject every shared path with a non-missile DBC owner, not just shared IDs."""
    by_id = {i:p for p, ids in models.items() for i in ids}
    references, non_missile, hashes, opaque = collections.defaultdict(list), collections.defaultdict(set), {}, []
    def rows(name, fields):
        path = 'dbfilesclient\\' + name + '.dbc'
        blob, records, _ = dbc_table(assets, path)
        if unpack('<I', blob, 8)[0] != fields:
            raise ValueError('Unsupported lifecycle graph schema: ' + path)
        hashes[path] = hashlib.sha256(blob).hexdigest()
        return records
    for row in rows('spellvisual', 32):
        if row[7] and row[8] in by_id:
            references[by_id[row[8]]].append({'visual_id': row[0], 'effect_id': row[8]})
    for row in rows('spellvisualkit', 38):
        for effect in row[3:15]:
            if effect in by_id:
                non_missile[by_id[effect]].add('SpellVisualKit: ' + str(row[0]))
    for row in rows('spellvisualkitmodelattach', 10):
        if row[2] in by_id:
            non_missile[by_id[row[2]]].add('SpellVisualKitModelAttach: ' + str(row[0]))
    for row in rows('unitblood', 10):
        for effect in row[1:5]:
            if effect in by_id:
                non_missile[by_id[effect]].add('UnitBlood: ' + str(row[0]))
    _, effect_rows, effect_strings = dbc_table(assets, 'dbfilesclient\\spellvisualeffectname.dbc')
    for row in effect_rows:
        if row[0] in by_id and string(effect_strings, row[1]).startswith('hardcoded '):
            non_missile[by_id[row[0]]].add('Hardcoded CEffect owner: ' + str(row[0]))
    # Also reject direct file-name bindings in all other client DBC string
    # tables (ItemVisualEffects, AreaModel, CreatureModelData, GameObject, ...).
    for path in sorted(n for n in assets.providers if n.startswith('dbfilesclient\\') and n.endswith('.dbc')):
        blob = assets.read(path)
        hashes[path] = hashlib.sha256(blob).hexdigest()
        if path == 'dbfilesclient\\spellvisualeffectname.dbc':
            continue
        if blob[:4] != b'WDBC':
            # Opaque auxiliary tables cannot hide a direct M2 path from this
            # conservative byte scan; any occurrence excludes the model.
            opaque.append(path)
            raw = blob.lower().replace(b'/', b'\\')
            for name in references:
                if any(v.encode() in raw for v in (name, name[:-3]+'.mdx')):
                    non_missile[name].add(path)
            continue
        _, count, fields, size, length = unpack('<4s4I', blob)
        if not fields or not size or len(blob) != 20 + count * size + length:
            raise ValueError('Cannot audit malformed owner table: ' + path)
        strings = blob[20 + count * size:]
        for value in strings.split(b'\0'):
            name = value.decode('utf8', errors='replace').lower().replace('/', '\\')
            if name.endswith('.mdx'):
                name = name[:-4] + '.m2'
            if name in references:
                non_missile[name].add(path)
    eligible = {p:v for p,v in references.items() if not non_missile[p]}
    return eligible, {'missile_models': len(references), 'exclusive_missile_models': len(eligible),
        'other_owners': {p:sorted(v) for p,v in non_missile.items() if p in references and v},
        'opaque_tables_scanned_for_model_paths': opaque, 'dbc_sha256': hashes}


def luminous_missile_tail(blob, emitters, rejected):
    """Every particle keeping WaitForModelFX alive must remain visibly luminous."""
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    if rejected or {i for e in emitters for i in e['particles']} != set(range(count)) or unpack('<I', blob, 288)[0]:
        raise ValueError('Missile must have only qualified additive particles, no ribbons')
    bounds = []
    for index in range(count):
        p = offset + index * stride
        life = lifecycle_track(blob, p+152, '<f', 0.)
        variation = unpack('<f', blob, p+172)[0]
        values = [v for c in life['curves'] for v in c['values']]
        if not math.isfinite(variation) or not all(0 < v-abs(variation) <= v+abs(variation) <= 8 for v in values):
            raise ValueError('Missile particle lifetime must be finite and bounded by eight seconds')
        if unpack('<f', blob, p+196)[0] != 0 or not unpack('<I', blob, p+4)[0] & 0x20000 or unpack('<HH', blob, p+48) != (1,1):
            raise ValueError('Missile requires deterministic emission and single-cell particle heads')
        colors = lifetime_track(blob, p+260, '<3f')
        alpha = lifetime_track(blob, p+276, '<h')
        scale = lifetime_track(blob, p+292, '<2f')
        scale_variation = unpack('<2f', blob, p+308)
        _, on, low, high = unpack('<4f', blob, p+352)
        if not all(math.isfinite(v) for v in (*scale_variation, on, low, high)) or on < 1 or min(low, high) <= 0:
            raise ValueError('Missile twinkle can hide living particles')
        if not all(max(v) > .01 for t,v in colors if t < 32767):
            raise ValueError('Missile RGB extinguishes before particle death')
        if unpack('<B', blob, p+40)[0] == 4 and not all(v[0] > 0 for t,v in alpha if t < 32767):
            raise ValueError('Missile alpha extinguishes before particle death')
        if not all(all(math.isfinite(v[k]) and v[k] > abs(scale_variation[k]) for k in range(2)) for t,v in scale if t < 32767):
            raise ValueError('Missile scale extinguishes before particle death')
        bounds.append(max(values) + abs(variation))
    return max(bounds)


def build(assets, output, excluded, audit_broad=False):
    output.mkdir(parents=True, exist_ok=True)
    candidates = effect_models(assets)
    if audit_broad:
        candidates.update({p:candidates.get(p,[]) for p in assets.providers if p.startswith('spells\\') and p.endswith('.m2')})
    eligible, graph = ({}, {}) if audit_broad else missile_models(assets, candidates)
    report = {'format': 'native WotLK v264 spell lights with authored lifecycle',
        'scope': 'audit-only broad spell models' if audit_broad else 'exclusive missile models with luminous finite particle tails',
        'lifecycle_graph': graph,
        'models': {}, 'skipped': {}, 'excluded_models': sorted(excluded), 'source_archives': assets.fingerprint(),
        'format_references': REFERENCES + [NATIVE_REFERENCE + p for p in ('Engine/Source/Model2/M2Model.cpp',
            'Engine/Source/Services/ParticleSystem2.cpp', 'WoW/Source/Object/ObjectClient/Missile_C.cpp',
            'WoW/Source/Object/ObjectClient/Unit_C.cpp', 'WoW/Source/Object/ObjectClient/Effect_C.cpp',
            'WoW/Source/Object/ObjectClient/DynamicObject_C.cpp')],
        'native_lifecycle_symbols': ['CM2Model::AnimateParticlesMT', 'M2AnimateTrack<float, float>',
            'M2AnimateTrack<uint8_t, uint8_t>', 'CM2Model::AnimateMT', 'CM2Model::SetEmittersEnabled', 'CM2Model::DetachFromScene'],
        'retirement_proof': [
            'CGUnit_C::ReleaseMissiles releases the borrowed precast reference before FireMissiles (Unit_C.cpp:18302-18322).',
            'CMissile::WaitForModelFX stops emitters/ribbons, retains only live particles/ribbons, otherwise DeleteSelf (Missile_C.cpp:1101-1130).',
            'Default qualification forbids ribbons and requires every particle to remain luminous with a finite <=8s lifetime.',
            'CParticleEmitter2::StepUpdate destroys particles when age reaches lifetime; HasLiveParticles checks the live list (ParticleSystem2.cpp:1393,2033-2081).',
            'CMissile destructor detaches and releases the model; CM2Model free/detach unlinks its native lights (Missile_C.cpp:960-970; M2Model.cpp:73-100,3880-3919).'],
        'installation_ready': not audit_broad, 'requires_external_emitter_lifecycle_review': audit_broad,
        'limitations': ['Only SpellVisualEffectName-referenced Spells/*.m2; no DBC is changed or packed.',
            'Intensity follows authored emission rate immediately; existing particle tails are not simulated.',
            'Broad audit mode is unsafe for installation: external SetEmittersEnabled is independent of native lights.',
            'Default scope excludes shared CEffect/DynamicObject/DBC owners. Missile retirement stops emission, waits only for luminous finite tails, then deletes/releases the model and unlinks lights.',
            'Native lights and particles share effectiveAlpha/fadeAlpha; baseAlpha alone does not suppress native lights.',
            'Only inline step/linear lifecycle tracks, actual additive particles 3/4, actual texture RGB and lifetime colour.',
            'At most sixteen independent sources per model; over-budget models are reported, with no silent source omissions. Runtime dynamic admission remains capped at eight.',
            'Model VS2/3 capture is required for Northlight local/fog lighting; particle-only draws may not expose a light.',
            'No installation or in-game test was performed.'],
        'license': "Original MIT helper; derived model binaries are generated only from the user's client and must not be redistributed."}
    payload = {}
    for path, visual_ids in sorted(candidates.items()):
        if path in excluded:
            report['skipped'][path] = 'Excluded: equipment/enchant patch owns this model'
            continue
        if not audit_broad and path not in eligible:
            report['skipped'][path] = 'Not an exclusive missile model; external emitter gate is not proven safe'
            continue
        try:
            original = assets.read(path)
            emitters, rejected = spell_sources(assets, original)
            tail_bound = None if audit_broad else luminous_missile_tail(original, emitters, rejected)
            patched = append_spell_lights(original, emitters)
        except (ValueError, FileNotFoundError, struct.error) as exc:
            report['skipped'][path] = str(exc)
            continue
        payload[path] = patched
        report['models'][path] = {'before_sha256': hashlib.sha256(original).hexdigest(),
            'after_sha256': hashlib.sha256(patched).hexdigest(), 'emitters': emitters,
            'rejected_particles': rejected, 'source_archive': assets.origin(path), 'spell_visual_effect_ids': visual_ids,
            'missile_visual_bindings': eligible.get(path, []), 'maximum_particle_tail_seconds': tail_bound}
    if not payload:
        raise ValueError('No supported spell emitters')
    mpq = output / 'spell-lights-staged.mpq'
    with Archive(mpq, create=True, capacity=max(4096, len(payload)*2)) as archive:
        temporary = output / 'model.tmp'
        for name, data in payload.items():
            temporary.write_bytes(data)
            archive.add(temporary, name)
        temporary.unlink()
    with Archive(mpq) as archive:
        names = {p.lower() for p in archive.names() if not p.startswith('(')}
        if names != set(payload) or any(not n.endswith('.m2') for n in names):
            raise ValueError('MPQ contains unexpected/non-M2 files')
        for name, data in payload.items():
            if archive.read(name) != data:
                raise ValueError('MPQ byte roundtrip failed: ' + name)
    report['counts'] = {'referenced_models': len(candidates), 'excluded_owned_models': sum(p in excluded for p in candidates),
        'patched': len(payload), 'native_lights': sum(len(v['emitters']) for v in report['models'].values()),
        'animated_rate_sources': sum(len({x for c in e['rate_track']['curves'] for x in c['values']}) > 1 for v in report['models'].values() for e in v['emitters']),
        'gated_enable_sources': sum(any(0 in c['values'] for c in e['enable_track']['curves']) for v in report['models'].values() for e in v['emitters']),
        'validated_native_models': len(payload), 'mpq_byte_roundtrips': len(payload), 'dbc_files': 0, 'skin_files': 0, 'texture_files': 0}
    report['mpq_sha256'] = hashlib.sha256(mpq.read_bytes()).hexdigest()
    (output / 'spell-lights-report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    client_archives.add_arguments(parser)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--exclude-report', type=Path, required=True, help='Equipment/enchant report whose model paths must not be replaced')
    parser.add_argument('--audit-broad', action='store_true', help='Audit unsupported external lifecycles too; output must never be installed')
    args = parser.parse_args()
    excluded = {p.lower().replace('/', '\\') for p in json.loads(args.exclude_report.read_text())['models']}
    assets = Assets.from_args(args)
    try:
        result = build(assets, args.output, excluded, args.audit_broad)
    finally:
        assets.close()
    print(json.dumps(result['counts'], indent=2))
