#!/usr/bin/env python3
# northlight-test:
"""0.3.188 (task 3) translucent depth census: a diagnostics-only, read-only count of translucent
Z-writing world draws per sample frame, logged as one TRANSLUCENT line. Source inspection only."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
r=fp.tracked('renderer.cpp').read_text()
impl=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
mark='/* 0.3.188 (task 3)'
assert impl.count(mark)==1
dom=impl.index('if(worldDomain){')
cen=impl.index(mark);cap=impl.index('capture(vs);')
assert dom<cen<cap,'census runs inside worldDomain before capture(vs)'
start=impl.rindex('if(sampled()){',0,cen)
assert start>dom and impl[dom:start].count('capture(')==0
end='else ++opaqueZWriteAfterOther;}}}'
block=impl[start:impl.index(end,cen)+len(end)]
assert block.startswith('if(sampled()){'),'gated on sampled()'
for rs in ('D3DRS_ZWRITEENABLE','D3DRS_ALPHABLENDENABLE','D3DRS_COLORWRITEENABLE','D3DRS_SRCBLEND','D3DRS_DESTBLEND'):
    assert 'GetRenderState('+rs in block,rs
assert 'SetRenderState' not in block
assert 'isSkinnedShader(vs)' in block and 'drawWaterVS' in block
assert 'color=(cw&7)!=0' in block,'colour means an RGB write'
assert 'after=firstTranslucentZAt!=0' in block,'only a blended Z-writing draw opens the after window'
assert 'lastOpaqueZAt=at' in block and 'firstTranslucentZAt=at' in block and 'firstDepthOnlyAt=at' in block
assert 'if(skin&&!firstTranslucentZSkinnedAt)firstTranslucentZSkinnedAt=at;' in block,'first skinned translucent Z-writing draw'
counters=['translucentZWriteSkinned','translucentZWriteOther','translucentNoZWrite','depthOnlyPrepass','depthOnlyPrepassSkinned',
          'opaqueZWriteAfterSkinned','opaqueZWriteAfterOther','waterZWriteAfterTranslucent','translucentSrcBlend','translucentDestBlend',
          'censusDraws','firstTranslucentZAt','firstTranslucentZSkinnedAt','firstDepthOnlyAt','lastOpaqueZAt','clearResolveAt']
reset=r[r.index('void resetTranslucentCensus(){'):]
reset=reset[:reset.index('}')]
fin=r[r.index('void finishFrame() {'):r.index('++frame;mirrorState.gate.frame')]
line=fin[fin.index('logf("TRANSLUCENT frame=%u'):]
line=line[:line.index(');')]
for c in counters:
    assert c in reset,'reset '+c
    assert c in line,'log '+c
for k in ('zwriteSkinned=','zwriteOther=','noZWrite=','depthOnly=','depthOnlySkinned=','opaqueZAfterSkinned=','opaqueZAfterOther=','waterZAfter=','firstBlend=',
          'draws=','firstTranslucentZ=','firstTranslucentZSkinned=','firstDepthOnly=','lastOpaqueZ=','clearResolve='):assert k in line,k
assert 'if(ds==worldDepth&&resolveDepth()&&sampled()&&!clearResolveAt)clearResolveAt=censusDraws+1;' in r,'Clear(Z) resolve position'
t=fin.index('TRANSLUCENT frame=');rs=fin.index('resetTranslucentCensus();',t)
assert t<rs<fin.index('clearFrame();',rs)
assert 'if(sampled())logf("TRANSLUCENT' in fin
assert 'if(extensionFault){resetTranslucentCensus();clearFrame();return;}' in fin

print('PASS translucent depth census: read-only, sample frames only, before capture, logged and reset per frame')
