"""Extract a private MPQ-backed terrain/world corpus and recorded camera orbit; never runs the client."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import time

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
                str(Path(__file__).resolve().parents[1] / 'renderer')]
import northlight_paths as fp
import world_scene_builder as world


def sha(data):
    return hashlib.sha256(data).hexdigest()


def camera_trace(log):
    rows = []
    for line in log.read_text(encoding='utf-8', errors='replace').splitlines():
        if not line.startswith('WORLD camera '):
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        row = {key: float(fields[key]) for key in ('tick', 'pivotDistance')}
        for key in ('eye', 'forward'):
            found = re.search(key + r'=\(([^)]+)\)', line)
            row[key] = [float(v) for v in re.split(r'[, ]+', found[1]) if v]
        row['pivotSource'] = fields.get('pivotSource', 'unknown')
        if row['pivotDistance'] > 0:
            row['pivot'] = [row['eye'][i] + row['forward'][i] * row['pivotDistance'] for i in range(3)]
            rows.append(row)
    if not rows:
        raise ValueError('No valid camera/pivot samples in log')
    return rows


def terrain_draws(blob):
    """Use the existing MPQ builder for exact XYZ, winding and hole masks, two adjacent MCNKs per draw."""
    builder = world.Builder(None)
    builder.material = lambda *args: 0
    records = []
    for tag, data in world.chunks(blob):
        if tag != 'MCNK':
            continue
        builder.reset()
        builder.terrain(data, [])
        if not builder.triangles:
            continue
        records.append(([v[:3] for v in builder.vertices], [t[:3] for t in builder.triangles]))
    draws = []
    for first in range(0, len(records), 2):
        vertices, triangles = [], []
        for positions, indices in records[first:first + 2]:
            base = len(vertices)
            vertices.extend(positions)
            triangles.extend(tuple(base + i for i in triangle) for triangle in indices)
        draws.append((vertices, triangles))
    return draws


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--map', default='Northrend')
    parser.add_argument('--start-tick', type=int, required=True)
    parser.add_argument('--end-tick', type=int, required=True)
    parser.add_argument('--terrain-radius', type=float, default=928)
    parser.add_argument('--terrain-only', action='store_true', help='Skip the independent FGS3 world corpus')
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit('Output must be new or empty; existing fixtures are immutable')
    if not re.fullmatch(r'[A-Za-z0-9_]+', args.map) or args.terrain_radius <= 0:
        raise SystemExit('Invalid map or radius')
    args.output.mkdir(parents=True, exist_ok=True)
    log_bytes = args.log.read_bytes()
    (args.output / 'renderer.log').write_bytes(log_bytes)
    cameras = [r for r in camera_trace(args.log) if args.start_tick <= r['tick'] <= args.end_tick]
    if len(cameras) < 4:
        raise SystemExit('Need at least four camera samples in the selected interval')
    (args.output / 'camera.json').write_text(json.dumps(cameras, indent=2) + '\n', encoding='utf-8')
    with (args.output / 'camera.bin').open('wb') as out:
        out.write(struct.pack('<4sI', b'NLC1', len(cameras)))
        for row in cameras:
            out.write(struct.pack('<Q7f', int(row['tick']), *row['eye'], *row['forward'], row['pivotDistance']))
    low = [min(r['eye'][i] for r in cameras) - args.terrain_radius for i in range(2)]
    high = [max(r['eye'][i] for r in cameras) + args.terrain_radius for i in range(2)]
    tile = lambda value: math.floor((world.ZERO - value) / (world.UNIT * 128))
    tiles = [(x, y) for y in range(max(0, tile(high[0])), min(63, tile(low[0])) + 1)
             for x in range(max(0, tile(high[1])), min(63, tile(low[1])) + 1)]
    assets = world.Assets(fp.client_root(), maps=[args.map])
    original_read = assets.read
    reads, draws, records, missing = {}, [], [], []
    archive_before = {str(path): (path.stat().st_size, path.stat().st_mtime_ns) for path in assets.paths}

    def read(name):
        blob = original_read(name)
        key = name.lower().replace('/', '\\')
        reads[key] = {'archive': assets.origin(name), 'bytes': len(blob), 'sha256': sha(blob)}
        return blob

    assets.read = read
    builder = world.InstancedBuilder(assets, args.output / 'world-cache')
    started = time.monotonic()
    try:
        for x, y in tiles:
            name = f'world\\maps\\{args.map}\\{args.map}_{x}_{y}.adt'
            try:
                blob = read(name)
            except FileNotFoundError:
                missing.append(name)
                continue
            if not blob:
                missing.append(name + ':deleted')
                continue
            raw = args.output / 'adt' / f'{x}_{y}.adt'
            raw.parent.mkdir(parents=True, exist_ok=True)
            raw.write_bytes(blob)
            tile_draws = terrain_draws(blob)
            first = len(draws)
            draws.extend(tile_draws)
            record = {'tile': [x, y], 'asset': name, 'first_draw': first, 'draws': len(tile_draws)}
            if not args.terrain_only:
                record['world'] = builder.build(args.map, x, y, args.output / 'world-cache' / args.map / f'{x}_{y}.fg3')
            records.append(record)
            print(json.dumps({'tile': [x, y], 'draws': len(draws), 'seconds': round(time.monotonic() - started, 1)}), flush=True)
        if not draws:
            raise ValueError('No terrain geometry extracted')
        with (args.output / 'terrain.bin').open('wb') as out:
            out.write(struct.pack('<4sI', b'NLT1', len(draws)))
            for vertices, triangles in draws:
                out.write(struct.pack('<II', len(vertices), len(triangles) * 3))
                for vertex in vertices:
                    out.write(struct.pack('<3f', *vertex))
                for triangle in triangles:
                    out.write(struct.pack('<3I', *triangle))
        assert archive_before == {str(path): (path.stat().st_size, path.stat().st_mtime_ns) for path in assets.paths}, 'MPQ inputs changed'
        assert args.log.read_bytes() == log_bytes, 'Source log changed'
        outputs = {p.relative_to(args.output).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p.read_bytes())}
                   for p in sorted(args.output.rglob('*')) if p.is_file()}
        complete = not (missing or assets.missing or assets.unsupported or assets.unreadable)
        report = {'format': 1, 'complete': complete, 'map': args.map, 'baseline_ref': 'fd6b3a4bb7853f36abfd299079b34e37d12c7fad',
                  'log_sha256': sha(log_bytes), 'camera_samples': len(cameras), 'draws': len(draws),
                  'terrain_radius': args.terrain_radius, 'tiles': records, 'missing_tiles': missing,
                  'mpq_view': assets.fingerprint(), 'assets': reads, 'outputs': outputs,
                  'limitations': ['Recorded camera positions, not a captured D3D draw stream.',
                                  'Terrain candidate visibility is reconstructed offline; game occlusion/LOD and driver stalls are not replayed.',
                                  'Two adjacent nonempty MCNKs form each exact terrain draw; original heights, holes and winding are preserved.',
                                  'World FGS3 uses the existing static MPQ builder and its documented material/model limitations.'],
                  'unsupported': sorted(assets.unsupported), 'missing_assets': sorted(assets.missing),
                  'game_launched': False, 'seconds': time.monotonic() - started}
        (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({'complete': complete, 'draws': len(draws), 'tiles': len(records), 'asset_count': len(reads)}), flush=True)
    finally:
        assets.close()


if __name__ == '__main__':
    main()
