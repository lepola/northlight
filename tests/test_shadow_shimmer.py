#!/usr/bin/env python3
# northlight-test:
"""0.3.170 shadow shimmer (A): a Python emulation of TemporalLight's final lines and a source
audit of their wiring. With a still camera (0.3.171: within .25 half-res px) the visibility (alpha) history may stay up to .15 of
full visibility outside the 5-tap box, and the rgb by the direct light that change can carry
(TemporalReach.w = c15.w, from the CPU). History visibility inside the box, a moving camera or no
drawn source: exactly the 0.3.169 clamp. Reads files only; no Wine, GPU or game.

Writes shadow-shimmer-validation.json to the test output dir.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import random
import re

hlsl = fp.src('world_effects.hlsl').read_text()
w = fp.src('world_renderer.h').read_text()
checks = {}



def code(text):
    return re.sub(r'//[^\n]*', '', text)


# ---- 1. source pins ----
temporal = hlsl[hlsl.index('TemporalOutput TemporalLight('):hlsl.index('// 1x1 pass: fraction of the source disc')]
LINES = ['    float4 tight=clamp(history,lo,hi);',
         '    float still=saturate(2-4*length(puv*half-(base+.5)));',  # 0.3.171 gate: full to .25 px, none from .5
         '    float margin=.15/max(RemovalInfo.z,1e-4)*RemovalInfo.y*still;',
         '    float loose=clamp(history.a,lo.a-margin,hi.a+margin);',
         '    float3 reach=abs(loose-tight.a)*RemovalInfo.z*TemporalReach.w;',
         '    o.light=lerp(current,float4(clamp(history.rgb,lo.rgb-reach,hi.rgb+reach),loose),TemporalInfo.x);']
positions = [temporal.find(l + '\n') for l in LINES]
checks['TemporalLight: the six new lines, in order, once each, as the last statements'] = (
    all(temporal.count(l + '\n') == 1 for l in LINES) and positions == sorted(positions) and -1 not in positions
    and temporal[positions[-1]:].split('\n')[1].strip() == 'return o;')
checks['TemporalReach aliases c15; Camera.w is read nowhere; TemporalReach.w only in TemporalLight'] = (
    'float4 TemporalReach : register(c15);' in hlsl and 'float4 Camera : register(c15);' in hlsl
    and all('Camera.w' not in code(p.read_text()) for p in fp.SHADERS.glob('*.hlsl'))
    and code(hlsl).count('TemporalReach.w') == 1 and code(temporal).count('TemporalReach.w') == 1)

# ---- 2. CPU wiring: c15.w from the drawn, active sources before the one bank upload ----
upload = w.index('c[30][0]=waterMask?(surface?2.f:1.f):0.f;d->SetPixelShaderConstantF(0,&c[0][0],68);')
cpu = ('        for(int source=0;source<2;++source)if(sourceWeights[source]>0&&sourceActive[source])\n'
       '            c[15][3]=std::max(c[15][3],std::max({sourceColors[source].x,sourceColors[source].y,sourceColors[source].z})*drawnWeight/sourceWeights[source]);\n')
checks['c[15][3] set once, from drawn active sources, after drawnWeight and before the bank upload'] = (
    w.count(cpu) == 1 and w.index('const float drawnWeight=') < w.index(cpu) < upload
    and len(re.findall(r'c\[15\]\[3\]=', w)) == 1 and 'memcpy(c[15],context.camera,12);' in w
    and 'float c[68][4]={};' in w)

# ---- 3. no new configuration ----
keys = {'TemporalReach', 'ShadowShimmer', 'ShimmerMargin', 'ShadowHistoryMargin', 'NearShadowTent'}
config = [fp.src('quality_settings.h').read_text(), fp.src('windows-package/northlight-quality.ini').read_text(),
          fp.src('windows-package/README.txt').read_text(encoding='utf-8')]
checks['no new key in quality_settings.h, the ini or README'] = not any(k in text for k in keys for text in config)


# ---- 4. emulation of the final lines (per channel; alpha is index 3) ----
def clamp(v, lo, hi):
    return min(max(v, lo), hi)


def today(current, history, lo, hi, weight=.75):
    return [c + weight * (clamp(h, l, u) - c) for c, h, l, u in zip(current, history, lo, hi)]


def shimmer(current, history, lo, hi, z, y, reach_w, motion, weight=.75):
    """TemporalLight 0.3.170 after the box: z = RemovalInfo.z, y = RemovalInfo.y, reach_w = c15.w,
    motion = reprojection distance in half-res pixels."""
    tight = [clamp(h, l, u) for h, l, u in zip(history, lo, hi)]
    still = clamp(2 - 4 * motion, 0, 1)
    margin = .15 / max(z, 1e-4) * y * still
    loose = clamp(history[3], lo[3] - margin, hi[3] + margin)
    reach = abs(loose - tight[3]) * z * reach_w
    held = [clamp(history[i], lo[i] - reach, hi[i] + reach) for i in range(3)] + [loose]
    return [c + weight * (h - c) for c, h in zip(current, held)]


rng = random.Random(170)


def box(current, spread):
    return [c - rng.random() * spread for c in current], [c + rng.random() * spread for c in current]


# 4.1 history visibility inside the box, any rgb change (GI fade, lamp, point shadow): today's value
same = 0
for _ in range(20000):
    W = rng.uniform(.2, 2)
    current = [rng.uniform(-1, 1) for _ in range(3)] + [rng.uniform(.15, 1) * W]
    lo, hi = box(current, .05)
    history = [c + rng.uniform(-2, 2) for c in current[:3]] + [rng.uniform(lo[3], hi[3])]
    same += shimmer(current, history, lo, hi, 1 / W, 1, rng.uniform(0, 5), rng.uniform(0, 3)) == today(current, history, lo, hi)
checks[f'history alpha inside the box: identical to 0.3.169 ({same}/20000)'] = same == 20000

# 4.2 moving camera (>= .5 px, 0.3.171) or no drawn source: today's value
same = 0
for _ in range(20000):
    W = rng.uniform(.2, 2)
    current = [rng.uniform(-1, 1) for _ in range(3)] + [rng.uniform(.15, 1) * W]
    lo, hi = box(current, .05)
    history = [c + rng.uniform(-1, 1) for c in current]
    moving = shimmer(current, history, lo, hi, 1 / W, 1, 3, rng.uniform(.5, 50))
    unlit = shimmer(current, history, lo, hi, 0, 0, rng.uniform(0, 5), 0)
    same += moving == today(current, history, lo, hi) and unlit == today(current, history, lo, hi)
checks[f'camera moving >= .5 px, or RemovalInfo.y=0 (z=0): identical to 0.3.169 ({same}/20000)'] = same == 20000

# 4.3 still camera: a near 5x5 flip (.85*14.8% = .126 normalised) is held fully and fades by .75^n;
# a far 4x4 flip (.85*25% = .21) is capped at .15.
W, K = 1.3, 2.
current = [.4, .3, .2, .5 * W]
excess = .126 * W
history = [current[0] + excess / W * K * .8, current[1], current[2], current[3] + excess]
held_ok = True
for n in range(1, 12):
    out = shimmer(current, history, current, current, 1 / W, 1, K, 0)
    held_ok &= abs((out[3] - current[3]) - excess * .75 ** n) < 1e-9 and abs((out[0] - current[0]) - excess / W * K * .8 * .75 ** n) < 1e-9
    history = out
far = shimmer(current, [c for c in current[:3]] + [current[3] + .21 * W], current, current, 1 / W, 1, K, 0)
checks['still camera: a .126 flip held fully, fading .75^n in alpha and rgb; a .21 flip capped at .15'] = (
    held_ok and abs(far[3] - current[3] - .75 * .15 * W) < 1e-9)
half = shimmer(current, [c for c in current[:3]] + [current[3] + .126 * W], current, current, 1 / W, 1, K, .375)
checks['still gate: half at .375 px'] = abs(half[3] - current[3] - .75 * .075 * W) < 1e-9

# 4.5 the rgb reach bounds the true sun/moon rgb change of the accepted visibility change
worst = 0.
bounded = True
for _ in range(20000):
    sources = rng.choice((1, 2))
    weights = [rng.uniform(.05, 1) for _ in range(sources)]
    colours = [[rng.uniform(0, 2) for _ in range(3)] for _ in range(sources)]
    W = sum(weights)
    K = max(max(c) * W / ws for c, ws in zip(colours, weights))
    sign = rng.choice((-1, 1))
    dv = [sign * rng.uniform(0, .2) for _ in range(sources)]
    nl = [rng.uniform(0, 1) for _ in range(sources)]
    da_norm = sum(ws * d for ws, d in zip(weights, dv)) / W
    drgb = [sum(c[i] * n * d for c, n, d in zip(colours, nl, dv)) for i in range(3)]
    reach = abs(da_norm) * K
    bounded &= all(abs(x) <= reach + 1e-12 for x in drgb)
    worst = max(worst, max(abs(x) for x in drgb) / max(reach, 1e-12))
    if abs(da_norm) <= .15:
        # the whole physical change is inside the reach: that history is kept in full
        current = [.5, .5, .5, .6 * W]
        history = [c + d for c, d in zip(current[:3], drgb)] + [current[3] + da_norm * W]
        out = shimmer(current, history, current, current, 1 / W, 1, K, 0)
        bounded &= all(abs(o - (c + .75 * (h - c))) < 1e-9 for o, c, h in zip(out, current, history))
checks[f'rgb reach >= true sun/moon change for 1-2 sources, same-sign flips (worst ratio {worst:.3f})'] = bounded and worst <= 1 + 1e-9

# 4.6 a shadow edge sweeping through a pixel under a still camera (a walking NPC): the lag is
# bounded by .75*(.15 + box) and does not accumulate; it decays once the edge has passed.
lag_ok = True
max_lag = 0.
for step in (.1, .2, .3):
    for b in (0., .01, .03):
        W = 1.
        visibility, history = 1., None
        for frame in range(40):
            visibility = max(visibility - step, .15) if frame < 20 else visibility
            current = [.2 + .5 * visibility] * 3 + [visibility * W]
            lo, hi = [c - b for c in current], [c + b for c in current]
            history = current if history is None else history
            out = shimmer(current, history, lo, hi, 1 / W, 1, .5, 0)
            lag = abs(out[3] - current[3])
            max_lag = max(max_lag, lag)
            lag_ok &= lag <= .75 * (.15 * W + b) + 1e-12
            if frame == 39:  # 19 frames after the edge stopped: .75^19 of the held excess remains
                lag_ok &= lag <= .75 * b + .75 ** 19 * .75 * .15 * W + 1e-12
            history = out
checks[f'moving edge: lag <= .75*(.15+box), no accumulation (max {max_lag:.3f})'] = lag_ok

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values())
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
(out / 'shadow-shimmer-validation.json').write_text(json.dumps({
    'scope': 'Python emulation of TemporalLight 0.3.170 final lines and source audit; no game, Wine or GPU.',
    'checks': checks, 'max_moving_edge_lag': max_lag, 'worst_rgb_to_reach_ratio': worst}, indent=2) + '\n')
print('PASS shadow shimmer temporal history: identity in the box, still-camera flip held, bounded rgb reach and lag')
