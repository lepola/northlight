#!/usr/bin/env python3
"""Build a separate WotLK light-only MPQ from this client's own models.

No game launch. Exact source hashes and evidence are written alongside the MPQ.
Existing native lights, OFF/BROKEN/UNLIT models and unsupported animation are
preserved. Classic v256 particle offsets are never used for WotLK v264.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
from world_scene_builder import Assets, unpack
from outdoor_light_profiles import load, pinned, fixture_emitters
from m2_emitter_patch import append_lights, verify_lights
from mpq import Archive

from world_emitter_coverage import inspect_model, DISABLED


def profile(assets, path, blob, catalog, reviewed_only=False, measured=None):
    """Exact reviewed pins or measured current-source emission; never filename guesses."""
    if DISABLED.search(path.rsplit('\\', 1)[-1]):
        raise ValueError('Explicit disabled/broken variant')
    sha = hashlib.sha256(blob).hexdigest()
    if reviewed_only:
        value = pinned(catalog[path]).get(sha)
        if value:
            pin = catalog[path] if sha == catalog[path]['asset_sha256'] else catalog[path]['variants'][sha]
            result = {**value, 'family': catalog[path]['family'], 'position_basis': pin['position_basis']}
            if 'emitters' in result:
                result['emitters'] = fixture_emitters(result)
            return result
        raise ValueError('Source revision has no reviewed hash pin')
    measured = measured or inspect_model(assets, path, blob)
    if measured['status'] != 'addable':
        raise ValueError(measured.get('reason', measured['status']))
    emitters = measured['emitters']
    return {**emitters[0], 'emitters': emitters, 'family': 'wotlk_' + emitters[0]['emission'],
            'partial': measured.get('partial', False), 'refusals': measured.get('refusals', {})}


def build(assets, output, catalog_path, reviewed_only=False):
    output.mkdir(parents=True, exist_ok=True)
    catalog = load() if catalog_path == Path(__file__).with_name('outdoor-light-profiles.json') else load(catalog_path)
    document = json.loads(json.dumps({'version':1,'profiles':catalog}))
    original_profiles = json.loads(catalog_path.read_text())['profiles']
    payload = {}
    report = {'format': 'WotLK MD20/v264', 'models': {}, 'coverage': {}, 'skipped': {}, 'counts': {},
              'source_archives': assets.fingerprint(), 'comfy_reference': 'aloofbit/comfyatmosphere@3d8ac3b',
              'licensing': 'Original MIT implementation using Northlight readers; no GPL source or Classic game assets copied.'}
    stats = collections.Counter()
    for path in sorted(n for n in assets.providers if n.startswith('world\\') and n.endswith('.m2')):
        stats['world_models_scanned'] += 1
        if reviewed_only and path not in catalog:
            continue
        measured = None
        if not reviewed_only:
            measured = inspect_model(assets, path)
            report['coverage'][path] = measured
            stats['coverage_' + measured['status']] += 1
            if measured['status'] != 'addable':
                if measured['status'] == 'native':
                    stats['native_lights_preserved'] += 1
                elif measured['status'] in ('unsupported', 'missing'):
                    report['skipped'][path] = measured.get('reason', measured['status'])
                    stats['unsupported_candidates'] += 1
                continue
        blob = assets.read(path)
        if reviewed_only and hashlib.sha256(blob).hexdigest() not in pinned(catalog[path]):
            report['skipped'][path] = 'Source revision has no reviewed hash pin'
            stats['unsupported_candidates'] += 1
            continue
        if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
            continue
        if unpack('<I', blob, 264)[0]:
            stats['native_lights_preserved'] += 1
            continue
        try:
            p = profile(assets, path, blob, catalog, reviewed_only=reviewed_only, measured=measured)
            emitters = p.get('emitters',[p])
            patched = append_lights(blob,emitters)
            verify_lights(blob, patched, emitters)
        except (ValueError, IndexError, struct.error, FileNotFoundError) as exc:
            stats['unsupported_candidates'] += 1
            report['skipped'][path] = str(exc)
            continue
        before, after = hashlib.sha256(blob).hexdigest(), hashlib.sha256(patched).hexdigest()
        entry = document['profiles'].setdefault(path, {**p, 'asset_sha256': before})
        variant = {k: p[k] for k in ('position', 'rgb', 'intensity', 'start', 'end', 'position_basis')}
        if 'emitters' in p:variant['emitters'] = p['emitters']
        if not reviewed_only and before == entry['asset_sha256']:
            entry.update(p)
        elif not reviewed_only:
            entry.setdefault('variants', {})[before] = variant
        entry.setdefault('variants', {})[after] = variant
        entry['injected_sha256'] = after
        payload[path] = patched
        report['models'][path] = {**p, 'original_sha256': before, 'patched_sha256': after,
                                  'source_archive': assets.origin(path), 'original_bytes': len(blob), 'patched_bytes': len(patched)}
        stats['patched_models'] += 1
    if not payload:
        raise ValueError('No supported model changes')
    archive = output / 'patch-y.mpq'
    with Archive(archive, create=True, capacity=max(4096, len(payload) * 2)) as mpq:
        for i, (name, blob) in enumerate(payload.items()):
            temp = output / 'model.tmp'
            temp.write_bytes(blob)
            mpq.add(temp, name)
        temp.unlink()
    with Archive(archive) as mpq:
        for name, blob in payload.items():
            if mpq.read(name) != blob:
                raise ValueError('MPQ round-trip failed: ' + name)
    report['counts'] = dict(stats)
    report['mpq_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / 'outdoor-light-profiles.json').write_text(json.dumps(document, indent=2) + '\n')
    extension = {}
    for name, value in document['profiles'].items():
        old = original_profiles.get(name)
        if old is None:
            extension[name] = value
        elif old != value:
            extension[name] = {k: v for k, v in value.items() if k != 'variants' and old.get(k) != v}
            extension[name]['variants'] = {s: v for s, v in value['variants'].items() if old.get('variants', {}).get(s) != v}
    (output / 'wotlk-emitter-profiles.json').write_text(json.dumps({'version': 1, 'profiles': extension}, indent=2) + '\n')
    load(output / 'outdoor-light-profiles.json')
    (output / 'emitter-patch.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['counts']), flush=True)
    return report


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--client', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--reviewed-only', action='store_true', help='only exact catalog pins; no automatic discovery')
    args = ap.parse_args()
    assets = Assets(args.client, without='y')
    try:
        build(assets, args.output, Path(__file__).with_name('outdoor-light-profiles.json'), args.reviewed_only)
    finally:
        assets.close()
