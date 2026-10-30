#!/usr/bin/env python3
"""Regenerate the authored issue-106 depth/ownership/ray-truth fixtures offline.

No client assets, game, graphics library or optional Python packages are used.
The native-resolution far view keeps its contact ROI above 100 visible pixels.
"""
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile
import base64
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from ao_actor_reference import export_fixture, scenes, ray, trace, reference

BASE = ('close', 'cube', 'down', 'far-native', 'mid', 'sky', 'top', 'up', 'wall')
RADII = (('close', .5), ('close', 1.), ('mid', 2.5), ('far-native', 2.25), ('far-native', 2.5),
         ('far-native', 2.75), ('far-native', 3.), ('far-native', 4.), ('far-native', 10.), ('up', 4.), ('top', 10.))


def radius_name(base, radius):
    return base + '-r' + ('%g' % radius).replace('.', '_')


def derive_radii(folder):
    for base, radius in RADII:
        fixture = json.loads((folder / (base + '.json')).read_text())
        fixture.update(radius=radius, expect_halo_fix=False)
        fixture['expect_baseline_methods'] = ['HBAO+', 'GTAO'] if radius <= .5 else ['HBAO+'] if radius >= 3 else []
        scene = scenes()[fixture['scene']]
        w, h = fixture['size']
        aw, ah = fixture['ao_size']
        for region in fixture['rois'].values():
            for sample in region['samples']:
                x = max(1, min(w - 2, math.floor((sample['x'] + .5) / aw * w + .25)))
                y = max(1, min(h - 2, math.floor((sample['y'] + .5) / ah * h + .25)))
                hit = trace(scene, scene.eye, ray(scene, x, y, w, h))
                truth = reference(scene, hit.point, hit.normal, radius, fixture['rays']) if hit else {'hard': 0., 'quadratic': 0.}
                sample.update(truth)
        (folder / (radius_name(base, radius) + '.json')).write_text(json.dumps(fixture) + '\n', encoding='utf-8')


def pack(folder, output):
    names = BASE + tuple(radius_name(base, radius) for base, radius in RADII)
    manifest = {'type': 'manifest', 'schema': 'ao_actor_fixture_pack_v2', 'generator_sha256': hashlib.sha256(
        (ROOT / 'tests/ao_actor_reference.py').read_bytes()).hexdigest(), 'fixtures': [name + '.json' for name in names]}
    records, written = [manifest], set()
    for name in names:
        fixture = json.loads((folder / (name + '.json')).read_text())
        records.append({'type': 'fixture', 'name': name + '.json', 'data': fixture})
        for kind in ('depth', 'actor'):
            filename = fixture[kind + '_file']
            if filename in written:
                continue
            written.add(filename)
            data = (folder / filename).read_bytes()
            assert hashlib.sha256(data).hexdigest() == fixture[kind + '_sha256']
            records.append({'type': 'texture', 'name': filename,
                            'zlib_base64': base64.b64encode(zlib.compress(data, 9)).decode('ascii')})
    output.write_text(''.join(json.dumps(record, separators=(',', ':')) + '\n' for record in records),
                      encoding='utf-8', newline='\n')


def main():
    output = ROOT / 'tests/fixtures/ao_actor_106.jsonl'
    with tempfile.TemporaryDirectory(prefix='ao-actor-fixtures-') as temp:
        folder = Path(temp)
        for name in ('close', 'cube', 'down', 'mid', 'sky', 'top', 'up', 'wall', 'far-native'):
            width, height = (1728, 1117) if name == 'far-native' else (640, 400)
            export_fixture(scenes()['far' if name == 'far-native' else name], width, height, 2.,
                           folder / (name + '.json'), rays=4096, samples=64)
            print('Generated', name, flush=True)
        derive_radii(folder)
        pack(folder, output)
    print(output, hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
