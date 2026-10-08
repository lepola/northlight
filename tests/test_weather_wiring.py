#!/usr/bin/env python3
"""0.3.198 (rain): wiring of the weather detection in renderer.cpp - source checks, no compiler.
The game-facing stream path (stream_device.h, stream_proxies.h, game_snapshot.h) must not know weather exists; Device::SetTexture
stays Guard + unwrap + forward (it is on the generator's DIRECT list: the replay calls ext->SetTexture, never the Device method);
the draw hook's whole cost is one pointer comparison placed before any other work, so it covers both gate paths; textures are noted
with the raw pointer before mirrorResources.wrap replaces it; the frame end feeds the tracker and the world."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

r=fp.src('renderer.cpp').read_text()
checks={}
for name in ('stream_device.h','stream_proxies.h','game_snapshot.h'):
    checks[f'{name} does not mention weather']='weather' not in fp.src(name).read_text().lower()
checks['Device::SetTexture unchanged']=('    HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IDirect3DBaseTexture9* pTexture) override{Guard mirrorLock(mirrorState.gate);return ext->SetTexture(Stage, mirrorResources.unwrap(pTexture));}\n') in r
checks['no weather in any Set*/Get* Device method']=not re.search(r'HRESULT STDMETHODCALLTYPE (Set|Get)\w+\([^\n]*weather',r)

a=r.index('template<class Capture,class Draw> HRESULT drawHook(');b=r.index('    // 0.3.154: blob shadow claim')
hook=r[a:b]
cmp_line='if(weatherDetect.hot&&mirrorState.textureKnown[0]&&mirrorState.textures[0]==weatherDetect.hot){weatherSample.primitives+=count;++weatherSample.draws;rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();}'
checks['drawHook: the single comparison, once']=hook.count(cmp_line+' /* 0.3.199 (rain): counted after applied too (the rest of the rain); 0.3.201: no boundary here, the effects run at the UI again */')==1 and hook.count('weatherDetect')==4 and hook.count('weatherSample')==2
checks['drawHook: before the gate split and any other work']=0<=hook.index(cmp_line)<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)')<hook.index('prepareDrawImpl(capture,count)') and hook.index(cmp_line)<hook.index('noteFirst')
# 0.3.199 (rain): RainBlend - the override lives in the matched branch (a bool set there), only for Rain and the setting, wraps exactly the game's draw
helper=r[r.index('template<class Draw> HRESULT rainBlendDraw('):r.index('    // One game draw: capture')]
checks['RainBlend: the bool is set only inside the matched branch, for Rain and the setting']=hook.count('rainBlend=weatherDetect')==1 and 'rainBoundary' not in r and hook.count('rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();')==1 and cmp_line.split('{',1)[1].count('rainBlend=')==1 and 'bool claimed=false,rainBlend=false,mist=false;' in hook
checks['RainBlend: both gate paths draw through rainBlendDraw, none through blobFaintDraw directly']=hook.count('rainBlendDraw(rainBlend,claimed||mist,draw)')==2 and 'blobFaintDraw' not in hook
# 0.3.199 (rain mist): the mist skip is the else branch of the one comparison, gated first by the armed bool; armed only at frame end for rain and RainBlend
mist_line='else if(weatherDetect.mistArmed)mist=mistDraw(count);'
checks['rain mist: the else branch right after the comparison, armed bool first']=hook.count(mist_line)==1 and hook.index(cmp_line)<hook.index(mist_line)<hook.index('if(!frameDrawGates){') and hook[hook.index(cmp_line)+len(cmp_line):hook.index(mist_line)].strip().startswith('/*')
checks['rain mist: armed at frame end only for rain, RainBlend and an active mirror']=r.count('weatherDetect.mistArmed=')==1 and 'weatherDetect.mistArmed=mirrorState.enabled&&st.kind==W::Kind::Rain&&world&&world->rainBlendSetting()&&weatherDetect.mistCount();' in r
checks['rain mist: skipped when stage 0 is a mist texture (known), any blend']='if(!mirrorState.textureKnown[0]){++weatherMistUnknown;return false;}' in r and 'if(!weatherDetect.isMist(mirrorState.textures[0])){' in r
checks['RainBlend: not rain = the plain draw; override sets SrcAlpha/InvSrcAlpha/Add/blend on, inside the draw callback, and restores the previous values after it']=(
    'if(!rain)return blobFaintDraw(claimed,draw);' in helper and 'return blobFaintDraw(claimed,[&]{' in helper
    and all(t in helper for t in ('D3DRS_ALPHABLENDENABLE','D3DRS_SRCBLEND','D3DRS_DESTBLEND','D3DRS_BLENDOP','TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD'))
    and 0<helper.index('GetRenderState')<helper.index('draw();')<helper.index('prev[i]);') and helper.count('draw()')==1)
checks['RainBlend: on with Weather=1 (no key of its own)']='bool rainBlendSetting()const{return quality.weather!=0;}' in fp.src('world_renderer.h').read_text() and 'rainBlend' not in fp.src('quality_settings.h').read_text()
checks['drawHook: no Get*, peek, lock or lookup in the weather lines']=not re.search(r'weather[^\n]*(Get|peek|lock|find|unordered_map|CpuScope)',hook)
checks['drawHook: no gateFrame weather branch; the probe sits in prepareDrawImpl\'s existing gateFrame branch']='weatherProbeDraw' not in hook and 'gateFrame' not in hook.split(cmp_line)[0] and 'if(gateFrame){++gateCounts.prep;weatherProbeDraw(count);}' in r

ct=next(l for l in r.split('\n') if 'STDMETHODCALLTYPE CreateTexture(' in l)
checks['CreateTexture: noteCreate with the raw pointer before wrap']=('weatherDetect.noteCreate(static_cast<IDirect3DBaseTexture9*>(*ppTexture),Width,Height,Levels,Format)' in ct and ct.index('noteCreate')<ct.index('mirrorResources.wrap(ppTexture)'))
for m,p in (('CreateVolumeTexture','ppVolumeTexture'),('CreateCubeTexture','ppCubeTexture')):
    l=next(l for l in r.split('\n') if f'STDMETHODCALLTYPE {m}(' in l)
    checks[f'{m}: forget before wrap']=f'weatherDetect.forget(static_cast<IDirect3DBaseTexture9*>(*{p}))' in l and l.index('forget')<l.index(f'mirrorResources.wrap({p})')
reset=r[r.index('STDMETHODCALLTYPE Reset('):r.index('    // Frame boundary only (after clearFrame())')]
checks['Reset clears the detector']='weatherDetect.reset();' in reset
s0=r.index('    void finishFrameImpl() {');fin=r[s0:r.index('HRESULT STDMETHODCALLTYPE Present(',s0)]
checks['finishFrameImpl calls weatherFrame before the next frame is latched']=0<fin.index('weatherFrame(sampleFrame);')<fin.index('++frame;')
wf=r[r.index('    void weatherFrame('):r.index('    void logWeatherProbe(')]
checks['weatherFrame: tracker, world, rotate, mirror-off']=all(s in wf for s in ('weatherTracker.frame(weatherSample,','world->setWeather(st)','weatherDetect.endFrame(counted)','weatherDetect.setOff(true)','WEATHER detect=off (mirror inactive)'))
checks['DRAWGATE line carries weatherDraws/weatherPrims']='weatherDraws=%u weatherPrims=%u weatherNs=%.1f' in r and 'weatherSample.draws,weatherSample.primitives,b.weatherNs)' in r
w=fp.src('world_renderer.h').read_text()
checks['WorldRenderer: POD state, setter and getter only']=all(s in w for s in ('NorthlightWeather::State weatherState{};','void setWeather(const NorthlightWeather::State& s){weatherState=s;}','const NorthlightWeather::State& weather()const{return weatherState;}')) and w.count('weatherState')==4
# 0.3.201 (rain): the rain boundary is gone (the UI boundary is the only one again); the rain mask replaces it
pdi=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
checks['no rain boundary: no rainBoundary anywhere, no kind=rain log, no stage-0 relearn in prepareDrawImpl']=('rainBoundary' not in r and 'kind=rain' not in r and 'IDirect3DBaseTexture9* stage0=nullptr' not in pdi and 'ext->GetTexture(0,&stage0)' not in pdi and 'renderEffects();' not in pdi)
branch=helper[helper.index('return blobFaintDraw(claimed,[&]{'):]
checks['rain mask: the pass runs once, only after draw() in the rain branch, inside extensionWork("rain mask"), before the blend restore']=(helper.count('rainMaskPass(')==1 and 'if(SUCCEEDED(hr))extensionWork("rain mask",[&]{rainMaskPass(draw,false);});' in branch
    and branch.index('const HRESULT hr=draw();')<branch.index('rainMaskPass(draw,false)')<branch.index('prev[i]);') and 'rainMaskPass' not in helper.split('return blobFaintDraw(claimed,[&]{')[0])
mp=r[r.index('template<class Draw> void rainMaskPass('):r.index('template<class Draw> HRESULT rainBlendDraw(')]
checks['rain mask: eligibility (not applied, terrain, enabled, not failed, world, mask not failed), scrub needs rainMaskDrawn, eligibility cached per frame']=(
    'if(scrub&&!rainMaskDrawn)return;' in mp and 'if(applied||!terrain||!enabled||failed||!world||rainMaskFailed||!width||!height)return;' in mp and 'if(!rainMaskFrame){rainMaskFrame=true;rainMaskOk=rainMaskEligible();}' in mp)
el=r[r.index('bool rainMaskEligible()'):r.index('bool rainMaskCreate()')]
checks['rain mask: eligible only at the effect size, no multisampling on target and depth']=('rd.Width==width&&rd.Height==height&&rd.MultiSampleType==D3DMULTISAMPLE_NONE' in el and 'dd.MultiSampleType==D3DMULTISAMPLE_NONE' in el and 'GetDepthStencilSurface' in el)
checks['rain mask: states set (alpha-only write, blend on, no separate alpha, no Z write, no sRGB, no stencil, the game scissor rect), mask is ONE/ZERO/MAX, scrub ZERO/ZERO/ADD with LESS widened to LESSEQUAL']=(
    all(t in mp for t in ('D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA','D3DRS_ALPHABLENDENABLE,TRUE','D3DRS_SEPARATEALPHABLENDENABLE,FALSE','D3DRS_ZWRITEENABLE,FALSE','D3DRS_SRGBWRITEENABLE,FALSE','scrub?D3DBLEND_ZERO:D3DBLEND_ONE','D3DRS_DESTBLEND,D3DBLEND_ZERO','scrub?D3DBLENDOP_ADD:D3DBLENDOP_MAX','D3DRS_STENCILENABLE,FALSE','ext->GetScissorRect(&scissor)','if(scrub&&known[10]&&prev[10]==D3DCMP_LESS)ext->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);'))
    and mp.count('ext->SetScissorRect(&scissor)')==2
    and 'D3DRS_ZENABLE' not in mp and 'D3DRS_ALPHATESTENABLE' not in mp and 'SetTexture' not in mp and 'SetPixelShader' not in mp)
checks['rain mask: render target and viewport saved, restored after the draw (SetRenderTarget resets the viewport), cleared once per frame with the scissor off']=(
    'GetRenderTarget(0,&gameRT)' in mp and 'ext->GetViewport(&vp)' in mp and mp.index('SetRenderTarget(0,rainMaskSurface)')<mp.index('const HRESULT hr=draw();')<mp.rindex('restore();')
    and 'ext->SetRenderTarget(0,gameRT);ext->SetViewport(&vp);if(scissorKnown)ext->SetScissorRect(&scissor);drop(gameRT);' in mp and 'if(!rainMaskCleared){' in mp and 'rainMaskCleared=true;' in mp and mp.count('Clear(')==1 and 'D3DRS_SCISSORTESTENABLE,FALSE' in mp and mp.count('draw()')==1)
checks['rain mask: created lazily at the effect size, failure logged once and disables the mask only']=('D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&rainMask' in r and 'rainMaskFailed=true;logf("WEATHER rain mask disabled' in r and 'if(!rainMaskSurface&&!rainMaskCreate())return;' in mp)
rr=r[r.index('    void releaseResources() {'):r.index('    bool error(HRESULT hr')]
rs=r[r.index('    bool resources(UINT w'):r.index('    // Swap chain 0')]
checks['rain mask: dropped in releaseResources (with the failed flag reset) and on a size change']=('drop(rainMaskSurface);drop(rainMask);rainMaskFailed=false;' in rr and 'drop(rainMaskSurface);drop(rainMask);' in rs.split('captured=false;')[1].split('}')[0])
checks['rain mask: per-frame flags reset where terrain/applied reset (clearFrame)']='terrain = captured = applied = projectionValid = false;rainMaskCleared=rainMaskDrawn=rainMaskFrame=rainMaskOk=rainScrubDraw=false;' in r
checks['rain mask: setRainMask right before world->render in renderEffects, only when drawn']=0<r.index('world->setRainMask(rainMaskDrawn?rainMask:nullptr);')<r.index('world->render(saved.targets[0]') and r.index('world->render(saved.targets[0]')-r.index('world->setRainMask(')<260
checks['rain scrub: per-draw flag only with rainMaskDrawn&&!applied for world or skinned shaders; dry frames one bool test']=('if(rainMaskDrawn&&world&&!applied){const VsClass& vc=classifyVs(vs);rainScrubDraw=vc.world||vc.skinned;}' in pdi and pdi.count('rainScrubDraw=')==2 and pdi.index('rainScrubDraw=false;')<pdi.index('if(applied){'))
checks['rain scrub: after the game draw in both gate paths, not for rain, claimed or mist draws, only on success']=(hook.count('if(rainScrubDraw&&!rainBlend&&!claimed&&!mist&&SUCCEEDED(hr)){rainScrubDraw=false;extensionWork("rain mask",[&]{rainMaskPass(draw,true);});}')==2
    and hook.index('rainBlendDraw(rainBlend,claimed||mist,draw)')<hook.index('rainScrubDraw&&')<hook.index('if(!frameDrawGates){')+hook[hook.index('if(!frameDrawGates){'):].index('return hr;'))
checks['rain mask: WEATHER line carries the counters; world binds RainMask on s13 for the composite only']=('rainMask=%u rainMaskScrub=%u' in r and 'd->SetTexture(13,rainMask?rainMask:neutralZero);rainMask=nullptr;' in w and 'composited=true;d->SetTexture(13,regionalFogTexture);' in w and 'void setRainMask(IDirect3DTexture9* t){rainMask=t;}' in w)
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)

