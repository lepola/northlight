#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""build_lighting.underwater_daylight on synthetic Light and LiquidType tables; no client, game or GPU.

The combo's art layer lights underwater like above water through data alone: each Light row's underwater
slots (8 clear, 10 storm) take the row's above-water profiles (7, 9; the clear one when a row has no storm
profile), and the water, ocean and slime liquids lose their own Light and depth darkening. Magma, every
other column and every other row stay byte identical, and a second run changes nothing."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import struct
import build_lighting as bl
from build_lighting import DBC, u, f


def table(rows, fields, strings=b'\0'):
    return DBC(struct.pack('<4s4I', b'WDBC', len(rows), fields, fields*4, len(strings))+b''.join(bytes(r) for r in rows)+strings)


def light_row(id, slots):
    r = bytearray(15*4)
    struct.pack_into('<II', r, 0, id, 0)
    for i, v in enumerate(slots):
        struct.pack_into('<I', r, (7+i)*4, v)
    return r


def liquid_row(id, kind, darken, light):
    r = bytearray(45*4)
    struct.pack_into('<IIII', r, 0, id, 1, 0x0f, kind)
    struct.pack_into('<ffff', r, 6*4, darken, .5, .5, .25)
    struct.pack_into('<I', r, 10*4, light)
    struct.pack_into('<f', r, 11*4, 1.5)
    return r


light = table([light_row(1, [11, 12, 13, 14, 15]),      # every slot has its own profile
               light_row(2, [21, 22, 0, 24, 0]),        # no storm profile: storm underwater takes the clear one
               light_row(3, [0, 32, 0, 34, 0]),         # no clear profile: nothing to point at, left as it is
               light_row(4, [41, 41, 43, 43, 0])], 15)  # already above-water underwater
liquid = table([liquid_row(1, 0, 0, 0), liquid_row(2, 1, 30, 0), liquid_row(3, 2, 0, 7),
                liquid_row(4, 3, 0, 6), liquid_row(181, 0, 0, 0)], 45, b'\0Water\0')
light_before, liquid_before = light.bytes(), liquid.bytes()

report = bl.underwater_daylight(light, liquid)
rows = light.index
assert [u(rows[1], c) for c in range(7, 12)] == [11, 11, 13, 13, 15], 'underwater slots take the above-water profiles'
assert [u(rows[2], c) for c in range(7, 12)] == [21, 21, 0, 21, 0], 'storm underwater falls back to the clear profile'
assert [u(rows[3], c) for c in range(7, 12)] == [0, 32, 0, 34, 0], 'a row without a clear profile is left alone'
assert [u(rows[4], c) for c in range(7, 12)] == [41, 41, 43, 43, 0]
assert report['light_slots'] == 4, report
for id, row in light.index.items():
    before = DBC(light_before).index[id]
    assert row[:7*4] == before[:7*4] and row[9*4:10*4] == before[9*4:10*4] and row[11*4:] == before[11*4:], 'only the underwater slots change'

liquids = liquid.index
assert f(liquids[2], 6) == 0 and u(liquids[2], 10) == 0, 'ocean no longer darkens with depth'
assert u(liquids[4], 10) == 0, 'slime no longer has a light of its own'
assert f(liquids[3], 6) == 0 and u(liquids[3], 10) == 7, 'magma keeps its own look'
assert report['liquids'] == [2, 4], report
for id, row in liquid.index.items():
    before = DBC(liquid_before).index[id]
    assert row[:6*4] == before[:6*4] and row[7*4:10*4] == before[7*4:10*4] and row[11*4:] == before[11*4:], 'only darkening and light change'
assert liquid.bytes().endswith(b'\0Water\0'), 'strings are kept'

once_light, once_liquid = light.bytes(), liquid.bytes()
assert bl.underwater_daylight(light, liquid) == {'light_slots': 0, 'liquids': []}
assert light.bytes() == once_light and liquid.bytes() == once_liquid, 'a second run changes nothing'
print('PASS underwater daylight: above-water profiles underwater, liquid light and darkening off, magma and other data kept')
