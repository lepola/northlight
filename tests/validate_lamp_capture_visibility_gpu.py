#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client manual
"""Verify a recorded lamp shadow survives material composition and HDR presentation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from render_lamp_self_shadow import capture_plan, display_rgb, header, load, save, replay_contract
from PIL import Image


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument('--capture', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--receiver', type=int, nargs=4, required=True, metavar=('X0', 'Y0', 'X1', 'Y1'),
                   help='An identified ground receiver, excluding overlays and stencil-protected names')
    p.add_argument('--reuse-composite', type=Path)
    a = p.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    prefix = str(a.capture.resolve() / 'capture-1-')
    records = [json.loads(line) for line in Path(prefix+'lighting-inputs.jsonl').read_text().splitlines()]
    plan = capture_plan(records)
    assert plan['hdr'], 'This regression follows the HDR final pipeline'
    contract = replay_contract(records, prefix, plan)
    from validate_native_scene_trace import processes
    processes(False)
    backend = fp.client_root() / 'renderer-backends/dxvk/dxvk_d3d9.dll'
    env = {**fp.zig_env(), 'DXVK_LOG_PATH': str(out), 'DISABLE_VULKAN_OBS_CAPTURE': '1'}
    inputs = sorted(a.capture.glob('capture-1-*'))
    fingerprints = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs if path.is_file()}

    def compile(source, name):
        exe = out / (name+'.exe')
        command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                   *fp.test_include_flags(), source, '-luser32', '-o', exe]
        with (out/(name+'-compile.log')).open('w') as log:
            subprocess.run(list(map(str, command)), env=env, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        return exe

    def run(command, name):
        result = subprocess.run(list(map(str, command)), env=env, cwd=out, capture_output=True, text=True, timeout=180)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        assert result.returncode == 0, (name, result.stdout, result.stderr)
        return result.stdout

    exe = a.reuse_composite.resolve() if a.reuse_composite else compile(fp.TESTS/'lamp_capture_composite_gpu.cpp', 'composite')
    shutil.copyfile(prefix+'lighting-after-local.fgr', out/'current-local-fixed.fgr')
    args = [exe, backend, prefix, out, 'world', plan['material_pass'], plan['material_shader'], plan['material_group'], 1,
            int(contract['hdr_base_srgb'])]
    replay_log = run(args, 'world')
    before, after = (load(out/('world-'+name+'.fgr')) for name in ('before', 'after'))
    native = Path(prefix+f"lighting-input-g{plan['material_group']}-s0.fgr")
    captured = load(native)
    x0, y0, x1, y1 = a.receiver
    assert 0 <= x0 < x1 <= before.shape[1] and 0 <= y0 < y1 <= before.shape[0]
    roi = np.s_[y0:y1, x0:x1, :3]
    error = float(np.abs(before[roi]-captured[roi]).mean())
    assert error < .0005, ('Receiver replay differs from the captured world result', error)
    delta = after[roi]-before[roi]
    assert int((delta.max(axis=2) > 1/255).sum()) >= 256, 'Captured point shadow was lost in WorldComposite'
    save(out/'material-input-fixed.fgr', captured+after-before, header(native)[2])
    args[4] = 'material'; run(args, 'material')
    material_before, material_after = (load(out/('material-'+name+'.fgr')) for name in ('before', 'after'))
    final = load(Path(prefix+'lighting-final-scene.fgr'))
    material_error = float(np.abs(material_before[roi]-final[roi]).mean())
    assert material_error < .0005, ('Receiver material replay differs from the final capture', material_error)
    unshadowed = np.maximum(final+material_after-material_before, 0)
    save(out/'final-shadowed.fgr', final); save(out/'final-unshadowed.fgr', unshadowed)
    final_delta = unshadowed[roi]-final[roi]
    assert int((final_delta.max(axis=2) > 1/255).sum()) >= 256, 'MaterialComposite erased the lamp shadow'
    for name, pixels in (('shadowed', final), ('unshadowed', unshadowed)):
        Image.fromarray(display_rgb(pixels, True)).crop((x0, y0, x1, y1)).resize(((x1-x0)*2, (y1-y0)*2)).save(out/(name+'.png'))
    Image.fromarray(np.rint(np.clip(final_delta*8, 0, 1)*255).astype('u1')).resize(((x1-x0)*2, (y1-y0)*2)).save(out/'shadow-difference-times-eight.png')
    presenter = compile(fp.TESTS/'lamp_capture_presentation_gpu.cpp', 'presentation')
    run([presenter, backend, out], 'presentation')
    presentation = []
    for exposure in (-2, 0, 1):
        shadowed, clear = (load(out/f'present-{exposure}-{name}.fgr')[roi] for name in ('shadowed', 'unshadowed'))
        difference = clear-shadowed
        changed = int((difference.max(axis=2)*80 > .25).sum())
        assert changed >= 256, ('HDR presentation erased the lamp shadow', exposure, changed)
        assert np.isfinite(shadowed).all() and np.isfinite(clear).all() and shadowed.min() >= 0
        presentation.append({'exposure_ev': exposure, 'pixels_over_quarter_nit': changed,
                             'maximum_delta_nits': (difference.max(axis=(0, 1))*80).tolist()})
    assert fingerprints == {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in fingerprints}
    processes(False)
    report = {'pass': True, 'capture': str(a.capture.resolve()), 'renderer_frame': records[0]['renderer_frame'],
              'receiver': a.receiver, 'capture_sha256': fingerprints,
              'artifact_sha256': {name: hashlib.sha256((out/name).read_bytes()).hexdigest()
                                  for name in ('shadowed.png', 'unshadowed.png', 'shadow-difference-times-eight.png', 'final-shadowed.fgr', 'final-unshadowed.fgr')},
              'world_receiver_mean_absolute_error': error, 'material_receiver_mean_absolute_error': material_error,
              'visible_shadow_pixels': int((final_delta.max(axis=2)>1/255).sum()),
              'maximum_final_linear_delta': final_delta.max(axis=(0, 1)).tolist(), 'presentation': presentation,
              'bindings': replay_log.strip(), 'game_launched': False,
              'replay_contract': contract,
              'limitations': [('Historical final-mask omissions require an identified unmasked receiver; its replay is checked against the real captured image.'
                               if 'historical-omission' in contract['masks'].values() else
                               'Mask exports match their journal bindings; only the identified receiver is asserted.'),
                              'Actual automatic exposure history and display peak were not exported; production HDR presentation is bounded at -2/0/+1 EV, paper white 203 nits, peak 1000 nits, contrast 1, bloom 1.',
                              'Captured resolved MSAA color/depth inputs; native geometry, stencil and individual MSAA samples are not replayed.']}
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    print('LAMP_CAPTURE_VISIBILITY_PASS', json.dumps({k: report[k] for k in ('visible_shadow_pixels', 'presentation')}))


if __name__ == '__main__':
    main()
