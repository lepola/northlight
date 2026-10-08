#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""build_weather_textures (0.3.198, rain): the generated rain, red rain and snow flake BLPs; no client or GPU.

Uncompressed BLP2 (encoding 3, 8-bit alpha) with a full mip chain, rain 32x512 (1:16) and snow
32x64 (1:2), the aspect ratios the renderer recognises at CreateTexture. Deterministic. The moon
builder's transparent moon02, now written through the shared BLP writer, is byte-identical to before.
Writes weather-textures-validation.json."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib
import json
import struct
import build_outdoor_single_moon
import build_weather_textures as wt
from world_scene_builder import decode_blp

textures = wt.weather_textures()
checks, info = {}, {}
MEAN0 = (.034, .046)   # rain alpha .8, core sigma .62 px on texel 16 (0.3.199 game tests: alpha .45 -> .35 -> .55 -> .75 -> 1 -> .8; core 1.1 -> .9 -> .75 -> .62 px)
checks['three textures, at the client\'s paths'] = set(textures) == {'textures\\Weather\\RainDrop01.blp', 'Textures\\WEATHER\\RAINDROPRED01.BLP', 'textures\\Weather\\SnowFlake01.blp'}
# Palette layout: 256 BGRA entries, entry 0 = the colour exactly, the rest zero; per mip all-zero indices then the alpha plane.
rain = textures['textures\\Weather\\RainDrop01.blp']
checks['rain palette: entry 0 = (230,230,230,255), 255 other entries zero'] = rain[148:152] == bytes((230, 230, 230, 255)) and not any(rain[152:148+1024])
checks['rain mip 0: indices all 0, then the alpha plane (decodes back to it)'] = (
    not any(rain[148+1024:148+1024+32*512]) and list(decode_blp(rain, 512)[2][3::4]) == list(rain[148+1024+32*512:148+1024+2*32*512]))
checks['deterministic: a second run gives the same bytes'] = wt.weather_textures() == textures

want = {'RainDrop01': (32, 512, .8), 'RAINDROPRED01': (32, 512, .8), 'SnowFlake01': (32, 64, .8)}
for name, data in textures.items():
    key = next(k for k in want if k.lower() in name.lower())
    w, h, alpha = want[key]
    header = struct.unpack_from('<4sI4B2I', data)
    offsets, sizes = struct.unpack_from('<16I', data, 20), struct.unpack_from('<16I', data, 84)
    levels = [(max(1, w >> i), max(1, h >> i)) for i in range(max(w, h).bit_length())]
    checks[f'{key}: BLP2, encoding 1 (palettized), alpha depth 8, alpha type 8, mips, {w}x{h}'] = header == (b'BLP2', 1, 1, 8, 8, 1, w, h)
    checks[f'{key}: full mip chain to 1x1 ({len(levels)} levels), contiguous'] = (
        [s for s in sizes if s] == [a*b*2 for a, b in levels] and offsets[0] == 148+1024 and all(offsets[i+1] == offsets[i]+sizes[i] for i in range(len(levels)-1))
        and len(data) == 148+1024+sum(sizes) and not any(offsets[len(levels):]))
    checks[f'{key}: aspect {h//w}:1 and width <= 32'] = h == (16 if h == 512 else 2)*w and w <= 32
    pixels = decode_blp(data, 4096)[2]
    alphas = pixels[3::4]
    peak = max(alphas)
    checks[f'{key}: alpha peak {alpha}'] = peak == round(alpha*255)
    checks[f'{key}: decodes at every level'] = all(decode_blp(data, max(lw, lh))[:2] == (lw, lh) for lw, lh in levels[::3])
    info[key] = {'bytes': len(data), 'levels': len(levels), 'alpha_peak': peak}
    if h == 512:   # streak: centred, thin, fading at both ends
        row = lambda y: [alphas[y*w+x] for x in range(w)]
        mid = row(h//2)
        checks[f'{key}: streak is centred on texel 16, a thin crisp core ~1.3 px wide'] = mid.index(max(mid)) == 16 and sum(a > peak//2 for a in mid) == 1 and mid[15] == mid[17] and mid[15] > peak//4 and mid[0] == mid[-1] == 0 and mid[16] > mid[18] > mid[20]
        # every mip keeps one continuous streak: no gap along it, the peak never thins out, the far mean alpha stays near the stock texture's ~0.21
        chain = [decode_blp(data, max(lw, lh)) for lw, lh in levels]
        peaks = [max(px[3::4]) for _, _, px in chain]
        checks[f'{key}: every mip level has alpha in every row but the faded ends (continuous streak, no dots)'] = all(
            all(max(px[3::4][y*lw:(y+1)*lw]) > 0 for y in range(lh//10, lh-lh//10)) for lw, lh, px in chain)
        checks[f'{key}: mip peak never below 24/255 at any level, never above level 0'] = min(peaks) >= 24 and max(peaks) == peaks[0]
        checks[f'{key}: mean alpha 0.034-0.046 at level 0, 0.22-0.29 at the small mips (stock far mips ~0.21), full peak down to 4 texels wide'] = MEAN0[0] <= sum(chain[0][2][3::4])/(255*w*h) <= MEAN0[1] and all(.22 <= sum(px[3::4])/(255*lw*lh) <= .29 for lw, lh, px in chain[-3:]) and all(max(px[3::4]) == peak for lw, lh, px in chain if lw >= 4)
        checks[f'{key}: ends fade out'] = max(row(0)) == 0 and max(row(h-1)) == 0 and max(row(h//40)) < peak//2 and max(row(h-h//40)) < peak//2 and row(h//8)[16] >= peak-1 and row(h-h//8)[16] >= peak-1
    else:          # flake: round in uv (a 1:2 squash in texels), centred, transparent corners and rim
        cx, cy = w//2, h//2
        checks[f'{key}: flake is centred on the texture'] = alphas[cy*w+cx] >= peak-2 and alphas[(cy-1)*w+cx-1] >= peak-4
        checks[f'{key}: flake is 1:2 in texels (round on a square quad)'] = (
            sum(alphas[cy*w+x] > peak//4 for x in range(w))*1.8 < sum(alphas[y*w+cx] > peak//4 for y in range(h)) < sum(alphas[cy*w+x] > peak//4 for x in range(w))*2.2)
        checks[f'{key}: corners and edges transparent'] = all(alphas[i] == 0 for i in (0, w-1, (h-1)*w, h*w-1)) and not any(alphas[y*w] for y in range(h))
    colour = {tuple(pixels[i*4:i*4+3]) for i in range(0, w*h) if pixels[i*4+3]}
    checks[f'{key}: one colour at every visible texel'] = len(colour) <= 1 + 2 and len({c for c in colour if all(abs(a-b) <= 1 for a, b in zip(c, sorted(colour)[len(colour)//2]))}) == len(colour)
    info[key]['colour'] = sorted(colour)[len(colour)//2]
checks['rain is near-white neutral (230,230,230), red rain red, snow neutral white'] = info['RainDrop01']['colour'] == (230, 230, 230) and info['RAINDROPRED01']['colour'] == (191, 56, 46) and len(set(info['SnowFlake01']['colour'])) == 1

moon = build_outdoor_single_moon.transparent_moon()
checks['moon02: byte-identical to the pre-refactor writer (sha256, 64x64 x7 levels)'] = (
    len(moon) == 21992 and hashlib.sha256(moon).hexdigest() == '852ac7ee4fc439442a09c6f48565a6bf6448b12fcc80aac5261395706c80735f')
checks['moon02: still fully transparent'] = not any(decode_blp(moon, 64)[2])

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values()), [n for n, ok in checks.items() if not ok]
out = fp.output_dir(); out.mkdir(parents=True, exist_ok=True)
(out / 'weather-textures-validation.json').write_text(json.dumps({'checks': checks, 'textures': info, 'game_launched': False}, indent=2) + '\n')
print('PASS weather textures: BLP2 encoding 3, mips, aspect signatures, deterministic, moon unchanged')
