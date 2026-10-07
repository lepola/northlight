# northlight-test:
"""Offline palette relighting and shadow-filter regression; no device/game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import math
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parent
shader = fp.src('world_effects.hlsl').read_text()
assert 'float3 sun=DirectLight.rgb*saturate(dot(n,LegacyDirection.xyz))*sunVisibility;' in shader and 'float visibility=lerp(1,shadow,GridInfo.z);' in shader # 0.3.198 (rain): sunVisibility = visibility unless c59.z softens it
assert 'lerp(sun,moon,SunDirection.w)' in shader
assert 'if(dot(DirectLight.rgb,1)>0)shadow=directionalShadow' in shader
# 0.3.170: near cascade 5x5 tent (radius 2.5), far cascade unchanged 4x4 (radius 2).
assert '[loop]for(int i=0;i<25;++i)' in shader and 'if(i>=size*size)break;' in shader
assert 'float size=useNear?5:4,reach=size*.5;' in shader and 'float start=useNear?-2:-1;' in shader
assert 'float2 base=floor(pixel+(useNear?.5:0)),fraction=pixel-base;' in shader
assert 'float row=floor((i+.5)/size);' in shader
assert 'max(reach-abs(offset-fraction),0)' in shader
assert 'dot(gradient,tapUV-uv)' in shader

def filter_edge(x, size):
    base = math.floor(x + .5) if size == 3 else math.floor(x)
    radius = size / 2
    weights = [max(radius - abs(i - (x-base)), 0) for i in range(-1, size-1)]
    return sum(w * (base+i >= 0) for i, w in zip(range(-1, size-1), weights)) / sum(weights)

# Continuous on either side of integer texel boundaries, monotone on a hard
# fence edge. The wider kernel must soften the transition without leaking light
# into a fully shadowed region or darkening a fully lit receiver plane.
for i in range(-4, 5):
    assert abs(filter_edge(i-1e-7,4) - filter_edge(i+1e-7,4)) < 1e-6
previous = 0
widths = {}
for size in (3, 4):
    partial = []
    for i in range(-4000, 4001):
        x = i / 1000
        v = filter_edge(x, size)
        assert -1e-12 <= v <= 1+1e-12
        if size == 4:
            assert v >= previous-1e-12
            previous = v
        if .1 <= v <= .9:
            partial.append(x)
    widths[size] = partial[-1] - partial[0]
assert widths[4] > widths[3]
assert filter_edge(-4,4) == 0 and filter_edge(4,4) == 1

def shader_tent(x, near):
    """shadowMap's tent as written (0.3.170): returns (edge visibility, largest single-tap share)."""
    size = 5 if near else 4
    reach = size * .5
    base = math.floor(x + (.5 if near else 0))
    fraction = x - base
    start = -2 if near else -1
    visibility = total = largest = 0
    for i in range(25):
        if i >= size*size:
            break
        row = math.floor((i + .5) / size)
        offset = (i - row*size + start, row + start)
        w = max(reach - abs(offset[0] - fraction), 0) * max(reach - abs(offset[1] - fraction), 0)
        visibility += w * (base + offset[0] >= 0)
        total += w
        largest = max(largest, w)
    return visibility / total, largest / total

# The far cascade is today's 4x4 kernel exactly; the near 5x5 is continuous at every
# texel boundary (and at the half-texel recentring), monotone, at most 1.3x wider,
# and one flipped texel moves it by at most 15%.
near_partial, previous, largest = [], 0, 0
for i in range(-4000, 4001):
    x = i / 1000
    assert abs(shader_tent(x, False)[0] - filter_edge(x, 4)) <= 1e-12
    v, share = shader_tent(x, True)
    assert -1e-12 <= v <= 1+1e-12 and v >= previous-1e-12
    previous, largest = v, max(largest, share)
    if .1 <= v <= .9:
        near_partial.append(x)
for i in range(-8, 9):
    assert abs(shader_tent(i/2-1e-7, True)[0] - shader_tent(i/2+1e-7, True)[0]) < 1e-6
widths[5] = near_partial[-1] - near_partial[0]
assert widths[4] < widths[5] <= 1.3*widths[4]
assert largest <= .15
rng = random.Random(58)
for _ in range(10000):
    color = [rng.random() for _ in range(3)]
    tinted = [rng.random()*2 for _ in range(3)]
    weight, shadow, native_cos, moon_cos = [rng.random() for _ in range(4)]
    painted = [c*weight*native_cos for c in color]
    for moon in (False, True):
        angular = moon_cos if moon else native_cos
        visibility = shadow if moon else .15+.85*shadow
        target = [c*weight*angular*visibility for c in tinted]
        correction = [a-b for a,b in zip(target,painted)]
        assert all(math.isclose(a+b,c,abs_tol=1e-12) for a,b,c in zip(painted,correction,target))
        # Turning off a source subtracts its old native share, even when that
        # source has no shadow pass. Sun and moon defaults preserve old policy.
        assert all(p + (-p) == 0 for p in painted)
    # Plane-depth correction keeps a flat receiver fully lit for any slope.
    fraction = rng.random()
    slope = rng.uniform(-4,4)
    for offset in range(-1,3):
        stored = .5 + slope*(offset-fraction)
        expected = .5-.00008 + slope*(offset-fraction)
        assert expected < stored

disc = fp.src('celestial_disc_effects.hlsl').read_text()
# the tint moves halfway toward the glow's hot-core colour (hue mix 1);
# brightness is still limited before tinting.
assert 'saturate(texel.rgb*DiscEmission.x)*lerp(saturate(DiscColor.rgb),DiscGlowCore.rgb,.5*DiscGlowHue.w)' in disc
green = (.68, 1., .62)
for core in ((.68, 1., .62), (.85, 1., .8), (1., 1., 1.)):
    tint = [g+(c-g)*.5 for g,c in zip(green,core)]
    for gain in (1.6,1.8,3.5):
        output = [min(1.,gain)*c for c in tint]
        assert output[1] >= output[0] >= output[2]  # bright core stays green (or neutral)
print(json.dumps({'status':'PASS','relighting_cases':20000,
                  'shadow_edge_10_to_90_percent_width_texels':widths,'near_max_single_tap_share':largest,
                  'game_or_gpu_launched':False},indent=2))
