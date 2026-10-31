#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Zone skybox fallback: native decode/mode/signature test (test_celestial_skybox.cpp) plus static checks that a skybox frame
restores the late disc and full glare (the planet phase never draws under a full skybox) while other frames keep the
planet-phase path. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-celestial-skybox-') as tmp:
    binary=str(Path(tmp)/'test')
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*fp.test_include_flags(),
                    str(HERE/'test_celestial_skybox.cpp'),'-o',binary],check=True)
    native=subprocess.check_output([binary],text=True).strip()
assert native.startswith('PASS'),native

host=fp.src('celestial_disc_renderer.h').read_text()
claim=host[host.index('template<class Palette> bool claimNativeDraw('):host.index('    bool claimNativeGlare(')]
late=host[host.index('    bool render(IDirect3DSurface9*'):host.index('    bool renderRing(')]
# One decision per frame: the claim decides from its context read, render() only when no claim did; endFrame resets it.
assert 'if(!skyboxTrigger&&skyMode.skybox())return claimSuppress(body);' in claim and 'else if(skyboxFrame(currentSky))return claimSuppress(body);' in claim
assert claim.index('else if(skyboxFrame(currentSky))')<claim.index('resolveRendererSky(')<claim.index('drawDisc(')  # ordinary planet claims retain the skybox fallback
assert 'const bool skybox=skyboxFrame(context);' in late
assert 'void endFrame(){' in host and 'skyMode.endFrame();}' in host
assert 'bool claimSuppress(int body){\n        if(!NorthlightCelestial::suppressNative(renderFrame,frames))return false;' in host  # as the pre-planet-phase F1b: needs a render() last frame
assert 'ownFrame=renderFrame=frames;' in late
# Skybox frame late path: both bodies, occluded surface, then the full glare (core and tail) with the sky-band depth.
sky=late[late.index('if(NorthlightCelestial::lateBody('):late.index('if(i==1)continue;')]
assert 'observer.lateDisc(i,suppressed)' in late[late.index('lateBody('):late.index('lateBody(')+160]
assert 'context.skyboxCover>0,(drawnSkyboxEarly>>i)&1)){' in late
assert late.index('if(i==1)continue;')>late.index('lateBody(')  # unevidenced frames fall through to PR #90's sun-only late tail
# Full-skybox frames: the game draws no planets, so the previous frame's late bodies keep owning the native glare.
assert 'void endFrame(){lateOwnedPrevious=drawnLate;' in host and host.count('|suppressedPrevious|lateOwnedPrevious')==2
assert 'bool owning()const{return ownFrame==frames||ownFrame+1==frames;}' in host and 'ownFrame=renderFrame=frames;' in late  # render() last frame keeps owning() true
assert 'prepareTerrain(i,disc[i],inverseView)' in sky
assert 'legacyFog,true,bodies[i]->emission,false,skyBand.depth(maxZ)))continue;' in sky
assert 'legacyFog,true,bodies[i]->emission,true,skyBand.depth(maxZ),true))drawnLate|=1u<<i;' in sky
# The late full glare keeps the core weight and is the shader's mode 2; the sky-phase compact core stays mode 1.
assert 'glare&&!occlude?1.f:(glare&&fullGlare?2.f:0.f)' in host and '(occlude&&!fullGlare?0.f:NorthlightCelestialGlow::SunCoreWeight)' in host
assert 'if(glare&&!occlude){c[47][3]=0;c[48][3]=0;}' in host
shader=fp.src('celestial_disc_effects.hlsl').read_text()
assert 'bool skyCore=abs(DiscTerrainPolicy.w-1)<.5;' in shader and 'encoded=DiscTerrainPolicy.w>.5?glowColor(radius):DiscGlowHue.rgb;' in shader
# Diagnostics: gated mode-transition log (capped) and the skybox fields on the 120-frame line.
assert 'NorthlightDiagnostics::enabled()&&skyModeLogs++<32' in host and 'skybox=%.2f mode=%s switches=%u' in host
assert 'logf("CELESTIAL skybox signatures unverified: late disc path for the session")' in host
ctxh=fp.src('celestial_context.h').read_text()
assert 'verifyCode(NorthlightWorldContext::readSelf)&&verifySkyboxCode(NorthlightWorldContext::readSelf)' in ctxh and 'assumeSkybox(out,skyboxVerified())' in ctxh
print('PASS celestial skybox fallback')
