"""Export the GPU replay's unmodified pixel buffers and a local A/B viewer."""
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image


def load(path):
    raw = path.read_bytes()
    magic, w, h, fmt, stride = struct.unpack('<5I', raw[:20])
    assert magic == 0x31524746 and len(raw) == 20 + w*h*stride
    data = np.frombuffer(raw, dtype={113: '<f2', 21: 'u1', 22: 'u1'}[fmt], offset=20)
    data = data.reshape(h, w, 4).astype('float32')
    assert np.isfinite(data).all()
    return data[:, :, [2, 1, 0, 3]]/255 if fmt in (21, 22) else data


def export(capture, baseline, shaders, output):
    files = {'old': output/'old.fgr', 'new': output/'new.fgr',
             'captured': Path(str(capture)+'lighting-final-scene.fgr')}
    buffers = {name: load(path)[:, :, :3] for name, path in files.items()}
    for name, data in buffers.items():
        Image.fromarray(np.rint(np.clip(data, 0, 1)*255).astype('uint8')).save(output/(name+'.png'))
    error = np.abs(buffers['old']-buffers['captured'])*255
    points = {'distant_wall': (1375, 225), 'farther_wall': (1187, 225),
              'fade_wall': (1563, 275), 'nearer_wall': (1751, 225), 'floor': (1751, 375)}
    samples = {name: {'pixel': [x, y], **{mode: np.rint(a[y, x]*255).astype(int).tolist()
               for mode, a in buffers.items()}} for name, (x, y) in points.items()}
    for name in ('distant_wall', 'farther_wall'):
        assert samples[name]['old'] == samples[name]['captured']
        assert all(a < b for a, b in zip(samples[name]['new'], samples[name]['old']))
    sources = [*files.values(), *capture.parent.glob(capture.name+'*'),
               baseline/'WorldGI.bin', *[shaders/(entry+'.bin') for entry in
               ('WorldGI', 'WorldLighting', 'WorldLightingSoft', 'RemovalSmooth', 'WorldComposite', 'MaterialComposite')],
               *output.glob('probe-*.fgr'), Path(__file__), Path(__file__).with_name('gi_capture_replay.cpp')]
    report = dict(capture=str(capture), backend_log='replay.log', samples=samples,
                  old_vs_capture_8bit_error=dict(mean=float(error.mean()),
                     percentiles=dict(zip(('50', '90', '99', '100'), np.percentile(error, [50, 90, 99, 100]).tolist()))),
                  reconstruction=dict(static_probes=True, dynamic_actors=False, retained_probe_history=False,
                      stencil_exported=False, original_temporal_plus_filtered_delta=True, temporal_convergence=False,
                      color_format_matches_capture=True, dither_neutral=True),
                  sha256={str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources if path.is_file()})
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    html = Path(__file__).with_name('gi_capture_viewer.html').read_text(encoding='utf-8')
    (output/'index.html').write_text(html.replace('ERROR_MEAN', f'{error.mean():.2f}').replace(
        'ERROR_P99', f'{np.percentile(error, 99):.2f}').replace('ERROR_MAX', f'{error.max():.2f}'), encoding='utf-8')
    print('CAPTURE_REPORT_PASS', output/'index.html')
