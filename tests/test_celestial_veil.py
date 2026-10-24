#!/usr/bin/env python3
# northlight-test: requires=cxx
"""veil over geometry and the sun glow: native CPU policy and recorded draw
sequence (test_celestial_veil.cpp), plus static checks of the call site order
(after water, before UI, gated like the discs), the raw-depth classification,
the halo's source-depth occlusion and the slot budgets. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,os,subprocess,tempfile
HERE=Path(__file__).resolve().parent

with tempfile.TemporaryDirectory(prefix='northlight-celestial-veil-') as tmp:
    binary=str(Path(tmp)/'test')
    command=([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG'] if os.name=='nt' else
             ['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
    subprocess.run([*command,*fp.test_include_flags(),str(HERE/'test_celestial_veil.cpp'),'-o',binary],env=fp.zig_env(),check=True)
    native=subprocess.check_output([binary],text=True).strip()
assert native.startswith('PASS'),native

def body(source,start):
    i=source.index(start);return source[i:source.index('\n}\n',i)+3]
renderer=fp.src('renderer.cpp').read_text()
effects=renderer[renderer.index('    void renderEffects() {'):renderer.index('template<class Capture> void prepareDraw(')]
order=['if (applied || !enabled || failed || !projectionValid) return;',
       'const bool celestial=celestialDiscs&&world&&debugMode==0&&worldDebug==0;',
       'celestialDiscs->render(saved.targets[0],',
       'gpuProfile->mark("CelestialDiscs");',
       'if(celestial){celestialDiscs->renderRing();gpuProfile->mark("CelestialRing");}',
       'ext->StretchRect(saved.targets[0], nullptr, sceneSurface',
       'world->render(saved.targets[0],',
       'celestialDiscs->renderVeil(saved.targets[0],NorthlightCelestialGlow::hazeAtSun(haze.haze[3],haze.shape[2],sunElevation),world->skyTransmittance());',
       'gpuProfile->mark("CelestialVeil");',
       'applied = true;']
at=[effects.index(s) for s in order]
assert at==sorted(at),list(zip(order,at))
veil_site=effects[effects.index('        if(celestial){\n            const auto& haze='):effects.index('applied = true;')]
assert 'if(celestial){' in veil_site and 'SetRenderTarget' not in veil_site

host=fp.src('celestial_disc_renderer.h').read_text()
veil=host[host.index('    bool renderVeil('):]
veil=veil[:veil.index('\n    }\n')]
assert 'SetRenderTarget' not in veil and 'SavedState' not in veil  # the caller restores; no pass switch
assert 'return NorthlightCelestialVeil::draw(d,target,veilPS,textures,c,g.bounds,g.width,g.height,veilStats);' in veil
assert 'if(!(veil>0)||!(c[12][2]+c[47][3]>0)){++veilStats.skipped;return false;}' in veil
assert veil.index('if(!(veil>0)')<veil.index('CreatePixelShader')  # no shader or call when off
ring=host[host.index('    bool renderRing('):host.index('    NorthlightCelestialVeil::Stats veilStats;')]
assert 'd->SetPixelShader(haloPS);' in ring and 'SetPixelShaderConstantF(0,c[0],47)' in ring
assert 'sourceTaps(g.disc,g.inverseView,g.projection,g.width,g.height,SunMaskRadius,true,c)' in ring

shader_path=fp.src('celestial_disc_effects.hlsl');shader=shader_path.read_text()
# Disc and ring share source-depth occlusion, including immediate full coverage.
halo_ps=body(shader,'float4 CelestialHaloVisibilityPS(')
assert 'current+=filteredSkyVisibility(source)*terrain*' in halo_ps
assert 'if(current<=0)return 0;' in halo_ps
veil_ps=body(shader,'float4 CelestialVeilPS(')
# Raw-depth classification: WDL/beyond-far silhouettes (normalised depth 1, raw
# depth below the sky sprite) are geometry; the complement of the glare.
assert 'float sky=(depth>=.99999994&&rawDepth>=DiscRepair.x-DiscRepair.z)?1:0;' in veil_ps
assert 'float geometry=(1-sky*terrainVisibility(ray))*(1-sea);' in veil_ps  # visible water takes no veil
assert 'float alpha=geometry*glareProfile(radius);' in veil_ps
assert 'overEdge' not in veil_ps  # no far-edge cut: it drew a line across near hills
# the core only by the disc taps' visibility; the ring (x wrap) only for the
# core-free tail shifted out by 3R, so the profile is monotonic (no dark ring).
profile=shader[shader.index('float glareProfile(float radius){'):shader.index('float3 glowColor(')]
assert 'float ring=DiscTerrainPolicy.w>.5?0:DiscGlowCore.w*saturate(tex2Dlod(DiscRingVisibility,float4(.5,.5,0,0)).r);' in profile
assert 'float disc=DiscTerrainPolicy.w>.5?1:saturate(tex2Dlod(DiscHaloVisibility,float4(.5,.5,0,0)).r);' in profile
assert 'float tailed=max(disc*dot(tail,float3(.25,.35,.40)),ring*dot(tail,float3(.118257,.299458,.380527)));' in profile
assert 'return (DiscEmission.z*exp2(-DiscEmission.x*r2)*disc+DiscGlowHue.w*tailed)*edge;' in profile
assert 'smoothstep(1.5,3' not in profile
# The shifted-tail literals are w_i*2^(-9 k_i) of the tail terms (k .12, .025, .008).
for w,k,c in ((.25,.12,.118257),(.35,.025,.299458),(.40,.008,.380527)):assert abs(w*2**(-9*k)-c)<1e-6
assert 'glowVisibility' not in shader

compile_script=fp.tracked('compile_celestial_disc_shaders.py').read_text()
assert "'CelestialVeilPS'" in compile_script
manifest=json.loads(fp.src('celestial-disc-shader-build.json').read_text())
compiled=manifest['source_sha256']==hashlib.sha256(shader_path.read_bytes()).hexdigest()
budget={}
if compiled:
    shaders=manifest['shaders']
    assert shaders['CelestialHaloVisibilityPS']['sha256']==hashlib.sha256((fp.COMPILED/'CelestialHaloVisibilityPS.bin').read_bytes()).hexdigest()
    assert shaders['CelestialHaloVisibilityPS']['static_instruction_slots']<=512
    for entry in ('CelestialDiscPS','CelestialVeilPS'):
        assert shaders[entry]['static_instruction_slots']<=480 and shaders[entry]['temporary_registers']<=32,entry
        budget[entry]=shaders[entry]['static_instruction_slots']
print(json.dumps({'status':'PASS','native':native,'call_order':len(order),
                  'manifest':'compiled' if compiled else 'pending shader compile (build refuses until then)','slots':budget,
                  'game_or_gpu_launched':False},indent=2))
