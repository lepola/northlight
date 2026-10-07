#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""build_lighting.stormify (0.3.198, rain) on synthetic Light/LightParams/band tables; no client, game or GPU.

Every outdoor Light row gets a private storm profile in column 9, made after relight, retime and the
sky steps. A row whose stock storm profile is its stock clear profile gets a copy of its final clear
profile (relit, retimed, sky-edited); any other row gets its stock storm profile relit, retimed and
then stormed. Rows with the same source and sky share one profile. Only column 9 changes: the clear,
underwater and storm-underwater slots, the water bands, other rows and every old profile stay byte
identical. Writes storm-bands-validation.json."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import copy
import json
import struct
import build_lighting as bl
from build_lighting import DBC, u, f, rgb

H = 120
SKY_CLEAR, SKY_STORM, SKY_SKYSTEP = 10, 20, 99
N, D, Y, S = (10, 20, 40), (220, 160, 100), (150, 170, 200), (230, 120, 60)
def c(v): return 0xff000000 | v[0] << 16 | v[1] << 8 | v[2]


def table(rows, fields):
    return DBC(struct.pack('<4s4I', b'WDBC', len(rows), fields, fields*4, 1)+b''.join(bytes(r) for r in rows)+b'\0')


def band(id, keys, floating=False):
    r = bytearray(34*4)
    struct.pack_into('<II', r, 0, id, len(keys))
    for i, (t, v) in enumerate(keys):
        struct.pack_into('<I', r, (2+i)*4, t)
        struct.pack_into('<f' if floating else '<I', r, (18+i)*4, v if floating else c(v))
    return r


def profile(pid, scale, sky, fog=(3000., 5000.), ratio=.3, keys=None):
    """(LightParams row, 18 int bands, 6 float bands) of profile `pid`; `scale` brightens the colours."""
    p = bytearray(12*4)
    struct.pack_into('<IIIffff', p, 0, pid, 0, sky, .4, .5, .5, .5)
    colour = lambda v: tuple(min(255, int(x*scale)) for x in v)
    times = keys or [(0, N), (6*H, D), (12*H, Y), (21*H, S), (22*H, N)]
    ints = [band((pid-1)*18+ch+1, [(t, colour((v[0]+ch, v[1]+ch, v[2]+ch))) for t, v in times]) for ch in range(18)]
    floats = [band((pid-1)*6+ch+1, [(t, 0.) for t, _ in times], True) for ch in range(6)]
    floats[0] = band((pid-1)*6+1, [(t, fog[0] if v == N else fog[1]) for t, v in times], True)
    floats[1] = band((pid-1)*6+2, [(t, ratio) for t, _ in times], True)
    return p, ints, floats


def light(id, map, clear, storm, water=0, storm_water=0):
    r = bytearray(15*4)
    struct.pack_into('<II', r, 0, id, map)
    struct.pack_into('<8I', r, 28, clear, water, storm, storm_water, 0, 0, 0, 0)
    return r


def stock_tables(keys2=None):
    # P1 clear (sky 10); P2 a storm of its own (sky 10, same as the clear's); P3 a storm with sky 20; P4 underwater.
    profiles = [profile(1, 1., SKY_CLEAR), profile(2, .6, SKY_CLEAR, fog=(2000., 4000.), keys=keys2), profile(3, .5, SKY_STORM), profile(4, .3, 30)]
    rows = [
        light(1, 0, 1, 1, 4, 4),     # storm == clear
        light(2, 1, 1, 2),           # separate storm, same sky
        light(3, 0, 1, 3),           # separate storm, other sky
        light(4, 530, 1, 2),         # shares row 2's stock storm
        light(5, 99, 1, 3),          # not outdoor
        light(6, 571, 7, 1),         # clear profile 7 does not exist: skipped
    ]
    return {'Light': table(rows, 15), 'LightParams': table([p for p, _, _ in profiles], 12),
            'LightIntBand': table([b for _, i, _ in profiles for b in i], 34), 'LightFloatBand': table([b for _, _, fl in profiles for b in fl], 34)}


def final_tables(stock):
    t = copy.deepcopy(stock)
    changes, skipped, _ = bl.relight(t)
    bl.retime(t, [x['new'] for x in changes.values()])
    for old, x in changes.items():   # a sky step: the private clear profile gets its own skybox
        putsky = t['LightParams'].index[x['new']]; struct.pack_into('<I', putsky, 8, SKY_SKYSTEP)
    return t, changes


stock = stock_tables()
final, changes = final_tables(stock)
before = {n: {i: bytes(r) for i, r in t.index.items()} for n, t in final.items()}
clear_private = changes[1]['new']
report = bl.stormify(final, stock)
after = {n: {i: bytes(r) for i, r in t.index.items()} for n, t in final.items()}
L = final['Light'].index
slot = lambda row, col: u(row, col)
checks = {}

# Rows and groups.
ids = {i: slot(L[i], 9) for i in L}
checks['outdoor rows 1-4 get a private storm id above every old id'] = all(ids[i] >= report['first_id'] > max(before['LightParams']) for i in (1, 2, 3, 4))
checks['row 5 (not outdoor) and row 6 (no clear profile) keep column 9'] = ids[5] == 3 and ids[6] == 1 and report['rows_skipped'] == [6]
checks['rows 2 and 4 (same stock storm, same sky) share one profile'] = ids[2] == ids[4]
checks['rows 1, 2 and 3 get three different profiles'] = len({ids[1], ids[2], ids[3]}) == 3
checks['3 profiles for 4 rows: 1 from clear, 2 from stock'] = (report['profiles'], report['rows'], report['from_clear'], report['from_stock']) == (3, 4, 1, 2)
checks['no storm id is another slot of any row'] = all(ids[i] not in {slot(L[j], c) for j in L for c in range(7, 15) if c != 9} for i in (1, 2, 3, 4))
checks['only column 9 changed in Light'] = all(before['Light'][i][:36] == after['Light'][i][:36] and before['Light'][i][40:] == after['Light'][i][40:] for i in L)
checks['clear, underwater and storm-underwater slots keep their ids'] = all(slot(L[i], c) == u(bytearray(before['Light'][i]), c) for i in L for c in (7, 8, 10))
checks['every old row and profile is byte-identical'] = all(after[n][i] == b for n, rows in before.items() if n != 'Light' for i, b in rows.items())
checks['new rows are 3 params, 54 int and 18 float bands'] = (len(after['LightParams'])-len(before['LightParams']), len(after['LightIntBand'])-len(before['LightIntBand']), len(after['LightFloatBand'])-len(before['LightFloatBand'])) == (3, 54, 18)

# Row 1: storm == clear, so a copy of the final clear profile (its retimed keys, its sky) with the storm look.
P = lambda pid: final['LightParams'].index[pid]
Int = lambda pid, ch: final['LightIntBand'].index[(pid-1)*18+ch+1]
Flt = lambda pid, ch: final['LightFloatBand'].index[(pid-1)*6+ch+1]
src, dst = clear_private, ids[1]
checks['row 1: skybox is the final clear profile\'s (sky step kept)'] = u(P(dst), 2) == SKY_SKYSTEP == u(P(src), 2)
checks['row 1: key times equal the final clear bands (already retimed)'] = all([u(Int(dst, ch), 2+i) for i in range(u(Int(dst, ch), 1))] == [u(Int(src, ch), 2+i) for i in range(u(Int(src, ch), 1))] for ch in range(18))
def expected(ch, pid, i): return bl.storm_color(ch, u(Int(pid, ch), 18+i))
checks['row 1: colour channels are the storm transform of the clear'] = all(u(Int(dst, ch), 18+i) == (u(Int(src, ch), 18+i) if ch in (11, 13, 14, 15, 16, 17) else expected(ch, src, i)) for ch in range(18) for i in range(u(Int(src, ch), 1)))
checks['row 1: water channels 14-17 byte-identical to the clear\'s'] = all(bytes(Int(dst, ch))[4:] == bytes(Int(src, ch))[4:] for ch in bl.STORM_WATER)
checks['row 1: glow x0.6'] = abs(f(P(dst), 3)-f(P(src), 3)*.6) < 1e-6

# The look, with hand-checked values (a grey key (100,100,100), a saturated one).
grey, red = c((100, 100, 100)), c((200, 100, 50))
look = {
    'sun x0.4': bl.storm_color(9, grey) == c((40, 40, 40)), 'halo x0.3': bl.storm_color(10, grey) == c((30, 30, 30)),
    'shadow x0.5': bl.storm_color(8, grey) == c((50, 50, 50)),
    'direct: x0.6, 50% desaturated': rgb(bl.storm_color(0, red)) == [round(v*.6) for v in bl.mix([200, 100, 50], [bl.lum([200, 100, 50])]*3, .5)],
    'ambient: 25% desaturated, x1.15': rgb(bl.storm_color(1, red)) == [round(v*1.15) for v in bl.mix([200, 100, 50], [bl.lum([200, 100, 50])]*3, .25)],
    'clouds: the warm grey at the key luminance, x0.9': abs(bl.lum(rgb(bl.storm_color(12, red)))-bl.lum([200, 100, 50])*.9) < 3 and rgb(bl.storm_color(12, red))[0] > rgb(bl.storm_color(12, red))[2],
    'sky: luminance kept by the grey, then x0.95': abs(bl.lum(rgb(bl.storm_color(3, red)))-bl.lum([200, 100, 50])*.95) < 6,
    'sky: light warm grey on a neutral key (R >= G >= B, spread under 25%)': (lambda k: k[0] >= k[1] >= k[2] and (k[0]-k[2]) < .25*k[0])(rgb(bl.storm_color(3, grey))),
    'fog colour: 55% to the warm grey, luminance kept (x1.0)': abs(bl.lum(rgb(bl.storm_color(7, red)))-bl.lum([200, 100, 50])) < 3,
    'alpha byte kept': bl.storm_color(3, red) >> 24 == 0xff,
    'channels 11 and 13-17 are not touched': all(bl.storm_color(ch, red) == red for ch in (11, 13, 14, 15, 16, 17)),
}
checks.update({f'storm_color: {k}': v for k, v in look.items()})

# Rows 2 / 4: the stock storm profile relit, retimed and stormed; its sky unless the clear's stock sky is the same.
s2 = ids[2]
checks['row 2: same stock sky as the clear: the final clear\'s skybox'] = u(P(s2), 2) == SKY_SKYSTEP
checks['row 3: a different stock storm sky is kept'] = u(P(ids[3]), 2) == SKY_STORM
checks['row 2: relit then stormed (direct light, first night key)'] = u(Int(s2, 0), 18) == bl.storm_color(0, bl.transform_color(0, u(stock['LightIntBand'].index[(2-1)*18+1], 18), 0, 5, bl.sample_color(stock['LightIntBand'].index[(2-1)*18+10], 0)))
checks['row 2: retimed (night by 21:00 in the colour bands, sunset at 20:15)'] = [u(Int(s2, 0), 2+i) for i in range(u(Int(s2, 0), 1))][-2:] == [bl.SUNSET_KEY, bl.NIGHT_KEY]
fog = [(u(Flt(s2, 0), 2+i), f(Flt(s2, 0), 18+i)) for i in range(u(Flt(s2, 0), 1))]
checks['row 2: fog end: 2000 (below 3600) kept, the relit 4000 below the 10800 floor stays'] = {v for _, v in fog if v == 2000} == {2000} and all(2000 == v or 4000*.94-1 <= v <= 4000*1.1+1 for _, v in fog)
checks['row 2: fog start ratio kept from the relit source (stock .3 x0.88), not capped'] = all(abs(f(Flt(s2, 1), 18+i)-.3*.88) < 1e-6 for i in range(u(Flt(s2, 1), 1)))
checks['row 2: water channels are the relit stock ones, not stormed'] = all(u(Int(s2, ch), 18) == bl.transform_color(ch, u(stock['LightIntBand'].index[(2-1)*18+ch+1], 18), 0, 5, None) for ch in bl.STORM_WATER)

# Fog: never thicker than the clear's, key-wise; the floor.
for i in (1, 2, 3, 4):
    pid, clear = ids[i], u(L[i], 7)
    if clear == 1: continue
    end, clear_end = Flt(pid, 0), Flt(clear, 0)
    checks[f'row {i}: storm fog end <= clear fog end at every storm key'] = all(f(end, 18+k) <= bl.band_value(bl.band_pairs(clear_end, True), True, u(end, 2+k))+1e-3 for k in range(u(end, 1)))
checks['fog end floor: x0.7 never below 300 yards = 10800 units (>= 3600 only)'] = (bl.STORM_FOG_END_FLOOR, bl.STORM_FOG_END_FLOOR_YARDS, bl.STORM_FOG_END_SCALE) == (10800., 300., .7)
fogs = copy.deepcopy(final)
probe = fogs['LightFloatBand'].index[(ids[1]-1)*6+1]   # one value per key; the first five keys carry the probes
for k, v in enumerate((12000., 6000., 36000., 3000., 3600.)): bl.putf(probe, 18+k, v)
bl.storm_profile(fogs, ids[1]); fend = fogs['LightFloatBand'].index[(ids[1]-1)*6+1]
checks['fog end: 12000 -> 10800 (floor), 6000 kept (never raised), 36000 -> 25200 (x0.7), <3600 untouched, 3600 -> 3600'] = [round(f(fend, 18+k), 1) for k in range(5)] == [10800., 6000., 25200., 3000., 3600.]

# Glow: only 0 < glow <= 1 is scaled x0.6 (as relit_profile); anything else stays.
for g, want_g in ((.4, .24), (1., .6), (0., 0.), (2.5, 2.5), (-1., -1.)):
    gl = copy.deepcopy(final); bl.putf(gl['LightParams'].index[ids[1]], 3, g); bl.storm_profile(gl, ids[1])
    checks[f'glow {g}: {want_g}'] = abs(f(gl['LightParams'].index[ids[1]], 3)-want_g) < 1e-6

# Key limit and invariants over every new band.
checks['<= 16 keys, increasing times in every new band'] = all(0 <= u(r, 1) <= 16 and all(u(r, 2+k) < u(r, 3+k) for k in range(u(r, 1)-1)) for n, t in after.items() if n.endswith('Band') for i, b in t.items() if i not in before[n] for r in [bytearray(b)])
sixteen = [(0, N)] + [(h*H, D) for h in range(1, 6)] + [(h*H, Y) for h in range(6, 15)] + [(21*H, S)]
st16 = stock_tables(sixteen)
f16, _ = final_tables(st16)
bl.stormify(f16, st16)
checks['a 16-key stock storm band stays within 16 keys (retime insert skipped)'] = len(sixteen) == 16 and all(u(r, 1) <= 16 for r in f16['LightIntBand'].rows+f16['LightFloatBand'].rows)

# The tables serialise.
checks['round trip: the built tables parse again'] = all(DBC(t.bytes()).index.keys() == t.index.keys() for t in final.values())

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values()), [n for n, ok in checks.items() if not ok]
out = fp.output_dir(); out.mkdir(parents=True, exist_ok=True)
(out / 'storm-bands-validation.json').write_text(json.dumps({'checks': checks, 'report': {k: v for k, v in report.items() if k != 'retime'}, 'game_launched': False}, indent=2) + '\n')
print('PASS storm bands: private per-row storm profiles, grouping, skybox rule, storm look, untouched slots and water')
