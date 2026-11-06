#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""AO visual lab: actual kernels, FP16 filter and legacy/world composites.

python tests/test_ao_pipeline_gpu.py --output out/ao-lab
Uses NumPy/Pillow for authored depth fixtures, metrics and lossless contact sheets.
No game process, installation or settings changes. Images use visibility [0,1]
without contrast stretching. --observe records metrics before fixing a regression.
"""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

METHODS = ('AO', 'HDAO', 'HBAOPlus', 'GTAO', 'ASSAO')
QUALITIES = ('High', 'Medium', 'Low')
STAGES = ('raw', 'blur', 'legacy', 'world')


def fixtures(output, sizes):
    result = []
    for w, h in sizes:
        y, x = np.mgrid[:h, :w]
        rx, ry = (2*(x+.5)/w-1)/1.3, (1-2*(y+.5)/h)/1.9
        for shape in ('flat', 'slope', 'corner', 'corner-horizontal', 'corner-diagonal', 'corner-motion', 'grazing', 'cylinder', 'grooves', 'silhouette', 'sky', 'water'):
            z = np.full((h, w), 4.)
            if shape == 'slope':
                z = 4/(1+.4*rx+.25*ry)
            if shape.startswith('corner'):
                axis = ry if shape.endswith('horizontal') else .6*rx+.8*ry if shape.endswith('diagonal') else rx
                z = (4+.003 if shape.endswith('motion') else 4)/(1+1.4*np.abs(axis))
            if shape == 'grazing':
                z = np.minimum(30, 1.6/np.maximum(.15-ry, .001))
            if shape in ('cylinder', 'grooves'):
                z.fill(7)
                radius = np.full((h, w), .65)
                for _ in range(8):
                    disc = 4**2-(1+rx**2)*(4**2-radius**2)
                    z = np.where(disc>=0, (4-np.sqrt(np.maximum(disc, 0)))/(1+rx**2), 7)
                    if shape == 'grooves':
                        radius = .65+.035*np.sin(np.arctan2(z*rx, 4-z)*16)
            if shape == 'silhouette':
                z = np.where((np.abs(rx)<.11)&(np.abs(ry)<.3), 1., z)
            nd = 1000/(1000-.2)*(1-.2/z)
            depth = (np.round(nd*.94*16777215)/16777215).astype('<f4')
            if shape == 'sky':
                depth.fill(1)
            name = f'{shape}-{w}x{h}'
            prefix = output / name
            depth.tofile(str(prefix)+'.depth.f32')
            result.append(dict(name=name, shape=shape, width=w, height=h, prefix=prefix))
    return result


def metrics(a):
    # Camera-space test surfaces are vertically invariant except grazing and
    # the silhouette: vertical variation in their interior is sampling noise.
    inner = a[8:-8, 8:-8]
    residual = inner-inner.mean(axis=0, keepdims=True)
    # Remove the smooth perspective/radius response before measuring the
    # pattern. These nine samples do not average across a geometric crease.
    smooth = sum(a[8+i:a.shape[0]-8+i, 8:-8] for i in range(-4, 5))/9
    return dict(min=float(inner.min()), mean=float(inner.mean()),
                pattern_rms=float(np.sqrt(np.mean((inner-smooth)**2))),
                vertical_noise=float(np.sqrt(np.mean(residual**2))),
                checker=float(abs(np.mean(residual[::2, ::2])-np.mean(residual[1::2, 1::2]))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(os.environ.get('NORTHLIGHT_TEST_OUTPUT_DIR', ROOT/'out/ao-lab')))
    parser.add_argument('--compiled', type=Path, default=fp.COMPILED)
    parser.add_argument('--backend', type=Path)
    parser.add_argument('--observe', action='store_true')
    parser.add_argument('--size', action='append', help='WIDTHxHEIGHT; default: 640x360 and 641x361')
    parser.add_argument('--compare', type=Path, help='Previous lab output: write before/after crops and metric changes')
    args = parser.parse_args()
    from windows_gpu_guard import processes
    processes()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    backend = args.backend or Path(os.environ.get('NORTHLIGHT_TEST_D3D9', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
    backend = backend.resolve()
    assert backend.is_file() and backend != fp.client_root()/'d3d9.dll'
    exe = out/'ao_pipeline_gpu.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    '-Wno-return-type', *fp.include_flags(), str(ROOT/'tests/ao_pipeline_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    sizes = [tuple(map(int, s.lower().split('x'))) for s in (args.size or ('640x360', '641x361'))]
    assert all(len(s)==2 and min(s)>=64 for s in sizes)
    scenes = fixtures(out, sizes)
    cases = out/'cases.txt'
    cases.write_text('\n'.join(json.dumps(str(s['prefix']))+f" {s['width']} {s['height']} 2 .6 {int(s['shape']=='water')}" for s in scenes)+'\n')
    with (out/'gpu.log').open('w') as log:
        subprocess.run([str(exe), str(args.compiled.resolve()), str(backend), str(cases)], cwd=out, stdout=log, stderr=log, check=True, timeout=180)
    report = dict(backend=str(backend), backend_sha256=hashlib.sha256(backend.read_bytes()).hexdigest(), game_launched=False, cases=[])
    report['bytecode_sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.compiled.glob('*.bin') if 'AO' in p.name or p.stem in ('Composite', 'WorldComposite')}
    errors = []
    gallery = []
    for s in scenes:
        w, h = s['width'], s['height']
        for quality in QUALITIES:
            sheet = Image.new('RGB', (w*5, (h+24)*4), '#15202b'); draw = ImageDraw.Draw(sheet)
            for m, method in enumerate(METHODS):
                for t, stage in enumerate(STAGES):
                    sw, sh = (w//2, h//2) if t<2 else (w, h)
                    a = np.fromfile(str(s['prefix'])+f'.{method}{quality}.{stage}.f32', dtype='<f4').reshape(sh, sw)
                    assert np.isfinite(a).all() and a.min()>=0 and a.max()<=1.001
                    stats = metrics(a.T if s['shape']=='corner-horizontal' else a)
                    report['cases'].append(dict(scene=s['name'], method=method, quality=quality, stage=stage, **stats))
                    if s['shape'] in ('flat', 'slope', 'sky', 'water') and stats['min'] < .98:
                        errors.append(f"{s['name']} {method}/{quality}/{stage}: open surface darkened {stats['min']:.4f}")
                    if s['shape'].startswith('corner') and stats['min']>.99:
                        errors.append(f"{method}/{quality}/{stage}: concave contact lost")
                    if s['shape'] in ('corner', 'corner-horizontal') and stage=='blur':
                        # Absolute visibility error limits, independent of the
                        # candidate implementation. Old rotation fails these.
                        # Budgets in 8-bit visibility levels: Low permits more
                        # noise, but must not retain the old multi-level lattice.
                        levels = (.5, 1, 1.25) if method=='ASSAO' else (.5, .5, .5) if method=='AO' else (.25, .3, .4)
                        limit = levels[QUALITIES.index(quality)]/255
                        if stats['pattern_rms']>limit:
                            errors.append(f"{s['name']} {method}/{quality}: pattern RMS {stats['pattern_rms']:.6f} > {limit}")
                    im = Image.fromarray(np.uint8(np.clip(a,0,1)*255+.5)).resize((w,h), Image.Resampling.NEAREST)
                    sheet.paste(im, (m*w, t*(h+24)+24));draw.text((m*w+6,t*(h+24)+5), f"{'SSAO' if method=='AO' else method} {quality} | {stage}", fill='white')
            filename = f"{s['name']}-{quality}.png"
            sheet.save(out/filename)
            gallery.append(f'<details><summary>{html.escape(s["name"])} / {quality}</summary><a href="{filename}">Open full size</a><br><img loading="lazy" src="{filename}" style="max-width:100%"></details>')
    if args.compare:
        previous = json.loads((args.compare/'report.json').read_text())
        old = {(c['scene'],c['method'],c['quality'],c['stage']): c for c in previous['cases']}
        report['comparison'] = [dict(scene=c['scene'], method=c['method'], quality=c['quality'], stage=c['stage'],
            before=old[(c['scene'],c['method'],c['quality'],c['stage'])].get('pattern_rms'), after=c['pattern_rms'])
            for c in report['cases'] if (c['scene'],c['method'],c['quality'],c['stage']) in old]
        for s in scenes:
            if s['shape']!='corner': continue
            w,h=s['width'],s['height']; crop=min(w//4,160)
            comparison=Image.new('RGB',(crop*2*5,(h+24)*3),'#15202b'); draw=ImageDraw.Draw(comparison)
            for m,method in enumerate(METHODS):
                for q,quality in enumerate(QUALITIES):
                    for version,folder in enumerate((args.compare,out)):
                        a=np.fromfile(folder/(s['name']+f'.{method}{quality}.world.f32'),dtype='<f4').reshape(h,w)
                        im=Image.fromarray(np.uint8(np.clip(a,0,1)*255+.5)).crop((w//2-crop//2,0,w//2+crop//2,h))
                        x=(m*2+version)*crop; y=q*(h+24)
                        comparison.paste(im,(x,y+24));draw.text((x+3,y+5),f"{method} {quality} {'before' if version==0 else 'after'}",fill='white')
            filename=s['name']+'-comparison.png';comparison.save(out/filename)
            gallery.insert(0,f'<p><a href="{filename}">Before / after, all methods and qualities</a><br><img src="{filename}" style="max-width:100%"></p>')
    report['errors'] = errors
    (out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>Northlight AO lab</title><style>body{font:16px system-ui;background:#15202b;color:#edf2f7;margin:24px}a{color:#8cc8ff}summary{cursor:pointer;padding:12px}details{border-bottom:1px solid #455565}</style><h1>Northlight AO lab</h1><p>Five methods × three qualities. Rows: raw AO, FP16 filter, legacy composite, world composite. White = unoccluded. No contrast stretching. Synthetic D24 scenes; not game screenshots or FPS measurements.</p><p><a href="report.json">Metrics and bytecode hashes</a></p>'+''.join(gallery),encoding='utf-8')
    print(f"AO lab: {len(report['cases'])} stage images, {len(errors)} failures; {out}")
    for error in errors[:20]: print(error)
    if not args.observe:
        assert not errors, '\n'.join(errors)


if __name__ == '__main__':
    main()
