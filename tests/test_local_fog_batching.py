# northlight-test:
"""Offline model of LocalFog batching (gh:lepola/northlight#20).

Lamps are drawn four per pass in closest-first order (score = distance -
attenuationEnd, ties by id). Applying the non-linear soft cap per batch makes
the sum depend on how lamps are grouped, so a regroup (two lamps swapping
order across a batch boundary) steps or spikes the glow wherever glows
overlap. Capping the accumulated raw sums once (plus a brightness restoration that
uses only per-lamp quantities) is grouping independent.
"""
import math
import random
import statistics
import unittest
from pathlib import Path

BATCH = 4
FOG_X, FOG_Y = 3.5, 1 / 12                      # near start, near ramp 1/length
GAIN = .755                                      # night lamp gain
FOG_Z, FOG_W = 10 * GAIN, .12 * GAIN
COLOR = (1., .6, .3)
RECEIVER = 60.                                   # fixed far receiver distance
BOOST = .6                                       # LocalFogCombine's n_eff exponent
HLSL = Path(__file__).resolve().parents[1] / 'shaders' / 'world_effects.hlsl'


def fast_atan(x):
    a = abs(x)
    r = a * (.785398 + .273 * (1 - a)) if a <= 1 else (
        1.570796 - (1 / a) * (.785398 + .273 * (1 - 1 / a)))
    return -r if x < 0 else r


def raw_glow(cam, ray, light, depth=RECEIVER):
    """Uncapped per-light rgb of the LocalFog loop body (extinction factor 1)."""
    pos, radius = light['pos'], light['end']
    oc = [c - p for c, p in zip(cam, pos)]
    b = sum(o * r for o, r in zip(oc, ray))
    h2 = max(sum(o * o for o in oc) - b * b, 0.)
    disc = radius * radius - h2
    root = math.sqrt(max(disc, 0.))
    t0, t1 = max(-b - root, FOG_X), min(-b + root, depth)
    core = max(.75, radius * .06)
    inv_h = 1 / math.sqrt(h2 + core * core)
    boundary = 1 / max(radius * radius + core * core, .001)
    integral = 0.
    if disc > 0 and t1 > t0:
        integral = max((fast_atan((t1 + b) * inv_h) - fast_atan((t0 + b) * inv_h)) * inv_h
                       - (t1 - t0) * boundary, 0.)
    near = min(max((min(max(-b, t0), t1) - FOG_X) * FOG_Y, 0.), 1.)
    near *= near * (3 - 2 * near)
    return [c * integral * near for c in COLOR]


def cap(rgb):
    """Shader soft cap applied to a raw sum (FogRange.z/.w folded in)."""
    s = [c * FOG_Z for c in rgb]
    k = FOG_W / (FOG_W + max(s))
    return [c * k for c in s]


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def order(lights, cam):
    """Closest-first by score, ties by id (local_light_selection.h)."""
    return sorted(lights, key=lambda l: (math.dist(cam, l['pos']) - l['end'], l['id']))


def legacy(lights, cam, ray):
    out = [0.] * 3
    seq = order(lights, cam)
    for i in range(0, len(seq), BATCH):
        raw = [0.] * 3
        for l in seq[i:i + BATCH]:
            raw = add(raw, raw_glow(cam, ray, l))
        out = add(out, cap(raw))
    return out


def total_cap(lights, cam, ray, seq=None):
    """Shipped law: one cap on the summed raw glow, then a brightness
    restoration k = max(a / capPeak, 1)^.6 where a is the sum of the per-lamp
    capped peaks (so many overlapping lamps do not look dimmer than the
    legacy per-batch result)."""
    raw, a = [0.] * 3, 0.
    for l in (seq if seq is not None else order(lights, cam)):
        g = raw_glow(cam, ray, l)
        raw = add(raw, g)
        p = max(g) * FOG_Z
        a += FOG_W * p / (FOG_W + p)
    c = cap(raw)
    peak = max(c)
    k = max(a / peak, 1.) ** BOOST if peak > 0 else 1.
    return [x * k for x in c]


def batches(lights, cam):
    seq = order(lights, cam)
    return tuple(tuple(l['id'] for l in seq[i:i + BATCH]) for i in range(0, len(seq), BATCH))


def make_lamps(n=17):
    # A road along +z with a lamp on each side, ~6 off the centreline, 10..14
    # apart. The two lamps of a station sit at slightly different heights
    # (1.5 / 2.0, eye level between them) with the same range, so their scores
    # swap order whenever the camera sways across 1.75 -- the up-and-back
    # regroup that happens when such a pair straddles a batch boundary.
    lamps, z = [], 8.
    for i in range(1, n):  # i=0 is skipped: a single first lamp offsets the pairing
        lamps.append({'id': i + 1, 'pos': (6. if i % 2 else -6., 2.0 if i % 2 else 1.5, z),
                      'end': 9. + (i // 2 * 5) % 12})   # 9..20, varied per station
        if i % 2:
            z += 10. + (i // 2 * 3) % 5                    # 10..14 spacing
    return lamps


def unit(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


# Fixed view rays aimed down the road toward the lamp rows.
RAYS = [unit(v) for v in ((-.30, .02, 1), (.30, .02, 1), (-.15, -.01, 1),
                          (.15, .0, 1), (-.45, .05, 1), (.45, .05, 1), (0., .0, 1))]


def camera(frame, bob):
    z = frame * .1                                           # walking pace
    y = 1.7 + (bob * math.sin(frame * .9) if bob else 0.)    # mounted-camera sway
    return (0., y, z)


def series(fn, ray, frames=1100, bob=.12):
    lamps = make_lamps()
    cams = [camera(f, bob) for f in range(frames)]
    return cams, [sum(fn(lamps, c, ray)) for c in cams]


def jump_stats(values):
    """Per-frame changes and each change relative to the median change of the
    surrounding +-20 frames (the glow legitimately grows near lamps, so a
    global median would flag smooth growth)."""
    d = [abs(b - a) for a, b in zip(values, values[1:])]
    rel = [x / max(statistics.median(d[max(0, i - 20):i + 21]), 1e-12) for i, x in enumerate(d)]
    return max(rel), rel


class LocalFogBatching(unittest.TestCase):
    def test_legacy_per_batch_cap_steps_and_spikes_on_regroup(self):
        lamps, steps, spikes = make_lamps(), 0, 0
        best = 0.
        for ray in RAYS:
            cams, v = series(legacy, ray)
            _, rel = jump_stats(v)
            groups = [batches(lamps, c) for c in cams]
            for i, jump in enumerate(rel):
                if jump > 25 and groups[i] != groups[i + 1]:
                    steps += 1
                    best = max(best, jump)
                    # up-and-back: the value returns near its old level soon after
                    for k in range(i + 2, min(i + 11, len(v))):
                        if abs(v[k] - v[i]) < .3 * abs(v[i + 1] - v[i]):
                            spikes += 1
                            break
        print('legacy: regroup jumps %d (max %.0fx median), up-and-back %d' % (steps, best, spikes))
        self.assertGreater(steps, 0)
        self.assertGreater(spikes, 0)

    def test_total_law_is_order_invariant_and_has_no_regroup_artifacts(self):
        lamps = make_lamps()
        cam = camera(317, .12)
        for ray in RAYS:
            ref = total_cap(lamps, cam, ray)
            for seed in range(8):
                seq = lamps[:]
                random.Random(seed).shuffle(seq)
                got = total_cap(lamps, cam, ray, seq)
                for a, b in zip(ref, got):
                    self.assertAlmostEqual(a, b, delta=1e-9 * max(abs(a), 1e-12) + 1e-15)
        worst, regroup_worst = 0., 0.
        for ray in RAYS:
            cams, v = series(total_cap, ray)
            top, rel = jump_stats(v)
            worst = max(worst, top)
            groups = [batches(lamps, c) for c in cams]
            for i, jump in enumerate(rel):
                if groups[i] != groups[i + 1]:
                    regroup_worst = max(regroup_worst, jump)
        print('total law: max jump %.1fx local median (at regroups %.1fx)' % (worst, regroup_worst))
        # The law grows fast near lamps (~12x the local median on smooth
        # ramps), but the legacy regroup artifacts are in the thousands and
        # always paired with a return; neither may appear. 25 is the legacy
        # detector's threshold.
        self.assertLess(worst, 25)
        self.assertLess(regroup_worst, 25)

    def test_single_lamp_matches_legacy(self):
        lamps = make_lamps()[:1]
        for f in range(0, 600, 37):
            cam = camera(f, .12)
            for ray in RAYS:
                for a, b in zip(legacy(lamps, cam, ray), total_cap(lamps, cam, ray)):
                    self.assertAlmostEqual(a, b, delta=1e-12)

    def test_brightness_parity_with_legacy(self):
        lamps, ratios = make_lamps(), []
        for ray in RAYS:
            for f in range(0, 1100, 5):
                cam = camera(f, 0)
                old = sum(legacy(lamps, cam, ray))
                if old > 1e-4:
                    ratios.append(sum(total_cap(lamps, cam, ray)) / old)
        mean, med = statistics.mean(ratios), statistics.median(ratios)
        print('brightness new/legacy: mean %.3f median %.3f (n=%d)' % (mean, med, len(ratios)))
        self.assertTrue(.85 <= mean <= 1.15)
        self.assertTrue(.85 <= med <= 1.15)

    def test_model_matches_the_shader(self):
        # The law above is a transcription of LocalFog (alpha) and LocalFogCombine; pin the shader lines so the two cannot drift apart.
        h = HLSL.read_text()
        fog = h[h.index('float4 LocalFog('):h.index('float4 LocalFogCombine(')]
        combine = h[h.index('float4 LocalFogCombine('):]
        combine = combine[:combine.index('\n}\n')]
        self.assertIn('capped+=FogRange.w*lampPeak/(FogRange.w+lampPeak);', fog)
        self.assertIn('return float4(result*FogRange.z,capped);', fog)
        self.assertIn('scatter*=FogRange.w/(FogRange.w+peak);', combine)
        self.assertIn('scatter*=pow(max(acc.a/max(capPeak,1e-6),1),%s);' % ('%g' % BOOST).lstrip('0'), combine)


if __name__ == '__main__':
    unittest.main()
