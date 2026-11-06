#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib,world-cache
"""Captured Stormwind lamp housing masks a nearby actor; moving it exposes its shadow.

This captured-client fixture requires the recorded installed FGS model hash.
A different model needs a fresh physical audit, not a blind expected-hash update.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from test_m2_lamp_self_shadow_gpu import character_fixture, compile_gpu
from validate_native_scene_trace import processes

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir()/'captured-lamp-actor')
parser.add_argument('--backend', type=Path, action='append')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
case = out/'captured-lamp'
(case/'models').mkdir(parents=True, exist_ok=True)
cache = fp.world_cache()
source_id = 14933062424609180460
owner = None
for tile in sorted((cache/'Azeroth').glob('*.fg3')):
    data = tile.read_bytes()
    if data[:8] != struct.pack('<4sI', b'FGS3', 3):
        continue
    for row in struct.iter_unpack('<32sQI18f', data[12:]):
        if row[2] in (1, 3) and int.from_bytes(hashlib.sha256(row[0]+struct.pack('<QII', row[1], row[2], 0)).digest()[:8], 'little') == source_id:
            owner = row
            break
    if owner:
        break
assert owner is not None, 'The captured Stormwind placement must exist in the installed read-only cache'
model = cache/'models'/(owner[0].hex()+'.fgs')
expected = '877d120a6916b13d8640aa81012023b1df3bcae0c77b778e29e8977bb78ad95e'
assert hashlib.sha256(model.read_bytes()).hexdigest() == expected, 'Captured fixture geometry changed; inspect its physical controls before updating this fixture'
shutil.copy2(model, case/'models'/model.name)
(case/'tile.fg3').write_bytes(struct.pack('<4sII', b'FGS3', 3, 1)+struct.pack('<32sQI18f', *owner))
fp.use_source_modules()
from world_lights_builder import RECORD
source = (-8564.9423828125, 1011.7713012695312, 99.7315444946289, 1., .55, .2, 1., 12., source_id, 2, 2)
(case/'sources.fgl').write_bytes(struct.pack('<4sIII', b'FGL1', 2, RECORD.size, 1)+RECORD.pack(*source))
(case/'actor-foot.f32').write_bytes(struct.pack('<3f', -8564., 1011.55, 95.73))
character = character_fixture(out)
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
inputs = {str(path): digest(path) for path in [*fp.SRC.rglob('*.h'), *fp.SRC.rglob('*.inl'), *fp.SRC.rglob('*.cpp'),
          fp.TESTS/'m2_lamp_self_shadow_gpu.cpp', fp.TESTS/'captured_lamp_actor.inc',
          fp.TESTS/'test_m2_lamp_self_shadow_gpu.py', Path(__file__),
          out/'character.bin', out/'character-vs.bin', model, tile, *case.rglob('*')]
          if path.is_file()}
exe = compile_gpu(out)
backends = args.backend or [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
                            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
                            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
inputs.update({str(path): digest(path) for path in backends})
env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(out), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
settings = out/'settings'
settings.mkdir(exist_ok=True)
(settings/'northlight-renderer.ini').write_text('[Renderer]\nSettingsOverlay=0\n')
inputs.update({str(path): digest(path) for path in [exe, settings/'northlight-renderer.ini']})
report = {'pass': False, 'source_id': str(source_id), 'source': source[:3], 'model': str(model),
          'model_sha256': expected, 'character': character, 'game_modified': False,
          'historical_actor_admission_proven': False, 'controlled_pose': True,
          'receiver': 'horizontal plane at captured curb Z95.73; independent top-down view', 'inputs': inputs, 'cases': []}

def load(path):
    data = path.read_bytes()
    magic, w, h, fmt, stride = struct.unpack('<5I', data[:20])
    assert magic == 0x31524746 and fmt == 116 and stride == 16
    return np.frombuffer(data, '<f4', offset=20).reshape(h, w, 4)

for index, backend in enumerate(backends):
    for soft in (False, True):
        folder = out/f'backend-{index}-soft-{int(soft)}'
        folder.mkdir(exist_ok=True)
        for name in ('character.bin', 'character-vs.bin'):
            shutil.copy2(out/name, folder/name)
        processes(False)
        run = subprocess.run(list(map(str, [exe, backend, str(settings)+os.sep, case,
                                            'housing-actor-soft' if soft else 'housing-actor', folder])),
                             cwd=out, env=env, text=True, capture_output=True, timeout=120)
        (folder/'gpu.log').write_text(run.stdout+run.stderr)
        print(index, soft, run.returncode, (run.stdout+run.stderr)[-1800:], flush=True)
        assert run.returncode == 0 and 'CAPTURED_HOUSING_ACTOR_PASS' in run.stdout
        names = ['housing-only', 'actor-only', 'housing-actor', 'housing-shifted-actor']
        energy = [-load(folder/(name+'.fgr'))[..., :3].sum(axis=2) for name in names]
        panels = []
        for title, shadow in zip(['Housing only', 'Actor only', 'Housing + nearby actor', 'Additional shadow after moving actor'],
                                 [energy[0], energy[1], energy[2], np.maximum(energy[3]-energy[0], 0)]):
            mask = shadow > 1e-5
            image = Image.fromarray(np.uint8(np.where(mask[..., None], [45, 50, 60], [210, 190, 135])))
            canvas = Image.new('RGB', (512, 540), 'white')
            canvas.paste(image, (0, 28))
            ImageDraw.Draw(canvas).text((8, 8), title, fill='black')
            panels.append(canvas)
        proof = Image.new('RGB', (2048, 540))
        for i, panel in enumerate(panels):
            proof.paste(panel, (i*512, 0))
        proof.save(folder/'proof.png')
        report['cases'].append({'backend': str(backend), 'soft': soft, 'pass': True,
                                'proof': str(folder/'proof.png'), 'proof_sha256': hashlib.sha256((folder/'proof.png').read_bytes()).hexdigest()})
# The same fixture must fail if actor replay is intentionally omitted.
folder = out/'no-actor-control'
folder.mkdir(exist_ok=True)
for name in ('character.bin', 'character-vs.bin'):
    shutil.copy2(out/name, folder/name)
run = subprocess.run(list(map(str, [exe, backends[0], str(settings)+os.sep, case, 'housing-actor-no-replay', folder])),
                     cwd=out, env=env, text=True, capture_output=True, timeout=120)
(folder/'gpu.log').write_text(run.stdout+run.stderr)
assert run.returncode != 0 and 'actorPixels>100' in run.stderr, run.stdout+run.stderr
report['actor_omission_control_failed'] = True
report['inputs_unchanged'] = all(digest(Path(path)) == sha for path, sha in inputs.items())
report['pass'] = report['inputs_unchanged']
(out/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
assert report['pass'], 'Source or immutable fixture inputs changed during validation; rerun after source freezes'
print('CAPTURED_LAMP_ACTOR_GPU_PASS', out/'validation.json')
