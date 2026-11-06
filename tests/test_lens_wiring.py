#!/usr/bin/env python3
"""Check the lens pass follows weather shelter and pre-UI composition gates."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
r=(root/'src/proxy/renderer.cpp').read_text(encoding='utf-8')
s=(root/'src/water/surface_renderer.h').read_text(encoding='utf-8')
surface=r[r.index('    void surfaceWeather(){'):r.index('    void renderEffects() {')]
assert 'world->weatherSetting()' in surface and 'memoryGuard.capsHalved()' in surface
assert 'weather.kind!=NorthlightWeather::Kind::Rain' in surface and 'weather.kind!=NorthlightWeather::Kind::Snow' in surface and 'weather.blend<=0' in surface
assert 'skyBandSeen' not in surface
assert surface.index('surface->precipitation(')<surface.index('world->supplementalWeatherOutdoors()')<surface.index('surface->lens(')
frame=r[r.index('    void weatherFrame('):r.index('    void logWeatherProbe(')]
assert 'NorthlightWeatherMemory::counts(memoryWanted,memory.valid)' in frame
hotkey=r[r.index('        if(k12&&!key12){'):r.index('        key12=k12;')]
assert 'if(accepted)NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Weather);' in hotkey
assert 'snow?q.snowLens:q.rainLens' in surface and 'snow?q.snowLensStrength:q.rainLensStrength' in surface
assert 'if(cameraWanted&&world->supplementalWeatherOutdoors())' in surface and 'nameDepth' in surface
assert 'nameDepth,weather.intensity,snow?world->weatherLensHeat():0.f,float(snow?q.snowLensSize:q.rainLensSize)*.01f,snow||q.rainRivulets!=0,frost)' in surface
assert 'surface->sandLens(' in surface and 'if(!sand){surface->precipitation(' in surface
assert 'surface->setMotion(context,unsigned(weather.kind),q,surfaceTime)' in surface
assert 'snow&&q.snowFrost' in surface and 'q.snowFrostStrength' in surface
assert 'lensState.advance(time,intensity,snow,heat' in s
replay=r[r.index('    void spellReplay(){'):r.index('    // Frame end (finishFrameImpl')]
assert replay.index('spell.replay()')<replay.rindex('surfaceWeather();')
lens=s[s.index('    void lens('):]
assert lens.index('!(visibility>0)')<lens.index('CreatePixelShader')<lens.index('StretchRect')<lens.index('DrawPrimitiveUP(')
assert 'NameMaskCompositeBind protect(d,nameDepth)' in lens and 'D3DRS_COLORWRITEENABLE,7' in lens
print('PASS lens weather, shelter, memory guard, pre-UI spell ordering, no-work and name-mask wiring')
