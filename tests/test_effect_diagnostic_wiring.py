#!/usr/bin/env python3
"""Capture requests must survive disabled effects and never export a previous frame."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text()
clear=r[r.index('    void clearFrame()'):r.index('    void captureEffectDiagnostics()')]
assert 'diagnosticAoRaw=diagnosticAo=diagnosticWorld=false;' in clear
capture=r[r.index('    void captureEffectDiagnostics()'):r.index('    void releaseResources()')]
assert capture.index('if(!scopes&&!fallback)return;')<capture.index('ExtensionDevice::RawScope')
assert 'renderEffects(' not in capture and 'finishCaptures(' not in capture
assert 'diagnosticAoRaw' in capture and 'diagnosticAo' in capture and 'capturedThisFrame()' in capture
assert 'captureSkippedDiagnostic(frame,w,h)' in capture and 'fpsEligible=0' in capture
finish=r[r.index('    void finishFrameImpl()'):r.index('bool k12=')]
assert finish.index('captureEffectDiagnostics();')<finish.index('const bool diagnosticFrame=')<finish.index('frameIntervals.sample(')
hotkey=r[r.index('bool k12='):r.index('key12=k12;')]
assert 'effects-off' not in hotkey and 'requestNormalGpuDiagnosticCapture()' in hotkey
products=w[w.index('    void captureProducts('):w.index('    bool captureSkippedDiagnostic(')]
assert 'current&&composited&&lastRenderDebug==0' in products
assert 'shadowCurrent' in products and 'ready&&effects.fog&&uploadedFogField' in products
assert 'if(debug==1||debug==2)return emptyDiagnosticView(targetSurface,w,h,debug);' in w
assert 'world->emptyDiagnosticView(target,desc.Width,desc.Height,worldDebug)' in r
assert 'if(!owner.diagnosticWorld)owner.world->emptyDiagnosticView(target,desc.Width,desc.Height,owner.worldDebug)' in r
panel=fp.src('settings_panel.cpp').read_text()
for scope in ['World','AO','Materials','Fog']:assert 'request('+scope+')' in panel
print('PASS effect diagnostics: independent requests, fresh frame flags, no forced producers, disabled fallback, empty skipped products, capture frame excluded from FPS')
