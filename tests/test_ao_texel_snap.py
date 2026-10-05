#!/usr/bin/env python3
# northlight-test:
"""0.3.189: the half-resolution AO pass reads the full-resolution depth at explicit texel centres.

CPU reference only: no game, graphics device or GPU is started. The old AO pass point-sampled the
full-resolution INTZ depth at the half-resolution texel centre (i+.5)/(w/2), exactly the boundary
between full-res texels 2i and 2i+1; GPU interpolation rounding can resolve it differently per row,
so the centre or a +-1 texel neighbour could land on the same texel, SurfaceNormal's tangent became
zero-length, the normal fell back to camera-facing and the ground taps counted as occluders (thin
dark rows). DepthTexelUV() snaps the centre to floor(uv*size+.25), written as
uv + (.75 - frac(uv*size+.25))/size; SurfaceNormal spans both vertical sides when the chosen one is
zero-length. Run this file to regenerate its
JSON report.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import itertools
import json
import math
import re
import struct
from pathlib import Path

JITTER = 1e-4
NEAR, FAR, PARTITION = .2, 1277., .94
SCALES = (1.2686, 1.9626)
DIMENSIONS = ((1920, 1080), (1921, 1081), (1728, 1117), (1728, 1116), (2560, 1440))


def point_texel(u, n, jitter=0.):
    """POINT sampling with CLAMP: the texel a GPU picks for uv u, its rounding perturbed by jitter."""
    return max(0, min(n-1, math.floor(u*n+jitter)))


def snap_texel(u, n, jitter=0.):
    """DepthTexelUV(): floor(uv*size+.25), kept one texel inside every edge (texels 1..n-2)."""
    t = math.floor(u*n+.25+jitter)
    assert 0 <= t <= n-1, (u, n, t)
    return max(1, min(n-2, t))


def snapped_uv(u, n, jitter=0.):
    return (snap_texel(u, n, jitter)+.5)/n


def half_uv(i, j, dimensions):
    half = (dimensions[0]//2, dimensions[1]//2)
    return ((i+.5)/half[0], (j+.5)/half[1])


def columns(half_width):
    return sorted({0, 1, 2, half_width//3, half_width//2, half_width-3, half_width-2, half_width-1})


def check_snap(dimensions):
    W, H = dimensions
    hw, hh = W//2, H//2
    unstable = ties = 0
    cases = 0
    for j in range(hh):
        for i in columns(hw):
            uv = half_uv(i, j, dimensions)
            for axis, n in ((0, W), (1, H)):
                base = snap_texel(uv[axis], n)
                x = uv[axis]*n+.25
                for jitter in (-JITTER, 0., JITTER):
                    if snap_texel(uv[axis], n, jitter) != base:
                        # Odd sizes drift uv*size across the .75 fraction once; a value within the jitter
                        # of that integer is a genuine tie, anything else must be stable.
                        if abs(x-round(x)) <= 2*JITTER:
                            ties += 1
                        else:
                            unstable += 1
                # Centre and its +-1 neighbours (read from the snapped uv) are three distinct texels.
                su = snapped_uv(uv[axis], n)
                for jitter in (-JITTER, 0., JITTER):
                    texels = [point_texel(su+d/n, n, jitter) for d in (-1, 0, 1)]
                    # The edge clamp keeps the centre off the outermost texel, so CLAMP never folds a
                    # neighbour onto it, on any edge.
                    assert 1 <= base <= n-2 and texels == [base-1, base, base+1], (dimensions, i, j, axis, texels)
                cases += 1
    # Full-resolution texel centres map to themselves, except the outermost ones, which move inward.
    for n in (W, H):
        for t in sorted({0, 1, n//2, n-2, n-1}):
            u = (t+.5)/n
            for jitter in (-JITTER, 0., JITTER):
                assert snap_texel(u, n, jitter) == max(1, min(n-2, t))
    assert unstable == 0, (dimensions, unstable)
    return {'dimensions': dimensions, 'cases': cases, 'unstable_under_jitter': unstable,
            'inherent_boundary_ties': ties}


def ground(dimensions, height=2., tilt=0.):
    """Ground plane in view space, sloping sideways by tilt: raw (D24, partitioned) depth per texel."""
    W, H = dimensions
    span = (1 << 24)-1

    def depth(x, y):
        u, v = (x+.5)/W, (y+.5)/H
        s = (2*v-1)/SCALES[1]+tilt*(2*u-1)/SCALES[0]  # plane height/z for the view ray, below the horizon
        if s <= 1e-9:
            return None
        z = height/s
        if z >= FAR*.9:
            return None
        ndc = FAR/(FAR-NEAR)-NEAR*FAR/((FAR-NEAR)*z)
        return round(ndc*PARTITION*span)/span
    return depth


def read_depth(raw):
    return min(1., max(0., raw/PARTITION))


def position(texel, dimensions, d):
    W, H = dimensions
    z = NEAR*FAR/max(FAR-d*(FAR-NEAR), 1e-5)
    u, v = (texel[0]+.5)/W, (texel[1]+.5)/H
    return ((u*2-1)*z/SCALES[0], (1-v*2)*z/SCALES[1], z)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def safe_normal(n):
    """SafeNormal(); returns (normal, fell_back)."""
    len2 = dot(n, n)
    fell_back = not len2 > 1e-14
    n = (0., 0., -1.) if fell_back else tuple(c/math.sqrt(len2) for c in n)
    return (tuple(-c for c in n) if n[2] > 0 else n), fell_back


def surface_normal(tex, dimensions, depth, guard):
    """SurfaceNormal() on the texels tex = (centre, left, right, up, down); neighbours are texel indices."""
    pts = []
    for t in tex:
        raw = depth(*t)
        if raw is None:
            return None
        pts.append(position(t, dimensions, read_depth(raw)))
    p, l, r, u, b = pts
    use_r = abs(r[2]-p[2]) < abs(p[2]-l[2])
    use_b = abs(b[2]-p[2]) < abs(p[2]-u[2])
    tx = sub(r, p) if use_r else sub(p, l)
    ty = sub(b, p) if use_b else sub(p, u)
    if guard and dot(ty, ty) < 1e-14:
        ty = sub(b, u)
    return safe_normal(cross(tx, ty))


def check_normals(dimensions, tilt=0.):
    W, H = dimensions
    hw, hh = W//2, H//2
    depth = ground(dimensions, tilt=tilt)
    old_bad_rows, new_bad_rows, rows, evaluated = set(), set(), 0, 0
    old_fallbacks = new_fallbacks = 0
    for j in range(hh):
        for i in columns(hw):
            uv = half_uv(i, j, dimensions)
            if depth(point_texel(uv[0], W), point_texel(uv[1], H)) is None:
                continue
            rows += 1
            # Old: every tap point-samples its raw uv; each tap's rounding is independent.
            for jit in itertools.product((-JITTER, JITTER), repeat=5):
                def tap(dx, dy, k):
                    return (point_texel(uv[0]+dx/W, W, jit[k]), point_texel(uv[1]+dy/H, H, jit[k]))
                tex = (tap(0, 0, 0), tap(-1, 0, 1), tap(1, 0, 2), tap(0, -1, 3), tap(0, 1, 4))
                got = surface_normal(tex, dimensions, depth, guard=False)
                if got is None:
                    continue
                evaluated += 1
                if got[1]:
                    old_bad_rows.add(j)
                    old_fallbacks += 1
            # New: snapped centre, neighbours exactly one texel away, with and without the guard.
            for jit in itertools.product((-JITTER, JITTER), repeat=2):
                cx, cy = snap_texel(uv[0], W, jit[0]), snap_texel(uv[1], H, jit[1])
                # Neighbours go through the sampler's CLAMP, like the shader's tex2Dlod.
                def clamped(x, y):
                    return (max(0, min(W-1, x)), max(0, min(H-1, y)))
                tex = (clamped(cx, cy), clamped(cx-1, cy), clamped(cx+1, cy), clamped(cx, cy-1), clamped(cx, cy+1))
                for guard in (False, True):
                    got = surface_normal(tex, dimensions, depth, guard)
                    if got is None:
                        continue
                    if got[1] or abs(got[0][1]) < .5:
                        new_bad_rows.add(j)
                        new_fallbacks += 1
    assert not new_bad_rows, (dimensions, tilt, sorted(new_bad_rows)[:8])
    return {'dimensions': dimensions, 'tilt': tilt, 'ground_samples': rows, 'old_evaluations': evaluated,
            'old_degenerate_rows': len(old_bad_rows), 'old_camera_facing_cases': old_fallbacks,
            'new_degenerate_rows': len(new_bad_rows), 'new_camera_facing_cases': new_fallbacks}


def check_guard():
    """A zero-length chosen vertical tangent spans both sides; all equal keeps the camera-facing fallback."""
    dimensions = (1920, 1080)
    depth = ground(dimensions)
    centre = (960, 900)
    # The lower neighbour resolved to the centre texel (the old failure), so the same-surface selection
    # picks the zero-length lower side.
    tex = (centre, (centre[0]-1, centre[1]), (centre[0]+1, centre[1]), (centre[0], centre[1]-1), centre)
    bad = surface_normal(tex, dimensions, depth, guard=False)
    good = surface_normal(tex, dimensions, depth, guard=True)
    assert bad[1] and not good[1] and abs(good[0][1]) > .5, (bad, good)
    allsame = surface_normal((centre,)*5, dimensions, depth, guard=True)
    assert allsame == ((0., 0., -1.), True)


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def check_float32():
    """The shader's float32 form uv + (.75 - frac(uv*rcp(1/size) + .25)) / size, then the edge clamp,
    lands on the centre of the texel snap_texel() names, for every half-res and full-res pixel centre."""
    for n in (1080, 1081, 1116, 1117, 1366, 1440, 1728, 1920, 1921, 2560, 3840):
        inv = f32(1/n)
        size = f32(1/inv)
        for out in (n//2, n):
            for i in range(out):
                u = f32((i+.5)/out)
                t = f32(f32(u*size)+.25)
                snapped = f32(f32(f32(.75-(t-math.floor(t)))*inv)+u)
                snapped = min(max(snapped, f32(1.5*inv)), f32(1-f32(1.5*inv)))
                expected = (snap_texel(u, n)+.5)/n
                assert abs(snapped-expected)*n < .02, (n, out, i, snapped*n, expected*n)


def check_composite(dimensions):
    """Legacy Composite: its ambient tap (ambient texel centre + a quarter full-res texel) and the
    NeighbourNormal reads one texel right/down each point-sample one stable, distinct texel."""
    for axis, n in enumerate(dimensions):
        a = max(1, math.floor(.5*n+.01))
        for k in range(1, a-1):
            u = (k+.5)/a+.25/n
            for d in (0, 1):
                x = (u+d/n)*n
                picks = {point_texel(u+d/n, n, j) for j in (-JITTER, 0., JITTER)}
                assert len(picks) == 1 or abs(x-round(x)) <= 2*JITTER, (dimensions, axis, k, d, picks)
            # Odd sizes drift the tap across a texel boundary once; that genuine tie is the old behaviour.
            if abs(u*n-round(u*n)) > 2*JITTER:
                assert point_texel(u, n) != point_texel(u+1/n, n), (dimensions, axis, k)


def audit_source():
    text = fp.src('effects.hlsl').read_text()
    helper = re.search(r'float2 DepthTexelUV\(float2 uv\)\s*\{(.*?)\n\}', text, re.S)
    assert helper, 'DepthTexelUV missing'
    body = helper.group(1)
    assert 'uv = mad(0.75 - frac(mad(uv, 1.0 / ImageAndClip.xy, 0.25)), ImageAndClip.xy, uv);' in body
    assert 'return clamp(uv, 1.5 * ImageAndClip.xy, 1.0 - 1.5 * ImageAndClip.xy);' in body
    impl = text[text.index('float4 AOImpl('):text.index('float4 AO(float2 uv')]
    assert impl.count('DepthTexelUV(uv)') == 1 and 'uv = DepthTexelUV(uv);' in impl
    assert impl.index('DepthTexelUV') < impl.index('ReadDepth(uv)') < impl.index('SurfaceNormal(uv, p)')
    assert 'float2 pixel = uv / ImageAndClip.xy;' in impl
    assert 'DepthTexelUV(suv)' not in impl and 'float2 suv = uv + kernel[i].x * axisX' in impl
    normal = text[text.index('float3 SurfaceNormal('):text.index('float4 AOImpl(')]
    assert 'DepthTexelUV' not in normal and 'ty = dot(ty, ty) < 1e-14 ? b - u : ty;' in normal
    composite = text[text.index('float4 Composite('):]
    assert 'float2 ambientCenter = mad(floor(uv * ambientSize) + 0.5, ambientTexel, 0.25 * ImageAndClip.xy);' in composite
    build = json.loads(fp.src('shader-build.json').read_text())
    digest = hashlib.sha256(fp.src('effects.hlsl').read_bytes()).hexdigest()
    assert build['source_sha256'] == digest, 'shaders/shader-build.json is stale: run scripts/shaders/compile_shaders.py'
    slots = {name: build['shaders'][name]['static_instruction_slots'] for name in ('AO', 'AOContactBloom', 'Composite')}
    assert all(n <= 512 for n in slots.values()), slots
    return digest, slots


def run():
    snap = [check_snap(d) for d in DIMENSIONS]
    normals = [check_normals(d, tilt) for d in DIMENSIONS for tilt in (0., .3, -.3)]
    check_float32()
    for d in DIMENSIONS:
        check_composite(d)
    check_guard()
    digest, slots = audit_source()
    assert any(n['old_degenerate_rows'] for n in normals), 'the old selection never produced a degenerate normal'
    report = {
        'result': 'pass', 'game_launched': False, 'gpu_test': False,
        'source_sha256': digest, 'test_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'jitter': JITTER, 'snap': snap, 'normals': normals, 'static_instruction_slots': slots,
        'limitations': ['CPU float64 reference of the sampling choice, not GPU execution or a screenshot.',
                        'Jitter stands in for per-GPU interpolation rounding at the texel boundary.']}
    output = fp.output_dir()/'ao-texel-snap-validation.json'
    output.write_text(json.dumps(report, indent=2)+'\n')
    old = sum(n['old_degenerate_rows'] for n in normals)
    print(f'PASS snap {sum(s["cases"] for s in snap)} cases; old degenerate rows {old}, new 0; AO slots {slots}; {output}')


if __name__ == '__main__':
    run()
