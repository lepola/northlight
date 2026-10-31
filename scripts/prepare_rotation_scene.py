"""Freeze a logged camera route, MPQ terrain and referenced static scene assets.

This prepares inputs, not an FPS benchmark. Native draw/animation state cannot
be reconstructed from a renderer log or a static lighting cache.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_rotation_fixture import camera_trace


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def prepare(args):
    log = args.log.resolve()
    cache = args.cache.resolve()
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be new or empty; existing evidence is immutable')
    if output == cache or output.is_relative_to(cache) or cache.is_relative_to(output):
        raise ValueError('The installed cache and output must be separate trees')
    if not re.fullmatch(r'[A-Za-z0-9_]+', args.map) or not 0 < args.radius <= 2048:
        raise ValueError('Invalid map or scene radius')
    source_hash = digest(log)
    cameras = [c for c in camera_trace(log) if args.start_tick <= c['tick'] <= args.end_tick]
    if len(cameras) < 4:
        raise ValueError('At least four recorded camera samples are required')
    tiles = set()
    tile = lambda value: math.floor((17066.666666666668 - value) / (1600 / 3))
    for c in cameras:
        for y in range(max(0, tile(c['eye'][0] + args.radius)), min(63, tile(c['eye'][0] - args.radius)) + 1):
            for x in range(max(0, tile(c['eye'][1] + args.radius)), min(63, tile(c['eye'][1] - args.radius)) + 1):
                tiles.add((x, y))
    files = set()
    missing = []
    for x, y in sorted(tiles):
        relative = Path(args.map) / f'{x}_{y}.fg3'
        path = cache / relative
        if not path.is_file():
            missing.append(relative.as_posix())
            continue
        data = path.read_bytes()
        if len(data) < 12 or struct.unpack_from('<4sII', data) != (b'FGS3', 3, (len(data) - 12) // 116) or (len(data) - 12) % 116:
            raise ValueError(f'Invalid FGS3 tile: {path}')
        files.add(relative)
        for offset in range(12, len(data), 116):
            model = Path('models') / (data[offset:offset + 32].hex() + '.fgs')
            if (cache / model).is_file():
                files.add(model)
            else:
                missing.append(model.as_posix())
    lights = Path('lights') / (args.map + '.fgl')
    if (cache / lights).is_file():
        files.add(lights)
    else:
        missing.append(lights.as_posix())
    sizes = {p: (cache / p).stat().st_size for p in files}
    if sum(sizes.values()) > args.max_mib * 1048576:
        raise ValueError('Static inputs exceed the explicit copy budget; no partial corpus was made')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(log, output / 'renderer.log')
    (output / 'camera.json').write_text(json.dumps(cameras, indent=2) + '\n', encoding='utf-8')
    copied = {}
    for relative in sorted(files):
        source, target = cache / relative, output / 'world-cache' / relative
        before = digest(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        if source.stat().st_size != sizes[relative] or digest(source) != before or digest(target) != before:
            raise ValueError(f'Input changed while copying: {source}')
        if relative.suffix == '.fgs':
            with target.open('rb') as stream:
                header = stream.read(20)
            if len(header) != 20 or header[:8] != b'FGS2\x02\0\0\0':
                raise ValueError(f'Unsupported or truncated static model: {source}')
        elif relative.suffix == '.fgl':
            with target.open('rb') as stream:
                header = stream.read(16)
            if len(header) != 16:
                raise ValueError(f'Truncated light cache: {source}')
            magic, version, stride, count = struct.unpack('<4sIII', header)
            if (magic, version, stride) != (b'FGL1', 1, 48) or sizes[relative] != 16 + count * stride:
                raise ValueError(f'Unsupported light cache: {source}')
        copied[relative.as_posix()] = {'bytes': sizes[relative], 'sha256': before}
    settings = []
    for line, raw in enumerate((output / 'renderer.log').read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        if raw.startswith(('QUALITY preset=', 'MATERIAL_CONFIG ', 'AO method=', 'SETTINGS frame revision=', 'FRAME interval ')):
            settings.append({'line': line, 'raw': raw})
    (output / 'settings-observations.json').write_text(json.dumps(settings, indent=2) + '\n', encoding='utf-8')
    terrain = None
    if args.extract_terrain:
        command = [sys.executable, str(Path(__file__).with_name('prepare_rotation_fixture.py')),
                   '--log', str(output / 'renderer.log'), '--output', str(output / 'terrain'),
                   '--map', args.map, '--start-tick', str(args.start_tick), '--end-tick', str(args.end_tick),
                   '--terrain-radius', str(args.radius), '--terrain-only']
        with (output / 'terrain-extraction.log').open('w', encoding='utf-8') as transcript:
            subprocess.run(command, stdout=transcript, stderr=subprocess.STDOUT, check=True)
        terrain = json.loads((output / 'terrain/manifest.json').read_text(encoding='utf-8'))
    if digest(log) != source_hash or digest(output / 'renderer.log') != source_hash:
        raise ValueError('The source log changed; use a frozen session')
    manifest = {
        'format': 1, 'map': args.map, 'radius_yards': args.radius,
        'log_sha256': source_hash, 'camera_samples': len(cameras), 'tiles': sorted(tiles),
        'static_cache_origin': str(cache), 'static_outputs': copied,
        'missing_static_inputs': sorted(set(missing)),
        'static_inputs_complete': not missing,
        'static_payload_validation': 'Dependency headers and hashes; full loader/render replay still required',
        'mpq_terrain_complete': bool(terrain and terrain['complete']),
        'full_scene_replay_ready': False, 'fps_acceptance_proven': False,
        'required_native_capture': [
            'Ordered native draw/state/resource create-update-release commands for the same camera route',
            'Original VS/PS bytecode, texture mip contents and exact vertex/index layouts',
            'Animated M2 palettes, equipment/particle draws and dynamic resource updates',
            'Depth/projection/MSAA/HDR settings and the timing of every settings transition',
            'A separate uncapped native-only replay; effects-off still includes Northlight proxy cost',
        ],
        'acceptance': {
            'minimum_rotation_fps': 120, 'preferred_rotation_fps': 160,
            'rotation_p99_frame_ms_max': 1000 / 120,
            'same_static_and_rotating_scene': True, 'quality_reduction_allowed': False,
            'no_injected_sleep_to_match_game_fps': True,
        },
        'limitations': [
            'Static lighting-cache geometry is a CPU regression corpus, not the original game render stream.',
            'Camera samples are sparse observations; interpolation does not recover original visibility or draw order.',
            'Settings lines retain their original timing domain and are not automatically joined to stream frame IDs.',
            'No game or installed input was modified; missing native capture prevents a full-scene FPS claim.',
        ],
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: manifest[k] for k in ('camera_samples', 'static_inputs_complete', 'mpq_terrain_complete', 'full_scene_replay_ready')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--map', default='Northrend')
    parser.add_argument('--radius', type=float, default=928)
    parser.add_argument('--max-mib', type=int, default=2048)
    parser.add_argument('--start-tick', type=int, required=True)
    parser.add_argument('--end-tick', type=int, required=True)
    parser.add_argument('--extract-terrain', action='store_true')
    prepare(parser.parse_args())
