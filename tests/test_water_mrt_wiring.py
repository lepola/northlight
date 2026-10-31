#!/usr/bin/env python3
# northlight-test:
"""0.3.214 (water MRT): wiring of the water mask in renderer.cpp / water_renderer.h - source checks, no compiler.
A recognised water draw writes the liquid mask itself through render target 1 (the mask variants of its two shaders, no render
target 0 / depth switch, no second draw); render target 1 is unbound lazily before anything else that draws, clears or changes
targets (the rain mask owns the same target); every refusal keeps the 0.3.140 second-draw pass. Also: the appearance passes
(reflection / composite, NorthlightWaterContext, kWaterEffectsEnabled, their shaders and tools) are gone."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

r=fp.src('renderer.cpp').read_text();w=fp.src('water_renderer.h').read_text()
checks={}
def between(text,a,b):
    i=text.index(a);return text[i:text.index(b,i)]

# ---- the removed appearance passes ----
for name in ('water_effects.hlsl','water-shader-build.json','water_compiled_shaders.h'):
    checks[f'removed: {name}']=not any(p.name==name for d in (fp.SHADERS,fp.GENERATED) for p in d.iterdir())
checks['removed: compile_water_shaders.py']=not (fp.REPO/'scripts/shaders/compile_water_shaders.py').exists()
every=''.join(p.read_text(errors='replace') for d in fp.SRC_DIRS for p in d.iterdir() if p.suffix in ('.h','.inl','.cpp'))
checks['removed: no legacy appearance symbol left in src']=not re.search(r'kWaterEffectsEnabled|NorthlightWaterContext|waterContext|kWaterReflectionShader|kWaterCompositeShader|water_compiled_shaders|Bucket::Water',every) and 'reflectionPS' not in w and 'captureMaterial(' not in w
checks['removed: the renderer has no render() or appearance targets']=all(s not in w for s in ('bool render(','reflectionSurface','colorSurface','compositePS'))
checks['removed: build_renderer.py and the shader compilers do not know the water manifest']='water-shader-build' not in (fp.REPO/'scripts/build_renderer.py').read_text() and 'compile_water_shaders' not in ''.join(p.read_text() for p in (fp.REPO/'scripts/shaders').glob('*.py'))

# ---- the draw hook ----
hook=between(r,'template<class Capture,class Draw> HRESULT drawHook(','    // 0.3.154: blob shadow claim')
checks['drawHook: both gate paths wrap the name-mask chain in waterMrtDraw, with the bare draw as the fallback']=hook.count('waterMrtDraw(draw,[&]{return nameMaskDraw(rainBlend,claimed||mist,draw);})')==2 and 'const HRESULT hr=nameMaskDraw(' not in hook
unbind_rain='if(rainMrtBound&&!rainBlend)rainMrtUnbind();'
unbind_water='if(water&&water->isMrtBound()&&(rainBlend||applied||!enabled||failed||!nextDrawIsWater()))rainMrtUnbind();'
checks['drawHook: render target 1 is unbound before any other draw (rain first, then water unless the draw is a water draw), before both gate paths']=(
    hook.count(unbind_rain)==1 and hook.count(unbind_water)==1 and hook.index(unbind_rain)<hook.index(unbind_water)<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)')<hook.index('prepareDrawImpl(capture,count)'))
checks['drawHook: the pending plan is dropped at the top of every draw']=hook.count('waterPending.valid=false;')==1 and hook.index('waterPending.valid=false;')<hook.index('if(!frameDrawGates){')
checks['nextDrawIsWater: an identity lookup of the bound vertex shader, false when unknown']='bool nextDrawIsWater(){IDirect3DVertexShader9* v=nullptr;return ext->peekVertexShader(v)&&v&&water->hasVertex(v);}' in r
unbind=between(r,'    void rainMrtUnbind(){','    bool rainMaskEligible(){')
checks['rainMrtUnbind: one unbind serves both owners of render target 1']=('water->isMrtBound()' in unbind and 'water->unbindMrt()' in unbind and 'rainMrtBound=false' in unbind and unbind.count('SetRenderTarget(1,nullptr)')==1)

# ---- captureWater / waterMrtDraw ----
cw=between(r,'    template<class Draw> void captureWater(','    // 0.3.214 (water MRT): once per device, the caps')
checks['captureWater: the same gates as the second-draw pass (main depth, full viewport, bound pixel shader) before any plan']=(
    cw.index('mainDepth')<cw.index('fullViewport(desc,&viewport)')<cw.index('water->plan(')<cw.index('waterPending={'))
checks['captureWater: an MRT plan only records the draw and returns; the single second-draw call after it unbinds render target 1 first']=(
    cw.count('water->capture(')==1 and 'Plan::Mrt&&sameMs){waterPending={vs,ps,userPointer,desc.Width,desc.Height,desc.MultiSampleType,desc.MultiSampleQuality,viewport,true};return;}' in cw
    and cw.index('waterPending={')<cw.index('if(water->isMrtBound())rainMrtUnbind();')<cw.index('water->capture('))
checks['captureWater: any water draw not planned as MRT (every early return) unbinds render target 1 on the way out']=('~UnbindUnlessPlanned(){if(!self->waterPending.valid&&self->water->isMrtBound())self->rainMrtUnbind();}' in cw and cw.index('UnbindUnlessPlanned')<cw.index('water->usable()')<cw.index('mainDepth'))
checks['captureWater: the twin must match the depth buffer multisampling, else the second-draw pass']='worldDepthDesc.MultiSampleType==desc.MultiSampleType&&worldDepthDesc.MultiSampleQuality==desc.MultiSampleQuality' in cw
checks['captureWater: Skip returns before anything is drawn']=cw.index('Plan::Skip)return;')<cw.index('waterPending={')
mrtDraw=between(r,'    template<class Raw,class Chain> HRESULT waterMrtDraw(','    // 0.3.193 BlobShadowStrength')
checks['waterMrtDraw: begin, the one chain draw, end - all in extensionWork; a refusal makes the second draw first, with render target 1 unbound']=(
    mrtDraw.count('extensionWork("water mask"')==3 and 'water->beginMrt(' in mrtDraw and mrtDraw.count('chain()')==3 and mrtDraw.count('water->endMrt(SUCCEEDED(hr))')==1
    and mrtDraw.index('beginMrt(')<mrtDraw.index('water->capture(')<mrtDraw.index('if(!masked)return chain();')<mrtDraw.index('const HRESULT hr=chain();')<mrtDraw.index('endMrt(')
    and 'if(water->isMrtBound())rainMrtUnbind();' in mrtDraw and mrtDraw.index('rainMrtUnbind()')<mrtDraw.index('water->capture('))
checks['waterMrtDraw: no pending plan = the plain chain (after the unbind, see below)']='if(!waterPending.valid){if(water&&water->isMrtBound())rainMrtUnbind();return chain();}' in mrtDraw
checks['the only water->capture( calls are the two fallbacks (no draw override and no capture lambda calls it)']=r.count('water->capture(')==2 and 'captureWater(vs,NorthlightWaterRenderer::NoUserPointer,draw)' in r
caps=between(r,'    void waterMrtCaps(){','    // The next draw uses a water vertex shader')
checks['caps: two targets, independent write masks and bit depths, MRT blending, a blendable G32R32F target, ps_2_0 - else the second-draw pass with a log line']=all(s in caps for s in ('NumSimultaneousRTs>=2','D3DPMISCCAPS_INDEPENDENTWRITEMASKS','D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS','D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING','D3DPS_VERSION(2,0)','D3DUSAGE_RENDERTARGET|kUsageQueryPostPixelShaderBlending','0x00080000','D3DFMT_G32R32F','WATER mask MRT disabled: caps','water->setMrtCaps(ok)'))

# ---- the MRT path never switches target 0 or depth ----
begin=between(w,'    bool beginMrt(','    void endMrt(');end=between(w,'    void endMrt(','    // Once per frame, before the effects read the mask')
for name,body in (('beginMrt',begin),('endMrt',end)):
    checks[f'{name}: no render target 0 and no depth switch, no Clear, no draw']=not re.search(r'SetRenderTarget\(0|SetDepthStencilSurface|Clear\(|DrawPrimitive|DrawIndexed',body)
checks['beginMrt: clears before it binds; binds render target 1 once (lazily); colour write 1 on; the mask shaders; then nothing else']=(
    begin.index('clearTarget(')<begin.index('SetRenderTarget(1,target)')<begin.index('COLORWRITEENABLE1,15')<begin.index('SetVertexShader(vi->second)')
    and begin.count('SetRenderTarget(')==1 and 'if(!mrtBound){' in begin and 'if(!mrtCleared){' in begin)
checks['beginMrt: blend, fog, sRGB and resources can refuse with the device untouched (all reads before the first write)']=(
    begin.index('degrade(2,')<begin.index('degrade(4,')<begin.index('degrade(8,')<begin.index('mrtResources(')<begin.index('clearTarget(') and begin.index('GetRenderState(D3DRS_SRGBWRITEENABLE')<begin.index('mrtResources('))
checks['endMrt: gives the game its shaders and colour write 1 back; render target 1 stays bound']=('SetPixelShader(mrtPS)' in end and 'SetVertexShader(mrtVS)' in end and 'COLORWRITEENABLE1,mrtPrevWrite1' in end and 'unbindMrt' not in end)
checks['blendKeeps: disabled, or ADD with ONE/SRCALPHA over ZERO/INVSRCALPHA']='return !enable||(op==D3DBLENDOP_ADD&&(src==D3DBLEND_ONE||src==D3DBLEND_SRCALPHA)&&(dst==D3DBLEND_ZERO||dst==D3DBLEND_INVSRCALPHA));' in w
checks['fog: refused for pixel shaders below 3.0 only (they may get table fog on every target)']='if(fog&&pi->second.major<3)return degrade(4' in w
checks['the mask variant check precedes the MRT variant: patchMrt and verifyMrt run after maskShader() matched its recorded hash']=(
    between(w,'    void registerPixel(','logf("WATER registered pixel').index('maskShader(hash,false')<between(w,'    void registerPixel(','logf("WATER registered pixel').index('patchMrt(')<between(w,'    void registerPixel(','logf("WATER registered pixel').index('verifyMrt('))

# 0.3.215: a water draw captureWater never planned does not meet render target 1 left bound by the previous water draw
planned=between(r,'    template<class Raw,class Chain> HRESULT waterMrtDraw(','    // 0.3.193 BlobShadowStrength')
checks['waterMrtDraw: without a planned draw render target 1 is unbound (if the water mask holds it) before the game draw']=(
    'if(!waterPending.valid){if(water&&water->isMrtBound())rainMrtUnbind();return chain();}' in planned and planned.index('rainMrtUnbind();return chain();')<planned.index('const PendingWater p=waterPending;'))

# ---- the frame ----
fx=between(r,'        rainMrtUnbind(); /* 0.3.202 (rain mask MRT) */\n        ExtensionDevice::RawScope rawEffects','        // discs and glare')
checks['renderEffects: render target 1 unbound, then the water data decoded, then the mask handed out']=(
    fx.index('rainMrtUnbind();')<fx.index('water->resolveMask()')<fx.index('water->maskTextureForDepth(worldMinDepth,worldMaxDepth)') and 'if(water&&debugMode==0)water->resolveMask();' in fx)
resolve=between(w,'    bool resolveMask(){','    bool mrtResources(')
checks['resolveMask: unbinds, resolves the twin once, clears the mask unless a second draw did, one decode draw inside its own SavedState']=(
    resolve.index('unbindMrt()')<resolve.index('StretchRect(twin')<resolve.index('if(!captured&&!clearTarget(maskSurface))')<resolve.index('SavedState save')<resolve.index('quad()')
    and resolve.count('quad()')==1 and resolve.count('StretchRect(')==1)
checks['maskTextureForDepth: the min/max Z contract kept; MRT data only after the decode']=('std::fabs(minZ-maskMinZ)<.0001f&&std::fabs(maxZ-maskMaxZ)<.0001f' in w and '(captured||mrtResolved)&&!mrtDirty' in w)
checks['endFrame / reset: the MRT flags go with the frame and the resources']=('captured=mrtCaptured=mrtResolved=mrtDirty=mrtCleared=false;' in w and 'drop(rawSurface);drop(raw);drop(twin);' in w)
checks['WATER line: draws = mrt + redraw, with the three counters, the binds and the longest run']=('mrt=%u redraw=%u degraded=%u binds=%u longestRun=%u afterEffects=%u",waterDraws,0u,waterClears,waterScans,waterReadFailures,waterMrt,waterRedraws,waterDegraded,waterBinds,waterLongestRun,waterLate' in r and 'waterDraws=waterRedraws+waterMrt' in r)

# ---- the other unbind sites (the rain mask's list) still unbind ----
for site in ('HRESULT STDMETHODCALLTYPE Clear(','HRESULT STDMETHODCALLTYPE StretchRect(','HRESULT STDMETHODCALLTYPE ColorFill(','HRESULT STDMETHODCALLTYPE UpdateSurface(','HRESULT STDMETHODCALLTYPE GetRenderTargetData(','HRESULT STDMETHODCALLTYPE EndScene('):
    i=r.index(site);checks[f'{site.split()[-1]} unbinds render target 1 (both owners)']='rainMrtUnbind();' in r[i:i+700]
reset=between(r,'STDMETHODCALLTYPE Reset(','    // Frame boundary only (after clearFrame())')
checks['Reset: render target 1 unbound before the real Reset; the water renderer is reset after it']=reset.index('rainMrtUnbind();')<reset.index('water->reset()')
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
