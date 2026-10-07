"""Build a 3.3.5a data-only outdoor relighting patch, based on this HD client.

Schemas: WoWDBDefs; band meanings/indexing: Noggit RED Sky.h/Sky.cpp.
All existing rows are preserved. New profiles are referenced only by outdoor
clear-weather Light rows, so shared dungeon/underwater/weather profiles retain
their original data. This is authored legacy lighting, not global illumination.
"""
import copy
from collections import Counter
import hashlib
import json
import math
import struct
import tempfile
from pathlib import Path
from mpq import Archive, ROOT

SOURCE = ROOT / 'inspection/patch-x.mpq'
OUTPUT = ROOT / 'build/lighting'
MAPS = {0: 'Eastern Kingdoms', 1: 'Kalimdor', 530: 'Outland', 571: 'Northrend'}
TABLES = ('Light', 'LightParams', 'LightIntBand', 'LightFloatBand')

class Skip(Exception):
    """An input lacks what an HD-specific step needs (a sky id, profile or texture): skip that step."""

def require(condition, reason):
    if not condition:
        raise Skip(reason)

class DBC:
    def __init__(self, data):
        magic, count, self.fields, self.size, strings = struct.unpack_from('<4s4I', data)
        assert magic == b'WDBC' and self.size == self.fields*4
        assert len(data) == 20+count*self.size+strings
        self.rows = [bytearray(data[20+i*self.size:20+(i+1)*self.size]) for i in range(count)]
        self.strings = data[20+count*self.size:]
        self.index = {u(r, 0): r for r in self.rows}
        assert len(self.index) == count

    def add(self, row):
        assert len(row) == self.size and u(row,0) not in self.index
        self.rows.append(row)
        self.index[u(row,0)] = row

    def bytes(self):
        return struct.pack('<4s4I', b'WDBC', len(self.rows), self.fields, self.size,
                           len(self.strings)) + b''.join(self.rows) + self.strings

def u(r, col): return struct.unpack_from('<I', r, col*4)[0]
def f(r, col): return struct.unpack_from('<f', r, col*4)[0]
def putu(r, col, value): struct.pack_into('<I', r, col*4, value)
def putf(r, col, value):
    assert math.isfinite(value)
    struct.pack_into('<f', r, col*4, value)
def clamp(x, lo, hi): return max(lo, min(x, hi))
def rgb(v): return [(v>>16)&255, (v>>8)&255, v&255]
def pack(c, original):
    r,g,b = [round(clamp(x, 0, 255)) for x in c]
    return (original & 0xff000000) | (r<<16) | (g<<8) | b
def mix(a,b,t): return [x*(1-t)+y*t for x,y in zip(a,b)]
def lum(c): return c[0]*.2126+c[1]*.7152+c[2]*.0722

def sample_color(row, time):
    n = u(row,1)
    if not n: return None
    points = sorted((u(row,2+i), rgb(u(row,18+i))) for i in range(n))
    if n == 1: return points[0][1]
    points = [(points[-1][0]-2880, points[-1][1])] + points + [(points[0][0]+2880, points[0][1])]
    for (t0,c0),(t1,c1) in zip(points,points[1:]):
        if t0 <= time <= t1:
            return mix(c0,c1,(time-t0)/max(1,t1-t0))
    raise ValueError('Invalid time band')

def daylight(time):
    # Existing WoW key times use half-minutes. Smooth transitions avoid jumps.
    return clamp((math.cos((time-1560)*2*math.pi/2880)+.2)/.9,0,1)

def sort_band(row):
    # Some input bands are unsorted. Keep each time/value pair together.
    pairs=sorted((u(row,2+i),u(row,18+i)) for i in range(u(row,1)))
    for i,(time,value) in enumerate(pairs):
        putu(row,2+i,time); putu(row,18+i,value)

def transform_color(ch, original, time, count, sun):
    c = rgb(original)
    day = daylight(time) if count > 1 else 1.0
    if ch == 0:  # Diffuse/key light: less monochromatic HD sunlight.
        if sun and lum(sun)>0:
            target = [v*lum(c)/lum(sun) for v in sun]
            c = mix(c,target,.28*day)
        c = [v*(1.04+.08*day) for v in c]
    elif ch == 1:  # Ambient fill: stronger shape, still readable at night.
        c = [v*(.82-.04*day) for v in c]
        if lum(c)>0 and day<1:
            cool = [0.85,0.97,1.10]
            c = [v*(day+(1-day)*k) for v,k in zip(c,cool)]
    elif ch in (2,3,4):
        c = [v*(.90+.06*day) for v in c]
    elif ch == 7:  # Preserve each zone's fog hue.
        c = [v*(.90+.05*day) for v in c]
    elif ch == 8:  # Existing terrain shadow mask opacity, NOT a new shadow map.
        c = [clamp(v*(1.05+.23*day),0,160) for v in c]
    elif ch == 10:
        c = [v*.92 for v in c]
    elif ch in (14,16):
        c = [v*.95 for v in c]
    elif ch in (15,17):
        c = [v*.80 for v in c]
    return pack(c,original)

def profile_complete(tables, pid):
    light, params, ints, floats = [tables[n] for n in TABLES]
    return pid in params.index and all((pid-1)*18+c+1 in ints.index for c in range(18)) and all((pid-1)*6+c+1 in floats.index for c in range(6))

def relit_profile(source, tables, old, new):
    """Add profile `new` to `tables`: `source`'s profile `old` with the relight colour and fog transform; returns the changed key count."""
    params, ints, floats = tables['LightParams'], tables['LightIntBand'], tables['LightFloatBand']
    row = copy.copy(source['LightParams'].index[old]); putu(row,0,new)
    # Preserve skybox references and opaque flags, including this pack's
    # nonstandard last field. Tune only documented opacity/glow floats.
    glow=f(row,3)
    if 0<glow<=1: putf(row,3,min(glow*.8,.28))
    for col in (4,6):
        value=f(row,col)
        if 0<value<=1: putf(row,col,max(.12,value*.78))
    for col in (5,7):
        value=f(row,col)
        if 0<value<=1: putf(row,col,min(.92,value+.08))
    params.add(row)
    modifications=0
    sun=source['LightIntBand'].index[(old-1)*18+10]
    for ch in range(18):
        row=copy.copy(source['LightIntBand'].index[(old-1)*18+ch+1]); putu(row,0,(new-1)*18+ch+1)
        for i in range(u(row,1)):
            t=u(row,2+i); before=u(row,18+i)
            after=transform_color(ch,before,t,u(row,1),sample_color(sun,t))
            putu(row,18+i,after); modifications += before!=after
        sort_band(row); ints.add(row)
    for ch in range(6):
        row=copy.copy(source['LightFloatBand'].index[(old-1)*6+ch+1]); putu(row,0,(new-1)*6+ch+1)
        for i in range(u(row,1)):
            value=f(row,18+i); day=daylight(u(row,2+i)) if u(row,1)>1 else 1.0
            if ch==0 and value>=3600: # >=100 yards; retain tight local fog volumes.
                putf(row,18+i,value*(.94+.16*day))
            elif ch==1 and 0<value<1:
                putf(row,18+i,clamp(value*.88,0,.95))
        sort_band(row); floats.add(row)
    return modifications

def relight(tables):
    """The outdoor relighting, in place on {name: DBC} for TABLES; returns (changes, skipped, zones)."""
    light, params, ints, floats = [tables[n] for n in TABLES]
    profile_ids = sorted({u(r,7) for r in light.rows if u(r,1) in MAPS and u(r,7)})
    # Reserve beyond ALL existing band IDs, including unused/incomplete profiles.
    next_id = max(max(params.index), (max(ints.index)+17)//18, (max(floats.index)+5)//6)+1
    mapping, changes, skipped = {}, {}, []
    for old in profile_ids:
        if old not in params.index or any((old-1)*18+c+1 not in ints.index for c in range(18)) or any((old-1)*6+c+1 not in floats.index for c in range(6)):
            skipped.append(old); continue
        new = next_id; next_id += 1; mapping[old] = new
        modifications = relit_profile(tables, tables, old, new)
        changes[old]={'new':new,'color_keys_changed':modifications}
    zones={name:0 for name in MAPS.values()}
    for row in light.rows:
        if u(row,1) in MAPS and u(row,7) in mapping:
            putu(row,7,mapping[u(row,7)]); zones[MAPS[u(row,1)]]+=1
    return changes, skipped, zones

# Retime (0.3.178): the native bands of relight's private clear-weather profiles, moved onto
# Northlight's sun and moon. Times are half-minutes (0-2879). Only times move; the (relit) values
# stay, so the colour of every key is kept and only when it is reached changes.
SUNSET_KEY, NIGHT_KEY, DAWN_KEY = 2430, 2520, 540   # 20:15 sunset colour, 21:00 night, 04:30 dawn start
INSERT_LIMIT = 11   # Mulgore and Stormwind add up to 5 keys afterwards, under the 16-key limit

def band_pairs(row, floating):
    return [(u(row,2+i), f(row,18+i) if floating else u(row,18+i)) for i in range(u(row,1))]

def band_value(pairs, floating, time):
    """The band's (interpolated, wrapping) value at `time`: rgb for ints, a float for floats."""
    points = [(t, v if floating else rgb(v)) for t, v in pairs]
    if len(points) == 1: return points[0][1]
    points = [(points[-1][0]-2880, points[-1][1])] + points + [(points[0][0]+2880, points[0][1])]
    for (t0,a),(t1,b) in zip(points, points[1:]):
        if t0 <= time <= t1:
            w = (time-t0)/max(1, t1-t0)
            return a+(b-a)*w if floating else mix(a, b, w)
    raise ValueError('Invalid time band')

def band_difference(a, b, floating):
    return abs(a-b) if floating else max(abs(x-y) for x, y in zip(a, b))

def retime_plan(row, floating):
    """(new pairs, reason): reason is None for a retimed band, else why it is left byte-identical."""
    pairs = band_pairs(row, floating)
    times = [t for t, _ in pairs]
    if len(set(times)) < 3: return None, 'fewer than 3 key times'
    night = band_value(pairs, floating, 0)
    value = lambda v: v if floating else rgb(v)
    spread = max(band_difference(value(v), night, floating) for _, v in pairs)
    tolerance = max(1e-3, .1*spread) if floating else max(3, .1*spread)
    if spread <= (1e-3 if floating else 3): return None, 'constant'
    dark = lambda v: band_difference(v, night, floating) <= tolerance
    # Evening: N_old is the first key from 18:00 on after which every key is night; S_old the key before.
    night_start = next((t for i, (t, _) in enumerate(pairs) if t >= 2160 and all(dark(value(v)) for _, v in pairs[i:])), 2880)
    before = [(t, v) for t, v in pairs if t < night_start]
    if not before or before[-1][0] < 1920 or dark(value(before[-1][1])): return None, 'evening shape not recognised'
    sunset = before[-1][0]
    # Morning: M_old is the last key before 06:00 that ends an all-night run from 00:00.
    dawn = 0
    for t, v in pairs:
        if t >= 720 or not dark(value(v)): break
        dawn = t
    if dark(band_value(pairs, floating, 720)): return None, 'morning shape not recognised'
    def move(t):
        if dawn < DAWN_KEY and t <= 720:
            if t < dawn: return t*DAWN_KEY/dawn
            if t == dawn: return DAWN_KEY if dawn else 0   # dawn 0: 00:00 stays, a night key is inserted at 04:30
            return DAWN_KEY+(t-dawn)*(720-DAWN_KEY)/(720-dawn)
        if t <= 1440: return t
        if t <= sunset: return 1440+(t-1440)*(SUNSET_KEY-1440)/(sunset-1440)
        if t <= night_start: return SUNSET_KEY+(t-sunset)*(NIGHT_KEY-SUNSET_KEY)/(night_start-sunset)
        return NIGHT_KEY+(t-night_start)*(2880-NIGHT_KEY)/(2880-night_start)
    moved = [(round(move(t)), v) for t, v in pairs]
    # The night value, as a key: the stored 00:00 key when there is one (keeps an int band's alpha byte).
    key = next((v for t, v in pairs if t == 0), night if floating else pack(night, pairs[0][1]))
    inserts, skipped = [], []
    for t, needed in ((NIGHT_KEY, night_start == 2880), (DAWN_KEY, dawn == 0)):
        if needed: (inserts if len(moved)+len(inserts) < INSERT_LIMIT else skipped).append(t)
    result = sorted(moved+[(t, key) for t in inserts], key=lambda p: p[0])
    if any(a >= b for (a, _), (b, _) in zip(result, result[1:])) or result[-1][0] >= 2880: return None, 'rounding would merge keys'
    return result, ('inserts skipped (key limit): ' + ','.join(map(str, skipped))) if skipped else None

def retime(tables, profiles):
    """Retime every band of `profiles` (relight's new profile ids) in place; returns the report."""
    report = {'bands': 0, 'retimed': 0, 'inserted_keys': 0, 'insert_skipped': [], 'unchanged': {}, 'templates': {}, 'rows': {}}
    for name, floating, channels in (('LightIntBand', False, 18), ('LightFloatBand', True, 6)):
        table = tables[name]
        for profile in sorted(profiles):
            for ch in range(channels):
                row = table.index[(profile-1)*channels+ch+1]
                before = band_pairs(row, floating)
                template = ' '.join(f'{t//120:02d}' + (f':{t%120//2:02d}' if t % 120 else '') for t, _ in before)
                result, reason = retime_plan(row, floating)
                report['bands'] += 1
                entry = report['templates'].setdefault(template, {'bands': 0})
                entry['bands'] += 1
                if result is None:
                    report['unchanged'][reason] = report['unchanged'].get(reason, 0)+1
                    entry[reason] = entry.get(reason, 0)+1
                    continue
                if reason: report['insert_skipped'].append({'table': name, 'id': u(row,0), 'template': template})
                # Invariants: the original values survive as a multiset, inserts copy the night value,
                # times are unique, increasing and in range, and 06:00-12:00 is untouched.
                extra = Counter(v for _, v in result); extra.subtract(v for _, v in before)
                assert all(n >= 0 for n in extra.values()) and set(extra.elements()) <= {v for t, v in result if t in (NIGHT_KEY, DAWN_KEY)}
                assert all(b > a for (a, _), (b, _) in zip(result, result[1:])) and 0 <= result[0][0] and result[-1][0] < 2880
                assert len(result) <= 16 and (len(result) == len(before) or len(result) <= INSERT_LIMIT)
                assert [p for p in result if 720 <= p[0] <= 1440] == [p for p in before if 720 <= p[0] <= 1440]
                putu(row, 1, len(result))
                for i in range(16):
                    putu(row, 2+i, result[i][0] if i < len(result) else 0)
                    if floating: putf(row, 18+i, result[i][1] if i < len(result) else 0.)
                    else: putu(row, 18+i, result[i][1] if i < len(result) else 0)
                report['retimed'] += 1
                report['inserted_keys'] += len(result)-len(before)
                entry['retimed'] = entry.get('retimed', 0)+1
                report['rows'][f'{name}:{u(row,0)}'] = {'before': [t for t, _ in before], 'after': [t for t, _ in result]}
    return report

# 0.3.198 (rain): a private storm profile (Light.dbc column 9) for every outdoor row, so the
# "Forever-style" rain has a dark, grey, short-fog sky of its own. Only the storm slot changes; the
# clear, underwater and storm-underwater slots (7, 8, 10) stay as they are.
STORM_SLOT = 9
STORM_FOG_END_SCALE, STORM_FOG_END_FLOOR = .40, 220.   # fog end x.40 (like relight, only >= 3600), never below 220
STORM_FOG_START_MAX = .15                                # fog start ratio cap
STORM_DIRECT_SCALE, STORM_DIRECT_DESAT = .55, .50        # ch0 direct light
STORM_AMBIENT_SCALE, STORM_AMBIENT_DESAT = 1.05, .40     # ch1 ambient
STORM_SKY_LERP, STORM_SKY_SCALE = .70, .60               # ch2-6 sky toward grey-blue, then darker
STORM_FOG_LERP = .70                                     # ch7 fog colour toward the same grey
STORM_GREY = (.42, .45, .50)                             # grey-blue chroma; scaled to keep each key's luminance
STORM_SHADOW_SCALE = .5                                  # ch8 terrain shadow opacity
STORM_SUN_SCALE, STORM_HALO_SCALE = .35, .25             # ch9 sun, ch10 halo
STORM_CLOUD_SCALE = .70                                  # ch12 clouds: grey, then darker
STORM_GLOW_SCALE = .5                                    # LightParams column 3
STORM_WATER = range(14, 18)                              # untouched

def storm_color(ch, original):
    c = rgb(original)
    L = lum(c)
    grey = [L*k/lum(STORM_GREY) for k in STORM_GREY]
    if ch == 0: c = [v*STORM_DIRECT_SCALE for v in mix(c, [L]*3, STORM_DIRECT_DESAT)]
    elif ch == 1: c = [v*STORM_AMBIENT_SCALE for v in mix(c, [L]*3, STORM_AMBIENT_DESAT)]
    elif 2 <= ch <= 6: c = [v*STORM_SKY_SCALE for v in mix(c, grey, STORM_SKY_LERP)]
    elif ch == 7: c = mix(c, grey, STORM_FOG_LERP)
    elif ch == 8: c = [v*STORM_SHADOW_SCALE for v in c]
    elif ch == 9: c = [v*STORM_SUN_SCALE for v in c]
    elif ch == 10: c = [v*STORM_HALO_SCALE for v in c]
    elif ch == 12: c = [v*STORM_CLOUD_SCALE for v in [L]*3]
    return pack(c, original)

def storm_profile(tables, pid):
    """The storm look, in place on profile `pid` (a private copy): colour and fog bands, glow."""
    params, ints, floats = tables['LightParams'], tables['LightIntBand'], tables['LightFloatBand']
    glow = f(params.index[pid], 3)
    putf(params.index[pid], 3, glow*STORM_GLOW_SCALE)
    for ch in range(18):
        if ch in STORM_WATER: continue
        row = ints.index[(pid-1)*18+ch+1]
        for i in range(u(row, 1)): putu(row, 18+i, storm_color(ch, u(row, 18+i)))
    for ch in (0, 1):
        row = floats.index[(pid-1)*6+ch+1]
        for i in range(u(row, 1)):
            value = f(row, 18+i)
            if ch == 0 and value >= 3600: putf(row, 18+i, max(value*STORM_FOG_END_SCALE, STORM_FOG_END_FLOOR))
            elif ch == 1: putf(row, 18+i, min(value, STORM_FOG_START_MAX))

def copy_profile(tables, old, new):
    """Add profile `new` to `tables` as a plain copy of `old`."""
    params, ints, floats = tables['LightParams'], tables['LightIntBand'], tables['LightFloatBand']
    row = copy.copy(params.index[old]); putu(row, 0, new); params.add(row)
    for table, channels in ((ints, 18), (floats, 6)):
        for ch in range(channels):
            row = copy.copy(table.index[(old-1)*channels+ch+1]); putu(row, 0, (new-1)*channels+ch+1); table.add(row)

def stormify(tables, stock):
    """Private storm profiles for the outdoor rows, in place on the final `tables` (after relight, retime and
    the sky steps); `stock` is the same tables as the client's own chain has them. Returns the report."""
    light, params, ints, floats = [tables[n] for n in TABLES]
    stock_rows = stock['Light'].index
    # Above every existing id, also a Light slot's dangling one.
    first = next_id = max(max(params.index), (max(ints.index)+17)//18, (max(floats.index)+5)//6, max(u(r, c) for r in light.rows for c in range(7, 15)))+1
    groups, skipped, rows = {}, [], []
    for row in light.rows:
        if u(row, 1) not in MAPS: continue
        clear = u(row, 7)
        if not clear or not profile_complete(tables, clear): skipped.append(u(row, 0)); continue
        before = stock_rows.get(u(row, 0))
        old_clear, old_storm = (u(before, 7), u(before, STORM_SLOT)) if before is not None else (clear, clear)
        if old_storm == old_clear or not profile_complete(stock, old_storm):
            kind, source, sky = 'clear', clear, u(params.index[clear], 2)
        else:
            kind, source = 'stock', old_storm
            same = old_clear in stock['LightParams'].index and u(stock['LightParams'].index[old_storm], 2) == u(stock['LightParams'].index[old_clear], 2)
            sky = u(params.index[clear], 2) if same else u(stock['LightParams'].index[old_storm], 2)
        groups.setdefault((kind, source, sky), []).append(row)
    made = {}
    for key, members in groups.items():
        kind, source, sky = key
        made[key] = next_id; next_id += 1
        if kind == 'clear': copy_profile(tables, source, made[key])
        else: relit_profile(stock, tables, source, made[key])
        putu(params.index[made[key]], 2, sky)
    stock_made = [made[k] for k in made if k[0] == 'stock']
    retimed = retime(tables, stock_made) if stock_made else None
    clamped = 0
    for key, pid in made.items():
        water = {ch: bytes(ints.index[(pid-1)*18+ch+1])[4:] for ch in STORM_WATER}
        storm_profile(tables, pid)
        assert all(bytes(ints.index[(pid-1)*18+ch+1])[4:] == v for ch, v in water.items())
        # Never thicker than the clear fog: key-wise at the storm's key times, against every row of the group.
        end, clears = floats.index[(pid-1)*6+1], [floats.index[(u(r, 7)-1)*6+1] for r in groups[key]]
        for i in range(u(end, 1)):
            limit = min(band_value(band_pairs(c, True), True, u(end, 2+i)) for c in clears)
            if f(end, 18+i) > limit: putf(end, 18+i, limit); clamped += 1
    for key, members in groups.items():
        for row in members: putu(row, STORM_SLOT, made[key])
    # Validation: private, grouped, not shared with any other slot, short fog, <= 16 keys, water untouched.
    others = {u(r, c) for r in light.rows for c in range(7, 15) if c != STORM_SLOT}
    for key, members in groups.items():
        pid = made[key]
        assert pid >= first and pid not in others and pid not in {u(r, STORM_SLOT) for r in light.rows if r not in members}
        end = floats.index[(pid-1)*6+1]
        for r in members:
            clear_end = floats.index[(u(r, 7)-1)*6+1]
            assert u(r, STORM_SLOT) == pid
            for i in range(u(end, 1)):
                assert f(end, 18+i) <= band_value(band_pairs(clear_end, True), True, u(end, 2+i))+1e-3, (u(r, 0), pid)
        for table, n in ((ints, 18), (floats, 6)):
            assert all(0 <= u(table.index[(pid-1)*n+c+1], 1) <= 16 for c in range(n))
    return {'profiles': len(made), 'rows': sum(map(len, groups.values())), 'rows_skipped': skipped, 'first_id': first,
            'from_clear': sum(k[0] == 'clear' for k in made), 'from_stock': len(stock_made), 'fog_keys_clamped_to_clear': clamped,
            'retime': {k: v for k, v in (retimed or {}).items() if k != 'rows'}}

def build():
    tables = {n: DBC((SOURCE/(n+'.dbc')).read_bytes()) for n in TABLES}
    original_rows = {n:len(t.rows) for n,t in tables.items()}
    changes, skipped, zones = relight(tables)
    OUTPUT.mkdir(parents=True,exist_ok=True)
    report={'source':'data/patch-x.mpq (identical to enUS sky patch)',
            'profiles':changes,'skipped_incomplete_profiles':skipped,
            'light_volumes':zones,'tables':{},'runtime_validated':False}
    for name,table in tables.items():
        data=table.bytes(); DBC(data)
        path=OUTPUT/(name+'.dbc');path.write_bytes(data)
        report['tables'][name]={'old_rows':original_rows[name],'new_rows':len(table.rows),
                               'sha256':hashlib.sha256(data).hexdigest()}
    (OUTPUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    archive_path=ROOT/'build/patch-z.mpq'
    with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
        staging=Path(temp)/'patch-z.mpq'
        with Archive(staging,create=True,capacity=16) as archive:
            for name in tables: archive.add(OUTPUT/(name+'.dbc'),'DBFilesClient\\'+name+'.dbc')
        staging.replace(archive_path)
    with Archive(archive_path) as archive:
        for name in tables:
            assert archive.read('DBFilesClient\\'+name+'.dbc')==(OUTPUT/(name+'.dbc')).read_bytes()
    print(json.dumps({'profiles':len(changes),'light_volumes':zones,'tables':report['tables'],'skipped':skipped},indent=2))

if __name__=='__main__': build()
