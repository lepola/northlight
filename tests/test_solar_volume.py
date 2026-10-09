#!/usr/bin/env python3
# northlight-test:
"""Portable numeric reference checks for finite geometry-shadowed solar volume.

These checks exercise CPU equivalents of the shader equations and source
contracts. They do not execute HLSL or prove the in-game appearance/GPU timing.
"""
from __future__ import annotations
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import math
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent
N = 64
SPACING = 8.0
AIR = (.0055, .0037, 64.0, 48.0)
NIGHT_EXTRA = (.0089, .00705)
FOREST_DAY = .0035
FOREST_NIGHT = .0066
RANGE = 128.0


def clamp(x):
    return min(max(x, 0.0), 1.0)


def smooth(x):
    x = clamp(x)
    return x*x*(3-2*x)


def node(zone, ground=100., indoor=False, wet=0., basin=0.):
    # Representative general forest (Elwynn) plus Duskwood/STV.
    if indoor or zone == 0:
        return (ground, 0., 0., 0.)
    if zone == 10:
        return (ground, .0007+.0003*basin+.0004*wet, 1.2*(.003+.015*wet+.008*basin), 5.)
    if zone == 33:
        return (ground, .00055*wet+.00008*basin, 1.2*(.0045+.025*wet+.012*basin), 2.5)
    if zone in (1519, 1637):
        return (ground, 0., 0., 1.15625)
    if zone == 215:
        return (ground, 0., 0., 1.)
    if zone not in (12, 85):
        return (ground, 0., 0., .9375)
    return (ground, 0., 1.2*(.0036+.024*wet+.0144*basin), 1.25)


def sample_field(uv, nodes):
    grid = (uv[0]*N-.5, uv[1]*N-.5)
    base = (math.floor(grid[0]), math.floor(grid[1]))
    f = (grid[0]-base[0], grid[1]-base[1])
    taps = [nodes(min(max(base[0]+x, 0), N-1), min(max(base[1]+y, 0), N-1)) for x, y in ((0, 0), (1, 0), (0, 1), (1, 1))]
    weights = [(1-f[0])*(1-f[1]), f[0]*(1-f[1]), (1-f[0])*f[1], f[0]*f[1]]
    valid = [w if t[3] > 0 else 0 for w, t in zip(weights, taps)]
    coverage = sum(valid)
    inv = 1/max(coverage, 1e-6)
    field = (sum(w*t[0] for w, t in zip(valid, taps))*inv,
             sum(w*t[1] for w, t in zip(weights, taps)),
             sum(w*t[2] for w, t in zip(weights, taps)),
             sum(w*t[3] for w, t in zip(valid, taps))*inv)
    return field, coverage


def density(point, nodes, origin=(-256., -256.), night=0., seconds=0.):
    uv = tuple((point[i]-origin[i])/(N*SPACING)+.5/N for i in (0, 1))
    field, coverage = sample_field(uv, nodes)
    altitude = point[2]-field[0]
    valid = all(0 <= q <= 1 for q in uv) and field[3] > 0 and altitude >= 0
    if not valid:
        return (0., 0.)
    profile = clamp((field[3]-2.5)/2.5)
    general_forest = 1-clamp((field[3]-1.25)/1.25)
    ground_height = field[3]+(6-field[3])*night*(1-profile)
    vertical = clamp(1-altitude/max(ground_height, .001))
    ground = max(field[1]+field[2]*night, 0)*vertical**2
    profile = clamp((field[3]-2.5)/2.5)
    height = AIR[3]+(AIR[2]-AIR[3])*profile
    air_vertical = clamp(1-altitude/max(height, .001))
    grid = tuple(q*N-.5 for q in uv)
    edge = smooth(min(grid[0], grid[1], 63-grid[0], 63-grid[1])*.5)
    dusk, jungle = AIR[0]+NIGHT_EXTRA[0]*night, AIR[1]+NIGHT_EXTRA[1]*night
    base = jungle+(dusk-jungle)*profile
    base = base+((FOREST_DAY+(FOREST_NIGHT-FOREST_DAY)*night)-base)*general_forest
    base = .0017+base*clamp((field[3]-.625)/.625)
    air = base*air_vertical**2*coverage*edge
    return ground*coverage*edge, air


def phase_gain(degrees, old=False):
    cosine = math.cos(math.radians(degrees))
    if old:
        return .25*(1-.65**2)/(1+.65**2-2*.65*cosine)**1.5
    return .25*(.12*.2775/(1.7225-1.7*cosine)**1.5 + .88*(1+.15*cosine))


def intervals(distance, camera=(0., 0., 0.), ray=(1., 0., 0.)):
    # Fixed planes along the dominant world axis; both partial end cells carry
    # their exact path length. This is independent of the camera's forward travel.
    dominant = 0 if abs(ray[0]) >= abs(ray[1]) else 1
    if abs(ray[2]) > abs(ray[dominant]):
        dominant = 2
    spacing = RANGE/48
    step = spacing/abs(ray[dominant])
    coordinate = camera[dominant]*(1 if ray[dominant] >= 0 else -1)/spacing
    offset = coordinate-math.floor(coordinate)
    distance = min(distance, RANGE)
    result = []
    for i in range(49):
        start = max(0., (i-offset)*step)
        if start >= distance:
            break
        result.append((start, min((i+1-offset)*step, distance)))
    return result


def soften(rgb, cap):
    scale = cap/(cap+max(rgb))
    return tuple(value*scale for value in rgb)


SUN_CAP = .95  # 0.3.163 sun forward soft cap, sun_hue.h SunForwardCap (was .38)


def integrate(distance, sigmas, lit, direct=(1., .8, .6), ambient=(.2, .3, .4), first=True, gain=1.2, cap=SUN_CAP, angle=0., altitude=12.):
    transmittance, direct_scalar, shadow_reads = 1., 0., 0
    source = tuple(max(value, 0)*phase_gain(angle)*gain for value in direct)
    for index, (start, end) in enumerate(intervals(distance)):
        absorb = 1-math.exp(-sigmas[index]*(end-start))
        if sigmas[index] > 0:
            if max(source) > 0:
                shadow_reads += 1
                direct_scalar += transmittance*absorb*lit[index]*smooth(altitude/12)
            transmittance *= 1-absorb
    direct_rgb = soften(tuple(value*direct_scalar for value in source), cap)
    scatter = tuple((a*.35*(1-transmittance) if first else 0)+d for a, d in zip(ambient, direct_rgb))
    return scatter, transmittance, shadow_reads, direct_rgb


def main():
    shader = fp.src('world_effects.hlsl').read_text()
    cpu = fp.src('world_renderer.h').read_text()
    policy = fp.src('regional_fog.h').read_text()
    # Tie this reference to the actual implementation's interface and essential
    # equations; a future change requires revisiting these numeric expectations.
    for anchor in ('float4 VolumeAir : register(c32)', 'regionalFogAt(float2 uv,out float coverage)',
                   'float phasePi=mad(.03*visible,narrow,mad(.033,cosine,.22))', '[loop]for(int i=0;i<49;++i)',
                   'float start=max(0,(i-offset)*stepLength);', 'if(start>=surface)break;',
                   'float end=min((i+1-offset)*stepLength,surface);',
                   'visibility+=(receiver<=z?1:0)*w.x*w.y;', 'float4 ScreenSize : register(c33)',
                   'if(dot(sunSource,1)>0)directScatter+=transmittance*absorb*sunSource*(fogShadow(p)*heightFade)',
                   'directScatter*=cap/(cap+peak)', 'skySource*(1-transmittance)+directScatter'):
        assert anchor in shader, anchor
    assert 'c[32][0]=.0055f+.0089f*c[31][3];c[32][1]=.0037f+.00705f*c[31][3]' in cpu
    assert 'c[32][2]=64.f*volumeHeightScale;c[32][3]=48.f*volumeHeightScale' in cpu
    assert 'c[22][3]=.0035f+.0031f*c[31][3]' in cpu
    assert 'airBase=lerp(airBase,FogColor.w,generalForest);' in shader
    # Horizon haze sits between the scene and the local scattering, same debug gate.
    # 0.3.202 (rain mask): the lerp back to the unfogged colour on rain-mask pixels wraps haze + fog mad.
    assert ('if(PassInfo.z<.5){\n        float3 unfogged=color;\n'
            '        // Rain streaks were drawn into the scene before the composite: on mask pixels go back toward the unfogged pixel so they are not hazed.\n'
            '        // 0.3.203 (particle mask): translucent particles write no depth, so their pixels carry the background\'s depth: on mask pixels (g) go back to the\n'
            '        // scene colour, before the contact AO, relight, haze and fog, which were all computed from that depth.\n'
            '        float2 mask=tex2Dlod(RainMask,float4(uv,0,0)).rg;\n'
            '        color=lerp(mad(horizonHaze(color,centerUV,viewZ,d>=.99999&&liquid<=0),fog.a,fog.rgb),unfogged,mask.x);\n'
            '        color=lerp(color,original.rgb,mask.y);\n    }') in shader
    assert 'mad(legacyT,fog.rgb,fogPart)' not in shader
    # 0.3.163: fog-pass-only overrides. The sun's forward soft cap .38 -> .95 (moon .24 kept),
    # the sun's c17 takes the glow hue, c18.rgb is cooled (w kept) for the fog loop only and
    # c17/c18 are restored from the bank before lamp fog, blur and composite. WorldFog unchanged.
    assert 'c[21][1]=1.2f*volumePalette.fogGain[source]*wx.shaftGain();c[21][2]=source==0?NorthlightSunHue::SunForwardCap:NorthlightSunHue::MoonForwardCap' in cpu
    hue = fp.src('sun_hue.h').read_text()
    assert f'SunForwardCap={SUN_CAP:g}f,MoonForwardCap=.24f'.replace('0.', '.') in hue
    set_source = cpu[cpu.index('auto setSource=[&](int source,bool first,bool volume=false){'):]
    set_source = set_source[:set_source.index('\n        };\n')]
    assert set_source.index('if(volume&&source==0)NorthlightSunHue::fogDirect(rgb,glowHueFrame,rgb);') < set_source.index('d->SetPixelShaderConstantF(17,rgb,1);')
    fog_loop = cpu.index('d->SetRenderTarget(0,fogSurface);d->SetPixelShader(fogPS);first=true;')
    cool = cpu.index('d->SetPixelShaderConstantF(18,fogAmbient,1);')
    march = cpu.index('"celestial volumetric raymarch"')
    restore = cpu.index('d->SetPixelShaderConstantF(17,c[17],2);')
    assert fog_loop < cool < march < restore < cpu.index('d->SetPixelShader(localFogPS);') < cpu.index('"volume blur horizontal"') < cpu.index('"world composite"')
    assert 'float fogAmbient[4]={0,0,0,c[18][3]};NorthlightSunHue::coolAmbient(c[18],glowHueFrame,sourceWeights[0],fogAmbient);' in cpu
    # wrap: SourceVisibilityPS keeps .6 x the clear fraction of an 8-tap ring (2.5/3.5 disc
    # radii, on-screen taps only) when larger than the disc value; WorldFog reads it unchanged.
    visibility = shader[shader.index('float4 SourceVisibilityPS('):shader.index('float4 WorldFog(')]
    for anchor in ('[unroll]for(int j=0;j<8;++j){', '((j%2)?3.5:2.5)*r;', 'float inside=step(abs(q.x-.5),.5)*step(abs(q.y-.5),.5);',
                   'current=max(current,.6*ringClear/max(ringTaps,1));', 'return lerp(current,previous,SourceInfo.y).xxxx;'):
        assert anchor in visibility, anchor
    assert visibility.index('current=clear*(1.0/16);') < visibility.index('current=max(current,.6*ringClear')
    assert cpu.count('NorthlightSunHue::coolAmbient(') == 2 and cpu.count('SetPixelShaderConstantF(18,') == 1  # the fog loop + the sampled log
    # Mulgore daytime: taller air and stronger scatter increase the lit/shadow
    # contrast above ground without moving shadows or adding steps. Unlit air
    # must not acquire direct radiance; the existing soft cap still holds.
    for altitude in (0., 6., 12., 24., 48., 70.):
        before_sigma=.00295*max(1-altitude/48,0)**2
        after_sigma=.00295*max(1-altitude/72,0)**2
        before=integrate(100,[before_sigma]*49,[1.]*49,angle=30,altitude=altitude)
        after=integrate(100,[after_sigma]*49,[1.]*49,angle=30,altitude=altitude,gain=2.16,cap=SUN_CAP)
        blocked=integrate(100,[after_sigma]*49,[0.]*49,angle=30,altitude=altitude,gain=2.16,cap=SUN_CAP)
        assert blocked[3]==(0.,0.,0.)
        assert all(0<=v<SUN_CAP for v in after[3])
        if altitude>0:assert after[3][0]>before[3][0]
        else:assert before[3]==after[3]==(0.,0.,0.)
    assert 'c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(w/2);c[33][3]=float(h/2)' in cpu
    assert cpu.count('d->SetPixelShaderConstantF(0,&c[0][0],68)') == 2
    assert 'uploadedFogField->fogCells||uploadedFogField->airCells' in cpu
    assert 'if(cell.height>0&&edge>0)++out.airCells' in policy
    heights = []
    for zone in (10, 33):
        for altitude in (8., 20.):
            ground, air = density((0., 0., 100+altitude), lambda x, y: node(zone))
            assert ground == 0 and air > 0
            heights.append({'zone': zone, 'altitude': altitude, 'ground_sigma': ground, 'air_sigma': air})
        height = AIR[2] if zone == 10 else AIR[3]
        assert density((0., 0., 100+height), lambda x, y: node(zone)) == (0., 0.)
        assert density((0., 0., 99.), lambda x, y: node(zone)) == (0., 0.)
    for nodes in (lambda x, y: node(10, indoor=True), lambda x, y: node(0)):
        assert density((0., 0., 108.), nodes) == (0., 0.)
    for zone in (440, 14, 4197):
        for night in (0., 1.):
            ground, air = density((0., 0., 108.), lambda x, y: node(zone), night=night)
            prairie = density((0., 0., 108.), lambda x,y: node(215), night=night)[1]
            assert ground == 0 and .88 < air/prairie < .91
            assert math.isclose(air, (.00345+.00155*night)*(1-8/48)**2, rel_tol=1e-12)
            assert density((0., 0., 148.), lambda x, y: node(zone), night=night) == (0., 0.)
            assert density((0., 0., 108.), lambda x, y: node(zone, indoor=True), night=night) == (0., 0.)
    assert 'airBase=WeatherInfo.w+airBase*saturate(mad(field.w,1.6,-1))' in shader  # 0.3.198 (rain): c59.w = .0017 + the rain's extra extinction
    assert 'airBase=airFloor+airBase*std::clamp((t.height-.625f)/.625f,0.f,1.f)' in cpu and 'const float airFloor=.0017f+wx.airExtinction()*denseDamp;' in cpu
    # Stormwind has ~90% of Elwynn daytime air at any shared height;
    # indoor/unknown regions remain empty, with no city ground blanket.
    for night in (0., .25, .5, .75, 1.):
        for altitude in (0., 2., 12., 30., 47.):
            city = density((0., 0., 100+altitude), lambda x,y: node(1519), night=night)
            forest = density((0., 0., 100+altitude), lambda x,y: node(12), night=night)
            assert city == density((0., 0., 100+altitude), lambda x,y: node(1637), night=night)
            assert city[0] == 0 and 0 < city[1] < forest[1]
            if night == 0: assert .89 < city[1]/forest[1] < .91
            assert density((0., 0., 100+altitude), lambda x,y: node(1519, indoor=True), night=night) == (0., 0.)
    # Stronger generic air; authored Mulgore/forest totals remain unchanged.
    for zone,addition in ((440,.5*FOREST_DAY), (215,.6*FOREST_DAY), (12,FOREST_DAY)):
        assert math.isclose(density((0.,0.,100.), lambda x,y: node(zone))[1], .0017+addition, rel_tol=1e-12)
    assert density((1000., 0., 108.), lambda x, y: node(10)) == (0., 0.)
    # Coverage fades air across invalid columns without pulling valid ground
    # toward the dummy ground value of absent/indoor nodes.
    center = ((31+1)/64, (31+1)/64)
    field, coverage = sample_field(center, lambda x, y: node(10) if x == 31 and y == 31 else (0., 0., 0., 0.))
    assert field[0] == 100 and field[3] == 5 and coverage == .25
    # Moving a field/camera center does not move its interior height profile.
    point = (40., -30., 120.)
    for zone in (10, 33):
        values = [density(point, lambda x, y: node(zone), origin=o, seconds=13.) for o in ((-256., -256.), (-248., -240.), (-288., -232.))]
        assert all(math.isclose(a, b, abs_tol=1e-15) for a, b in zip(values[0], values[1]))
        assert all(math.isclose(a, b, abs_tol=1e-15) for a, b in zip(values[0], values[2]))
    for distance in (0., .01, .1, 5., 32., 128.):
        segments = intervals(distance)
        assert len(segments) <= 49
        if distance == 0:
            assert segments == []
        else:
            assert segments[0][0] == 0 and segments[-1][1] == min(distance, RANGE)
        assert all(a <= b for a, b in segments)
        assert math.isclose(sum(b-a for a, b in segments), min(distance, RANGE), abs_tol=1e-12)
        result = integrate(distance, [.0005]*49, [1.]*49)
        assert math.isclose(result[1], math.exp(-.0005*min(distance, RANGE)), abs_tol=1e-14)
        if distance == 0:
            assert result[0] == (0., 0., 0.) and result[1] == 1
    # Identical world points for two pixels whose surfaces differ: the shared
    # prefix of the schedule is bitwise equal, only the clipped tail differs.
    near, far = intervals(30.), intervals(90.)
    assert near[:-1] == far[:len(near)-1] and near[-1][1] == 30.
    # No-source atmosphere retains environment scattering, never reads absent
    # shadow maps, and is not counted a second time in the additive source pass.
    no_source = integrate(128, [.0005]*49, [1.]*49, direct=(0., 0., 0.))
    assert no_source[2] == 0 and all(c > 0 for c in no_source[0]) and no_source[1] < 1
    secondary = integrate(128, [.0005]*49, [1.]*49, direct=(0., 0., 0.), first=False)
    assert secondary[0] == (0., 0., 0.) and secondary[1] == no_source[1]
    # Actual geometry visibility can completely remove direct beams while the
    # same medium continues to scatter ambient light and attenuate background.
    blocked = integrate(128, [.0005]*49, [0.]*49)
    assert blocked[0] == no_source[0] and blocked[3] == (0., 0., 0.)
    previous = blocked[0]
    for visibility in (.1, .25, .5, .75, 1.):
        current = integrate(128, [.0005]*49, [visibility]*49)[0]
        assert all(a <= b for a, b in zip(previous, current)); previous = current
    rng = random.Random(0x834)
    for cap in (.38, .24, .95):
        for _ in range(10000):
            rgb = tuple(rng.random()*100 for _ in range(3)); bounded = soften(rgb, cap)
            assert all(0 <= value < cap for value in bounded)
            assert math.isclose(bounded[0]/bounded[1], rgb[0]/rgb[1], rel_tol=1e-12)
            assert all(a <= b for a, b in zip(bounded, soften(tuple(v*2 for v in rgb), cap)))
    # the .95 sun cap lifts a bright forward aureole (raw ~1.8) about 2x but a dim side
    # view (raw ~.15) by only ~20%, so the fog brightens toward the sun.
    near_old, near_new = soften((1.8, 1.8, 1.8), .38)[0], soften((1.8, 1.8, 1.8), .95)[0]
    side_old, side_new = soften((.15, .15, .15), .38)[0], soften((.15, .15, .15), .95)[0]
    assert 1.9 < near_new/near_old < 2.1 and 1.15 < side_new/side_old < 1.25 and near_new < .95
    sun = integrate(128, [.0005]*49, [1.]*49)
    moon = integrate(128, [.0005]*49, [1.]*49, gain=1.2, cap=.24)
    assert all(m < s for m, s in zip(moon[3], sun[3]))
    # Foreground radiance must survive a completely fogged background or sky.
    def composite(background, scatter, transmittance):
        return tuple(b*transmittance+s for b, s in zip(background, scatter))
    for background in ((0., 0., 0.), (.01, .18, .26), (1., .8, .4)):
        assert composite(background, (0., 0., 0.), 1.) == background
        assert composite(background, (.1, .12, .15), 0.) == (.1, .12, .15)
    # A vertical source and horizontal view form a 90-degree side view. Opaque
    # canopy strips at z=24 cover x outside [-2,2]. Upward shadow rays through
    # that opening are lit all the way down to the ground; no screen-space sun
    # position or camera visibility participates in the occlusion function.
    # The 90-degree case is this geometric fixture; other angles below test
    # the phase response independently with the same lit/blocked segment masks.
    def canopy_visibility(point):
        return 1. if point[2] >= 24 or abs(point[0]) < 2 else 0.
    moon_rgb = (.39216, .62745, .80784)
    visibility_samples = []
    for altitude in (.5, 8., 20.):
        for angle in (60., 90., 120., 180.):
            lit = []; blocked = []; sigmas = []
            for start, end in intervals(80):
                y = (start+end)*.5
                point = (0., y, 100+altitude)
                ground, air = density(point, lambda x, y: node(10), night=1.)
                sigmas.append(ground+air)
                lit.append(canopy_visibility((0., y, altitude)))
                blocked.append(canopy_visibility((8., y, altitude)))
            bright = integrate(80, sigmas, lit, direct=moon_rgb, cap=.24, angle=angle, altitude=altitude)
            dark = integrate(80, sigmas, blocked, direct=moon_rgb, cap=.24, angle=angle, altitude=altitude)
            # Opaque distant fog still leaves the local illuminated column clear.
            bg = (0., .18824, .25882)
            contrast = tuple(a-b for a, b in zip(composite(bg, bright[0], bright[1]),
                                                   composite(bg, dark[0], dark[1])))
            assert 0 < contrast[2] < .12 and .28**1.2*math.exp(-80*.00085) < bright[1] < .65
            if altitude == .5: assert contrast[2] < .001
            if altitude >= 8: assert contrast[2] > .04
            assert math.isclose(contrast[2], bright[3][2], abs_tol=1e-14)
            visibility_samples.append(dict(altitude=altitude, degrees=angle,
                ideal_blue_contrast=contrast[2], transmittance=bright[1]))
    # Integrated phase weights stay normalized; backward scattering remains
    # supported instead of turning off when the light exits the viewport.
    slices = 20000
    phase_integral = sum(phase_gain(math.degrees(math.acos(-1+(i+.5)*2/slices)))
                         for i in range(slices))*4/slices
    assert abs(phase_integral-1) < .00002
    # 0.3.145: the broad lobe fades gently away from the source but never turns off.
    assert .18 < phase_gain(180) < .2 and phase_gain(90) > .22 and phase_gain(0) > phase_gain(90) > phase_gain(180)
    # Night fog now has 20% more optical depth; transmittance changes
    # exponentially (T_new = T_old**1.2), not by a linear brightness offset.
    dusk_sigma = density((0., 0., 108.), lambda x, y: node(10), night=1.)[1]
    assert math.isclose(dusk_sigma, (.0017+.0055+.0089)*(1-8/64)**2, rel_tol=1e-12)
    for zone in (10, 33):
        values = [density((0., 0., 108.), lambda x, y: node(zone), night=i/100)[1]
                  for i in range(101)]
        assert all(a <= b for a, b in zip(values, values[1:]))
    assert density((0., 0., 108.), lambda x, y: node(33), night=1.)[1] < dusk_sigma
    # STV air now fills dry uplands too; day 2x, night 1.5x, all heights.
    for night,old,factor in ((0.,.0027,2.),(1.,.0083,1.5)):
        for altitude in (0.,2.,12.,30.,47.):
            point=(0.,0.,100+altitude)
            dry=density(point,lambda x,y: node(33),night=night)
            wet=density(point,lambda x,y: node(33,wet=1,basin=1),night=night)
            assert math.isclose(dry[1],factor*old*(1-altitude/48)**2,rel_tol=1e-12)
            assert dry[1]==wet[1]
            assert density(point,lambda x,y: node(33,indoor=True),night=night)==(0.,0.)
    forest_mist = []
    for zone in (12, 33, 85):
        dry = lambda x, y: node(zone)
        wet = lambda x, y: node(zone, wet=1., basin=1.)
        assert density((0., 0., 102.), dry, night=0.)[0] == 0
        if zone in (12, 85):
            gd0, ad0 = density((0., 0., 102.), wet, night=0.)
            assert gd0 == 0 and math.isclose(ad0, (FOREST_DAY+.0017)*(1-2/48)**2, rel_tol=1e-9)
            assert density((0., 0., 102.), wet, night=1.)[1] > ad0
        for height in (1., 3., 5.):
            gd, ad = density((0., 0., 100+height), dry, night=1.)
            gw, aw = density((0., 0., 100+height), wet, night=1.)
            assert gw > gd > 0 and aw == ad
            forest_mist.append(dict(zone=zone, height=height, wet_ground_sigma=gw, dry_ground_sigma=gd))
        assert density((0., 0., 106.), wet, night=1.)[0] == 0
        for night in (0., .25, .5, 1.):
            assert density((0., 0., 103.), lambda x,y: node(zone, indoor=True), night=night) == (0., 0.)
        assert density((0., 0., 108.), wet, night=1.)[1] == density((0., 0., 108.), dry, night=1.)[1]
    # Gain and soft cap both scale by .8: exactly 20% less direct radiance at
    # every angle and optical depth, with unchanged extinction/ambient terms.
    for angle in (0., 30., 90., 180.):
        for sigma in (.0005, .005, .05):
            old = integrate(80, [sigma]*49, [1.]*49, gain=1.5, cap=.30, angle=angle)
            new = integrate(80, [sigma]*49, [1.]*49, gain=1.2, cap=.24, angle=angle)
            assert old[1] == new[1]
            assert all(math.isclose(b,.8*a,rel_tol=1e-12) for a,b in zip(old[3],new[3]))
    balance_samples = []
    for distance in (20, 40, 80, 100):
        previous = integrate(distance, [dusk_sigma*2]*49, [1.]*49,
            direct=moon_rgb, gain=2., cap=.45, angle=90.)
        current = integrate(distance, [dusk_sigma]*49, [1.]*49,
            direct=moon_rgb, gain=1.5, cap=.30, angle=90.)
        assert current[1] > previous[1]
        assert .40 < current[3][2]/previous[3][2] < .7
        assert current[0][2] < previous[0][2]
        if distance <= 40: assert current[1] > .69**1.2*math.exp(-distance*.0017*(1-8/64)**2)
        balance_samples.append(dict(distance=distance, previous_T=previous[1],
            current_T=current[1], previous_direct_blue=previous[3][2],
            current_direct_blue=current[3][2]))
    for _ in range(1000):
        transmittance, accumulated = 1., 0.
        for _ in range(32):
            absorb = 1-math.exp(-rng.random()*.1)
            accumulated += transmittance*absorb; transmittance *= 1-absorb
        assert abs(accumulated-(1-transmittance)) < 2e-15
    report = {'status': 'PASS', 'scope': 'CPU numeric shader-equation reference and source contracts; no GPU/game execution',
              'air_samples': heights, 'forest_night_ground': forest_mist, 'video_feedback_balance': balance_samples, 'ideal_canopy_moon_visibility': visibility_samples, 'phase_integral': phase_integral,
              'angular_pi_phase': [{'degrees': a, 'old_g065': phase_gain(a, True), 'broad_mixture': phase_gain(a)} for a in (0, 10, 30, 60, 90, 120, 180)],
              'checks': ['mount/canopy air with no added ground fog', 'finite top/below-ground bounds', 'indoor/missing/out-of-field zero; sparse outdoor haze without ground blanket',
                         'invalid-node coverage and ground anchoring', 'same-world-point field-center independence', '49 world-plane optical intervals including clipped endpoints',
                         'first-source-only ambient with zero direct source', 'zero-source shadow fetch avoidance', 'geometry-blocked direct rays',
                         'monotonic hue-preserving bounded direct energy', 'lower moon peak bound', 'analytic ambient identity', 'foreground scattering survives opaque legacy fog', 'canopy columns at ground and mount height', 'side/back-view night beam contrast', 'normalized broad phase', 'regional day/night optical depth']}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
