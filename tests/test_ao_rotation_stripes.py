#!/usr/bin/env python3
# northlight-test:
"""0.3.198 (stripes): the AO kernel rotation is hashed from the AO pass's own texel index.

CPU reference only: no game, graphics device or GPU. AOImpl runs in the half-resolution pass, but its
interleaved-gradient-noise hash took uv*size, the snapped FULL-resolution texel (2i+1.5): a step of 2 per
AO texel. The phase then advanced by .11 (rot.x) and .02 (rot.y) along a row but by .62 (the golden ratio) and
.44 per row, so the kernel rotation was nearly constant along each row and different on every row: where the AO
genuinely occludes (cloth folds, armour, hair, a shield) that is screen-fixed horizontal banding that stays the
same size in pixels at any zoom. Half the coefficients is the AO texel index: a step of 1, as IGN is designed for.
This test (1) measures the rotation field's x/y coherence for the old and new coefficients and (2) runs a float32
emulation of AOImpl (DepthTexelUV, SurfaceNormal, 8-tap kernel, horizon bias) and WorldComposite's half-res tent on
surfaces whose AO occludes (grooved cylinders, an inner box corner) and measures the row-stripe amplitude.
Run this file to regenerate its JSON report.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import json
import math
import re
import struct

W, H = 1920, 1080
NEAR, FAR, PART = .2, 1277., .94
SX, SY = 1.2686, 1.9626
RADIUS, STRENGTH = 2., .6
SPAN = (1 << 24)-1
OLD, NEW = (0.06711056, 0.00583715), (0.03355528, 0.002918575)
KERNEL = [(.3234, .1339), (-.2488, .6005), (-.8315, -.3444), (.3827, -.9239),
          (.8315, -.3444), (.2488, .6005), (-.3234, .1339), (-.3827, -.9239)]


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def frac(x):
    return x-math.floor(x)


def rotation(coeff, i, j):
    """AOImpl's rot for AO texel (i, j): pixel is the snapped full-res texel centre 2i+1.5 (even sizes)."""
    g = frac((2*i+1.5)*coeff[0]+(2*j+1.5)*coeff[1])
    rx, ry = frac(52.9829189*g)*2-1, frac(37.4136*g)*2-1
    n = 1/math.sqrt(max(rx*rx+ry*ry, 1e-4))
    return rx*n, ry*n


def rotation_field(coeff, size=96):
    return [[rotation(coeff, i, j) for i in range(size)] for j in range(size)]


def lowpass(field, window=4):
    """RMS length of the mean rotation vector over `window` consecutive AO texels, along x and along y: how much
    of the rotation survives an average along a row (x) or a column (y). A rotation constant along rows and
    different on every row (banding) keeps most of it along x and loses it along y."""
    n = len(field)
    ax, ay = [], []
    for j in range(n):
        for i in range(n-window+1):
            ax.append(math.hypot(*(sum(field[j][i+k][c] for k in range(window))/window for c in (0, 1))))
            ay.append(math.hypot(*(sum(field[i+k][j][c] for k in range(window))/window for c in (0, 1))))
    rms = lambda v: math.sqrt(sum(a*a for a in v)/len(v))
    return rms(ax), rms(ay)


class Surface:
    """Raw D24 depth (with the .94 world partition) of a camera-facing surface seen from 18 degrees above:
    a vertical cylinder with vertical grooves, or an inner box corner (valley), against a far wall."""
    def __init__(self, kind, depth=3.2, groove=0., pitch=20):
        self.kind, self.D, self.C, self.m = kind, depth, groove/2, pitch
        self.pitch = math.radians(18)
        self.cache = {}

    def raw(self, x, y):
        x, y = max(0, min(W-1, x)), max(0, min(H-1, y))
        key = (x, y)
        if key not in self.cache:
            self.cache[key] = self._raw(x, y)
        return self.cache[key]

    def _raw(self, x, y):
        sx, sy = (2*(x+.5)/W-1)/SX, (1-2*(y+.5)/H)/SY
        dz = sy*math.sin(self.pitch)+math.cos(self.pitch)
        dy = sy*math.cos(self.pitch)-math.sin(self.pitch)
        if self.kind == 'corner':
            t = (self.D+.4)/(dz+abs(sx))
        else:
            t, r = 7./dz, .28
            for _ in range(4):
                a, b, c = sx*sx+dz*dz, -2*dz*self.D, self.D**2-r*r
                disc = b*b-4*a*c
                if disc < 0:
                    break
                t = (-b-math.sqrt(disc))/(2*a)
                r = .28+self.C*math.sin(math.atan2(t*sx, self.D-t*dz)*self.m)
            else:
                pass
        ndc = FAR/(FAR-NEAR)-NEAR*FAR/((FAR-NEAR)*t)
        return round(ndc*PART*SPAN)/SPAN


def depth(surface, x, y):
    return min(1., max(0., f32(surface.raw(x, y)/PART)))


def linear(d):
    return f32(NEAR*FAR/max(FAR-f32(d*(FAR-NEAR)), 1e-5))


def position(uv, d):
    z = linear(d)
    return ((uv[0]*2-1)*z/SX, (1-uv[1]*2)*z/SY, z)


def vsub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])


def vdot(a, b):
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]


def vcross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def ao_texel(surface, coeff, i, j):
    """AOImpl for AO texel (i, j): snapped centre, SurfaceNormal, rotated 8-tap kernel; returns the AO alpha."""
    cx = max(1, min(W-2, math.floor((i+.5)/(W//2)*W+.25)))
    cy = max(1, min(H-2, math.floor((j+.5)/(H//2)*H+.25)))
    uv = ((cx+.5)/W, (cy+.5)/H)

    def at(x, y):
        return position(((x+.5)/W, (y+.5)/H), depth(surface, x, y))
    p = at(cx, cy)
    l, r, u, b = at(cx-1, cy), at(cx+1, cy), at(cx, cy-1), at(cx, cy+1)
    tx = vsub(r, p) if abs(r[2]-p[2]) < abs(p[2]-l[2]) else vsub(p, l)
    ty = vsub(b, p) if abs(b[2]-p[2]) < abs(p[2]-u[2]) else vsub(p, u)
    n = vcross(tx, ty)
    n2 = vdot(n, n)
    n = (0., 0., -1.) if not n2 > 1e-14 else tuple(c/math.sqrt(n2) for c in n)
    if n[2] > 0:
        n = tuple(-c for c in n)
    ur = [min(max(.5*RADIUS*s/max(p[2], NEAR), 2./d), .05) for s, d in ((SX, W), (SY, H))]
    g = frac(uv[0]*W*coeff[0]+uv[1]*H*coeff[1])
    rx, ry = frac(52.9829189*g)*2-1, frac(37.4136*g)*2-1
    k = 1/math.sqrt(max(rx*rx+ry*ry, 1e-4))
    rx, ry = rx*k, ry*k
    ax, ay = (rx*ur[0], ry*ur[1]), (-ry*ur[0], rx*ur[1])
    ob = 0.
    for kx, ky in KERNEL:
        s = (uv[0]+kx*ax[0]+ky*ay[0], uv[1]+kx*ax[1]+ky*ay[1])
        sd = depth(surface, math.floor(s[0]*W), math.floor(s[1]*H))
        v = vsub(position(s, sd), p)
        d2 = vdot(v, v)
        dist = math.sqrt(max(d2, 1e-12))
        horizon = min(max((vdot(n, v)/dist-.08)/.92, 0), 1)
        rng = min(max(1-dist/RADIUS, 0), 1)**2
        if sd < .99999 and d2 > 1e-10 and 0 < s[0] < 1 and 0 < s[1] < 1:
            ob += horizon*rng
    return min(max(1-STRENGTH*ob*.25, 0), 1)


def composite(surface, coeff, x0, x1, y0, y1):
    """WorldComposite's AO: the depth-weighted tent over the four surrounding half-res texels."""
    hw, hh = W//2, H//2
    cache = {}

    def half(i, j):
        if (i, j) not in cache:
            cache[(i, j)] = ao_texel(surface, coeff, i, j)
        return cache[(i, j)]
    out = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            center = linear(depth(surface, x, y))
            gx, gy = (x+.5)/W*hw-.5, (y+.5)/H*hh-.5
            bx, by = math.floor(gx), math.floor(gy)
            fx, fy = gx-bx, gy-by
            acc = tot = 0.
            closest, nearest = 1e20, 1.
            for k in range(4):
                bit = (k & 1, k >> 1)
                i, j = max(0, min(hw-1, bx+bit[0])), max(0, min(hh-1, by+bit[1]))
                delta = abs(linear(depth(surface, 2*i+1, 2*j+1))-center)
                w = ((fx if bit[0] else 1-fx)*(fy if bit[1] else 1-fy))*math.exp(-delta/max(.15, center*.01))
                v = half(i, j)
                acc, tot = acc+v*w, tot+w
                if delta < closest:
                    closest, nearest = delta, v
            out[(x, y)] = nearest if tot < .02 else acc/tot
    return out


def high_pass(v, w=3):
    n = len(v)
    return math.sqrt(sum((v[k]-sum(v[max(0, k-w):k+w+1])/len(v[max(0, k-w):k+w+1]))**2 for k in range(n))/n)


def row_stripes(out, x0, x1, y0, y1, window=8):
    """Mean over narrow column windows of the high-passed row profile: what a viewer sees as horizontal bands."""
    vals = []
    for xa in range(x0, x1-window+1, window):
        rows = [sum(out[(x, y)] for x in range(xa, xa+window))/window for y in range(y0, y1)]
        vals.append(high_pass(rows))
    return sum(vals)/len(vals)


SURFACES = (('groove .04 yd, pitch .044', dict(kind='cylinder', groove=.04, pitch=40)),
            ('groove .10 yd, pitch .088', dict(kind='cylinder', groove=.10, pitch=20)),
            ('box corner', dict(kind='corner')))


def check_rotation():
    (ox, oy), (nx, ny) = lowpass(rotation_field(OLD)), lowpass(rotation_field(NEW))
    assert ox/oy > 1.8, (ox, oy)                    # old: row-coherent rotation (the stripes' source)
    assert .8 < nx/ny < 1.25 and nx < ox/1.8, (nx, ny)  # new: no preferred direction, nothing survives a row average
    return {'old': {'row_average': ox, 'column_average': oy, 'anisotropy': ox/oy},
            'new': {'row_average': nx, 'column_average': ny, 'anisotropy': nx/ny}}


def check_stripes():
    x0, x1, y0, y1 = W//2-48, W//2+48, H//2-40, H//2+40
    report = []
    for name, spec in SURFACES:
        s = Surface(**spec)
        old = composite(s, OLD, x0, x1, y0, y1)
        new = composite(s, NEW, x0, x1, y0, y1)
        mean = sum(new.values())/len(new)
        so, sn = row_stripes(old, x0, x1, y0, y1), row_stripes(new, x0, x1, y0, y1)
        assert mean < .9, (name, mean)  # the AO really occludes here
        assert sn <= so*1.001, (name, so, sn)
        report.append({'surface': name, 'mean_ao': mean, 'old_row_stripe_rms': so, 'new_row_stripe_rms': sn})
    corner = next(r for r in report if r['surface'] == 'box corner')
    assert corner['new_row_stripe_rms'] < corner['old_row_stripe_rms']*.6, corner
    return report


def audit_source():
    text = fp.src('effects.hlsl').read_text()
    impl = text[text.index('float4 AOImpl('):text.index('float4 AO(float2 uv')]
    assert '0.03355528, 0.002918575' in impl and '0.06711056' not in impl, 'AOImpl must hash the AO texel index'
    build = json.loads(fp.src('shader-build.json').read_text())
    digest = hashlib.sha256(fp.src('effects.hlsl').read_bytes()).hexdigest()
    assert build['source_sha256'] == digest, 'shaders/shader-build.json is stale: run scripts/shaders/compile_shaders.py'
    slots = {k: build['shaders'][k]['static_instruction_slots'] for k in ('AO', 'AOContactBloom', 'Composite')}
    assert all(v <= 512 for v in slots.values()), slots
    return slots


def run():
    rotation_report = check_rotation()
    stripes = check_stripes()
    slots = audit_source()
    report = {'result': 'pass', 'game_launched': False, 'gpu_test': False, 'rotation': rotation_report,
              'stripes': stripes, 'static_instruction_slots': slots,
              'test_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'limitations': ['CPU float32/float64 reference of AOImpl and the composite tent on synthetic surfaces.']}
    output = fp.output_dir()/'ao-rotation-stripes-validation.json'
    output.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS rotation', json.dumps(rotation_report), 'stripes', json.dumps(stripes), 'slots', slots)


if __name__ == '__main__':
    run()
