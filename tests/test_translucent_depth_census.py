#!/usr/bin/env python3
# northlight-test:
"""0.3.188 (task 3) translucent depth census: a diagnostics-only, read-only monitor of the early depth resolve per
sample frame, logged as one TRANSLUCENT line. Source inspection only (stable tokens, not exact one-liners)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
r=fp.tracked('renderer.cpp').read_text()
impl=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
mark='/* 0.3.188 (task 3)'
dom=impl.index('if(worldDomain){');cen=impl.index('if(sampled()){const unsigned at=++censusDraws;');cap=impl.index('capture(vs);')
assert dom<cen<cap,'census runs inside worldDomain before capture(vs)'
block=impl[cen:cap]
assert block.startswith('if(sampled()){'),'gated on sampled()'
assert 'SetRenderState' not in block and 'resolveDepth' not in block,'read-only'
assert 'GetRenderState' not in block,'census reads states only through the shared lazy lambdas'
assert 'opaqueZWrite(' in block and 'drawWaterVS' in block and 'lastOpaqueZAt=at' in block,'lastOpaqueZ uses the undo rule, non-water'
counters=['censusDraws','lastOpaqueZAt','clearResolveAt','earlyResolves','earlyResolveAt','earlyResolveUndone']
reset=r[r.index('void resetTranslucentCensus(){'):];reset=reset[:reset.index('}')]
fin=r[r.index('void finishFrameImpl() {'):r.index('++frame;mirrorState.gate.frame')]
line=fin[fin.index('logf("TRANSLUCENT frame=%u'):];line=line[:line.index(');')]
for c in counters:
    assert c in reset,'reset '+c
for k in ('draws=','lastOpaqueZ=','clearResolve=','earlyResolve=','earlyResolveAt=','earlyResolveTotal=','earlyResolveUndone=','earlyResolveUndoneTotal='):assert k in line,k
for k in ('zwriteSkinned','noZWrite','depthOnly','firstBlend','firstTranslucentZ','firstDepthOnly','waterZAfter','opaqueZAfter'):assert k not in r,'removed counter '+k
clr=r[r.index('HRESULT STDMETHODCALLTYPE Clear('):][:900]
assert 'resolveDepth()' in clr and 'earlyDepth.freeze()' in clr and 'clearResolveAt=censusDraws+1' in clr,'Clear(Z) resolve or freeze position'
assert 'if(sampled())logf("TRANSLUCENT' in fin
cf=r[r.index('void clearFrame() {'):r.index('void releaseResources()')]
assert 'resetTranslucentCensus()' in cf,'clearFrame resets the census'
assert fin.index('TRANSLUCENT frame=')<fin.index('clearFrame();'),'the log runs before clearFrame resets'
assert 'resetTranslucentCensus();' not in fin,'only clearFrame resets (also on the extensionFault path)'
assert 'if(extensionFault){clearFrame();return;}' in r[r.index('void finishFrame() {'):r.index('void finishFrameImpl() {')]
print('PASS translucent depth census: read-only, sample frames only, before capture, logged and reset per frame')
