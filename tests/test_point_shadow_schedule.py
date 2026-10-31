#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Point cube schedule, 0.3.151 face cycles (PointShadowFacesPerFrame) and per-face static content keys:
native test_point_shadow_schedule.cpp (plain and ASan/UBSan) plus renderer wiring. No game, Wine or device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
pt=fp.src('world_point_rendering.inl').read_text()
# The same per-face invariants now apply to each independently owned light slot.

body=pt[pt.index('    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true){'):pt.index('    bool renderPointLighting(')]
pred=pt[pt.index('    bool pointRefreshPredicted()const{'):pt.index('    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true){')]
# 3a: static faces keyed on content; the generation only through the unknown-records fallback.
assert 'pointCacheSerial' not in pt and pt.count('pointStaticRebuild(')==2
assert 'NorthlightPointShadow::selectMany(active->localLights.data(),active->localLights.size(),context.camera,quality.pointShadowLimit).count!=0;' in pred and 'const unsigned rebuildMask=pointStaticRebuild(pointSlot().selected,lightChanged);' in body
assert 'if(!pointSlot().cacheValid||lightChanged||pointSlot().cacheSource!=light.sourceId)return 63;' in pt
assert 'if(rebuildMask&(1u<<face)){if(!bindFace(pointSlot().cacheSurface[face],"cube cache target"))return false;' in body
assert body.index('pointSlot().faceContent[face]=digests?digests[face]')>body.index('"cube static draw"'),'a face key is recorded only after its static draws'
# S1'': no fragment beyond the light's range is stored (every cube caster, one shader), so a
# face depends only on in-range casters and the sphere + face digest is complete.
hlsl=fp.src('local_light_effects.hlsl').read_text()
ps=hlsl[hlsl.index('float4 LocalShadowPS('):hlsl.index('float2 localDepthUV(')]
assert 'float4 LocalSphere : register(c1);' in hlsl and 'clip(LocalSphere.x-dot(offset,offset));' in ps
assert 'clipPosition.x+LocalSphere.y*clipPosition.w,clipPosition.y-LocalSphere.y*clipPosition.w,clipPosition.w' in ps
assert 'const float sphere[4]={pointSlot().selected.attenuationEnd*pointSlot().selected.attenuationEnd,1.f/PointResolution,0,0};' in body
assert 'pointCheck(d->SetPixelShaderConstantF(1,sphere,1),"cube sphere")' in body
# c1 lifetime: the union (c0..c1) runs after each face's casters, so the sphere is rebound by
# bindFace (cache and scratch targets) before any LocalShadowPS draw of the next face, and no
# other write of PS c1 happens between a bindFace and the union.
bind=body[body.index('auto bindFace=[&]'):body.index('if(rebuildMask&(1u<<face)){if(!bindFace(')]
assert bind.index('SetPixelShader(pointSlot().shadowPS)')<bind.index('SetPixelShaderConstantF(1,sphere,1)')
loop=body[body.index('if(rebuildMask&(1u<<face)){if(!bindFace('):body.index('// Union: min(scratch, cached static face) into the cube face.')]
assert loop.count('bindFace(')==2 and 'SetPixelShaderConstantF(1' not in loop and 'SetPixelShaderConstantF(0,&unionConstants[0][0],2)' in body[body.index('// Union: min(scratch'):]
for draw in ('"cube static draw"','"cube terrain draw"','"cube live draw"','"cube animated draw"'):
    assert loop.index('bindFace(pointSlot().scratchSurface')<loop.index(draw) or draw=='"cube static draw"',draw
assert 'recordOutsideSphere(r.low,r.high,l,FaceContentSphereMargin)' in fp.src('point_light_shadow.h').read_text()
assert 'POINT static faces rebuilt=%u keptByContent=%u' in pt and 'if(NorthlightQuality::renderProfile(quality))' in pt
# 3b: default 6 draws all six; any light change or unusable cube draws all six and invalidates first.
assert 'const bool cycling=quality.pointShadowFacesPerFrame<6&&pointSlot().schedule.usable&&!lightChanged&&pointSlot().cacheValid&&pointSlot().cacheSource==pointSlot().selected.sourceId;' in body
assert 'pointSlot().faceCycle.reset();pointSlot().schedule.invalidate();}' in body and 'faces=pointSlot().faceCycle.mask();pointSlot().schedule.stale();}' in body
# Candidate lists are rebuilt from this frame's replays every call (indices never cross frames).
assert body.index('list.clear();')<body.index('pointAppendCandidates(pointReplayCandidates,i,mask,faces);')
# No progress and no commit without replays; the cycle commits as an update of its first frame.
i=body.index('if(!fresh){')
assert i<body.index('if(cycling&&!pointSlot().faceCycle.advance())')<body.index('pointSlot().schedule.commit(pointSlot().faceCycle.startedAt,pointSlot().selected,pointSlot().faceCycle.generation);')
assert 'if(!pointSlot().schedule.usable||lightChanged)return false;' in body
assert body.index('pointSlot().schedule.due(')<i<body.index('for(unsigned face=0;face<6;++face)')
with tempfile.TemporaryDirectory(prefix='northlight-point-schedule-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_point_shadow_schedule.cpp'),'-o',str(exe)],check=True)
        out=subprocess.check_output([str(exe)],text=True)
        assert 'point face cycles passed' in out and 'point schedule passed' in out,out
        print(label,out,end='',flush=True)
print('PASS point schedule, face cycles and per-face static content keys')
