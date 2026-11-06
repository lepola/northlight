"""Near-fade clipping keeps every interval with nonzero optical depth."""
import math
import random
from pathlib import Path

shader = (Path(__file__).resolve().parents[1] / 'shaders/world_effects.hlsl').read_text()
assert shader.count('[branch]if(surface<=max(FogRange.x,0))return float4(0,0,0,1);') == 2

rng = random.Random(21)
near_empty_cases = 0
for count in (12, 20, 24, 32, 40, 48):
    for _ in range(3000):
        major = rng.choice((-1, 1)) * rng.uniform(1/math.sqrt(3), 1)
        camera = rng.uniform(-20000, 20000)
        spacing = 128/count
        step = spacing/abs(major)
        offset = (camera * (-1 if major < 0 else 1)/spacing) % 1
        near = rng.choice((0, 3.5, 8, rng.uniform(0, 256)))
        surface = rng.uniform(0, 128)
        first = max(0, math.floor(max(near, 0)/step+offset))
        before = after = 0.
        for i in range(count+1):
            a = max(0, (i-offset)*step)
            if a >= surface:
                break
            b = min((i+1-offset)*step, surface)
            t = max(0, min(1, ((a+b)*.5-near)/12))
            optical = t*t*(3-2*t)*(b-a)
            before += optical
            if i >= first:
                after += optical
            else:
                assert optical == 0, (count, near, surface, i, first)
        assert before == after
        if surface <= max(near, 0):
            assert before == 0, (count, near, surface, before)
            near_empty_cases += 1
assert near_empty_cases
print('PASS 18000 fog interval cases: exact optical path, both ray signs and all step budgets')

# Regional field clip (WorldFog/FogClouds): regionalFogAt's coverage is zero outside the 63/64 field
# span, so the skipped intervals, the shortened march and the early return keep the optical path exact.
def sat(x):
    return max(0., min(1., x))
def coverage(px, py, ox, oy, z):
    gx, gy = (px-ox)*64*z, (py-oy)*64*z
    return sat(min(gx, gy, 63-gx, 63-gy)*.5)
def sign(v):
    return -1 if v < 0 else 1
cases = skipped_cases = missed = 0
for count in (12, 24, 40, 48):
    for _ in range(4000):
        z = 1/512
        cam = [rng.uniform(-20000, 20000), rng.uniform(-20000, 20000), rng.uniform(-100, 400)]
        # The host centres the field on the camera; stale or missing fields can leave the camera outside it.
        shift = rng.choice((0., 0., rng.uniform(-400, 400)))
        ox, oy = cam[0]-256+shift, cam[1]-256-shift*rng.choice((0, 1))
        ray = [rng.gauss(0, 1) for _ in range(3)]
        if rng.random() < .1:
            ray[rng.randrange(2)] = 0.
        n = math.sqrt(sum(v*v for v in ray)) or 1
        ray = [v/n for v in ray]
        axis = [abs(v) for v in ray]
        major = (ray[0], cam[0]) if axis[0] >= axis[1] else (ray[1], cam[1])
        major = major if abs(major[0]) >= axis[2] else (ray[2], cam[2])
        if major[0] == 0:
            continue
        spacing = 128/count
        step = spacing/abs(major[0])
        offset = (major[1]*sign(major[0])/spacing) % 1
        near = rng.choice((0, 3.5, rng.uniform(0, 64)))
        surface = rng.uniform(0, 128)
        inv = [sign(v)/max(abs(v), 1e-8) for v in ray[:2]]
        span = 63/(64*z)
        fa = [(ox-cam[0])*inv[0], (oy-cam[1])*inv[1]]
        fb = [(ox+span-cam[0])*inv[0], (oy+span-cam[1])*inv[1]]
        entry = max(0, max(min(fa[0], fb[0]), min(fa[1], fb[1])))
        exit_ = min(max(fa[0], fb[0]), max(fa[1], fb[1]))
        march_end = min(surface, exit_+step)
        first = max(0, math.floor(max(max(near, 0), entry-step)/step+offset))
        before = after = 0.
        for i in range(count+1):
            a = max(0, (i-offset)*step)
            if a >= surface:
                break
            b = min((i+1-offset)*step, surface)
            mid = (a+b)*.5
            t = sat((mid-near)/12)
            optical = coverage(cam[0]+ray[0]*mid, cam[1]+ray[1]*mid, ox, oy, z)*t*t*(3-2*t)*(b-a)
            before += optical
            if exit_ >= entry and i >= first and a < march_end:
                after += optical
            else:
                assert optical == 0, (count, i, first, entry, exit_, optical)
                missed += 1
        skipped_cases += exit_ < entry
        assert before == after
        cases += 1
assert skipped_cases and missed
print(f'PASS {cases} regional field clip cases ({skipped_cases} early returns, {missed} skipped intervals): exact optical path')
