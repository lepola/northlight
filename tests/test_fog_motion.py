#!/usr/bin/env python3
# northlight-test:
"""Offline movement, world anchoring and ground-fade regressions; no GPU/game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import json
import math
import random
from pathlib import Path
from test_solar_volume import intervals, smooth, density, node, integrate

HERE = Path(__file__).resolve().parent


def old_intervals(distance):
    return [(128*(i/32)**2, min(distance, 128*((i+1)/32)**2))
            for i in range(32) if 128*(i/32)**2 < distance]


def leaf_shadow(x, y):
    # An opaque canopy with many small gaps. Light travels vertically, so this
    # is also the exact geometric shadow of those openings on the air below.
    return float(math.sin(x*2.7+.5)*math.cos(y*1.7+.2) > .2)


def soft_shadow(x, y):
    grid = (x/1.5, y/1.5)
    base = tuple(math.floor(v) for v in grid)
    f = tuple(smooth(v-b) for v, b in zip(grid, base))
    return sum(leaf_shadow((base[0]+a)*1.5, (base[1]+b)*1.5)
               * (f[0] if a else 1-f[0])*(f[1] if b else 1-f[1])
               for a in (0, 1) for b in (0, 1))


def march(camera_x, schedule, shadow):
    transmittance, scatter = 1., 0.
    for start, end in schedule:
        t = (start+end)*.5
        sigma = .012*smooth((t-4)/12)
        absorb = 1-math.exp(-sigma*(end-start))
        scatter += transmittance*absorb*shadow(camera_x+t, .3)*smooth(8/12)
        transmittance *= 1-absorb
    return scatter


def main():
    shader = fp.src('world_effects.hlsl').read_text()
    fog = shader.split('float4 worldFog(', 1)[1].split('// Glow of', 1)[0]
    for contract in ('frac(major.y*(major.x<0?-1:1)/spacing)',
                     'float altitude=p.z-field.x;',
                     'heightFade=saturate(altitude*(1.0/12))',
                     'directWeight+=transmittance*absorb*(visibility*heightFade)', 'SourcePolicy.x*.35'):
        assert contract in fog
    assert 'sin(' not in fog and 'SourcePolicy.z' not in fog
    assert '.35+.65*visible' not in fog
    shadow = shader.split('float fogShadowProjected(', 1)[1].split('float2 probeUV', 1)[0]
    assert 'ShadowNear' not in shadow
    assert '(q.xy-FarMatrix[3].xy)' in shadow
    assert 'visibility+=(receiver<=z?1:0)*w.x*w.y' in shadow

    rng = random.Random(52)
    coverage_cases = 0
    for _ in range(300):
        ray = [rng.uniform(-1, 1) for _ in range(3)]
        length = math.sqrt(sum(v*v for v in ray)); ray = tuple(v/length for v in ray)
        camera = tuple(rng.uniform(-18000, 18000) for _ in range(3))
        for distance in (0., .01, 4., 30., 128., 947.):
            cells = intervals(distance, camera, ray)
            assert len(cells) <= 49
            assert all(b > a for a, b in cells)
            assert all(abs(b-c) < 1e-12 for (_, b), (c, _) in zip(cells, cells[1:]))
            assert math.isclose(sum(b-a for a, b in cells), min(distance, 128), abs_tol=1e-10)
            optical = math.prod(math.exp(-.012*(b-a)) for a, b in cells)
            assert math.isclose(optical, math.exp(-.012*min(distance, 128)), abs_tol=1e-12)
            coverage_cases += 1
        # Move forward along exactly the same world ray: interior sample points
        # must coincide; only the two clipped end cells may change.
        advance = .37
        moved = tuple(c+r*advance for c, r in zip(camera, ray))
        first = [(a+b)*.5 for a, b in intervals(100, camera, ray)[1:-1]]
        second = [advance+(a+b)*.5 for a, b in intervals(100-advance, moved, ray)[1:-1]]
        assert all(min(abs(a-b) for b in first) < 1e-9 for a in second)

    # Changing the shadow pivot changes matrix translation, not the world grid.
    for x in (-17000., -1.3, 0., 47.9, 17000.):
        expected = x/1.5
        for pivot in (-17000., -48., 0., 48., 17000.):
            q = (x-pivot)/192
            origin = -pivot/192
            plane = (q-origin)*192/1.5
            assert math.isclose(plane, expected, abs_tol=1e-10)

    # Ground fade is terrain-relative, not absolute world Z. Source blocking
    # and zero intensity remove the direct term without changing extinction.
    heights = (0., .1, .5, 1., 2., 4., 8., 12., 20.)
    lit = [integrate(80, [.012]*49, [1.]*49, angle=90, altitude=h) for h in heights]
    dark = [integrate(80, [.012]*49, [0.]*49, angle=90, altitude=h) for h in heights]
    assert lit[0] == dark[0]
    assert all(a[3][2] <= b[3][2] for a, b in zip(lit, lit[1:]))
    assert lit[-2] == lit[-1]
    assert all(a[1] == b[1] and b[3] == (0., 0., 0.) for a, b in zip(lit, dark))
    for ground in (-1000., 0., 1000.):
        for h in heights:
            values = [density((0, 0, ground+h), lambda x,y: node(10, ground=ground),
                              night=1, seconds=t) for t in (0, 10, 100)]
            assert values[0] == values[1] == values[2]
            expected = density((0, 0, h), lambda x,y: node(10, ground=0), night=1)
            assert all(math.isclose(a, b, abs_tol=1e-14) for a,b in zip(values[0], expected))

    # Compare camera-motion integration errors against a dense reference of
    # EACH method's own shadow field, isolating aliasing from changed softness.
    old_error, new_error, new_radiance = [], [], []
    for frame in range(81):
        camera = frame*.025
        distance = 80-camera
        dense = [(i*.02, min(distance, (i+1)*.02)) for i in range(math.ceil(distance/.02))]
        old = march(camera, old_intervals(distance), leaf_shadow)
        new = march(camera, intervals(distance, (camera, .3, 8)), soft_shadow)
        old_error.append(old-march(camera, dense, leaf_shadow))
        new_error.append(new-march(camera, dense, soft_shadow))
        new_radiance.append(new)
    jump = lambda a: max(abs(b-c) for b, c in zip(a, a[1:]))
    old_jump, new_jump = jump(old_error), jump(new_error)
    assert new_jump < old_jump*.25, (old_jump, new_jump)
    assert jump(new_radiance) < .003
    report = dict(status='PASS', scope='CPU reference; no GPU/game test',
                  optical_path_cases=coverage_cases, world_translation_cases=300,
                  old_peak_alias_step=old_jump, new_peak_alias_step=new_jump,
                  new_peak_radiance_step=jump(new_radiance),
                  ground_fade={str(h):r[3][2] for h,r in zip(heights,lit)},
                  shader_sha256=hashlib.sha256(shader.encode()).hexdigest())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
