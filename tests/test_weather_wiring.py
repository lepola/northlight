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
checks['drawHook: the single comparison, once']=hook.count(cmp_line+' /* 0.3.199 (rain): counted after applied too (the rest of the rain); 0.3.202: no boundary here, the effects run at the UI again */')==1 and hook.count('weatherDetect')==4 and hook.count('weatherSample')==2
checks['drawHook: before the gate split and any other work']=0<=hook.index(cmp_line)<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)')<hook.index('prepareDrawImpl(capture,count)') and hook.index(cmp_line)<hook.index('noteFirst')
# 0.3.199 (rain): RainBlend - the override lives in the matched branch (a bool set there), only for Rain and the setting, wraps exactly the game's draw
helper=r[r.index('template<class Draw> HRESULT rainBlendDraw('):r.index('    // One game draw: capture')]
checks['RainBlend: the bool is set only inside the matched branch, for Rain and the setting']=hook.count('rainBlend=weatherDetect')==1 and 'rainBoundary' not in r and hook.count('rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();')==1 and cmp_line.split('{',1)[1].count('rainBlend=')==1 and 'bool claimed=false,rainBlend=false,mist=false,particle=false;' in hook
checks['RainBlend: both gate paths draw through rainBlendDraw, none through blobFaintDraw directly']=hook.count('rainBlendDraw(rainBlend,claimed||mist,draw,particle)')==2 and 'blobFaintDraw' not in hook
# 0.3.199 (rain mist): the mist skip is the else branch of the one comparison, gated first by the armed bool; armed only at frame end for rain and RainBlend
mist_line='else if(weatherDetect.mistArmed)mist=mistDraw(count);'
checks['rain mist: the else branch right after the comparison, armed bool first']=hook.count(mist_line)==1 and hook.index(cmp_line)<hook.index(mist_line)<hook.index('if(!frameDrawGates){') and hook[hook.index(cmp_line)+len(cmp_line):hook.index(mist_line)].strip().startswith('/*')
checks['rain mist: armed at frame end only for rain, RainBlend and an active mirror']=r.count('weatherDetect.mistArmed=')==1 and 'weatherDetect.mistArmed=mirrorState.enabled&&st.kind==W::Kind::Rain&&world&&world->rainBlendSetting()&&weatherDetect.mistCount();' in r
checks['rain mist: skipped when stage 0 is a mist texture (known), any blend']='if(!mirrorState.textureKnown[0]){++weatherMistUnknown;return false;}' in r and 'if(!weatherDetect.isMist(mirrorState.textures[0])){' in r
checks['RainBlend: not rain = the plain draw; override sets SrcAlpha/InvSrcAlpha/Add/blend on, inside the draw callback, and restores the previous values after it']=(
    'if(!rain)return particle?particleBlendDraw(claimed,draw):blobFaintDraw(claimed,draw);' in helper and 'return blobFaintDraw(claimed,[&]{' in helper
    and all(t in helper for t in ('D3DRS_ALPHABLENDENABLE','D3DRS_SRCBLEND','D3DRS_DESTBLEND','D3DRS_BLENDOP','TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD'))
    and 0<helper.index('GetRenderState')<helper.index('rainMrtDraw(draw);')<helper.index('prev[i]);') and helper.count('draw()')==0) # 0.3.202 (rain mask MRT): the draw goes through rainMrtDraw
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
# 0.3.202 (rain): the rain boundary is gone (the UI boundary is the only one again); the rain mask replaces it
pdi=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
checks['no rain boundary: no rainBoundary anywhere, no kind=rain log, no stage-0 relearn in prepareDrawImpl']=('rainBoundary' not in r and 'kind=rain' not in r and 'IDirect3DBaseTexture9* stage0=nullptr' not in pdi and 'ext->GetTexture(0,&stage0)' not in pdi and 'renderEffects();' not in pdi)
branch=helper[helper.index('return blobFaintDraw(claimed,[&]{'):]
# 0.3.202 (rain mask MRT): the mask is written by the rain draw itself (render target 1); no second draw, no scrub draw, no RT0 swap per draw
checks['rain mask MRT: the rain branch makes ONE draw through rainMrtDraw, between the blend setup and its restore; no second draw call, no rainMaskPass/rainScrubDraw anywhere']=(
    branch.count('rainMrtDraw(draw)')==1 and branch.count('draw()')==0 and branch.index('ext->SetRenderState(types[i],want[i])')<branch.index('const HRESULT hr=rainMrtDraw(draw);')<branch.index('prev[i]);')
    and 'rainMrtDraw' not in helper.split('return blobFaintDraw(claimed,[&]{')[0] and 'rainMaskPass' not in r and 'rainScrub' not in r.replace('rainScrubPS','').replace('kRainScrubShader','') and 'rainMaskScrub' not in r)
mrt=r[r.index('template<class Draw> HRESULT rainMrtDraw('):r.index('IDirect3DTexture9* rainMaskPrepare()')]
checks['rain mask MRT: rainMrtDraw: begin and end inside extensionWork("rain mask"), the draw once between them, unbound before a plain draw, counted only on success']=(
    mrt.count('draw()')==2 and mrt.count('extensionWork("rain mask"')==2 and 'masked=rainMrtBegin(particle)' in mrt and 'if(!masked){rainMrtUnbind();return draw();}' in mrt and mrt.index('rainMrtBegin(particle)')<mrt.index('const HRESULT hr=draw();')<mrt.index('rainMrtEnd()')
    and 'if(SUCCEEDED(hr)){rainMaskDrawn=true;if(particle){++particleDraws;if(gameShader){++particlePatchedDraws;if(particleFoggedNow)++particleFoggedDraws;}}else{rainMaskRainDrawn=true;++rainMaskDraws;}}' in mrt)
bg=r[r.index('bool rainMrtBegin(bool particle=false){'):r.index('void rainMrtEnd(){')]
checks['rain mask MRT: eligibility (not applied, terrain, enabled, not failed, world, mask not failed), cached per frame; caps once; no game VS or PS; the exact stage setup']=(
    'if(applied||!terrain||!enabled||failed||!world||rainMaskFailed||!width||!height){rainMrtUnbind();return false;}' in bg and 'if(!rainMaskFrame){rainMaskFrame=true;rainMaskOk=rainMaskEligible();}' in bg and 'if(!rainCapsChecked)rainMaskCaps();' in bg
    and all(t in bg for t in ('if(vsBound&&(vsModel<1||vsModel>2)&&!(particle&&psBound))why|=4;','if(psBound&&!particle)why|=8;','st[0]==D3DTOP_MODULATE&&st[1]==D3DTA_TEXTURE&&arg2&&st[3]==D3DTOP_MODULATE&&st[4]==D3DTA_TEXTURE&&alphaArg2&&st[6]==D3DTOP_DISABLE&&st[7]==0&&st[8]==D3DTTFF_DISABLE&&spec==FALSE',
        'st[2]==D3DTA_DIFFUSE||st[2]==D3DTA_CURRENT','st[5]==D3DTA_DIFFUSE||st[5]==D3DTA_CURRENT','D3DRS_SPECULARENABLE','ext->peekVertexShader(v)','vsMajor.find(v)','ext->peekPixelShader(p)')))
checks['rain mask MRT: only RT1\'s write mask (red+green+blue; red+blue for additive particles; alpha for mod2x halos; the separate-alpha state is only read) is saved with Get, set after the RT1 bind and restored in rainMrtEnd with the pixel shader back to none (a patched game shader: back to the game shader); RT0 blends as RainBlend (colour and alpha)']=(
    'types[1]={D3DRS_COLORWRITEENABLE1};' in bg and 'want[1]={DWORD(mod2x?D3DCOLORWRITEENABLE_ALPHA:particle&&!over?D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_BLUE:D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE)};' in bg and bg.count('D3DRS_SEPARATEALPHABLENDENABLE')==1 and 'ext->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE' not in bg and 'BLENDOPALPHA' not in bg
    and 'ext->GetRenderState(types[i],&rainMrtPrev[i])' in bg and 'ext->SetPixelShader(patchedPs?patchedPs:particle?particlePS[variant]:rainMrtPS);' in bg and bg.index('ext->SetRenderTarget(1,maskTarget)')<bg.index('ext->SetRenderState(types[i],want[i])')<bg.index('ext->SetPixelShader(patchedPs?patchedPs:particle?particlePS[variant]:rainMrtPS)')
    and 'ext->SetRenderState(types[i],rainMrtPrev[i])' in r and 'ext->SetPixelShader(nullptr);' in r[r.index('void rainMrtEnd(){'):r.index('template<class Draw> HRESULT rainMrtDraw(')])
checks['rain mask MRT: RT1 binds once per run (rainMrtRuns), lazily unbound by rainMrtUnbind; a skip unbinds, counts and logs the first reason once']=(
    'else if(!rainMrtBound){' in bg and 'rainMrtBound=true;++rainMrtRuns;' in bg and 'void rainMrtUnbind(){if(rainMrtBound){ext->SetRenderTarget(1,nullptr);rainMrtBound=false;}}' in r
    and '++rainMaskSkips;' in bg and 'if(!rainSkipLogged){rainSkipLogged=true;logf("WEATHER rain mask skip: reason=%u vs=%d vsModel=%u ps=%d stage0 color=%ld(%ld,%ld) alpha=%ld(%ld,%ld) stage1 color=%ld texcoord=%ld texTransform=%ld specular=%lu"' in bg)
el=r[r.index('bool rainMaskEligible()'):r.index('void rainMaskCaps()')]
checks['rain mask: eligible only at the effect size, depth type AND quality equal the RT (no depth: the RT values), the RT type/quality are kept for creation, skip log carries depthMs']=('rd.Width==width&&rd.Height==height' in el and 'MultiSampleType==D3DMULTISAMPLE_NONE' not in el and 'ok=dd.MultiSampleType==rd.MultiSampleType&&dd.MultiSampleQuality==rd.MultiSampleQuality;' in el and 'GetDepthStencilSurface' in el
    and 'dd.MultiSampleType=rd.MultiSampleType;dd.MultiSampleQuality=rd.MultiSampleQuality;' in el and 'rainMaskWantType=rd.MultiSampleType;rainMaskWantQuality=rd.MultiSampleQuality;' in el and 'ms=%u depthMs=%u' in el)
caps=r[r.index('void rainMaskCaps()'):r.index('bool rainMaskCreate()')]
checks['rain mask MRT: caps checked once (2 RTs, independent write masks, separate alpha blend, ps_2_0), disabled with one log naming them']=(
    'caps.NumSimultaneousRTs>=2' in caps and 'D3DPMISCCAPS_INDEPENDENTWRITEMASKS' in caps and 'D3DPMISCCAPS_SEPARATEALPHABLEND' not in caps and 'D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING' in caps and 'caps.PixelShaderVersion>=D3DPS_VERSION(2,0)' in caps and 'rainCapsChecked=true;' in caps and 'logf("WEATHER rain mask disabled: caps' in caps)
cr=r[r.index('bool rainMaskCreate()'):r.index('void rainMaskClearSwap(')]
checks['rain mask: created lazily at the effect size, failure logged once and disables the mask only (target, rain depth INTZ, both shaders)']=('D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&rainMask' in r and 'rainMaskFailed=true;logf("WEATHER rain mask disabled' in r and 'if(!rainMaskSurface&&!rainMaskCreate())why|=64;' in bg
    and 'D3DUSAGE_DEPTHSTENCIL,(D3DFORMAT)MAKEFOURCC(\'I\',\'N\',\'T\',\'Z\'),D3DPOOL_DEFAULT,&rainDepth' in cr and 'CreatePixelShader(kRainMaskMrtShader,&rainMrtPS)' in cr and 'CreatePixelShader(kRainScrubShader,&rainScrubPS)' in cr and 'drop(rainDepth);drop(rainMrtPS);drop(rainScrubPS);rainMaskFailed=true;' in cr)
checks['rain mask: MSAA twin via CreateRenderTarget with the wanted type/quality only when not NONE, all dropped on failure, type/quality stored; draw target is the twin else the texture surface; recreated on a type/quality change']=(
    'rainMaskWantType!=D3DMULTISAMPLE_NONE' in cr and 'CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,rainMaskWantType,rainMaskWantQuality,FALSE,&rainMaskMS,nullptr)' in cr and 'drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);rainMaskFailed=true;' in cr and 'ms=%u' in cr and 'rainMaskMsType=rainMaskWantType;rainMaskMsQuality=rainMaskWantQuality;' in cr
    and 'maskTarget=rainMaskMS?rainMaskMS:rainMaskSurface;' in bg and 'rainMaskMsType!=rainMaskWantType||rainMaskMsQuality!=rainMaskWantQuality' in bg)
st=r[r.index('bool rainMaskStart('):r.index('bool rainMrtBegin(bool particle=false){')]
checks['rain mask MRT: the frame start (once, before RT1 is bound): the bound depth must be worldDepth, ONE depth snapshot into rainDepth (non-fatal), then ColorFill of the mask target, the RT0-swap clear only if ColorFill fails']=(
    'rainMaskCleared=true;rainDepthOk=false;' in st and 'bound==worldDepth' in st and st.count('resolveDepthInto(rainDepth,false)')==1 and 'ExtensionDevice::RawScope raw(*ext);' in st
    and 'if(FAILED(ext->ColorFill(maskTarget,nullptr,kRainMaskClear)))rainMaskClearSwap(maskTarget);' in st and 'kRainMaskClear=0x80000000' in r and st.index('resolveDepthInto')<st.index('ColorFill')<st.index('rainDepthOk=true')
    and 'if(!rainMaskCleared&&!rainMaskStart(maskTarget))why|=32;' in bg and bg.index('rainMaskStart(maskTarget)')<bg.index('ext->SetRenderTarget(1,maskTarget)'))
sw=r[r.index('void rainMaskClearSwap('):r.index('bool rainMaskStart(')]
checks['rain mask MRT: the swap clear (fallback) keeps the old rules: RT, viewport and scissor saved and restored, scissor test off for the Clear, the whole target']=(
    'GetRenderTarget(0,&gameRT)' in sw and 'ext->GetViewport(&vp)' in sw and sw.index('SetRenderTarget(0,maskTarget)')<sw.index('Clear(')<sw.index('SetRenderTarget(0,gameRT)') and 'D3DRS_SCISSORTESTENABLE,FALSE' in sw and sw.count('Clear(')==1
    and 'ext->SetViewport(&vp);if(scissorKnown)ext->SetScissorRect(&scissor);drop(gameRT);' in sw)
rd=r[r.index('bool resolveDepth() {'):r.index('HRESULT quad(UINT w')]
checks['resolveDepth: the same RESZ code serves depthTex (fatal) and rainDepth (non-fatal); RT1 unbound first; captured set only for depthTex']=(
    'resolveDepthInto(depthTex, true)' in rd and 'bool resolveDepthInto(IDirect3DTexture9* target, bool fatal) {\n        rainMrtUnbind();' in rd and rd.count('captured = true; return true;')==1 and rd.index('resolveDepthInto(depthTex, true)')<rd.index('captured = true;')<rd.index('bool resolveDepthInto('))
rr=r[r.index('    void releaseResources() {'):r.index('    bool error(HRESULT hr')]
rs=r[r.index('    bool resources(UINT w'):r.index('    // Swap chain 0')]
checks['rain mask: dropped in releaseResources (RT1 unbound first, failed flag reset, shaders and rain depth too) and on a size change']=('rainMrtUnbind();stateBlocks.clear();' in rr and 'drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);drop(particleBgSurface);drop(particleBg);particleBgFormat=D3DFMT_UNKNOWN;rainMaskFailed=false;' in rr and 'drop(rainMrtPS);drop(rainScrubPS);' in rr and 'drop(rainMaskMS);drop(rainMaskSurface);drop(rainMask);drop(rainDepth);drop(particleBgSurface);drop(particleBg);' in rs.split('captured=false;')[1].split('}')[0])
checks['rain mask: per-frame flags reset where terrain/applied reset (clearFrame), RT1 unbound there']='rainMrtUnbind();terrain = captured = applied = projectionValid = false;rainMaskCleared=rainMaskDrawn=rainMaskFrame=rainMaskOk=rainDepthOk=rainMaskRainDrawn=particleBgTried=particleBgOk=false;' in r
pr=r[r.index('IDirect3DTexture9* rainMaskPrepare()'):r.index('template<class Draw> HRESULT rainBlendDraw(')]
checks['rain scrub: renderEffects asks rainMaskPrepare once, early (before the AO), then hands its texture to setRainMask right before world->render; no scrub draw per world draw']=(
    r.count('rainMaskPrepare()')==2 and 'IDirect3DTexture9* rainMaskTex=world&&debugMode==0?rainMaskPrepare():nullptr;' in r and r.index('rainMaskPrepare()')<r.index('bindEffects(nullptr);')<r.index('world->setRainMask(rainMaskTex);')<r.index('world->render(saved.targets[0]')
    and r.index('world->render(saved.targets[0]')-r.index('world->setRainMask(')<260)
checks['rain scrub: RT1 unbound, MSAA twin resolved by StretchRect (failure: nullptr and a one-shot log), ONE full-screen quad into the mask texture with depthTex (s0) and rainDepth (s1) POINT, alpha-only write, own SavedState']=(
    pr.startswith('IDirect3DTexture9* rainMaskPrepare(){\n        rainMrtUnbind();') and 'if(!rainMaskDrawn||!rainDepthOk||' in pr and 'ext->StretchRect(rainMaskMS,nullptr,rainMaskSurface,nullptr,D3DTEXF_NONE)' in pr and 'rainMaskResolveHr=rh;return nullptr;' in pr
    and 'SavedState scrub(ext,&stateBlocks);' in pr and 'effectState();' in pr and 'ext->SetRenderTarget(0,rainMaskSurface)' in pr and 'ext->SetTexture(0,depthTex);ext->SetTexture(1,rainDepth);' in pr
    and 'D3DSAMP_MINFILTER,D3DTEXF_POINT' in pr and 'D3DSAMP_MAGFILTER,D3DTEXF_POINT' in pr and 'ext->SetPixelShader(rainScrubPS);' in pr and 'D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED' in pr and pr.count('quad(width,height)')==1
    and 'if(FAILED(rainMaskResolveHr)&&!rainMaskResolveLogged){rainMaskResolveLogged=true;logf("WEATHER rain mask resolve failed HRESULT' in r)
# every device operation that could draw, clear, copy or change targets runs rainMrtUnbind() first
def method(name):
    i=r.index('HRESULT STDMETHODCALLTYPE '+name+'(');return r[i:r.index('\n',i)+1]
for name in ('UpdateSurface','UpdateTexture','GetRenderTargetData','StretchRect','ColorFill'):
    m=method(name);checks[f'rain mask MRT: Device::{name} unbinds RT1 before the call']=('rainMrtUnbind();' in m and m.index('rainMrtUnbind();')<m.index('return ext->'+name))
cl=r[r.index('HRESULT STDMETHODCALLTYPE Clear('):r.index('HRESULT STDMETHODCALLTYPE CreateVertexBuffer(')]
checks['rain mask MRT: Device::Clear unbinds RT1 first (before the Z resolve and the real Clear)']=('rainMrtUnbind();' in cl and cl.index('rainMrtUnbind();')<cl.index('resolveDepth()')<cl.index('return ext->Clear('))
def dmethod(name,span=240):
    i=r.index('HRESULT STDMETHODCALLTYPE '+name+'(',r.index('class Device final'));return r[i:i+span]
rst=dmethod('Reset');pre=dmethod('Present',480);es=dmethod('EndScene')
checks['rain mask MRT: Reset, Present and EndScene unbind first; finishFrame (also SwapChain::Present) unbinds before the extension work']=(
    'rainMrtUnbind();' in rst and 'rainMrtUnbind();' in pre and pre.index('rainMrtUnbind();')<pre.index('finishFrame()') and 'rainMrtUnbind();' in es and es.index('rainMrtUnbind();')<es.index('GuardedMirrorDevice::EndScene()')
    and 'void finishFrame() {\n        Guard mirrorLock(mirrorState.gate);\n        rainMrtUnbind();' in r and 'finishDeviceFrame(owner);' in r)
checks['rain mask MRT: renderEffects unbinds before its first device work; drawHook unbinds for every draw that is not a rain draw, before any capture or effect work']=(
    'rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */\n        ExtensionDevice::RawScope rawEffects(*ext);' in r and 'if(rainMrtBound&&!rainBlend&&!particle)rainMrtUnbind();' in hook and hook.index('if(rainMrtBound&&!rainBlend&&!particle)rainMrtUnbind();')<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)'))
checks['rain mask MRT: SetRenderTarget and SetDepthStencilSurface stay pure unwrap + forward (they are on the generator DIRECT list: the replay calls ext->, never the Device method); the next draw/Clear/StretchRect unbinds']=(
    'HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9* pRenderTarget) override{Guard mirrorLock(mirrorState.gate);return ext->SetRenderTarget(RenderTargetIndex, mirrorResources.unwrap(pRenderTarget));}' in r
    and 'HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* pNewZStencil) override{Guard mirrorLock(mirrorState.gate);return ext->SetDepthStencilSurface(mirrorResources.unwrap(pNewZStencil));}' in r)
pdi_=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
checks['rain mask: no scrub left in prepareDrawImpl or the hook']=('rainScrubDraw' not in pdi_ and 'rainScrubDraw' not in hook)
checks['rain mask: WEATHER line carries the counters; world binds RainMask on s13 for the composite only']=('rainMask=%u rainMrtRuns=%u rainMaskSkips=%u' in r and 'rainMaskScrub' not in r and 'd->SetTexture(13,rainMask?rainMask:neutralZero);rainMask=nullptr;' in w and 'composited=true;d->SetTexture(13,regionalFogTexture);' in w and 'void setRainMask(IDirect3DTexture9* t){rainMask=t;}' in w)
st=r[r.index('bool rainMaskStart(IDirect3DSurface9* maskTarget){'):r.index('bool rainMrtBegin(bool particle=false){')]
checks['rain mask: after the depth snapshot the mirror relearns stage 0 from the device, so the frame\'s later rain draws still match the rain texture']=(
    'snapped=resolveDepthInto(rainDepth,false);}' in st and st.index('resolveDepthInto(rainDepth,false)')<st.index('{IDirect3DBaseTexture9* stage0=nullptr;if(SUCCEEDED(ext->GetTexture(0,&stage0)))drop(stage0);}')<st.index('if(!snapped)return false;'))
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
