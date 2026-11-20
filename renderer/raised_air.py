"""Build the Dalaran and Telredor outdoor fog height samples from the selected client."""
import argparse
from contextlib import ExitStack
import math
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
import client_archives
from world_light_placements import names
from world_scene_builder import ZERO, array, chunks, matvec, open_archive, placement, wmo_group

SITES = {'Northrend': (5803.237, 678.237), 'Expansion01': (278.582, 6001.27)}
SPACING = 8
RADIUS = 22


def add_group(data, matrix, origin, outdoor, xs, ys, highest, floors):
    positions = []
    for vertex in struct.iter_unpack('<3f', data['MOVT']):
        point = tuple(a + b for a, b in zip(matvec(matrix, vertex), origin))
        if not all(map(math.isfinite, point)):
            raise ValueError('Nonfinite WMO position')
        positions.append(point)
    indices, properties = data['MOVI'], data['MOPY']
    if len(indices) % 6 or len(properties) * 3 != len(indices):
        raise ValueError('Invalid WMO triangle table')
    for (a, b, c), (flags, material) in zip(struct.iter_unpack('<3H', indices), struct.iter_unpack('BB', properties)):
        if not (flags & 8 or (flags & 32 and not flags & 4) or material == 255):
            continue
        x, y, z = positions[a]
        bx, by, bz = positions[b]
        cx, cy, cz = positions[c]
        left = max(xs.start, math.ceil(min(x, bx, cx) / SPACING))
        right = min(xs.stop - 1, math.floor(max(x, bx, cx) / SPACING))
        bottom = max(ys.start, math.ceil(min(y, by, cy) / SPACING))
        top = min(ys.stop - 1, math.floor(max(y, by, cy) / SPACING))
        ux, uy, uz = bx-x, by-y, bz-z
        vx, vy, vz = cx-x, cy-y, cz-z
        nz = ux*vy - uy*vx
        if abs(nz) <= 1e-6 or left > right or bottom > top:
            continue
        floor = outdoor and nz > math.hypot(uy*vz - uz*vy, uz*vx - ux*vz)
        for iy in range(bottom, top + 1):
            for ix in range(left, right + 1):
                dx, dy = ix * SPACING - x, iy * SPACING - y
                u, v = (dx*vy - dy*vx)/nz, (ux*dy - uy*dx)/nz
                if u >= 0 and v >= 0 and u + v <= 1:
                    height = z + u*uz + v*vz
                    cell = (iy, ix)
                    highest[cell] = max(highest.get(cell, -math.inf), height)
                    if floor:
                        floors[cell] = max(floors.get(cell, -math.inf), height)


def bake(read, map_name, x, y):
    tx, ty = (math.floor((ZERO - n) / (1600 / 3)) for n in (y, x))
    tile = dict(chunks(read(fr'world\maps\{map_name}\{map_name}_{tx}_{ty}.adt')))
    paths = names(tile, 'MWID', 'MWMO')
    records = tile.get('MODF', b'')
    highest, floors = {}, {}
    xs, ys = (range(math.floor(n/SPACING)-RADIUS, math.floor(n/SPACING)+RADIUS+1) for n in (x, y))
    for record in array(records, 0, len(records)//64, '<2I12f4H'):
        if (ZERO-record[13] > xs[-1]*SPACING or ZERO-record[10] < xs[0]*SPACING or
                ZERO-record[11] > ys[-1]*SPACING or ZERO-record[8] < ys[0]*SPACING):
            continue
        name = paths[record[0]]
        matrix, origin = placement(record[2:5], record[5:8], 1.)
        root = dict(chunks(read(name)))
        count, = struct.unpack_from('<I', root['MOHD'], 4)
        if count > 4096:
            raise ValueError('Unexpected group count in ' + name)
        for group_id in range(count):
            group = wmo_group(read(name[:-4] + f'_{group_id:03d}.wmo'))[0]['MOGP']
            flags, = struct.unpack_from('<I', group, 8)
            add_group(dict(chunks(group, 68)), matrix, origin, flags & 0x48, xs, ys, highest, floors)
    # Overhead and steep solids mask walkable floors. Keep the highest exposed
    # surface within the same tolerance as the runtime's original test windows.
    rows = [struct.pack('<hhf', ix, iy, z) for (iy, ix), z in sorted(floors.items())
            if z >= highest[iy, ix] - .05]
    return struct.pack('<4sI', b'RFS1', len(rows)) + b''.join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--locale', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with ExitStack() as stack:
        archives = [stack.enter_context(open_archive(path)) for path in reversed(client_archives.chain(args.client, locale=args.locale))]

        def read(name):
            for archive in archives:
                if archive.has(name):
                    return archive.read(name)
            raise FileNotFoundError(name)

        for name, (x, y) in SITES.items():
            (args.output / (name + '.rfs')).write_bytes(bake(read, name, x, y))


if __name__ == '__main__':
    main()
