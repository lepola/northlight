#!/usr/bin/env python3
"""Strict private world-cache staging; no executable, device, game or benchmark."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('prepare_native_scene', REPO / 'scripts/prepare_native_scene.py')
scene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scene)


def pe(path):
    # Only the preparation parser runs. These tiny architecture fixtures are
    # deliberately not runnable PE programs or an authentic game trace.
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, 0x14c)
    struct.pack_into('<H', data, 152, 0x10b)
    path.write_bytes(data)
    return path


with tempfile.TemporaryDirectory(prefix='native-static-stage-') as temporary:
    root = Path(temporary)
    cache = root / 'source-cache'
    cache.mkdir()
    payloads = {'Northrend/29_20.fg3': b'authored tile bytes', 'models/abc.fgs': b'authored model bytes',
                'lights/Northrend.fgl': b'authored light bytes'}
    for name, data in payloads.items():
        path = cache / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    manifest = {'format': 1, 'map': 'Northrend', 'static_inputs_complete': True,
                'static_outputs': {name: {'bytes': len(data), 'sha256': scene.digest(cache / name)}
                                   for name, data in payloads.items()}}
    source_manifest = root / 'scene.json'
    quality = root / 'quality.ini'
    quality.write_bytes(b'[Quality]\nAutoBumpDistance=48\n')
    args = argparse.Namespace(output=root / 'private', capture_layer='above-northlight',
        scene_manifest=source_manifest, static_cache_root=cache, trace=None, snapshot_index=None,
        tool_root=None, retracer=pe(root / 'd3dretrace.exe'), backend_dll=pe(root / 'backend.dll'),
        northlight_dll=pe(root / 'northlight.dll'), quality_ini=quality, backend_kind='dxvk')
    source_manifest.write_text(json.dumps(manifest), encoding='utf-8')
    before = {str(p): scene.digest(p) for p in root.rglob('*') if p.is_file()}
    result = scene.prepare(args)
    for relative, data in payloads.items():
        target = args.output / 'northlight/world-cache' / relative
        assert target.read_bytes() == data
        assert result['staged_sha256']['northlight/world-cache/' + relative] == scene.digest(cache / relative)
        assert result['input_sha256'][str((cache / relative).resolve())] == scene.digest(cache / relative)
    assert result['static_cache']['listed_files_staged'] == 3
    assert result['static_cache']['bytes'] == sum(map(len, payloads.values()))
    assert (args.output / 'northlight/northlight-quality.ini').read_bytes() == quality.read_bytes()
    assert not (args.output / 'native-only/world-cache').exists()
    assert not result['full_scene_replay_ready'] and not result['fps_acceptance_proven'] and not result['replay_executed']
    assert before == {p: scene.digest(p) for p in before}

    args.output = root / 'legacy'
    args.static_cache_root = None
    legacy = scene.prepare(args)
    assert legacy['static_cache'] is None and not (args.output / 'northlight/world-cache').exists()
    args.static_cache_root = cache
    cases = []
    for relative in ('../escape.fgs', '/absolute.fgs', 'models//abc.fgs', './models/abc.fgs',
                     'C:/absolute.fgs', 'models\\abc.fgs', 'models/NUL.fgs', 'models/trailing. ',
                     'models/abc.fgs:stream'):
        cases.append(('invalid-relative', {relative: next(iter(manifest['static_outputs'].values()))}))
    base = manifest['static_outputs']['models/abc.fgs']
    cases += [('wrong-size', {'models/abc.fgs': {**base, 'bytes': base['bytes'] + 1}}),
              ('bool-size', {'models/abc.fgs': {**base, 'bytes': True}}),
              ('negative-size', {'models/abc.fgs': {**base, 'bytes': -1}}),
              ('wrong-sha', {'models/abc.fgs': {**base, 'sha256': '0' * 64}}),
              ('bad-sha', {'models/abc.fgs': {**base, 'sha256': 'z' * 64}}),
              ('missing-file', {'models/missing.fgs': base}),
              ('case-alias', {'models/abc.fgs': base, 'Models/ABC.fgs': base}),
              ('ancestor-overlap', {'models/abc.fgs': base, 'models': base})]
    for number, (label, outputs) in enumerate(cases):
        args.output = root / f'refused-{number}'
        source_manifest.write_text(json.dumps({**manifest, 'static_outputs': outputs}), encoding='utf-8')
        try:
            scene.prepare(args)
        except (ValueError, FileNotFoundError):
            assert not args.output.exists(), label
        else:
            raise AssertionError(label)
    source_manifest.write_text(json.dumps(manifest), encoding='utf-8')
    args.output = cache / 'nested-output'
    try:
        scene.prepare(args)
    except ValueError:
        assert not args.output.exists()
    else:
        raise AssertionError('Output inside the read-only cache was accepted')
    args.output = root / 'ancestor-output'
    args.static_cache_root = args.output / 'cache'
    args.static_cache_root.mkdir(parents=True)
    try:
        scene.prepare(args)
    except ValueError:
        pass
    else:
        raise AssertionError('Input inside the output was accepted')
    args.static_cache_root = cache
    args.output = root / 'duplicate-json'
    raw = json.dumps(manifest)
    raw = raw.replace('"static_outputs": {', '"static_outputs": {}, "static_outputs": {', 1)
    source_manifest.write_text(raw, encoding='utf-8')
    try:
        scene.prepare(args)
    except ValueError:
        assert not args.output.exists()
    else:
        raise AssertionError('Duplicate JSON key was accepted')
    source_manifest.write_text(json.dumps(manifest), encoding='utf-8')
    alias = cache / 'models/alias.fgs'
    os.link(cache / 'models/abc.fgs', alias)
    args.output = root / 'physical-alias'
    source_manifest.write_text(json.dumps({**manifest, 'static_outputs': {'models/abc.fgs': base, 'models/alias.fgs': base}}), encoding='utf-8')
    try:
        scene.prepare(args)
    except ValueError:
        assert not args.output.exists()
    else:
        raise AssertionError('A duplicate physical input was accepted')
    alias.unlink()
    source_manifest.write_text(json.dumps(manifest), encoding='utf-8')
    # A live input changing after validation must fail both copied-byte and
    # final source guards. The original input is restored after this control.
    args.output = root / 'changed-during-copy'
    original_copy = scene.shutil.copyfile
    changed = cache / 'models/abc.fgs'

    def corrupt_before_copy(source, destination):
        if Path(source) == changed:
            changed.write_bytes(b'corrupted after validation')
        return original_copy(source, destination)

    scene.shutil.copyfile = corrupt_before_copy
    try:
        try:
            scene.prepare(args)
        except ValueError:
            assert not (args.output / 'manifest.json').exists()
        else:
            raise AssertionError('Changed source bytes were accepted')
    finally:
        scene.shutil.copyfile = original_copy
        changed.write_bytes(payloads['models/abc.fgs'])
    assert {name: (cache / name).read_bytes() for name in payloads} == payloads

print('PASS exact private static cache staging, legacy behaviour, strict path/size/SHA/duplicate/overlap and mutation controls; no execution/FPS claim.')
