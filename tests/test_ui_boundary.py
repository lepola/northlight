#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.203 (task 17) fallback effect boundary: native test of ui_boundary.h (clang++, plain and ASan/UBSan) and a wiring audit of renderer.cpp:
the hash boundary sets its flag before renderEffects(), the fallback is guarded by the sticky disarm, reads the cheap state first and the viewport last,
and the Armed path publishes the learned VS hash to the stream. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
r=fp.src('renderer.cpp').read_text();h=fp.src('ui_boundary.h').read_text()
fb=r[r.index('void uiFallbackDraw('):r.index('void uiFallbackEndFrame(')]
ef=r[r.index('void uiFallbackEndFrame('):r.index('\npublic:',r.index('void uiFallbackEndFrame('))]
hb=r[r.index('} else if (tag==2) {'):r.index('void uiFallbackDraw(')]
checks={}
checks['hash branch: clipWOne, flag set before renderEffects']='NorthlightUiBoundary::clipWOne(w)' in hb and 0<hb.index('hashBoundaryThisFrame=true;')<hb.index('renderEffects();') and 'kind=ui ' in hb
checks['hash branch: the fallback runs after it']=hb.index('renderEffects();')<hb.index('uiFallbackDraw(vc,vs);')
checks['fallback guarded by collecting() (sticky disarm, warm-up frames) before any state read']=fb.index('if(!uiFallback.collecting()||')<fb.index('peekPixelShader')
checks['fallback guard names terrain, applied, hashBoundaryThisFrame']=all(x in fb[:fb.index('IDirect3DPixelShader9* ps')] for x in ('!terrain','applied','hashBoundaryThisFrame','vc.wmo'))
checks['fallback: no ZENABLE requirement']='D3DRS_ZENABLE,&z)' in fb and fb.count('D3DRS_ZENABLE')==1 and 'zenable' not in fb.lower().replace('d3drs_zenable','')
checks['fallback: zwrite read, then isBoundary before it, accepts() the one predicate, quiet fullViewport last']=(fb.index('isBoundary(')<fb.index('D3DRS_ZWRITEENABLE')<fb.index('NorthlightUiBoundary::accepts(zw!=0,c)')<fb.index('fullViewport(desc,nullptr,true)')
    and fb.count('fullViewport(')==1 and 'screenSpaceRows' not in fb)
checks['fullViewport: a quiet probe neither counts viewportRejects nor logs VIEWPORT GATE']='bool fullViewport(D3DSURFACE_DESC& desc,D3DVIEWPORT9* viewport=nullptr,bool quiet=false)' in r and 'if(!ok&&!quiet){\n            ++viewportRejects;' in r
checks['fallback: learning skips frames without effects (enabled/failed), the armed boundary still runs renderEffects']=fb.index('if(!armed&&(!enabled||failed))return;')<fb.index('peekPixelShader')
checks['fallback: world/water test before the PS lookup']=fb.index('vc.world||drawWaterVS')<fb.index('peekPixelShader')
checks['fallback: renders effects with the fallback flag and counter']=all(x in fb for x in ('fallbackBoundaryThisFrame=true','++uiFallbackBoundaries','kind=ui-fallback','renderEffects();'))
checks['end frame: FrameKind mapping']='(!enabled||failed)?K::Neutral:hashBoundaryThisFrame?K::WorldHash:fallbackBoundaryThisFrame?K::WorldFallback:!terrain?K::NoWorld:applied?K::Neutral:K::WorldMissed' in ef
pub=r[r.index('void uiFallbackPublish(){'):r.index('void uiFallbackEndFrame(')]
checks['stream hash: one writer (uiFallbackPublish: learned VS while armed, else 0), called after the end-of-frame and forget changes']=(r.count('learnedUiVsHash.store(')==1 and 'learnedUiVsHash.store(want,std::memory_order_relaxed)' in pub and 'uiFallback.armed()?uiFallbackVsHash(uiFallback.learnedVs()):0' in pub
    and ef.index('uiFallback.endFrame(kind)')<ef.index('uiFallbackPublish();') and 'uiFallback.forget(reinterpret_cast<std::uintptr_t>(obj));uiFallbackPublish();' in pub)
checks['end frame: active, lost and no-candidate logs; a disarm after collecting is logged once, stock (never collecting) stays silent']=(all(x in ef for x in ('fallback lost; relearning','fallback active','fallback: no candidate'))
    and 'const bool active=uiFallback.collecting();' in ef and 'if(active)logf("EFFECT boundary fallback disarmed: stock UI shaders seen frame=%u where=%s"' in ef
    and ef.index('if(uiFallback.disarmed()){stockPairNoWorld=hashBoundaryThisFrame=fallbackBoundaryThisFrame=false;return;}')<ef.index('const bool active='))
checks['end frame runs before clearFrame and the periodic frame line']=r.index('uiFallbackEndFrame();\n')<r.index('logf("frame=%u applied=%u')<r.index('{CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}') and 'uiAfterTerrain=%u uiFallback=%u uiFallbackState=%c"' in r
checks['shader creation forgets the identity']=r.count('uiFallbackForget(*out);')==2
checks['ui_boundary.h is pure']=not any(x in h for x in ('windows.h','d3d9','Windows.h','<vector>','new '))
hb2=r[r.index('} else if (tag==2) {'):r.index('++uiAfterTerrain;')]
checks['pre-world stock UI pair: noted while not disarmed, disarms only at the end of a frame without world']=('if(!terrain){if(!uiFallback.disarmed()&&!stockPairNoWorld&&stockUiPair())stockPairNoWorld=true;return;}' in hb2
    and ef.index('const bool noWorldPair=stockPairNoWorld&&!terrain;')<ef.index('stockPairNoWorld=false;\n')<ef.index('if(noWorldPair)uiFallback.onHashBoundary();else result=uiFallback.endFrame(kind);'))
checks['periodic frame line shows the fallback state (d disarmed, a armed, l learning), no new line on stock']="uiFallback=%u uiFallbackState=%c" in r and "uiFallback.disarmed()?'d':uiFallback.armed()?'a':'l'" in r
sp=r[r.index('bool stockUiPair(){'):r.index('unsigned long long uiFallbackVsHash(')]
checks['stockUiPair: the hash test (UI PS tag, c3 via clipWOne), no state writes']='itp->second==2' in sp and 'GetVertexShaderConstantF(3,w,1)' in sp and 'clipWOne(w)' in sp and 'Set' not in sp
checks['fallback fires at most once a frame (renderEffects may return without applied)']='fallbackBoundaryThisFrame||' in fb[:fb.index('IDirect3DPixelShader9* ps')]
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-uiboundary-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_ui_boundary.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS ui boundary: learning, arming, loss, forget, wiring')
