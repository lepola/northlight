#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""build_weather_textures (0.3.198, rain): the generated rain, red rain and snow flake BLPs; no client or GPU.

Uncompressed BLP2 (encoding 3, 8-bit alpha) with a full mip chain, rain 32x512 (1:16) and snow
32x128 (1:4), the aspect ratios the renderer recognises at CreateTexture. Deterministic. The moon
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
checks['three textures, at the client\'s paths'] = set(textures) == {'textures\\Weather\\RainDrop01.blp', 'Textures\\WEATHER\\RAINDROPRED01.BLP', 'textures\\Weather\\SnowFlake01.blp'}
checks['deterministic: a second run gives the same bytes'] = wt.weather_textures() == textures

want = {'RainDrop01': (32, 512, .35), 'RAINDROPRED01': (32, 512, .35), 'SnowFlake01': (32, 128, .8)}
for name, data in textures.items():
    key = next(k for k in want if k.lower() in name.lower())
    w, h, alpha = want[key]
    header = struct.unpack_from('<4sI4B2I', data)
    offsets, sizes = struct.unpack_from('<16I', data, 20), struct.unpack_from('<16I', data, 84)
    levels = [(max(1, w >> i), max(1, h >> i)) for i in range(max(w, h).bit_length())]
    checks[f'{key}: BLP2, encoding 3, alpha 8, mips, {w}x{h}'] = header == (b'BLP2', 1, 3, 8, 0, 1, w, h)
    checks[f'{key}: full mip chain to 1x1 ({len(levels)} levels), contiguous'] = (
        [s for s in sizes if s] == [a*b*4 for a, b in levels] and offsets[0] == 148 and all(offsets[i+1] == offsets[i]+sizes[i] for i in range(len(levels)-1))
        and len(data) == 148+sum(sizes) and not any(offsets[len(levels):]))
    checks[f'{key}: aspect {h//w}:1 and width <= 32'] = h == (16 if w == 32 and h == 512 else 4)*w and w <= 32
    pixels = decode_blp(data, 4096)[2]
    alphas = pixels[3::4]
    peak = max(alphas)
    checks[f'{key}: alpha peak {alpha}'] = peak == round(alpha*255)
    checks[f'{key}: decodes at every level'] = all(decode_blp(data, max(lw, lh))[:2] == (lw, lh) for lw, lh in levels[::3])
    info[key] = {'bytes': len(data), 'levels': len(levels), 'alpha_peak': peak}
    if h == 512:   # streak: centred, thin, fading at both ends
        row = lambda y: [alphas[y*w+x] for x in range(w)]
        mid = row(h//2)
        checks[f'{key}: streak is centred and thin'] = mid.index(max(mid)) in (15, 16) and sum(a > peak//2 for a in mid) <= 5 and mid[0] == mid[-1] == 0
        checks[f'{key}: ends fade out'] = max(row(0)) == 0 and max(row(h-1)) == 0 and max(row(h//20)) < peak//2 and max(row(h-h//20)) < peak//2
    else:          # flake: round in uv (a 4:1 squash in texels), centred, transparent corners and rim
        cx, cy = w//2, h//2
        checks[f'{key}: flake is centred on the texture'] = alphas[cy*w+cx] >= peak-2 and alphas[(cy-1)*w+cx-1] >= peak-4
        checks[f'{key}: flake is 4:1 in texels (round on a square quad)'] = (
            sum(alphas[cy*w+x] > peak//4 for x in range(w))*3.6 < sum(alphas[y*w+cx] > peak//4 for y in range(h)) < sum(alphas[cy*w+x] > peak//4 for x in range(w))*4.4)
        checks[f'{key}: corners and edges transparent'] = all(alphas[i] == 0 for i in (0, w-1, (h-1)*w, h*w-1)) and not any(alphas[y*w] for y in range(h))
    colour = {tuple(pixels[i*4:i*4+3]) for i in range(0, w*h) if pixels[i*4+3]}
    checks[f'{key}: one colour at every visible texel'] = len(colour) <= 1 + 2 and len({c for c in colour if all(abs(a-b) <= 1 for a, b in zip(c, sorted(colour)[len(colour)//2]))}) == len(colour)
    info[key]['colour'] = sorted(colour)[len(colour)//2]
checks['rain tint is near-white blue, red rain red'] = info['RainDrop01']['colour'] == (219, 230, 255) and info['RAINDROPRED01']['colour'] == (191, 56, 46)

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
