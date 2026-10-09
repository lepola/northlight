#!/usr/bin/env python3
# northlight-test:
"""0.3.201 (task 18): AOBlur denoises the IGN-rotated contact AO without bleeding across silhouettes.

CPU reference only: no game, graphics device or GPU. The 8-tap AO rotates its kernel per AO texel (interleaved
gradient noise), which in occluded creases (character skin, cloth folds) leaves a regular dark diamond lattice;
WorldComposite only bilinear-upsamples the half-resolution AO, so nothing filtered it. AOBlur is a half-resolution
pass: 5x5 AO texels (weights .5,1,1,1,.5 per axis) kept on the centre's plane and normal, sky/water taps weight 0,
bloom rgb passed through. This test runs a float32 emulation of AOImpl (as test_ao_rotation_stripes.py) and of
AOBlur exactly as written in shaders/effects.hlsl, on synthetic raw-D24 depth surfaces:
  (1) pattern: RMS of (AO - its 5x5 box mean) over interior texels, raw versus blurred, on a wide-grooved
      cylinder, an inner box corner and a floor meeting a wall at a grazing angle: blurred must be <= raw/4. The
      two finely grooved cylinders of the stripes test have geometric AO structure of the filter's own size (a
      noise-free AO already has a larger residual than the raw one), so they only need >= 1.5x and to end at or
      below that noise-free floor;
  (2) mean preservation: the interior mean AO moves by < .02;
  (3) silhouette: a cylinder in front of a wall .6 behind it and in front of sky: edge texels of the far wall
      must not be darkened, and edge texels of the cylinder not lightened, by the other surface (blurred versus
      the plain mean of the same surface's raw AO in the 5x5), and must stay inside that surface's raw min/max;
  (4) passthrough of the sky centre (all four channels) and of the bloom rgb at a surface centre;
  (5) source and wiring audits (effects.hlsl, shader-build.json, compile_shaders.py, renderer.cpp).
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
HW, HH = W//2, H//2
NEAR, FAR, PART = .2, 1277., .94
SX, SY = 1.2686, 1.9626
RADIUS, STRENGTH = 2., .6
SKY = .99999
SPAN = (1 << 24)-1
COEFF = (0.03355528, 0.002918575)
KERNEL = [(.3234, .1339), (-.2488, .6005), (-.8315, -.3444), (.3827, -.9239),
          (.8315, -.3444), (.2488, .6005), (-.3234, .1339), (-.3827, -.9239)]
RES_RATIO = 4.        # blurred residual <= raw residual / 4
MEAN_TOL = .02
GROOVE_RATIO = 1.5
EDGE_TOL = .02        # silhouette bleed tolerance (AO units)
CENTRE = (HW//2, HH//2)   # AO texel the windows are centred on


def f32(x):
    return struct.unpack('f', struct.pack('f', x))[0]


def frac(x):
    return x-math.floor(x)


class Surface:
    """Raw D24 depth (with the .94 world partition) seen from 18 degrees above.
    cylinder: vertical cylinder (optionally grooved) in front of a far wall (wall=None: sky); corner: inner box
    corner; wedge: floor (camera 1.6 high) meeting a wall at a grazing angle; sky: nothing."""
    def __init__(self, kind, depth=3.2, groove=0., pitch=20, wall=7.):
        self.kind, self.D, self.C, self.m, self.wall = kind, depth, groove/2, pitch, wall
        self.pitch = math.radians(18)
        self.cache = {}

    def raw(self, x, y):
        x, y = max(0, min(W-1, x)), max(0, min(H-1, y))
        if (x, y) not in self.cache:
            self.cache[(x, y)] = self._raw(x, y)
        return self.cache[(x, y)]

    def _raw(self, x, y):
        sx, sy = (2*(x+.5)/W-1)/SX, (1-2*(y+.5)/H)/SY
        dz = sy*math.sin(self.pitch)+math.cos(self.pitch)
        dy = sy*math.cos(self.pitch)-math.sin(self.pitch)
        if self.kind == 'sky':
            return 1.
        if self.kind == 'corner':
            t = (self.D+.4)/(dz+abs(sx))
        elif self.kind == 'wedge':
            tw = 4.9/dz
            t = min(tw, -1.6/dy) if dy < 0 else tw
        else:
            t, r, hit = (self.wall/dz if self.wall else None), .28, False
            for _ in range(4):
                a, b, c = sx*sx+dz*dz, -2*dz*self.D, self.D**2-r*r
                disc = b*b-4*a*c
                if disc < 0:
                    break
                t, hit = (-b-math.sqrt(disc))/(2*a), True
                r = .28+self.C*math.sin(math.atan2(t*sx, self.D-t*dz)*self.m)
            if t is None or (not hit and t is None):
                return 1.
        ndc = FAR/(FAR-NEAR)-NEAR*FAR/((FAR-NEAR)*t)
        return round(ndc*PART*SPAN)/SPAN


def depth(s, x, y):
    return min(1., max(0., f32(s.raw(x, y)/PART)))


def linear(d):
    return f32(NEAR*FAR/max(FAR-f32(d*(FAR-NEAR)), 1e-5))


def position(uv, d):
    z = linear(d)
    return (f32((uv[0]*2-1)*z/SX), f32((1-uv[1]*2)*z/SY), z)


def vsub(a, b):
    return (a[0]-b[0], a[1]-b[1], a[2]-b[2])


def vdot(a, b):
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]


def vcross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def safe_normal(n):
    n2 = vdot(n, n)
    n = (0., 0., -1.) if not n2 > 1e-14 else tuple(c/math.sqrt(n2) for c in n)
    return tuple(-c for c in n) if n[2] > 0 else n


def snap(i, j):
    """DepthTexelUV for AO texel (i, j) (may lie outside the texture: the uv clamp keeps the centre one texel in)."""
    cx = max(1, min(W-2, math.floor((i+.5)/HW*W+.25)))
    cy = max(1, min(H-2, math.floor((j+.5)/HH*H+.25)))
    return cx, cy


class Scene:
    """Per-surface caches of full-resolution positions and normals."""
    def __init__(self, surface, coeff=COEFF):
        self.s, self.coeff, self.pc, self.nc, self.nnc = surface, coeff, {}, {}, {}

    def pos(self, x, y):
        if (x, y) not in self.pc:
            self.pc[(x, y)] = position(((x+.5)/W, (y+.5)/H), depth(self.s, x, y))
        return self.pc[(x, y)]

    def normal(self, x, y):
        """SurfaceNormal at a full-res texel centre (same-surface neighbour selection)."""
        if (x, y) not in self.nc:
            p = self.pos(x, y)
            l, r, u, b = self.pos(x-1, y), self.pos(x+1, y), self.pos(x, y-1), self.pos(x, y+1)
            tx = vsub(r, p) if abs(r[2]-p[2]) < abs(p[2]-l[2]) else vsub(p, l)
            ty = vsub(b, p) if abs(b[2]-p[2]) < abs(p[2]-u[2]) else vsub(p, u)
            self.nc[(x, y)] = safe_normal(vcross(tx, ty))
        return self.nc[(x, y)]

    def neighbour_normal(self, x, y):
        if (x, y) not in self.nnc:
            p = self.pos(x, y)
            self.nnc[(x, y)] = safe_normal(vcross(vsub(self.pos(x+1, y), p), vsub(self.pos(x, y+1), p)))
        return self.nnc[(x, y)]


def ao_texel(sc, i, j):
    """AOImpl (contact only) for AO texel (i, j): returns the AO alpha; sky returns 1."""
    surface = sc.s
    cx, cy = snap(i, j)
    if depth(surface, cx, cy) >= SKY:
        return 1.
    uv = ((cx+.5)/W, (cy+.5)/H)
    p, n = sc.pos(cx, cy), sc.normal(cx, cy)
    ur = [min(max(.5*RADIUS*s/max(p[2], NEAR), 2./d), .05) for s, d in ((SX, W), (SY, H))]
    g = frac(uv[0]*W*sc.coeff[0]+uv[1]*H*sc.coeff[1])
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
        if sd < SKY and d2 > 1e-10 and 0 < s[0] < 1 and 0 < s[1] < 1:
            ob += horizon*rng
    return min(max(1-STRENGTH*ob*.25, 0), 1)


class RawAO:
    """The raw AO target (aoRaw): rgba per AO texel, the texel the POINT sample lands on (edge clamped)."""
    def __init__(self, sc):
        self.sc, self.cache = sc, {}

    def __call__(self, i, j):
        i, j = max(0, min(HW-1, i)), max(0, min(HH-1, j))
        if (i, j) not in self.cache:
            cx, cy = snap(i, j)
            sky = depth(self.sc.s, cx, cy) >= SKY
            rgb = (0., 0., 0.) if sky else (f32(.125+(i % 7)*.01), f32(.25+(j % 5)*.01), f32(.5+((i+j) % 3)*.01))
            self.cache[(i, j)] = rgb+(ao_texel(self.sc, i, j),)
        return self.cache[(i, j)]


def ao_blur(sc, raw, i, j, diag=None):
    """AOBlur for AO texel (i, j), line by line as in effects.hlsl (WaterInfo.x = 0: no water)."""
    cx, cy = snap(i, j)
    d = depth(sc.s, cx, cy)
    r = raw(i, j)
    if d >= SKY:
        return r
    p, n = sc.pos(cx, cy), sc.normal(cx, cy)
    tol = f32(max(.025, min(RADIUS*.1, p[2]*.002)))
    sum_ao = sum_w = f32(0.)
    for t in range(25):
        row = math.floor((t+.5)*.2)
        ox, oy = t-row*5-2, row-2
        qx, qy = snap(i+ox, j+oy)
        qd = depth(sc.s, qx, qy)
        q = sc.pos(qx, qy)
        w = (.5 if abs(ox) > 1.5 else 1.)*(.5 if abs(oy) > 1.5 else 1.)
        w *= 2.**(-abs(vdot(vsub(q, p), n))/tol*2.)
        agree = min(max(vdot(n, sc.neighbour_normal(qx, qy)), 0.), 1.)
        w *= agree*agree
        w *= 1. if qd < SKY else 0.
        if diag is not None and (qd >= SKY or abs(q[2]-p[2]) > .3):   # tap on another surface (or sky)
            diag['cross'] = diag.get('cross', 0.)+w
        sum_ao = f32(sum_ao+raw(i+ox, j+oy)[3]*w)
        sum_w = f32(sum_w+w)
    if diag is not None:
        diag['total'] = sum_w
    return r[:3]+(f32(sum_ao/sum_w) if sum_w > 1e-5 else r[3],)


class Fields:
    """Raw and blurred AO over an AO-texel window (a margin of 2 more raw texels feeds the blur)."""
    def __init__(self, surface, half=28, centre=CENTRE, coeff=COEFF):
        self.sc = Scene(surface, coeff)
        self.raw_fn = RawAO(self.sc)
        self.half, self.c = half, centre
        self.blur_cache = {}

    def raw(self, i, j):
        return self.raw_fn(i, j)[3]

    def blur(self, i, j):
        if (i, j) not in self.blur_cache:
            self.blur_cache[(i, j)] = ao_blur(self.sc, self.raw_fn, i, j)[3]
        return self.blur_cache[(i, j)]

    def zc(self, i, j):
        return self.sc.pos(*snap(i, j))[2]

    def sky(self, i, j):
        return depth(self.sc.s, *snap(i, j)) >= SKY

    def window(self, margin=0):
        h = self.half-margin
        return [(i, j) for j in range(self.c[1]-h, self.c[1]+h) for i in range(self.c[0]-h, self.c[0]+h)]


def smooth(f, i, j, rad=4, gap=.25):
    """Interior test: no sky and every texel within `rad` AO texels on the centre's surface (depth step < gap)."""
    z = f.zc(i, j)
    for dj in range(-rad, rad+1):
        for di in range(-rad, rad+1):
            if f.sky(i+di, j+dj) or abs(f.zc(i+di, j+dj)-z) > gap:
                return False
    return True


def residual(field, pts):
    """RMS of (AO - its 5x5 box mean) over pts."""
    acc = 0.
    for i, j in pts:
        m = sum(field(i+di, j+dj) for dj in range(-2, 3) for di in range(-2, 3))/25
        acc += (field(i, j)-m)**2
    return math.sqrt(acc/len(pts))


# (name, spec, kind). 'lowfreq' surfaces have AO structure much wider than the 5x5 filter, so any residual is
# pattern: blurred <= raw/4 is required. 'groove' surfaces (the stripes test's cylinders, groove period 5 and
# 10 AO texels) have genuine geometric AO structure of the filter's own size: even a noise-free AO (constant
# kernel rotation) has a residual above the raw one there, so 4x is not reachable without erasing the grooves.
# Measured raw/blurred is about 1.75; they must still improve >= 1.5x and end up at or below the noise-free floor.
SURFACES = (('groove .10 cylinder', 'groove', dict(kind='cylinder', groove=.10, pitch=20, wall=None)),
            ('groove .04 cylinder', 'groove', dict(kind='cylinder', groove=.04, pitch=40, wall=None)),
            ('wide groove .06 cylinder', 'lowfreq', dict(kind='cylinder', groove=.06, pitch=8, wall=None)),
            ('box corner', 'lowfreq', dict(kind='corner')),
            ('floor-wall crease (grazing)', 'lowfreq', dict(kind='wedge')))


def check_pattern():
    report = []
    for name, kind, spec in SURFACES:
        f = Fields(Surface(**spec), half=26)
        pts = [(i, j) for i, j in f.window() if smooth(f, i, j)]
        assert len(pts) > 300, (name, len(pts))
        floor = residual(Fields(Surface(**spec), half=26, coeff=(0., 0.)).raw, pts)   # noise-free reference
        rr, rb = residual(f.raw, pts), residual(f.blur, pts)
        mr, mb = sum(f.raw(*p) for p in pts)/len(pts), sum(f.blur(*p) for p in pts)/len(pts)
        assert mr < .95, (name, 'the AO must really occlude here', mr)
        assert rr > .005, (name, 'raw AO must carry a pattern', rr)
        report.append({'surface': name, 'class': kind, 'interior_texels': len(pts), 'raw_residual_rms': rr,
                       'blurred_residual_rms': rb, 'ratio': rr/rb if rb else float('inf'),
                       'noise_free_residual_rms': floor, 'raw_mean': mr, 'blurred_mean': mb, 'mean_shift': mb-mr})
        if kind == 'lowfreq':
            assert rb <= rr/RES_RATIO, ('residual not reduced 4x', report[-1])
        else:
            assert rb <= rr/GROOVE_RATIO and rb <= floor, ('grooved residual', report[-1])
        assert abs(mb-mr) < MEAN_TOL, ('mean shifted', report[-1])
    return report


def cross_weight(f, pts):
    """Share of an edge texel's blur weight that sits on taps of another surface (or sky): 0 = no bleed.
    Returns (worst share, fraction of the texels with more than 1%)."""
    shares = []
    for i, j in pts:
        diag = {}
        ao_blur(f.sc, f.raw_fn, i, j, diag)
        # total ~ 0: the shader returns the raw texel
        shares.append(diag.get('cross', 0.)/diag['total'] if diag['total'] > 1e-5 else 0.)
    return max(shares), sum(v > .01 for v in shares)/len(shares)


def edge_stats(f, pts, surface_z):
    """For edge texels: blurred minus the plain mean of the same surface's raw AO in the 5x5 (same-surface =
    centre depth within .3), its signed worst values, and whether the blur stays inside the same-surface raw range."""
    devs, outside = [], 0
    for i, j in pts:
        same = [f.raw(i+di, j+dj) for dj in range(-2, 3) for di in range(-2, 3)
                if not f.sky(i+di, j+dj) and abs(f.zc(i+di, j+dj)-surface_z(i, j)) < .3]
        b = f.blur(i, j)
        devs.append(b-sum(same)/len(same))
        if b < min(same)-1e-4 or b > max(same)+1e-4:
            outside += 1
    return devs, outside


def check_silhouette():
    out = {}
    # (a) cylinder .6 in front of a wall: far-wall texels next to the silhouette
    f = Fields(Surface('cylinder', groove=.10, pitch=20, wall=3.9), half=26, centre=(CENTRE[0]+33, CENTRE[1]))
    cyl = lambda i, j: f.zc(i, j) < 3.6
    edge = [(i, j) for i, j in f.window() if any(cyl(i+di, j+dj) != cyl(i, j) for di in range(-2, 3) for dj in range(-2, 3))]
    wall = [p for p in edge if not cyl(*p)]
    fore = [p for p in edge if cyl(*p)]
    assert len(wall) > 40 and len(fore) > 40, (len(wall), len(fore))
    wd, wo = edge_stats(f, wall, f.zc)
    fd, fo = edge_stats(f, fore, f.zc)
    # what an unweighted 5x5 average would do on the same texels (the bleed AOBlur must not have)
    naive = [sum(f.raw(i+di, j+dj) for dj in range(-2, 3) for di in range(-2, 3))/25-f.raw(i, j) for i, j in wall]
    (cw, cwn), (cf, cfn) = cross_weight(f, wall), cross_weight(f, fore)
    out['cylinder_wall'] = {'wall_cross_weight_max': cw, 'foreground_cross_weight_max': cf, 'foreground_texels_over_1pct_cross': cfn, 'wall_edge_texels': len(wall), 'wall_dev_min': min(wd), 'wall_dev_mean': sum(wd)/len(wd),
                            'wall_dev_max': max(wd), 'wall_outside_range': wo, 'foreground_edge_texels': len(fore),
                            'fore_dev_min': min(fd), 'fore_dev_mean': sum(fd)/len(fd), 'fore_dev_max': max(fd),
                            'fore_outside_range': fo,
                            'naive_unweighted_box_mean_minus_raw_wall': sum(naive)/len(naive)}
    assert min(wd) > -EDGE_TOL and max(wd) < EDGE_TOL, ('wall edge bleed', out['cylinder_wall'])
    # the limb is steep and grooved, so single texels legitimately differ from the 5x5 mean (the plane filter keeps few
    # taps): the foreground is judged by its mean deviation and by the weight that reaches across the silhouette
    assert -EDGE_TOL < sum(fd)/len(fd) < EDGE_TOL, ('foreground edge bleed', out['cylinder_wall'])
    assert cw < 1e-3 and cfn < .15, ('weight across the silhouette', out['cylinder_wall'])
    # a smooth cylinder (no grooves) in front of the same wall: no texel gives the other surface 1% of its weight
    h = Fields(Surface('cylinder', groove=0., wall=3.9), half=26, centre=(CENTRE[0]+33, CENTRE[1]))
    smooth_cyl = lambda i, j: h.zc(i, j) < 3.6
    both = [(i, j) for i, j in h.window() if any(smooth_cyl(i+di, j+dj) != smooth_cyl(i, j) for di in range(-2, 3) for dj in range(-2, 3))]
    sw, _ = cross_weight(h, both)
    out['smooth_cylinder_wall'] = {'edge_texels': len(both), 'cross_weight_max': sw}
    assert sw < .01, out['smooth_cylinder_wall']
    assert wo == 0 and fo == 0, out['cylinder_wall']
    # (b) grooved cylinder against sky: sky taps (AO 1, weight 0) must not lighten the edge
    g = Fields(Surface('cylinder', groove=.10, pitch=20, wall=None), half=26, centre=(CENTRE[0]+33, CENTRE[1]))
    edge = [(i, j) for i, j in g.window() if not g.sky(i, j) and any(g.sky(i+di, j+dj) for di in range(-2, 3) for dj in range(-2, 3))]
    assert len(edge) > 40, len(edge)
    gd, go = edge_stats(g, edge, g.zc)
    cs, _ = cross_weight(g, edge)
    out['cylinder_sky'] = {'cross_weight_max': cs, 'edge_texels': len(edge), 'dev_min': min(gd), 'dev_mean': sum(gd)/len(gd), 'dev_max': max(gd),
                           'outside_range': go}
    assert -EDGE_TOL < sum(gd)/len(gd) < EDGE_TOL and go == 0 and cs < 1e-3, out['cylinder_sky']
    return out


def check_passthrough():
    s = Fields(Surface('sky'), half=4)
    r = s.raw_fn(*CENTRE)
    b = ao_blur(s.sc, s.raw_fn, *CENTRE)
    assert b == r == (0., 0., 0., 1.), (r, b)
    c = Fields(Surface('corner'), half=4)
    r = c.raw_fn(*CENTRE)
    b = ao_blur(c.sc, c.raw_fn, *CENTRE)
    assert b[:3] == r[:3], (r, b)
    return {'sky_centre': list(b), 'surface_rgb_unchanged': True}


def audit_source():
    text = fp.src('effects.hlsl').read_text()
    assert 'float4 AOBlur(float2 uv : TEXCOORD0) : COLOR0' in text
    blur = text[text.index('float4 AOBlur('):text.index('float4 Composite(')]
    assert re.search(r'for\s*\(int i = 0; i < 25; \+\+i\)', blur), '25-tap loop'
    assert re.search(r'a\.x > 1\.5 \? 0\.5 : 1\.0\) \* \(a\.y > 1\.5 \? 0\.5 : 1\.0\)', blur), '.5 edge weights'
    assert 'float depthTolerance = max(0.025, min(Options.y * 0.1, p.z * 0.002));' in blur
    assert re.search(r'normalAgreement = saturate\(dot\(n, NeighbourNormal\(quv, q\)\)\);\s*weight \*= normalAgreement \* normalAgreement;', blur)
    assert re.search(r'qd < SKY_DEPTH && !IsWater\(quv, qd\) \? 1\.0 : 0\.0', blur), 'sky/water taps weight 0'
    assert 'return float4(raw.rgb' in blur
    impl = text[text.index('float4 AOImpl('):text.index('float4 AO(float2 uv')]
    assert '0.03355528, 0.002918575' in impl and '0.06711056' not in impl, 'AOImpl IGN coefficients changed'
    build = json.loads(fp.src('shader-build.json').read_text())
    assert build['source_sha256'] == hashlib.sha256(fp.src('effects.hlsl').read_bytes()).hexdigest(), \
        'shaders/shader-build.json is stale: run scripts/shaders/compile_shaders.py'
    b = build['shaders']['AOBlur']
    assert b['static_instruction_slots'] <= 512 and b['temporary_registers'] <= 32, b
    script = (fp.SCRIPTS/'shaders'/'compile_shaders.py').read_text()
    assert re.search(r'\("AOBlur",\s*"kAoBlurShader"\)', script)
    return {'static_instruction_slots': b['static_instruction_slots'], 'temporary_registers': b['temporary_registers']}


def audit_wiring():
    text = fp.src('renderer.cpp').read_text()
    fn = text[text.index('SetRenderTarget(0,aoRawSurface)')-400:]
    a = fn.index('SetRenderTarget(0,aoRawSurface)')
    seq = [fn.index(s, a) for s in ('quad(width/2,height/2),"AO pass"', 'SetRenderTarget(0,aoSurface)', 'bindEffects(aoRaw)',
                                    'SetPixelShader(aoBlurPS)', 'quad(width/2,height/2),"AO blur pass"',
                                    'auto legacyComposite', 'world->render(')]
    assert seq == sorted(seq), seq
    assert 'SetPixelShader(constants[7]==0.f?aoContactBloomPS:aoPS)' in fn[a:seq[0]]
    assert 'ext->SetTexture(2,ao); ext->SetPixelShader(compositePS)' in fn
    assert re.search(r'fold\?scene:nullptr,fold\?ao:nullptr\)', fn), 'the world fold reads the blurred ao'
    assert re.search(r'if\(fold&&!world->composited\)\{effectState\(\);bindEffects\(ao\);', fn), 'fold fallback binds ao'
    assert 'aoRaw' not in fn[seq[2]+len('bindEffects(aoRaw)'):].split('legacyComposite')[0].split('SetTexture(2,nullptr)')[1]
    for fmt in (r'!ao && error\(ext->CreateTexture\(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &ao,',
                r'!aoRaw && error\(ext->CreateTexture\(w/2, h/2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &aoRaw,'):
        assert re.search(fmt, text), fmt
    assert re.search(r'drop\(sceneSurface\); drop\(aoSurface\); drop\(aoRawSurface\); drop\(scene\); drop\(depthTex\); drop\(ao\); drop\(aoRaw\);', text), 'releaseResources'
    assert re.search(r'drop\(aoSurface\); drop\(ao\); drop\(aoRawSurface\); drop\(aoRaw\); drop\(depthTex\);', text), 'resize path'
    assert 'drop(aoBlurPS)' in text and 'CreatePixelShader(kAoBlurShader, &aoBlurPS)' in text
    return True


def run():
    pattern = check_pattern()
    silhouette = check_silhouette()
    passthrough = check_passthrough()
    shader = audit_source()
    wiring = audit_wiring()
    checks = {'residual_reduced_4x': all(r['blurred_residual_rms'] <= r['raw_residual_rms']/RES_RATIO for r in pattern if r['class'] == 'lowfreq'),
              'grooved_residual_reduced': all(r['blurred_residual_rms'] <= min(r['raw_residual_rms']/GROOVE_RATIO, r['noise_free_residual_rms']) for r in pattern if r['class'] == 'groove'),
              'mean_preserved': all(abs(r['mean_shift']) < MEAN_TOL for r in pattern),
              'silhouette_no_bleed': True, 'passthrough': True, 'shader_source': True, 'renderer_wiring': wiring}
    report = {'result': 'pass', 'game_launched': False, 'gpu_test': False, 'checks': checks, 'pattern': pattern,
              'silhouette': silhouette, 'passthrough': passthrough, 'shader': shader,
              'test_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'limitations': ['CPU float32/float64 reference of AOImpl and AOBlur on synthetic surfaces.']}
    for name, ok in checks.items():
        print(('PASS' if ok else 'FAIL'), name)
    for r in pattern:
        print('  %-30s residual raw %.5f blurred %.5f ratio %.1f mean shift %+.4f' % (
            r['surface'], r['raw_residual_rms'], r['blurred_residual_rms'], r['ratio'], r['mean_shift']))
    print('  silhouette', json.dumps(silhouette))
    output = fp.output_dir()/'ao-blur-validation.json'
    output.write_text(json.dumps(report, indent=2)+'\n')
    assert all(checks.values()), checks


if __name__ == '__main__':
    run()
