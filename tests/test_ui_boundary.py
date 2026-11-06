#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.203 (task 17) fallback effect boundary: native test of ui_boundary.h (clang++, plain and ASan/UBSan) and a wiring audit of renderer.cpp:
the hash boundary sets its flag before renderEffects(), the fallback is guarded by the sticky disarm, reads the cheap state first and the viewport last,
and the Armed path publishes the learned VS hash to the stream. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile,os
HERE=Path(__file__).resolve().parent
r=fp.src('renderer.cpp').read_text();h=fp.src('ui_boundary.h').read_text()
fb=r[r.index('void uiFallbackDraw('):r.index('void uiFallbackEndFrame(')]
ef=r[r.index('void uiFallbackEndFrame('):r.index('\npublic:',r.index('void uiFallbackEndFrame('))]
hb=r[r.index('} else if (tag==2) {'):r.index('void uiFallbackDraw(')]
checks={}
checks['hash branch: screen-space state and main target, flag before effects']=(all(x in hb for x in ('NorthlightUiBoundary::accepts(zw!=0,rows,zf==D3DCMP_ALWAYS,z!=0)','fullViewport(desc,nullptr,true)','D3DRS_ZWRITEENABLE','D3DRS_ZFUNC','D3DRS_ZENABLE')) and 'psTags.find' not in hb and 0<hb.index('fullViewport(desc,nullptr,true)')<hb.index('hashBoundaryThisFrame=true;')<hb.index('renderEffects();') and 'kind=ui ' in hb)
checks['hash branch: the fallback runs after it']=hb.index('renderEffects();')<hb.index('uiFallbackDraw(vc,vs);')
checks['fallback uses a proven shader or collecting() before any state read']=fb.index('!uiFallback.collecting()&&(!remembered||uiFallback.disarmed())')<fb.index('peekPixelShader') and fb.index('known->second==remembered')<fb.index('peekPixelShader')
checks['fallback guard names terrain, applied, hashBoundaryThisFrame']=all(x in fb[:fb.index('IDirect3DPixelShader9* ps')] for x in ('!terrain','applied','hashBoundaryThisFrame','vc.wmo'))
checks['fallback: depth function and enable read before constants; unreadable states reject while aware']=('DWORD zf=D3DCMP_ALWAYS,z=1;if(zFuncAware&&(FAILED(ext->GetRenderState(D3DRS_ZFUNC,&zf))||FAILED(ext->GetRenderState(D3DRS_ZENABLE,&z))))return;' in fb
    and fb.index('D3DRS_ZWRITEENABLE')<fb.index('D3DRS_ZFUNC')<fb.index('GetVertexShaderConstantF(0,c,4)') and 'zFuncAware=world->depthFuncAware();' in r)
checks['fallback: disabled depth or ALWAYS, without changing the accepted projection']='NorthlightUiBoundary::accepts(zw!=0,c,zf==D3DCMP_ALWAYS,z!=0)' in fb
checks['fallback: zwrite read, then isBoundary before it, accepts() the one predicate, quiet fullViewport last']=(fb.index('isBoundary(')<fb.index('D3DRS_ZWRITEENABLE')<fb.index('NorthlightUiBoundary::accepts(zw!=0,c,zf==D3DCMP_ALWAYS,z!=0)')<fb.index('fullViewport(desc,nullptr,true)')
    and fb.count('fullViewport(')==1 and 'screenSpaceRows' not in fb)
checks['fullViewport: quiet probes preserve the world-size gate by default']='desc.Width>=640&&desc.Height>=360' in r
checks['fallback: learning skips frames without effects (enabled/failed), the armed boundary still runs renderEffects']=fb.index('if(!armed&&(!enabled||failed))return;')<fb.index('peekPixelShader')
checks['fallback: world/water test before the PS lookup']=fb.index('vc.world||drawWaterVS')<fb.index('peekPixelShader')
checks['fallback: renders effects with the fallback flag and counter']=all(x in fb for x in ('fallbackBoundaryThisFrame=true','++uiFallbackBoundaries','kind=ui-fallback','renderEffects();'))
checks['end frame: FrameKind mapping']='(!enabled||failed)?K::Neutral:hashBoundaryThisFrame?K::WorldHash:fallbackBoundaryThisFrame?K::WorldFallback:!terrain?K::NoWorld:applied&&!hdrPresentFallback?K::Neutral:K::WorldMissed' in ef
pub=r[r.index('void uiFallbackPublish(){'):r.index('void uiFallbackEndFrame(')]
checks['stream hash: one writer (uiFallbackPublish: learned VS while armed, else 0), called after the end-of-frame and forget changes']=(r.count('learnedUiVsHash.store(')==1 and 'learnedUiVsHash.store(want,std::memory_order_relaxed)' in pub and 'uiFallback.armed()?uiFallbackVsHash(uiFallback.learnedVs()):0' in pub
    and ef.index('uiFallback.endFrame(kind)')<ef.index('uiFallbackPublish();') and 'uiFallback.forget(reinterpret_cast<std::uintptr_t>(obj));uiFallbackPublish();' in pub)
checks['end frame: active, lost and no-candidate logs; a disarm after collecting is logged once, stock (never collecting) stays silent']=(all(x in ef for x in ('fallback lost; relearning','fallback active','fallback: no candidate'))
    and 'const bool active=uiFallback.collecting();' in ef and 'if(active)logf("EFFECT boundary fallback disarmed: stock UI shaders seen frame=%u where=world"' in ef
    and ef.index('if(uiFallback.disarmed()){hashBoundaryThisFrame=fallbackBoundaryThisFrame=false;return;}')<ef.index('const bool active='))
checks['end frame runs before clearFrame and the periodic frame line']=r.index('uiFallbackEndFrame();\n')<r.index('logf("frame=%u applied=%u')<r.index('{CpuScope cpu(sampledFrame?&cleanup:nullptr);clearFrame();}') and 'uiAfterTerrain=%u uiFallback=%u uiFallbackState=%c"' in r
checks['shader creation forgets the identity']=r.count('uiFallbackForget(*out);')==2
checks['ui_boundary.h is pure']=not any(x in h for x in ('windows.h','d3d9','Windows.h','<vector>','new '))
hb2=r[r.index('} else if (tag==2) {'):r.index('++uiAfterTerrain;')]
checks['pre-world stock UI cannot disarm the fallback: only a world hash boundary does']=('if(!terrain&&!skySeen)return;' in hb2
    and 'stockPairNoWorld' not in r and 'stockUiPair' not in r and '.onHashBoundary(' not in r
    and 'auto result=uiFallback.endFrame(kind);' in ef)
checks['periodic frame line shows the fallback state (d disarmed, a armed, l learning), no new line on stock']="uiFallback=%u uiFallbackState=%c" in r and "uiFallback.disarmed()?'d':uiFallback.armed()?'a':'l'" in r
checks['fallback fires at most once a frame (renderEffects may return without applied)']='fallbackBoundaryThisFrame||' in fb[:fb.index('IDirect3DPixelShader9* ps')]
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-uiboundary-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        if os.name=='nt' and label=='asan':continue
        exe=Path(tmp)/('test-'+label+('.exe' if os.name=='nt' else ''))
        compiler=[str(fp.zig()),'c++','-target','x86_64-windows-gnu'] if os.name=='nt' else ['clang++']
        subprocess.run([*compiler,'-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_ui_boundary.cpp'),'-o',str(exe)],env=fp.zig_env() if os.name=='nt' else None,check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS ui boundary: learning, arming, loss, forget, wiring')
