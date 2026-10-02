#!/usr/bin/env python3
# northlight-test:
"""0.3.174 G1: the contact AO and bloom composite folded into WorldComposite.

FOLD (world ready, no effect debug view): the AO pass runs AOContactBloom (bloom rgb, AO alpha),
the full-resolution legacy composite and the world's colour copy are skipped, and WorldComposite
forms original' = saturate(bloom*(1-saturate(o*ao)) + o*ao) before its own lines; the removal
smoothing multiplies its Scene reads by the AO. LEGACY (every other frame) keeps the old order
with a neutral (0,0,0,1) s10. A folded frame the world did not composite gets the legacy
composite afterwards. Float64 emulation plus a source/assembly audit; no game, Wine or GPU.
Writes ao-fold-validation.json.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import math
import random
import re

effects = fp.src('effects.hlsl').read_text()
world = fp.src('world_effects.hlsl').read_text()
renderer = fp.src('renderer.cpp').read_text()
wr = fp.src('world_renderer.h').read_text()
checks = {}
rng = random.Random(174)
LUM = (0.2126, 0.7152, 0.0722)


def clamp(v, lo=0., hi=1.):
    return min(max(v, lo), hi)


def bright(c):  # effects.hlsl Bright()
    return [x * clamp((sum(a * b for a, b in zip(c, LUM)) - 0.72) / 0.28) for x in c]


def bright_tap(c):  # AOContactBloom's single-mad weight
    return [x * clamp(sum(a * b for a, b in zip(c, LUM)) * (1 / 0.28) + (-0.72 / 0.28)) for x in c]


def legacy_bloom(taps):  # Composite tail: centre x4 plus four taps, *.125
    s = [4 * x for x in bright(taps[0])]
    for t in taps[1:]:
        s = [a + b for a, b in zip(s, bright(t))]
    return [x * .125 for x in s]


def fold_bloom(taps, options_x=.08):  # AOContactBloom rgb
    s = [4 * x for x in bright_tap(taps[0])]
    for t in taps[1:]:
        s = [a + b for a, b in zip(s, bright_tap(t))]
    return [x * (0.125 * options_x) for x in s]


def legacy_tail(o, ao, bloom, options_x=.08):  # Composite with Lighting.w = 0 (world ready)
    lit = [c * ao for c in o]
    lit = [l + b * options_x * (1 - clamp(l)) for l, b in zip(lit, bloom)]
    return [clamp(l) for l in lit]


def fold(o, ao, bloom_weighted):  # WorldComposite original'
    return [clamp(b * (1 - clamp(c * ao)) + c * ao) for c, b in zip(o, bloom_weighted)]


# 1. original' = the legacy composite tail (same ao, same bloom), sky/water included (ao 1)
worst = 0.
for _ in range(50000):
    o = [rng.random() * 1.1 for _ in range(3)]
    ao = 1. if rng.random() < .2 else rng.random()
    bloom = [rng.random() * 2 for _ in range(3)]
    a, b = legacy_tail(o, ao, bloom), fold(o, ao, [x * .08 for x in bloom])
    worst = max(worst, max(abs(x - y) for x, y in zip(a, b)))
checks[f"original' equals the legacy tail with Lighting.w=0 (max {worst:.1e} <= 1e-12)"] = worst <= 1e-12

# 2. AOContactBloom's bloom = the Composite bloom formula (the single-mad weight: <= 1e-6 relative)
rel = 0.
for _ in range(20000):
    taps = [[rng.random() * 1.05 for _ in range(3)] for _ in range(5)]
    a, b = [x * .08 for x in legacy_bloom(taps)], fold_bloom(taps)
    rel = max(rel, max(abs(x - y) / max(abs(x), 1e-6) for x, y in zip(a, b)))
checks[f'bloom equals the Composite formula incl. .125 and Options.x (max rel {rel:.1e} <= 1e-6)'] = rel <= 1e-6
entry = effects[effects.index('float4 AOContactBloom(float2 uv : TEXCOORD0) : COLOR0'):effects.index('float3 NeighbourNormal(')]
checks['AOContactBloom: five Scene taps (centre x4, +-4 px per axis), .125*Options.x, AO alpha of AOImpl(uv,false)'] = (
    'float4 b = ImageAndClip.xyxy * float4(4, 0, 0, 4);' in entry
    and 'float3 bloom = BrightTap(tex2D(Scene, uv).rgb, 0) * 4.0;' in entry
    and all(f'bloom = BrightTap(tex2D(Scene, uv {s} b.{c}).rgb, bloom);' in entry for s in '+-' for c in ('xy', 'zw'))
    and 'return float4(bloom * (0.125 * Options.x), AOImpl(uv, false).a);' in entry
    and 'saturate(mad(dot(c, float3(0.2126, 0.7152, 0.0722)), 1.0 / 0.28, -0.72 / 0.28))' in effects
    and 'AOContact(' not in effects and 'float3 Bright(float3 c)' in effects)
asm = fp.src('AOContactBloom.bin.asm').read_text().splitlines()
texld = [i for i, l in enumerate(asm) if re.match(r'\s*texld\s', l)]
flow = [i for i, l in enumerate(asm) if re.match(r'\s*(if|rep|loop|break|ret|call)', l)]
checks[f'AOContactBloom: its {len(texld)} tex2D reads (texld, s0) precede all flow control (sky/water return included)'] = (
    len(texld) == 5 and all(asm[i].rstrip().endswith('s0') for i in texld) and flow and max(texld) < min(flow))
manifest = json.loads(fp.src('shader-build.json').read_text())['shaders']
checks[f"effects manifest: AOContactBloom {manifest['AOContactBloom']['static_instruction_slots']} <= 500, AOContact gone"] = (
    manifest['AOContactBloom']['static_instruction_slots'] <= 500 and 'AOContact' not in manifest)

# 3. the AO upsample: WorldComposite's depth-weighted tent on the half-res lighting grid
def upsample(ao_row, depth_row, x, z):
    """1D tent at full-res position x (half-res px units) with WorldComposite's depth weight."""
    base = math.floor(x - .5)
    f = x - .5 - base
    total = acc = 0.
    closest, fallback = 1e20, 1.
    for bit in (0, 1):
        k = min(max(base + bit, 0), len(ao_row) - 1)
        delta = abs(depth_row[k] - z)
        w = (f if bit else 1 - f) * math.exp(-delta / max(.15, z * .01))
        acc += ao_row[k] * w
        total += w
        if delta < closest:
            closest, fallback = delta, ao_row[k]
    return fallback if total < .02 else acc / total


leak = 0.
for z in (5., 20., 50.):
    step = 10 * max(.15, z * .01)
    depth = [z] * 8 + [z + step] * 8  # a silhouette: the far side is 10 tolerances deeper
    ao_row = [1.] * 8 + [0.] * 8  # dark behind, lit in front
    for i in range(80):
        x = 6 + i / 20
        if x < 8:
            leak = max(leak, 1 - upsample(ao_row, depth, x, z))
checks[f'silhouette (10 depth tolerances): AO transferred across <= 1e-3 (max {leak:.1e})'] = leak <= 1e-3
flat = [10.] * 16
ao_row = [1.] * 8 + [0.] * 8
bleed = min(x for x in (5 + i / 100 for i in range(301)) if upsample(ao_row, flat, x, 10.) < 1 - 1e-12)
checks[f'equal depth: the dark side reaches at most 1 half-res px across (from x={bleed:.2f}, edge at 8)'] = 8 - bleed <= 1.0 + 1e-9

# 4. wiring
effects_cpp = renderer[renderer.index('    void renderEffects() {'):renderer.index('template<class Capture> void prepareDraw(')]
fold_line = 'const bool fold=world&&debugMode==0&&world->ready();'
legacy_before = 'if(!fold&&!legacyComposite())return;'
render_call = 'world->render(saved.targets[0],depthTex,width,height,sceneFormat,nearZ,farZ,worldMinDepth,worldMaxDepth,worldDebug,gpuProfile.get(),waterMask,fold?scene:nullptr,fold?ao:nullptr)'
legacy_after = 'if(fold&&!world->composited){effectState();bindEffects(ao);if(!legacyComposite())return;}'
checks['proxy: FOLD condition, legacy composite before render only when not folding, after it only if not composited'] = (
    all(effects_cpp.count(s) == 1 for s in (fold_line, legacy_before, render_call, legacy_after))
    and effects_cpp.index('gpuProfile->mark("AO");') < effects_cpp.index(fold_line) < effects_cpp.index(legacy_before)
    < effects_cpp.index(render_call) < effects_cpp.index(legacy_after) < effects_cpp.index('water->render(')
    and 'ext->SetPixelShader(constants[7]==0.f?aoContactBloomPS:aoPS);' in effects_cpp)
checks['proxy: one shared effect-state setup, used before the AO pass and before the late legacy composite'] = (
    renderer.count('    void effectState() {') == 1 and effects_cpp.count('effectState();') == 2
    and effects_cpp.index('effectState();') < effects_cpp.index('bindEffects(nullptr);')
    and 'D3DRS_MULTISAMPLEANTIALIAS,TRUE' in renderer[renderer.index('    void effectState() {'):renderer.index('    void renderEffects() {')]
    and 'kAoContactBloomShader' in renderer and 'kAoContactShader' not in renderer)
render_fn = wr[wr.index('    bool render(IDirect3DSurface9* targetSurface'):]
temporal = render_fn[render_fn.index('{   // Temporal stabilization'):render_fn.index('temporalIndex=prev;temporalValid=true;')]
checks['world: copy skipped only with foldScene, Scene s0 = foldScene, s10 = AO or neutral LINEAR before the temporal quad'] = (
    'if(!foldScene&&!check(d->StretchRect(targetSurface,nullptr,colorSurface,nullptr,D3DTEXF_NONE),"world color copy"))return false;' in render_fn
    and 'IDirect3DTexture9* textures[]={foldScene?foldScene:color,' in render_fn
    and temporal.count('d->SetTexture(10,foldAO?foldAO:neutralAO);d->SetSamplerState(10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);') == 1
    and temporal.index('SetTexture(10,') < temporal.index('quad(w/2,h/2),"temporal light pass"')
    and 'SetTexture(10' not in render_fn[render_fn.index('temporalIndex=prev;temporalValid=true;'):render_fn.index('"world composite"')])
checks['world: composited cleared at entry, set right after the WorldComposite quad; neutral 1x1 (0,0,0,1) created and released'] = (
    'shadowsComposited=false;composited=false;lastRenderDebug=debug;' in render_fn
    and 'if(!check(quad(w,h),"world composite"))return false;composited=true;' in render_fn
    and 'CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralAO,nullptr)' in wr
    and '*static_cast<DWORD*>(lock.pBits)=0xff000000u;' in wr and 'drop(neutralAO);' in wr)
checks['diagnostic capture: the folded scene dump is labelled pre-AO'] = 'foldScene?"pre-AO (folded AO/bloom)":"post-AO composite"' in wr

# 5. shaders: WorldComposite folds, smoothRemoval reads the AO, removalScale has no floor
composite = world[world.index('float4 WorldComposite('):]
checks['WorldComposite: AO in the tent (same weight, nearest fallback), bloom bilinear, original\' before fogPart'] = (
    'sampler2D AmbientOcclusion : register(s10);' in world
    and 'ao+=occlusion*weight;' in composite and 'fallbackAO=occlusion;' in composite
    and 'ao=relight?(total<.02?fallbackAO:ao/total):1;' in composite
    and 'float3 bloom=tex2Dlod(AmbientOcclusion,float4(uv,0,0)).rgb;' in composite
    and composite.index("original.rgb=saturate(mad(bloom,1-saturate(original.rgb*ao),original.rgb*ao));")
    < composite.index('float3 color=original.rgb;') < composite.index('float3 fogPart=min((1-legacyT)*LegacyFogColor.rgb,original.rgb);')
    < composite.index('if(relight){\n        // Thin receivers'))
checks['smoothRemoval: both Scene reads times the AO alpha; removalScale without the .0001 floor'] = (
    world.count('tex2Dlod(Scene,float4(q,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(q,0,0)).a') == 1
    and world.count('tex2Dlod(Scene,float4(tq,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(tq,0,0)).a') == 1
    and 'return max(legacyT*max(baseline,.15),scene-min(fog,scene));' in world and '.0001));' not in world[world.index('float3 removalScale('):world.index('float3 smoothRemoval(')])
wm = json.loads(fp.src('world-shader-build.json').read_text())['shaders']
# 0.3.185: the removal smoothing (Scene, AO, baseline reads) moved to RemovalSmooth; TemporalLight keeps no s0/s10/s12 read and gains s9.
checks[f"world manifest: WorldComposite {wm['WorldComposite']['static_instruction_slots']} <= 500 with s10; RemovalSmooth {wm['RemovalSmooth']['static_instruction_slots']} (s10 AO) <= 512; TemporalLight {wm['TemporalLight']['static_instruction_slots']} == 293"] = (
    wm['WorldComposite']['static_instruction_slots'] <= 500 and wm['WorldComposite']['samplers'] == [0, 1, 8, 9, 10, 11, 12]
    and wm['RemovalSmooth']['static_instruction_slots'] <= 512 and wm['RemovalSmooth']['samplers'] == [0, 1, 8, 10, 12, 14]
    and wm['TemporalLight']['static_instruction_slots'] == 293 and wm['TemporalLight']['samplers'] == [1, 8, 9, 14, 15])

# 6. no new key
config = [fp.src('quality_settings.h').read_text(), fp.src('windows-package/northlight-quality.ini').read_text(),
          fp.src('windows-package/README.txt').read_text(encoding='utf-8')]
checks['no new key in quality_settings.h, the ini or README'] = not any(
    k in text for k in ('AOFold', 'FoldAO', 'AmbientFold', 'BloomFold') for text in config)

for name, ok in checks.items():
    print(('PASS ' if ok else 'FAIL ') + name)
assert all(checks.values())
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
(out / 'ao-fold-validation.json').write_text(json.dumps({
    'scope': 'float64 emulation of the folded AO/bloom composite + source/assembly audit; no game, Wine or GPU.',
    'checks': checks, 'max_original_error': worst, 'max_bloom_relative': rel, 'silhouette_leak': leak}, indent=2) + '\n')
print('PASS AO fold: original\' equals the legacy tail, bloom formula, depth-aware AO, FOLD/LEGACY wiring')
