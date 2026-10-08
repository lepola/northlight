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
cmp_line='if(weatherDetect.hot&&!applied&&mirrorState.textureKnown[0]&&mirrorState.textures[0]==weatherDetect.hot){weatherSample.primitives+=count;++weatherSample.draws;rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();}'
checks['drawHook: the single comparison, once']=hook.count(cmp_line+' /* 0.3.199 (rain): rain draws only, ~50/frame */')==1 and hook.count('weatherDetect')==3 and hook.count('weatherSample')==2
checks['drawHook: before the gate split and any other work']=0<=hook.index(cmp_line)<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)')<hook.index('prepareDrawImpl(capture,count)') and hook.index(cmp_line)<hook.index('noteFirst')
# 0.3.199 (rain): RainBlend - the override lives in the matched branch (a bool set there), only for Rain and the setting, wraps exactly the game's draw
helper=r[r.index('template<class Draw> HRESULT rainBlendDraw('):r.index('    // One game draw: capture')]
checks['RainBlend: the bool is set only inside the matched branch, for Rain and the setting']=hook.count('rainBlend=weatherDetect')==1 and hook.count('rainBlend=weatherDetect.hotKind==NorthlightWeather::Kind::Rain&&world&&world->rainBlendSetting();')==1 and cmp_line.split('{',1)[1].count('rainBlend=')==1 and 'bool claimed=false,rainBlend=false;' in hook
checks['RainBlend: both gate paths draw through rainBlendDraw, none through blobFaintDraw directly']=hook.count('rainBlendDraw(rainBlend,claimed,draw)')==2 and 'blobFaintDraw' not in hook
checks['RainBlend: not rain = the plain draw; override sets SrcAlpha/InvSrcAlpha/Add/blend on, inside the draw callback, and restores the previous values after it']=(
    'if(!rain)return blobFaintDraw(claimed,draw);' in helper and 'return blobFaintDraw(claimed,[&]{' in helper
    and all(t in helper for t in ('D3DRS_ALPHABLENDENABLE','D3DRS_SRCBLEND','D3DRS_DESTBLEND','D3DRS_BLENDOP','TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD'))
    and 0<helper.index('GetRenderState')<helper.index('draw();')<helper.index('prev[i]);') and helper.count('draw()')==1)
checks['RainBlend: world setting needs Weather=1']='bool rainBlendSetting()const{return quality.weather&&quality.rainBlend;}' in fp.src('world_renderer.h').read_text()
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
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
