#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.190 shadow pivot distance correction: native model test of shadow_pivot.h (clang++, plain and
ASan/UBSan) and a wiring audit: shadowPivot() applies it after the orbit estimator and writes the
distance back, the self is read only when captured (radiusSelf==1) in the latest selection, reset()
clears it, ShadowPivotCorrection is a quality key (default 1, 0..1) that is in the ini and README, and
the WORLD camera line reports the source. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
w=fp.src('world_renderer.h').read_text();e=fp.src('world_shadow_experiment.inl').read_text();h=fp.src('shadow_pivot.h').read_text();q=fp.src('quality_settings.h').read_text()
ini=(ROOT/'renderer/windows-package/northlight-quality.ini').read_text();readme=(ROOT/'renderer/windows-package/README.txt').read_text()
pivot=w[w.index('    V shadowPivot(){'):w.index('    std::unordered_map<IDirect3DVertexShader9*,const WmoShaderSignature*> wmoShaders;')]
checks={}
checks['applied after the orbit estimator, written back to pivotDistance']=(pivot.count('NorthlightShadowPivot::correct(')==1
    and pivot.index('pivotDistance+=step;')<pivot.index('NorthlightShadowPivot::correct(')<pivot.index('return eye+forward*pivotDistance;')
    and 'pivotDistance=NorthlightShadowPivot::correct(quality.shadowPivotCorrection!=0,pivotCorrection,&eye.x,&forward.x,pivotDistance,' in pivot)
checks['self read only while captured in a recent selection (not selfHold alone)']=('const bool selfFresh=pivotSelfCaptured&&frames-pivotSelfFrame<=4&&actorShadowHistory.selfHold()>0;' in pivot
    and 'selfFresh?actorShadowHistory.selfAt():nullptr' in pivot
    and 'if(stableRan){pivotSelfCaptured=stable.radiusSelf==1;pivotSelfFrame=frames;}' in e)
checks['reset() clears the correction wherever pivotValid is reset']=w.count('pivotValid=false;pivotDistance=12.f;pivotCorrection.reset();pivotSelfCaptured=false;')==1 and w.count('pivotValid=false;')==2
checks['shadowPivot() callers unchanged: one per frame (static request or origin)']=w.count('shadowPivot();')==2 and w.count('V shadowPivot(){')==1 and 'staticFramePivot=shadowPivot();staticPivotReady=true;' in w and 'const V pivot=staticPivotReady?staticFramePivot:shadowPivot();' in w
checks['WORLD camera line extended, on the existing periodic line only']=(w.count('pivotSource=%s snapCorrections=%u selfDistance=%.1f radiusSelf=%d nearBlendAtSelf=%.2f')==1
    and 'NorthlightDiagnostics::enabled()&&now-diagnosticTick>=250' in w[:w.index('pivotSource=%s')][-600:]
    and 'pivotCorrection.nearBlendAtSelf(sourceMatrices[0][0])' in w)
checks['quality key: last, 0..1, presets 1/1/1, default 1, own origin slot']=('{"ShadowPivotCorrection",&Settings::shadowPivotCorrection,0,1,{1,1,1}},' in q and 'unsigned shadowPivotCorrection=1;' in q and 'char origin[33]=' in q)
checks['documented in the ini template and the README']=(';ShadowPivotCorrection=1' in ini and 'ShadowPivotCorrection' in readme)
checks['portable: no D3D or Win32, no allocation']=all(x not in h for x in ('d3d9','windows.h','#include <vector>','new ','malloc','push_back'))
checks['rules: 1 yd along, .35 across, .9995 forward, 4-frame run, 1 yd deadband, 8 yd jump, 0.5..80']=all(x in h for x in (
    'ForwardDot=.9995f,SnapAlong=1.f,SnapAcrossRatio=.35f,SnapAcrossMax=1.5f,SnapJump=40.f,Min=.5f,Max=80.f;','SnapRun=4;','SelfDeadband=1.f,SelfJump=8.f,SelfGain=.3f,SelfStep=1.5f;'))
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-pivot-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_shadow_pivot.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS shadow pivot correction: model and wiring')
