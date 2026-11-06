"""Reproducible actual-client city lamp inputs; no graphics device or game writes."""
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
import light_attachments
from world_scene_builder import Assets, InstancedBuilder
from world_lights_builder import extract_m2, m2_sources, instance_lights, RECORD, VERSION
from world_emitter_index import Index
from world_fixture_emission import native_fixture_offsets

MODELS = {
    'stormwind-street': r'world\generic\human\passive doodads\lamps\stormwindstreetlamp01.m2',
    'dwarf-hanging': r'world\generic\dwarf\passive doodads\lanterns\dwarvenhanginglanternsmall01.m2',
    'draenei-post': r'world\expansion01\doodads\generic\draenei\lampposts\dr_lamppost_01.m2',
    'orc-street': r'world\generic\orc\passive doodads\lampposts\orcbrazierstreetlamp.m2',
    'orc-hanging': r'world\generic\orc\passive doodads\lanterns\orchanginglantern.m2',
    'dalaran-post': r'world\expansion02\doodads\dalaran\dalaran_lamppost01.m2',
    'dalaran-wall': r'world\expansion02\doodads\dalaran\dalaran_lampwall01.m2',
}


def write_lights(path, rows):
    path.write_bytes(struct.pack('<4sIII', b'FGL1', VERSION, RECORD.size, len(rows))+b''.join(RECORD.pack(*row) for row in rows))


def build(out):
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    assets = Assets()
    evidence = []
    identity = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
    translation = (-8989.83, 780.14, 101.79)
    try:
        emitters = Index(assets, lazy=True)
        for label, model in MODELS.items():
            case = out/label
            case.mkdir(exist_ok=True)
            for name in ('stand-origin.f32', 'stand-ground.f32', 'stand-basis.f32', 'stand-pose-bone.u32'):
                (case/name).unlink(missing_ok=True)
            blob = assets.read(model)
            (case/'source.m2').write_bytes(blob)
            builder = InstancedBuilder(assets, case)
            mesh = builder.m2(model)
            native_only = label.startswith('dalaran-')
            builder.add_mesh(mesh, identity, (0, 0, 0) if native_only else translation, 'm2', 1234567)
            builder.write_tile(case/'tile.fg3', builder.instances)
            sources = m2_sources(model, blob, extract_m2(blob, collections.Counter())+light_attachments.native_sources(blob), emitters)
            assert len(sources) == 1, (label, sources)
            offsets = native_fixture_offsets(assets, model, blob, sources)
            raw = instance_lights(sources, builder.instances[0], model)
            physical = [(offsets[s[4]]['position'], *s[1:]) if s[4] in offsets else s for s in sources]
            rows = instance_lights(physical, builder.instances[0], model)
            if native_only:
                # The final stand loads this mesh for upload storage, then
                # removes every cached batch and uses actual opaque replay.
                write_lights(case/'source-proofs.fgl', [])
            else:
                write_lights(case/'source-proofs.fgl', rows)
                write_lights(case/'sources.fgl', rows)
            proof = light_attachments.native_proofs(blob, physical, offsets)
            bindings = light_attachments.placed_native(proof, builder.instances[0], [(s[4], row) for s, row in zip(physical, rows)])
            light_attachments.write(case/'sources.fga', bindings)
            (case/'stand-origin.f32').write_bytes(struct.pack('<3f', *raw[0][:3]))
            if label == 'orc-street':
                ground = min(v[2] for v in mesh[0])+translation[2]
                (case/'stand-ground.f32').write_bytes(struct.pack('<f', ground))
                c = math.sqrt(.5)
                (case/'stand-basis.f32').write_bytes(struct.pack('<9f', c, c, 0, -c, c, 0, 0, 0, 1))
                (case/'stand-pose-bone.u32').write_bytes(struct.pack('<I', 14))
            elif label == 'orc-hanging':
                (case/'stand-basis.f32').write_bytes(struct.pack('<9f', 0, -1, 0, 0, 0, 1, -1, 0, 0))
            owned = sum(len(mesh[2][t[3]]) > 3 for t in mesh[1])
            evidence.append({'case': label, 'asset': model, 'archive': assets.origin(model),
                             'model_sha256': hashlib.sha256(blob).hexdigest(), 'triangles': len(mesh[1]),
                             'owned_luminous_triangles': owned, 'native_only': native_only,
                             'placement': {'matrix': identity, 'translation': (0, 0, 0) if native_only else translation},
                             'native_offset_proof': offsets,
                             'scope': 'Controlled offline stand; actual model geometry. Dalaran uses live replay with zero cached lamp casters.'})
    finally:
        assets.close()
    # This executable uses production native inventory, classification, FGA,
    # publication and selection. No shadow flag is synthesized by this exporter.
    with (out/'native-export.log').open('w') as log:
        subprocess.run([sys.executable, str(fp.TESTS/'test_native_world_lamps.py'), '--fixtures', str(out)],
                       check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    for row in evidence:
        case = out/row['case']
        row['files'] = {str(path.relative_to(case)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sorted(case.rglob('*')) if path.is_file() and path.name != 'fixture.json'}
        (case/'fixture.json').write_text(json.dumps(row, indent=2)+'\n')
    (out/'city-fixtures.json').write_text(json.dumps({'game_launched': False, 'fixtures': evidence}, indent=2)+'\n')
    return {'actor_cases': [out/label for label in MODELS],
            'live_native_cases': [out/label for label in ('dalaran-post', 'dalaran-wall')],
            'partial_floor_cases': [out/'orc-street'], 'blocked_floor_cases': [out/'orc-hanging']}
