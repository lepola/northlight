#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Actual compact MPQ fixtures and inn placements against independent light-energy references.

Receivers are authored surfaces. This is a static lighting correctness stand,
not a recorded game scene, GPU timing result or full-scene FPS measurement.
All client inputs are read-only; generated M2/FGL bytes stay in memory.
"""
import collections
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1] / 'renderer')]
import northlight_paths as fp
import outdoor_light_profiles as profiles
from build_emitter_patch import profile
from m2_emitter_patch import append_lights, verify_preserved
from world_light_placements import building, group_blobs, group_refs, wmo_doodads
from world_lights_builder import RECORD, extract_m2, extract_wmo, instance_lights
from world_scene_builder import Assets

REFS = json.loads((Path(__file__).parent / 'fixtures/compact-emitter-references.json').read_text())['fixtures']
WMOS = {
    r'world\wmo\azeroth\buildings\goldshireinn\goldshireinn.wmo':
        '079608e789723183b6b2f226a60ce885c8b080eb25cba59f572ed2150e3841a1',
    r'world\wmo\azeroth\buildings\duskwood_inn\duskwood_inn.wmo':
        '81ca14f93472dfd4711a029b80538c6f8cd619591462df2f3ec9ec6558255b6d',
}


def reference(ref):
    return [(tuple(ref['position']), tuple(c * ref['intensity'] for c in ref['rgb']),
             ref['start'], ref['end'], 0x80000000, 3, 2)]


def old_authored(p):
    return [(tuple(e['position']), tuple(c * e['intensity'] for c in e['rgb']),
             e['start'], e['end'], 0x80000000 + i, 3,
             2 if e.get('casts_shadows', p.get('casts_shadows', False)) else 0)
            for i, e in enumerate(p.get('emitters', [p]))]


def shade(lights, point, normal):
    """Independent double reference for LocalDirect's reach² × Lambert term."""
    rgb = [0., 0., 0.]
    for light in lights:
        vector = [light[k] - point[k] for k in range(3)]
        distance = math.sqrt(sum(x * x for x in vector))
        inverse = 1 / math.sqrt(max(distance * distance, .0025))
        reach = max(0., min(1., (light[7] - 1 / inverse) /
                            max(light[7] - min(light[6], light[7] * .9), .05)))
        lambert = max(0., min(1., sum(a * b for a, b in zip(normal, vector)) * inverse))
        for k in range(3):
            rgb[k] += light[3 + k] * reach * reach * lambert
    return rgb


def receivers(center, span):
    # A floor and four inward-facing walls around each authored fixture.
    for u in (-1., -.5, 0., .5, 1.):
        for v in (-1., -.5, 0., .5, 1.):
            yield (center[0] + u * span, center[1] + v * span, center[2] - span), (0., 0., 1.)
            for axis in (0, 1):
                for side in (-1., 1.):
                    point = list(center)
                    point[axis] += side * span
                    point[1 - axis] += u * span
                    point[2] += v * span
                    normal = [0., 0., 0.]
                    normal[axis] = -side
                    yield tuple(point), tuple(normal)


def close(a, b, tolerance=0.):
    assert len(a) == len(b), (len(a), len(b))
    for left, right in zip(a, b):
        assert len(left) == len(right)
        for x, y in zip(left, right):
            assert x == y or isinstance(x, (int, float)) and abs(x - y) <= tolerance, (x, y, tolerance)


def run():
    catalog = profiles.load()
    assets = Assets(archives='stock')
    inputs = {}
    result = {'scope': __doc__.strip(), 'fixtures': [], 'inn_scenes': [], 'excluded_inputs': {}}
    try:
        for name, ref in REFS.items():
            blob = assets.read(name)
            inputs[name] = hashlib.sha256(blob).hexdigest()
            assert inputs[name] == ref['asset_sha256'], name
            assert not extract_m2(blob, collections.Counter()), name
            expected = reference(ref)
            close(profiles.fallback(catalog, name, blob, []), expected)
            # This stand validates the authored hash pins against independent
            # fixture references, rather than automatic source-emission discovery.
            generated = profile(assets, name, blob, catalog, reviewed_only=True)
            assert len(generated['emitters']) == 1
            patched = append_lights(blob, generated['emitters'])
            verify_preserved(blob, patched, 1)
            decoded = extract_m2(patched, collections.Counter())
            close([(*decoded[0][0], *decoded[0][1], decoded[0][2], decoded[0][3])],
                  [(*expected[0][0], *expected[0][1], expected[0][2], expected[0][3])], 2e-6)
            # Both previously generated six/five/four-light assets and newly
            # generated one-light assets resolve to one authored cache source.
            old_patched = append_lights(blob, catalog[name]['emitters'])
            assert hashlib.sha256(old_patched).hexdigest() == catalog[name]['injected_sha256']
            close(profiles.fallback(catalog, name, old_patched,
                                    extract_m2(old_patched, collections.Counter())), expected)
            updated = copy.deepcopy(catalog)
            after = hashlib.sha256(patched).hexdigest()
            updated[name]['injected_sha256'] = after
            updated[name]['variants'][after] = generated
            close(profiles.fallback(updated, name, patched, decoded), expected)
            native = [((0., 0., 0.), (1., .5, .25), 1., 8., 0, 2, 2)]
            assert profiles.fallback(catalog, name, b'unreviewed native asset', native) is native
            cells = []
            for scale in (.5, 1., 2.):
                instance = (bytes.fromhex(ref['asset_sha256']), 7, 3,
                            *([0.] * 6), scale, 0., 0., 0., scale, 0., 0., 0., scale, 0., 0., 0.)
                actual = instance_lights(profiles.fallback(catalog, name, blob, []), instance, name)
                identity = hashlib.sha256(instance[0] + struct.pack('<QII', 7, 3, 0x80000000)).digest()
                golden = [(*[v * scale for v in ref['position']],
                           *[c * ref['intensity'] for c in ref['rgb']],
                           ref['start'] * scale, ref['end'] * scale,
                           int.from_bytes(identity[:8], 'little'), 3, 2)]
                close(actual, golden)
                packed = [RECORD.unpack(RECORD.pack(*r)) for r in actual]
                previous = instance_lights(old_authored(catalog[name]), instance, name)
                old_energy = new_energy = 0.
                maximum_error = 0.
                center = tuple(v * scale for v in ref['position'])
                for point, normal in receivers(center, 3 * scale):
                    wanted = shade(golden, point, normal)
                    observed = shade(packed, point, normal)
                    error = max(abs(x - y) for x, y in zip(wanted, observed))
                    maximum_error = max(maximum_error, error)
                    assert error <= 2e-6, (name, scale, point, wanted, observed)
                    old_energy += max(shade(previous, point, normal))
                    new_energy += max(observed)
                assert new_energy > 0
                cells.append(dict(scale=scale, receivers=125, previous_energy=old_energy,
                                  current_energy=new_energy, max_reference_error=maximum_error))
            result['fixtures'].append(dict(asset=name, archive=assets.origin(name),
                                           source_sha256=inputs[name], original_count=ref['original_count'],
                                           current_count=1, generated_sha256=after,
                                           original_bytes_preserved=True, cells=cells))

        # Actual WMO-local fixtures, placements and native MOLT records. Every
        # available doodad set is a separate static scene, not a guessed active set.
        local_cache = {}
        for wmo, sha in WMOS.items():
            blob = assets.read(wmo)
            inputs[wmo] = hashlib.sha256(blob).hexdigest()
            assert inputs[wmo] == sha
            native = extract_wmo(blob, collections.Counter())
            assert len(native) == 10
            assert profiles.fallback(catalog, wmo, blob, native) is native
            metadata = wmo_doodads(blob, group_refs(group_blobs(assets.read, wmo, blob)))
            for set_index in range(max(1, len(metadata[1]))):
                old, current, expected, owners = [], [], [], {}
                centers = []
                for name, instance in building(wmo, 42, set_index,
                                               ((1., 0., 0.), (0., 1., 0.), (0., 0., 1.)),
                                               (0., 0., 0.), lambda _: metadata):
                    if name not in local_cache:
                        source = assets.read(name)
                        inputs[name] = hashlib.sha256(source).hexdigest()
                        try:
                            local = extract_wmo(source, collections.Counter()) if name.endswith('.wmo') else extract_m2(source, collections.Counter())
                            now = profiles.fallback(catalog, name, source, local)
                            previous = old_authored(profiles.pinned(catalog[name])[inputs[name]]) if name in catalog and (not local or catalog[name].get('injected_sha256') == inputs[name]) else local
                            wanted = reference(REFS[name]) if name in REFS else previous
                        except (ValueError, KeyError) as exc:
                            # Same unsupported-input outcome as the cache builder;
                            # the four fixtures under examination must never use it.
                            assert name not in REFS, (name, str(exc))
                            result['excluded_inputs'][name] = str(exc)
                            previous, now, wanted = [], [], []
                        local_cache[name] = previous, now, wanted
                    previous, now, wanted = local_cache[name]
                    for target, local in ((old, previous), (current, now), (expected, wanted)):
                        target.extend(instance_lights(local, instance, name))
                    for r in instance_lights(now, instance, name):
                        owners[r[8]] = (instance[2], instance[1])
                        if name in REFS:
                            centers.append(r[:3])
                close(current, expected)
                assert len({r[8] for r in current}) == len(current)
                # Keep the existing parent-owned duplicate policy in the path.
                current, removed = profiles.deduplicate_parent_lights(current, owners)
                expected, expected_removed = profiles.deduplicate_parent_lights(expected, owners)
                assert current == expected and removed == expected_removed
                old_native = [r for r in old if r[9] != 3]
                assert old_native == [r for r in current if r[9] != 3]
                errors = []
                previous_energy = current_energy = native_energy = 0.
                for center in centers:
                    for point, normal in receivers(center, 3.):
                        errors.append(max(abs(x - y) for x, y in
                                          zip(shade(current, point, normal), shade(expected, point, normal))))
                        previous_energy += max(shade(old, point, normal))
                        current_energy += max(shade(current, point, normal))
                        native_energy += max(shade(old_native, point, normal))
                assert not errors or max(errors) == 0
                result['inn_scenes'].append(dict(asset=wmo, doodad_set=set_index,
                                                previous_sources=len(old), current_sources=len(current),
                                                native_sources=len(old_native), compact_fixtures=len(centers),
                                                parent_duplicates_removed=removed, receiver_samples=len(errors),
                                                reference_max_error=max(errors, default=0.),
                                                previous_energy=previous_energy, current_energy=current_energy,
                                                unchanged_native_energy=native_energy))
        assert any(r['compact_fixtures'] > 1 for r in result['inn_scenes'])
        result['input_sha256'] = inputs
        result['pass'] = True
        target = fp.output_dir() / 'emitter-fixture-scene.json'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'fixtures': len(result['fixtures']), 'inn_scenes': len(result['inn_scenes']),
                          'receivers': sum(c['receivers'] for r in result['fixtures'] for c in r['cells']) +
                                       sum(r['receiver_samples'] for r in result['inn_scenes']),
                          'report': str(target), 'pass': True}))
    finally:
        assets.close()


if __name__ == '__main__':
    run()
