#!/usr/bin/env python3
"""Semantic diff of two world caches (A = reference, B = candidate); stdlib only, no client needed.

Byte equality is the wrong test: FGS3 transforms differ in float bits between builds of the
same client. This compares what the renderer consumes, by category:

- tiles:     <map>/<x>_<y>.fg3 present in A, B or both
- instances: FGS3 records keyed by (model key, instance id, kind); bounds and transforms equal
             within --tolerance (absolute, or 4 float32 ulps for large coordinates)
- models:    each model both sides reference: identical bytes, same geometry with other
             materials (texture/colour), or different geometry (vertex/triangle counts or
             positions); missing on one side
- lights:    <map>.fgl records keyed by source id; position, colour and range within tolerance.
             With --tiles, B is a partial build: every B light must match A, and A lights inside
             the selected tiles (minus --light-margin) must exist in B
- fog:       fog/<map>/<x>_<y>.frf zones exact, water and indoor records within tolerance
- celestial: celestial/*.fct identical bytes

    python3 scripts/world_cache_diff.py A B [--maps Azeroth] [--tiles Azeroth:31:48:33:50 ...] [--report r.json]

Exit status 0 when every category is equal, 1 otherwise.
"""
import argparse
import collections
import json
import struct
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parents[1] / 'renderer'))
import instance_maps  # noqa: E402

MAPS = ['Azeroth', 'Kalimdor', 'Expansion01', 'Northrend']   # the default selection
ALL_MAPS = MAPS + instance_maps.ALL_MAPS                      # --maps and --tiles also take the instance maps
INSTANCE = struct.Struct('<32sQI18f')
LIGHT = struct.Struct('<8fQII')
KINDS = {0: 'terrain', 1: 'm2', 2: 'wmo', 3: 'wmo_doodad'}
EXAMPLES = 8


def close(a, b, tol):
    return abs(a - b) <= max(tol, 4.8e-7 * max(abs(a), abs(b)))


def all_close(a, b, tol):
    return len(a) == len(b) and all(close(x, y, tol) for x, y in zip(a, b))


class Category:
    def __init__(self):
        self.counts = collections.Counter()
        self.examples = collections.defaultdict(list)

    def add(self, what, example=None, n=1):
        self.counts[what] += n
        if example is not None and len(self.examples[what]) < EXAMPLES:
            self.examples[what].append(example)

    def json(self, equal_keys):
        return {'equal': all(k in equal_keys for k in self.counts), 'counts': dict(sorted(self.counts.items())),
                'examples': dict(self.examples)}


def tiles_of(root, mapname, selection):
    found = {}
    for p in (root / mapname).glob('*.fg3') if (root / mapname).is_dir() else []:
        try:
            x, y = map(int, p.stem.split('_'))
        except ValueError:
            continue
        if selection is None or any(m == mapname and x0 <= x <= x1 and y0 <= y <= y1 for m, x0, y0, x1, y1 in selection):
            found[(x, y)] = p
    return found


def instances(path):
    data = path.read_bytes()
    magic, version, count = struct.unpack_from('<4sII', data)
    if magic != b'FGS3' or version != 3 or len(data) != 12 + count * INSTANCE.size:
        raise ValueError(f'invalid FGS3 {path}')
    records = collections.defaultdict(list)
    for r in INSTANCE.iter_unpack(data[12:]):
        records[(r[0], r[1], r[2])].append(r[3:])
    return records


def fgs2(path):
    data = path.read_bytes()
    magic, version, nv, nt, nm = struct.unpack_from('<4s4I', data)
    if magic != b'FGS2' or version != 2:
        raise ValueError(f'invalid FGS2 {path}')
    vertices = data[20:20 + 32 * nv]
    triangles = data[20 + 32 * nv:20 + 32 * nv + 16 * nt]
    return data, nv, nt, vertices, triangles, data[20 + 32 * nv + 16 * nt:]


def compare_model(a, b, tol):
    da, nva, nta, va, ta, ma = fgs2(a)
    db, nvb, ntb, vb, tb, mb = fgs2(b)
    if da == db:
        return 'identical'
    if (nva, nta) != (nvb, ntb) or ta != tb:
        return 'geometry'
    if va != vb:
        fa, fb = struct.unpack(f'<{8 * nva}f', va), struct.unpack(f'<{8 * nvb}f', vb)
        positions = all(close(fa[i], fb[i], tol) for i in range(len(fa)) if i % 8 < 3)
        if not positions:
            return 'geometry'
        if not all_close(fa, fb, tol):
            return 'normals_or_uv'
    return 'materials' if ma != mb else 'identical_within_tolerance'


def diff(a_root, b_root, maps, selection, tol, margin):
    tiles, inst, models, lights, fog = Category(), Category(), Category(), Category(), Category()
    celestial = Category()
    seen_models = {}
    regions = collections.defaultdict(list)   # map -> terrain XY rectangles of the selected tiles
    for mapname in maps:
        ta, tb = tiles_of(a_root, mapname, selection), tiles_of(b_root, mapname, selection)
        for key in sorted(set(ta) | set(tb)):
            name = f'{mapname}/{key[0]}_{key[1]}'
            if key not in tb:
                tiles.add('only_a', name)
                continue
            if key not in ta:
                tiles.add('only_b', name)
                continue
            tiles.add('both')
            ra, rb = instances(ta[key]), instances(tb[key])
            for ident in sorted(set(ra) | set(rb)):
                kind = KINDS.get(ident[2], str(ident[2]))
                example = {'tile': name, 'key': ident[0].hex()[:16], 'uid': ident[1], 'kind': kind}
                if ident not in rb:
                    inst.add('only_a:' + kind, example)
                    continue
                if ident not in ra:
                    inst.add('only_b:' + kind, example)
                    continue
                va, vb = ra[ident], rb[ident]
                if len(va) != len(vb):
                    inst.add('duplicate_count:' + kind, example)
                    continue
                pairs = list(zip(sorted(va, key=lambda v: v[6:]), sorted(vb, key=lambda v: v[6:])))
                if not all(all_close(p[6:], q[6:], tol) for p, q in pairs):
                    worst = max(max(abs(x - y) for x, y in zip(p[6:], q[6:])) for p, q in pairs)
                    inst.add('transform:' + kind, {**example, 'max_abs_difference': worst})
                elif not all(all_close(p[:6], q[:6], tol) for p, q in pairs):
                    inst.add('bounds_only:' + kind, example)   # same placement, other model extents
                else:
                    inst.add('equal:' + kind)
                if ident[2] == 0:
                    regions[mapname].append(va[0][:6])
                if ident[0] not in seen_models:
                    seen_models[ident[0]] = kind
            fa, fb = a_root / 'fog' / mapname / (ta[key].stem + '.frf'), b_root / 'fog' / mapname / (tb[key].stem + '.frf')
            compare_fog(fa, fb, name, tol, fog)
    for key, kind in sorted(seen_models.items()):
        pa, pb = a_root / 'models' / (key.hex() + '.fgs'), b_root / 'models' / (key.hex() + '.fgs')
        example = {'key': key.hex()[:16], 'kind': kind}
        if not pa.is_file() or not pb.is_file():
            models.add('missing_a' if not pa.is_file() else 'missing_b', example)
            continue
        models.add(compare_model(pa, pb, tol) + ':' + kind, example)
    for mapname in maps:
        compare_lights(a_root / 'lights' / (mapname + '.fgl'), b_root / 'lights' / (mapname + '.fgl'), mapname,
                       regions[mapname] if selection is not None else None, tol, margin, lights)
    for body in ('sun', 'moon'):
        pa, pb = a_root / 'celestial' / (body + '.fct'), b_root / 'celestial' / (body + '.fct')
        if not pa.is_file() and not pb.is_file():
            continue
        if not pa.is_file() or not pb.is_file():
            celestial.add('missing_' + ('a' if not pa.is_file() else 'b'), body)
        else:
            celestial.add('identical' if pa.read_bytes() == pb.read_bytes() else 'different', body)
    equal_instance = {k for k in inst.counts if k.startswith('equal:')}
    equal_model = {k for k in models.counts if k.split(':')[0] in ('identical', 'identical_within_tolerance')}
    return {
        'a': str(a_root), 'b': str(b_root), 'maps': maps, 'tolerance': tol,
        'tiles': tiles.json({'both'}),
        'instances': inst.json(equal_instance),
        'models': models.json(equal_model),
        'lights': lights.json({'equal', 'outside_selection_a', 'outside_selection_b'}),
        'fog': fog.json({'equal'}),
        'celestial': celestial.json({'identical'}),
    }


def read_fog(path):
    data = path.read_bytes()
    magic, version, tx, ty, nw, ni = struct.unpack_from('<4s5I', data)
    if magic != b'FRF1' or version not in (1, 2):
        raise ValueError(f'invalid FRF1 {path}')
    zones = struct.unpack_from('<256I', data, 24)
    offset = 24 + 1024
    waters = sorted(struct.unpack_from('<3fQ', data, offset + 20 * i) for i in range(nw))
    offset += 20 * nw
    indoors = sorted(struct.unpack_from('<6f', data, offset + 24 * i) for i in range(ni))
    offset += 24 * ni
    if len(data) != offset + (1024 if version == 2 else 0):
        raise ValueError(f'invalid regional fog tile size {path}')
    subzones = struct.unpack_from('<256I', data, offset) if version == 2 else (0,) * 256
    return (zones, subzones), waters, indoors


def compare_fog(pa, pb, name, tol, fog):
    if not pa.is_file() and not pb.is_file():
        return
    if not pa.is_file() or not pb.is_file():
        fog.add('missing_' + ('a' if not pa.is_file() else 'b'), name)
        return
    za, wa, ia = read_fog(pa)
    zb, wb, ib = read_fog(pb)
    problems = []
    if za != zb:
        problems.append('zones')
    if len(wa) != len(wb) or any(p[3] != q[3] or not all_close(p[:3], q[:3], tol) for p, q in zip(wa, wb)):
        problems.append('water')
    if len(ia) != len(ib) or any(not all_close(p, q, tol) for p, q in zip(ia, ib)):
        problems.append('indoor')
    if not problems:
        fog.add('equal')
    for p in problems:
        fog.add(p, {'tile': name, 'water': [len(wa), len(wb)], 'indoor': [len(ia), len(ib)]})


def read_lights(path):
    data = path.read_bytes()
    magic, version, size, count = struct.unpack_from('<4sIII', data)
    if magic != b'FGL1' or size != LIGHT.size or len(data) != 16 + count * size:
        raise ValueError(f'invalid FGL1 {path}')
    return {r[8]: r for r in LIGHT.iter_unpack(data[16:])}


def compare_lights(pa, pb, mapname, region, tol, margin, lights):
    if not pa.is_file() and not pb.is_file():
        return
    if not pa.is_file() or not pb.is_file():
        lights.add('missing_' + ('a' if not pa.is_file() else 'b'), mapname)
        return
    la, lb = read_lights(pa), read_lights(pb)

    def inside(r, m):
        return region is None or any(lo[0] + m <= r[0] <= hi[0] - m and lo[1] + m <= r[1] <= hi[1] - m
                                     for lo, hi in ((b[:3], b[3:]) for b in region))
    for sid in sorted(set(la) | set(lb)):
        a, b = la.get(sid), lb.get(sid)
        example = {'map': mapname, 'source_id': f'{sid:016x}', 'position': [round(v, 2) for v in (a or b)[:3]]}
        if a is None:
            lights.add('only_b' if inside(b, 0) else 'outside_selection_b', example)
        elif b is None:
            lights.add('only_a' if inside(a, margin) else 'only_a_edge' if inside(a, 0) else 'outside_selection_a', example)
        elif a[9:] != b[9:] or not all_close(a[:8], b[:8], tol):
            lights.add('values', {**example, 'kind': [a[9], b[9]]})
        else:
            lights.add('equal')


def name_examples(result):
    """Add 'name' to model and instance examples: FGS3 keys are sha256('FGS3-v1:' + asset name)."""
    import hashlib
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    import northlight_paths
    northlight_paths.use_source_modules()
    from world_scene_builder import Assets
    assets = Assets()
    try:
        names = {hashlib.sha256(('FGS3-v1:' + n).encode()).hexdigest()[:16]: n
                 for n in assets.providers if n.endswith(('.m2', '.wmo'))}
    finally:
        assets.close()
    for category in ('models', 'instances'):
        for examples in result[category]['examples'].values():
            for e in examples:
                if isinstance(e, dict) and 'key' in e:
                    e['name'] = names.get(e['key'], '?')


def parse_tiles(values):
    result = []
    for v in values:
        m, *nums = v.split(':')
        if m not in ALL_MAPS or len(nums) != 4:
            raise argparse.ArgumentTypeError(f'--tiles wants MAP:X0:Y0:X1:Y1, got {v!r}')
        result.append((m, *map(int, nums)))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('a', type=Path, help='reference world cache')
    ap.add_argument('b', type=Path, help='candidate world cache')
    ap.add_argument('--maps', nargs='+', choices=ALL_MAPS, metavar='MAP',
                    help='continents or instance maps (default: the four continents)')
    ap.add_argument('--tiles', nargs='+', metavar='MAP:X0:Y0:X1:Y1', help='compare only these tile rectangles')
    ap.add_argument('--tolerance', type=float, default=1e-3)
    ap.add_argument('--light-margin', type=float, default=64.0,
                    help='with --tiles: A lights this close to the selection edge may belong to unselected tiles')
    ap.add_argument('--report', type=Path, help='write the JSON report here')
    ap.add_argument('--names', action='store_true',
                    help="name the example models from the configured client's MPQs (needs StormLib)")
    args = ap.parse_args()
    selection = parse_tiles(args.tiles) if args.tiles else None
    maps = args.maps or (sorted({t[0] for t in selection}, key=ALL_MAPS.index) if selection else MAPS)
    result = diff(args.a, args.b, maps, selection, args.tolerance, args.light_margin)
    if args.names:
        name_examples(result)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    for name in ('tiles', 'instances', 'models', 'lights', 'fog', 'celestial'):
        print(f"{name:10} {'EQUAL' if result[name]['equal'] else 'DIFF '} {json.dumps(result[name]['counts'])}")
    return 0 if all(result[n]['equal'] for n in ('tiles', 'instances', 'models', 'lights', 'fog', 'celestial')) else 1


if __name__ == '__main__':
    sys.exit(main())
