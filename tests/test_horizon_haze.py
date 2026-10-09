#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Horizon haze: numeric reference of the WorldComposite term, native tests of the
real CPU policy (horizon_haze.h, clang++ plain and ASan/UBSan) and source/manifest
integration checks. Not a GPU or appearance benchmark; no game, Wine or GPU.

Writes horizon-haze-validation.json to the test output dir. Before the shader recompile the
compiled-output checks report compile_pending; --require-compiled makes that a failure
(use it after compile_world_shaders.py).
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import json
import math
import random
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOG2E = 1.44269504

# world-shader-build.json of 0.3.153 (source 4cc8dba2...). Every other pass must stay
# byte-identical; compare with the manifest, never the (partly stale) .bin sidecars.
BEFORE_SOURCE = '4cc8dba282e13bc4bdfc7f62832cb4af74a73a4b38247cd98c1830980efdfe04'
BEFORE_COMPOSITE_SLOTS = 378
# 0.3.163: the lift colour moved from the literal (1,.8,.55) to c35.yzw. The manifest before
# that change (0.3.162) and its WorldComposite; the recompile may change WorldComposite only.
PRE_LIFT_SOURCE = 'c6d5a39fb0ae08aa7edd2070a1ee9d65d081be6b4d16528d8f764c32cfdca472'
# 0.3.165 edits WorldLighting and LocalDirect: while that compile is pending the manifest is 0.3.164's.
PRE_LAMP_SOURCE = '498ebc3583d5635479837296e5539ea5f2ee25ba678651109142fe288f6eaba3'
PRE_LIFT_COMPOSITE = '45c3bf3aa0a864ed601f34534555bd85f744c8b4e3f914f59ebb810bd28b322a'
PRE_LIFT_COMPOSITE_SLOTS = 467
# Expected after the lift-colour compile. WorldComposite: the lift mad reads c35.yzw instead of a def, the
# same instruction, so 467 (by inspection of WorldComposite.bin.asm). SourceVisibilityPS: +8 ring taps,
# estimated 440-460 but not knowable without the compiler.
# 0.3.174: WorldComposite folds the AO/bloom composite (s10 AO in the tent loop, bloom, original'): 498.
LIFT_COMPOSITE_SLOTS = 505  # 0.3.202 (rain mask): was 498
SOURCE_VISIBILITY_SLOTS = 492  # compiled at integration (was 281 before the wrap ring)
BEFORE = {
    'WorldNormals': '1e8d22d66a1022bad26c930dd719398eba02c22179084a275f4fe52bf54c1db5',
    'WorldLighting': 'e132fcd3de96d86e61a1b33cecfd981e8a633bd8098946b3addf571306a680bb',
    'WorldGI': '23dfdd6e1d08800f8958b278b09aebc97f0558f6b0ac83b5cf447891ef1466e7',
    'WorldFog': '4b40e8e5e49892a4f47b41bc21bb1079abf61664d7d623abc12a784efda189fe',
    'FogBlur': 'b48b12488d6233e53031b9f947e643a47d5892fc7fd478356a0a08037a3a1e78',
    'LocalDirect': '94b34eebb6a542f79c23643c9b9b25ce70d49318c36703e5abf1692604bd7c8f',
    'TemporalLight': '9dd41a793a99539b457cc5cab7ab3aeaf3638349106addaa6ef970fa163aceff',
    'LocalFog': '3439bb63d20ae45ae5cd2d7975e7d97231d3445ab086063d65af1d44232d4f70',
    'SourceVisibilityPS': '55e6fbcb330f068f97418c9059fc6433381f3ec206c6563aa03a7abf9f2a0a1f',
    'ShadowVS': '10172c530a92bd9cd5bbf13dff11ebbb53888c355b5b9b13a8aebe764215c165',
    'ShadowCacheVS': '54092470516c229529f1e86cadacb240ac6fcb2a877114b00e0cfed1d4f7a753',
    'ShadowPS': '318fe2a445a91294b3f1b0a4711ba226fee07397c1f2816d9d96d09a18fdbffd',
    'ShadowReplayPS': '194f5665d22f12a8c0e337be2938d036386c4e7982f048051a804714f85a592d',
    'ShadowUnion': 'd090109910314885bc31fdf67ebd4c393f95c499da6dbd0c055498981f28d4c6',
}
# northlight-renderer.log of 0.3.153: the two known terrain fog sets (Pz=+1) and far clip.
FOG_SETS = {'default': (-0.0017645, 1.0, 1.0), 'tanaris': (-0.0015527, 0.690, 1.0)}
FAR = 727.12


def clamp(v, lo, hi):
    return min(max(v, lo), hi)


# ---- CPU policy mirror (horizon_haze.h) ----
def fog_end(p, pz, known=True):
    if not known:
        return 0.
    slope = -p[0]*pz
    return p[1]/slope if slope > 0 and p[1] > 0 else 0.


def effective_end(end, far):
    return clamp(end if end > 0 else far, min(100, far), far)


def shape(end, start=75, band=6, terrain=1):
    s0 = end*start*.01
    return s0, (1/max(end-s0, 1) if terrain else 0.), LOG2E/math.sin(math.radians(band))


# ---- Shader mirror (world_effects.hlsl horizonHaze) ----
def amount(elevation, view_z, sky, s0, ramp, band_k, tau2):
    r = 1. if sky else clamp((view_z-s0)*ramp, 0., 1.)
    if r <= 0 or tau2 <= 0:
        return 0.
    band = 2**(-max(elevation, 0)*band_k)
    return r*r*(3-2*r)*(1-2**(-tau2*band))


def lift(world_xy, sun_zw):
    lobe = math.hypot(*sun_zw)
    horizontal = math.hypot(*world_xy)
    az = (world_xy[0]*sun_zw[0]+world_xy[1]*sun_zw[1])/max(horizontal, 1e-6)/max(lobe, 1e-6)
    return min(lobe*.64*(1.36-1.2*az)**-1.5, .25)


def composite(scene, haze, a, local_rgb, local_t):
    background = scene+(haze-scene)*a
    return background*local_t+local_rgb


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(v):
    return tuple(x/math.sqrt(dot(v, v)) for x in v)


NATIVE = r'''
#include "horizon_haze.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace NorthlightHorizonHaze;
static bool near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e*std::max(1.f,std::fabs(b));}
int main(){
    const float Pz=1,farZ=727.12f,color[3]={.6f,.5f,.35f},colorB[3]={.2f,.3f,.5f},unknown[4]={0,1,1,0};
    const float fogDefault[4]={-0.0017645f,1,1,1},fogTanaris[4]={-0.0015527f,.69f,1,1};
    // Fog end: signed Z, both handednesses, unknown / disabled / zero slope / wrong sign.
    assert(near(fogEnd(fogDefault,1),566.73f)&&near(fogEnd(fogTanaris,1),444.39f));
    const float mirrored[4]={0.0017645f,1,1,1};assert(near(fogEnd(mirrored,-1),566.73f));
    const float flat[4]={0,1,1,1},wrong[4]={.001f,1,1,1},noEnd[4]={-.001f,0,1,1},off[4]={-.001f,1,1,0};
    assert(fogEnd(unknown,1)==0&&fogEnd(flat,1)==0&&fogEnd(wrong,1)==0&&fogEnd(noEnd,1)==0&&fogEnd(off,1)==0&&fogEnd(nullptr,1)==0);
    const float huge[4]={-1e-30f,1e30f,1,1};assert(fogEnd(huge,1)==0); /* infinite */
    Settings q;const float sun[3]={.8f,.6f,0};
    // No fog seen yet: far-clip end, but NO haze (colour unknown).
    State s;s.update("Kalimdor",unknown,color,Pz,1);
    assert(s.valid&&!s.colorKnown&&s.effectiveEnd(farZ)==farZ);
    auto c=constants(s,q,farZ,1,true,true,sun,1);
    assert(c.haze[3]==0&&c.haze[0]==0&&c.sun[0]==0&&c.sun[1]==0);
    assert(near(c.shape[0],.75f*farZ)&&near(c.shape[1],1/(.25f*farZ)));
    // First known fog snaps; defaults give tau .7 and the Tanaris start 333 yd.
    s.update("Kalimdor",fogTanaris,color,Pz,2);
    assert(s.colorKnown&&near(s.end,444.39f)&&s.color[0]==color[0]);
    c=constants(s,q,farZ,1,true,true,sun,1);
    assert(near(c.haze[3],.7f*Log2e)&&near(c.shape[0],333.29f)&&near(c.shape[1],1/111.1f)&&near(c.shape[2],Log2e/std::sin(6*3.14159265f/180)));
    assert(near(c.sun[0],.8f*SunLobe)&&near(c.sun[1],.6f*SunLobe));
    // Smoothing (1 s) toward the other context's fog; monotone, no jump; held while unknown.
    float previous=s.end;
    for(int i=1;i<=240;++i){s.update("Kalimdor",fogDefault,colorB,Pz,2+i/60.);assert(s.end>=previous&&s.end<=566.74f);previous=s.end;}
    assert(near(s.end,566.73f,.02f)&&near(s.color[2],.5f,.02f));
    const float held=s.end;for(int i=1;i<=60;++i)s.update("Kalimdor",unknown,nullptr,Pz,6+i/60.);assert(s.end==held&&s.colorKnown);
    // A long pause advances at most .25 s; a clock step back or map change snaps.
    s.update("Kalimdor",fogTanaris,color,Pz,100);assert(s.end>445&&s.end<held);
    s.update("Kalimdor",fogTanaris,color,Pz,50);assert(near(s.end,444.39f));
    s.update("Azeroth",unknown,nullptr,Pz,51);assert(s.end==0&&!s.colorKnown);
    // Clamp to [100, far]; far below 100 wins.
    State n;const float nearFog[4]={-.02f,1,1,1};n.update("A",nearFog,color,Pz,0);assert(n.effectiveEnd(farZ)==100&&n.effectiveEnd(50)==50);
    const float farFog[4]={-.0005f,1,1,1};n.update("B",farFog,color,Pz,0);assert(n.effectiveEnd(farZ)==farZ);
    // Strength 0, fog off, zone 0: optical depth 0 (exact scene colour).
    State t;t.update("Kalimdor",fogTanaris,color,Pz,0);
    Settings zero;zero.strength=0;
    for(auto k:{constants(t,zero,farZ,1,true,true,sun,1),constants(t,q,farZ,1,false,true,sun,1),constants(t,q,farZ,0,true,true,sun,1)})
        assert(k.haze[3]==0&&k.haze[0]==0&&k.sun[0]==0);
    // Terrain 0: sky band only. Range/zone clamps. Night: no sun lobe.
    Settings sky;sky.terrain=0;assert(constants(t,sky,farZ,1,true,true,sun,1).shape[1]==0);
    Settings wide;wide.strength=100;wide.startPercent=10;wide.bandDegrees=40;
    auto w=constants(t,wide,farZ,9,true,true,sun,1);
    assert(near(w.haze[3],1.4f*4*Log2e)&&near(w.shape[0],.5f*444.39f)&&near(w.shape[2],Log2e/std::sin(15*3.14159265f/180)));
    assert(constants(t,q,farZ,1,true,true,sun,0).sun[0]==0&&constants(t,q,farZ,1,true,false,sun,1).sun[0]==0);
    const float nanSun[3]={NAN,0,0};assert(constants(t,q,farZ,1,true,true,nanSun,1).sun[0]==0);
    assert(constants(t,q,farZ,NAN,true,true,sun,1).haze[3]==constants(t,q,farZ,1,true,true,sun,1).haze[3]);
    // Asymmetric fog end: a longer end (surfacing, leaving an interior) snaps; beyond
    // +30% the colour snaps too; a shorter end approaches over ~1 s. After every known
    // frame the end is at least the newest measured end: the start never lags inside.
    {
        const float water[4]={-1.f/60,1,1,1},blue[3]={.05f,.2f,.3f};
        State u;u.update("Azeroth",fogDefault,color,Pz,0);
        float last=u.end;
        for(int i=1;i<=120;++i){u.update("Azeroth",water,blue,Pz,i/60.);assert(u.end<=last&&u.end>=60);last=u.end;
            if(i==1)assert(u.end>500&&u.color[2]<.35f&&u.color[2]>.34f);}                 /* diving: smooth */
        assert(u.end<130);
        u.update("Azeroth",fogDefault,color,Pz,2.1);                                            /* surfacing: snap */
        assert(u.end==fogEnd(fogDefault,Pz)&&u.color[0]==color[0]&&u.color[2]==color[2]);
        assert(near(constants(u,q,farZ,1,true,true,sun,1).shape[0],.75f*566.73f));
        u.update("Azeroth",fogTanaris,colorB,Pz,2.2);                                           /* shorter: smooth */
        assert(u.end>444.4f&&u.end<566.7f&&u.color[0]>colorB[0]&&u.color[0]<color[0]);
        u.update("Azeroth",fogDefault,colorB,Pz,2.21);                                          /* +<30%: end snaps, colour smooth */
        assert(u.end==fogEnd(fogDefault,Pz)&&u.color[0]!=colorB[0]);
        std::mt19937 rng(9);const float* sets[]={fogDefault,fogTanaris,water,unknown};
        for(int i=0;i<20000;++i){const float* p=sets[rng()%4];u.update("Azeroth",p,color,Pz,3+i/60.);
            const float m=fogEnd(p,Pz);if(m>0)assert(u.end>=m);}
    }
    // Haze colour stays <= 1 toward the sun for any fog colour (shader literals mirrored);
    // fog up to .8 keeps the unchanged lobe.
    for(float bright:{0.f,.3f,.8f,.81f,.9f,.97f,1.f})for(float elevation:{0.f,.2f,.6f,1.2f}){
        const float fog[3]={bright,bright*.9f,bright*.7f},dir[3]={std::cos(elevation),0,std::sin(elevation)};
        State b;b.update("Northrend",fogDefault,fog,Pz,0);
        auto k=constants(b,q,farZ,4,true,true,dir,1);
        const float lobe=std::hypot(k.sun[0],k.sun[1]);
        if(bright<=.8f)assert(near(lobe,SunLobe*std::cos(elevation),1e-6f));
        for(int a=0;a<=360;++a){const float az=std::cos(a*3.14159265f/180);
            const float lift=std::min(lobe*.64f*std::pow(1.36f-1.2f*az,-1.5f),LiftCap);
            for(unsigned i=0;i<3;++i)assert(k.haze[i]*(1+lift*Warm[i])<=1+1e-6f);}
    }
    // lift colour (c35.yzw): default Warm; a given colour is clamped to [0,1], NaN falls back
    // to Warm, a zero channel is safe, and the <=1 limit holds for that colour.
    {
        auto k=constants(t,q,farZ,1,true,true,sun,1);
        assert(k.lift[0]==Warm[0]&&k.lift[1]==Warm[1]&&k.lift[2]==Warm[2]);
        const float bad[3]={NAN,2,-1};k=constants(t,q,farZ,1,true,true,sun,1,bad);
        assert(k.lift[0]==Warm[0]&&k.lift[1]==1&&k.lift[2]==0);
        const float orange[3]={1,.57f,0},green[3]={.76f,1,.64f};
        for(const float* hue:{orange,green})for(float bright:{.3f,.85f,.97f,1.f})for(float elevation:{0.f,.3f}){
            const float fog[3]={bright,bright,bright},dir[3]={std::cos(elevation),0,std::sin(elevation)};
            State b;b.update("Kalimdor",fogDefault,fog,Pz,0);
            auto h=constants(b,q,farZ,4,true,true,dir,1,hue);
            const float lobe=std::hypot(h.sun[0],h.sun[1]);assert(std::isfinite(lobe));
            for(int a=0;a<=360;++a){const float az=std::cos(a*3.14159265f/180);
                const float lift=std::min(lobe*.64f*std::pow(1.36f-1.2f*az,-1.5f),LiftCap);
                for(unsigned i=0;i<3;++i)assert(h.haze[i]*(1+lift*h.lift[i])<=1+1e-6f);}
        }
    }
    auto d=constants(t,q,farZ,1,true,true,sun,1);
    std::printf("{\"tanaris_start\":%.4f,\"tanaris_inverse_ramp\":%.8f,\"band_k\":%.6f,\"tau2\":%.6f}\n",d.shape[0],d.shape[1],d.shape[2],d.haze[3]);
}
'''


def native():
    with tempfile.TemporaryDirectory(prefix='fr-horizon-haze-') as temp:
        folder = Path(temp)
        (folder/'test.cpp').write_text(NATIVE)
        out = None
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(folder/'test.cpp'), '-o', str(folder/'test')], check=True)
            out = json.loads(subprocess.run([str(folder/'test')], check=True, capture_output=True, text=True).stdout)
        return out


def block(text, start):
    a = text.index(start)
    b = text.index('{', a)
    depth, end = 1, b+1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[a:end]


def main():
    require_compiled = '--require-compiled' in sys.argv
    tanaris_end = effective_end(fog_end(FOG_SETS['tanaris'], 1), FAR)
    s0, ramp, band_k = shape(tanaris_end)
    tau2 = .7*LOG2E

    # 1. Identity: at or nearer than the start, at ANY elevation, and with strength 0.
    for deg10 in range(-900, 901, 5):
        e = math.sin(math.radians(deg10/10))
        for z in [0, 1, 50, 100, 200, 300, s0-1e-6, s0]:
            assert amount(e, z, False, s0, ramp, band_k, tau2) == 0
        for z in [0, 400, 1e4]:
            assert amount(e, z, False, s0, ramp, band_k, 0) == 0 and amount(e, z, True, s0, ramp, band_k, 0) == 0
    for name, fog in FOG_SETS.items():  # the 0.3.108 complaint: mid landscape untouched
        end = effective_end(fog_end(fog, 1), FAR)
        start, r, k = shape(end)
        assert start > .74*end and all(amount(0, z, False, start, r, k, tau2) == 0 for z in range(0, int(start)))
    scene_values = [0., .123, .5, 1., 3.7]
    for v in scene_values:  # the shader returns the scene colour itself; with a=0 even the mix is exact
        assert composite(v, .6, 0., 0., 1.) == v

    # 2. Monotonic in distance (terrain) and decreasing with elevation above the horizon.
    for deg in range(-90, 91):
        e = math.sin(math.radians(deg))
        previous = 0
        for z in range(0, 1500):
            a = amount(e, z, False, s0, ramp, band_k, tau2)
            assert previous-1e-15 <= a < 1
            previous = a
        # 3. Ridge/sky: terrain at or beyond the game's fog end equals sky at the same elevation.
        assert amount(e, tanaris_end, False, s0, ramp, band_k, tau2) == amount(e, 5000, True, s0, ramp, band_k, tau2)
    previous = 1
    for tenth in range(0, 901):
        a = amount(math.sin(math.radians(tenth/10)), 0, True, s0, ramp, band_k, tau2)
        assert a <= previous
        previous = a
    below = {amount(math.sin(math.radians(-d)), 0, True, s0, ramp, band_k, tau2) for d in range(0, 91)}
    assert len(below) == 1  # full below the horizon: distance alone protects near ground
    # Continuity at both ramp ends and across the horizon.
    for edge in (s0, tanaris_end):
        assert abs(amount(0, edge-1e-3, False, s0, ramp, band_k, tau2)-amount(0, edge+1e-3, False, s0, ramp, band_k, tau2)) < 1e-6
    assert abs(amount(-1e-7, 0, True, s0, ramp, band_k, tau2)-amount(1e-7, 0, True, s0, ramp, band_k, tau2)) < 1e-5
    sky_t = {str(d): 1-amount(math.sin(math.radians(d)), 0, True, s0, ramp, band_k, tau2) for d in (0, 2, 5, 10, 15, 20, 30)}
    assert abs(sky_t['0']-.4966) < 1e-3 and abs(sky_t['5']-.738) < 2e-3 and abs(sky_t['10']-.875) < 2e-3
    assert abs(sky_t['20']-.973) < 2e-3 and sky_t['30'] > .99

    # 4. Camera orientation: 10k views, both handednesses, projection scales. The shader's
    # world ray (elevation and horizontal azimuth) equals the true world direction.
    rng = random.Random(154)
    max_error = 0
    sun = (.8*.03, .6*.03)
    for _ in range(10000):
        forward = unit(tuple(rng.uniform(-1, 1) for _ in range(3)))
        right = unit(cross(forward, (0, 0, 1)))
        up = cross(right, forward)
        angle = rng.uniform(-math.pi, math.pi)
        rolled_right = tuple(a*math.cos(angle)+b*math.sin(angle) for a, b in zip(right, up))
        rolled_up = tuple(b*math.cos(angle)-a*math.sin(angle) for a, b in zip(right, up))
        world = unit(tuple(rng.uniform(-1, 1) for _ in range(3)))
        for sign in (-1, 1):
            axes = (rolled_right, rolled_up, tuple(sign*x for x in forward))
            view = tuple(dot(world, axis) for axis in axes)
            if view[2]*sign < .01:
                continue
            sx, sy = rng.uniform(.5, 3), rng.uniform(.5, 3)
            ndc = (view[0]*sx/(view[2]*sign), view[1]*sy/(view[2]*sign))
            ray = (ndc[0]/sx, ndc[1]/sy, sign)
            w = tuple(sum(ray[i]*axes[i][j] for i in range(3)) for j in range(3))
            elevation = w[2]/math.sqrt(dot(ray, ray))
            max_error = max(max_error, abs(elevation-world[2]))
            assert abs(elevation-world[2]) < 1e-12
            if math.hypot(world[0], world[1]) > 1e-3:
                assert abs(lift(w[:2], sun)-lift(world[:2], sun)) < 1e-9
            assert abs(amount(elevation, 0, True, s0, ramp, band_k, tau2)-amount(world[2], 0, True, s0, ramp, band_k, tau2)) < 1e-12

    # 5. Sun lobe: capped, largest toward the sun, symmetric, zero at night, straight up safe.
    lifts = [lift((math.cos(math.radians(a)), math.sin(math.radians(a))), (.03, 0)) for a in range(0, 360)]
    assert max(lifts) == lifts[0] <= .25 and lifts[180] == min(lifts) > 0
    assert all(abs(lifts[a]-lifts[360-a]) < 1e-12 for a in range(1, 180))
    assert lift((1, 0), (0, 0)) == 0 and lift((0, 0), sun) >= 0
    assert lift((1, 0), (.03*math.cos(math.radians(60)), 0)) < lift((1, 0), (.03, 0))  # high sun: weaker

    # 6. Order: haze extinguishes the scene, local scattering stays in front (never attenuated).
    for _ in range(1000):
        scene, haze, local, a, lt = (rng.random() for _ in range(5))
        background = scene+(haze-scene)*a
        assert min(scene, haze)-1e-15 <= background <= max(scene, haze)+1e-15
        assert composite(scene, haze, a, local, lt) >= local*1-1e-15

    # 7. The real CPU policy (fog end, smoothing/hold/snap, clamps, off switches).
    cpp = native()
    assert abs(cpp['tanaris_start']-s0) < 1e-2 and abs(cpp['tanaris_inverse_ramp']-ramp) < 1e-7
    assert abs(cpp['band_k']-band_k) < 1e-4 and abs(cpp['tau2']-tau2) < 1e-5

    # 8. Source integration.
    shader = fp.src('world_effects.hlsl').read_text()
    cpu = fp.src('world_renderer.h').read_text()
    composite_src = shader.split('float4 WorldComposite(', 1)[1].split('// Separate geometry pass:', 1)[0]
    helper = block(shader, 'float3 horizonHaze(')
    # 0.3.202 (rain mask): haze, then the fog mad, in one lerp expression back toward the unfogged colour on rain-mask pixels.
    assert composite_src.index('horizonHaze(color,centerUV,viewZ,d>=.99999&&liquid<=0)') < composite_src.index('fog.a,fog.rgb)')
    assert 'if(PassInfo.z<.5){\n        float3 unfogged=color;' in composite_src
    assert '[branch]if(range<=0||HorizonHaze.w<=0)return color;' in helper
    assert helper.index('[branch]') < helper.index('viewPositionDistance') and 'tex2D' not in helper and 'loop' not in helper
    assert 'float range=sky?1:saturate((viewZ-HorizonShape.y)*HorizonShape.z);' in helper
    assert 'return lerp(color,haze,amount);' in helper
    # Literals mirrored by horizon_haze.h (LiftCap, LobePeak, Warm) for the haze colour <= 1 cap.
    assert 'float lift=min(lobe*.64*pow(1.36-1.2*azimuth,-1.5),.25);' in helper
    assert 'float3 haze=HorizonHaze.rgb*mad(lift,ShadowRange.yzw,1);' in helper  # lift colour from c35.yzw
    assert shader.count('ShadowRange.yzw') == 1 and 'float3(1,.8,.55)' not in shader
    policy = fp.src('horizon_haze.h').read_text()
    assert 'LiftCap=.25f,LobePeak=10.01f' in policy and 'Warm[3]={1,.8f,.55f}' in policy
    assert 'SunLobe=.06f;' in policy  # the warm lobe toward a low sun doubled
    assert shader.count('horizonHaze(') == 2  # defined once, used only by WorldComposite
    for decl in ('float4 HorizonHaze : register(c34);', 'float4 HorizonShape : register(c57);', 'float4 HorizonSun : register(c67);'):
        assert decl in shader
    for other in ('HorizonHaze.', 'HorizonShape.', 'HorizonSun.'):
        assert shader.count(other) == helper.count(other), other
    assert cpu.count('d->SetPixelShaderConstantF(0,&c[0][0],68)') == 2  # register bank unchanged
    upload = cpu.index('c[30][0]=waterMask?1.f:0.f;d->SetPixelShaderConstantF(0,&c[0][0],68);')
    assert cpu.index('c[57][1]=haze.shape[0];c[57][2]=haze.shape[1];c[57][3]=haze.shape[2];c[67][2]=haze.sun[0];c[67][3]=haze.sun[1];') < upload
    blur_end = cpu.index('"volume blur vertical"))return false;}')
    assert blur_end < cpu.index('d->SetPixelShaderConstantF(34,haze.haze,1);') < cpu.index('"world composite"')
    assert 'farZ,hazeZone,effects.fog,celestialValid,hazeSun,sourceWeights[0],hazeLift,wx.hazeTauScale());' in cpu
    assert 'float hazeLift[3];NorthlightSunHue::horizonLift(glowHueFrame,hazeLift);' in cpu
    assert cpu.index('c[35][1]=haze.lift[0];c[35][2]=haze.lift[1];c[35][3]=haze.lift[2];') < upload
    assert 'horizonHazeState.update(active->map,legacyFog.parameters,legacyFog.color,projection[2],' in cpu
    assert 'NorthlightCelestialProfiles::horizonHaze(volumePalette,c[31][3])' in cpu

    # 9. Compiled output (after compile_world_shaders.py): the others byte-identical.
    manifest = json.loads(fp.src('world-shader-build.json').read_text())
    source_sha = hashlib.sha256(fp.src('world_effects.hlsl').read_bytes()).hexdigest()
    compiled = manifest['source_sha256'] == source_sha
    unchanged = []
    if compiled:
        for name, info in manifest['shaders'].items():
            assert info['static_instruction_slots'] <= 512 and info['temporary_registers'] <= 32, name  # a count (highest rN + 1): ps_3_0 has r0..r31
            # 0.3.159: TemporalLight changed on purpose (R1 soft removal); its own compile gate is
            # verify_shadow_removal_smoothing_compile.py, so this haze test no longer pins it.
            # SourceVisibilityPS gains the wrap ring taps (test_solar_volume pins the source).
            # WorldLighting (baseline alpha) and LocalDirect (daylight sunlit factor) change on purpose.
            # 0.3.175: WorldNormals' wide-sample threshold .85 -> .95 (r76) and both-or-neither wide pair (r77).
            # 0.3.185: RemovalSmooth is new (the removal smoothing's own half-res pass; TemporalLight shrinks).
            # 0.3.197: WorldGI blends a same-key probe re-publication from the previous SH (task 13).
            # 0.3.198 (rain): WorldFog (air floor from c59.w) changes on purpose.
            if name not in ('WorldComposite', 'TemporalLight', 'SourceVisibilityPS', 'WorldLighting', 'LocalDirect', 'WorldNormals', 'RemovalSmooth', 'WorldGI', 'WorldFog', 'FogClouds', 'FogTemporal', 'LocalFog', 'LocalFogCombine', 'LocalFogBatchCapped'):  # FogClouds (0.3.199) is new, not in BEFORE; LocalFog (0.3.199) gains the near ramp, 0.3.205 (gh#20) returns the raw batch sum on purpose, LocalFogCombine and LocalFogBatchCapped are new
                assert info['sha256'] == BEFORE[name], name
                unchanged.append(name)
        assert sorted(unchanged) == sorted(k for k in BEFORE if k not in ('TemporalLight', 'SourceVisibilityPS', 'WorldLighting', 'LocalDirect', 'WorldNormals', 'WorldGI', 'WorldFog', 'LocalFog'))
        assert manifest['shaders']['WorldNormals']['static_instruction_slots'] == 434  # r77: both wide samples or neither
        assert manifest['shaders']['WorldFog']['static_instruction_slots'] == 512  # 0.3.198 (rain): the same 512 slots (the literal .0017 became c59.w)
        assert SOURCE_VISIBILITY_SLOTS is not None, 'TODO(lead): pin SOURCE_VISIBILITY_SLOTS to the compiled count (%d)' % manifest['shaders']['SourceVisibilityPS']['static_instruction_slots']
        assert manifest['shaders']['SourceVisibilityPS']['static_instruction_slots'] == SOURCE_VISIBILITY_SLOTS <= 512
        assert manifest['shaders']['WorldComposite']['sha256'] != '772d438995ed33d36db7e26f9b169a5effd94a876ce5d8b8af9fd8aad5381990'
        assert manifest['shaders']['WorldComposite']['sha256'] != PRE_LIFT_COMPOSITE  # lift colour compiled in
        assert manifest['shaders']['WorldComposite']['static_instruction_slots'] == LIFT_COMPOSITE_SLOTS
    else:
        assert manifest['source_sha256'] in (BEFORE_SOURCE, PRE_LIFT_SOURCE, PRE_LAMP_SOURCE), 'manifest is neither 0.3.153, 0.3.162, 0.3.164 nor this source'
        assert not require_compiled, 'compile pending: run compile_world_shaders.py first'

    result = dict(
        game_launched=False, gpu_tested=False, numeric_reference_passed=True, native_policy_passed=True,
        compiled=compiled, compile_pending=not compiled, camera_cases=10000, max_elevation_error=max_error,
        unchanged_shader_passes=unchanged,
        composite_slots_before=BEFORE_COMPOSITE_SLOTS,
        composite_slots_after=manifest['shaders']['WorldComposite']['static_instruction_slots'] if compiled else None,
        composite_temps_after=manifest['shaders']['WorldComposite']['temporary_registers'] if compiled else None,
        defaults=dict(strength=50, optical_depth=.7, start_percent=75, band_degrees=6, terrain=1),
        fog_end_yards={k: effective_end(fog_end(v, 1), FAR) for k, v in FOG_SETS.items()},
        terrain_start_yards={k: shape(effective_end(fog_end(v, 1), FAR))[0] for k, v in FOG_SETS.items()},
        unknown_fog_start_yards=shape(FAR)[0],
        sky_transmission_by_degrees=sky_t,
        tanaris_terrain_amount_by_yards={str(z): amount(0, z, False, s0, ramp, band_k, tau2) for z in (300, 333, 350, 400, 444, 600)},
        sun_lift_max=max(lifts), sun_lift_opposite=min(lifts))
    (fp.output_dir()/'horizon-haze-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    if not compiled:
        print('PASS (source + numeric + native); compiled-output checks PENDING until compile_world_shaders.py')


if __name__ == '__main__':
    main()
