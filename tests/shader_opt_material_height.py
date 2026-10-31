"""Float32 proof cases for the mutually exclusive red/green height accents."""
from itertools import product
from random import Random
from struct import pack, unpack


def f32(value):
    return unpack('<f', pack('<f', value))[0]


def term(a, b, denominator):
    value = f32(f32(f32(a - b) / denominator) * 4)
    return min(max(f32(value - f32(.1)), 0.), 1.)


def check(r, g, b):
    rg = term(r, g, max(r, f32(.05)))
    gr = term(g, r, max(g, f32(.05)))
    gb = term(g, b, max(g, f32(.05)))
    reference = max(f32(rg * gb), f32(gr * gb))
    edge = term(max(r, g), min(r, g), max(r, g, f32(.05)))
    optimized = f32(edge * gb)
    assert reference == optimized, (r, g, b, reference, optimized)


def main():
    # Byte colors, equal channels, denominator clamps and saturation boundaries.
    values = [f32(x / 255) for x in range(256)]
    count = 0
    for r, g in product(values, repeat=2):
        for b in (0., g, 1.):
            check(r, g, b)
            count += 1
    boundary = [f32(x) for x in (0., .001, .025, .05, .1, .5, 1.)]
    for value in tuple(boundary):
        bits = unpack('<I', pack('<f', value))[0]
        if bits:
            boundary.append(unpack('<f', pack('<I', bits - 1))[0])
        boundary.append(unpack('<f', pack('<I', bits + 1))[0])
    for rgb in product(boundary, repeat=3):
        check(*rgb)
        count += 1
    random = Random(335)
    for _ in range(10000):
        check(*(f32(random.random()) for _ in range(3)))
        count += 1
    print(f'PASS {count} float32 height-accent cases; exact equality')


if __name__ == '__main__':
    main()
