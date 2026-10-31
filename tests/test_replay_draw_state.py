#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Replay draw-state binding: shader/pass preconditions of the opaque-material skip, then the
native equivalence test (0.3.142 cache vs current, software R32F+D24 raster) and the existing
render-state cache test, at -O2 and under ASan/UBSan. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
from shader_compiler import instructions
import re
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
read = lambda name: fp.tracked(name).read_text()
world, point, proxy, probe = read('world_renderer.h'), read('world_point_rendering.inl'), read('renderer.cpp'), read('world_replay_probe.inl')

# The proof reads the shaders: ShadowReplayPS kills only on tex.a-c0.w<0 (x only) and writes the
# light depth; LocalShadowPS samples only when c0.w>=0. Any change must revisit replay_draw_state.h.
asm = lambda name: instructions(fp.tracked(name))
# Microsoft's disassembly: texkill r0 tests all four lanes, each the same tex.a-c0.w (FOrdLessThan: NaN survives).
assert asm('ShadowReplayPS.bin.asm') == ['ps_3_0', 'dcl_texcoord v0.xy', 'dcl_texcoord7 v1.z', 'dcl_2d s0',
    'texld r0, v0, s0', 'add r0, r0.w, -c0.w', 'texkill r0', 'mov oC0, v1.z'], 'ShadowReplayPS changed'
local = asm('LocalShadowPS.bin.asm')
# 0.3.151: a range clip (texkill on c1.x - |offset|^2, c1 set once per face in bindFace) precedes the
# alpha block. It reads only c1 and v1 (never the stage-0 texture or c0), so the skip proof is unchanged.
# The alpha block is a real branch on c0.w >= 0 ([branch]; c2.x is the literal 0).
gate = local.index('if_ge r0.w, c2.x')
assert local[gate-1:gate+5] == ['mov r0.w, c0.w', 'if_ge r0.w, c2.x', 'texld r0, v0, s0', 'add r0, r0.w, -c0.w', 'texkill r0', 'endif'] and \
    'def c2, 0, 0, 0, 0' in local and \
    [l for l in local if l.startswith('texld') or l.startswith('texkill')] == ['texkill r0', 'texld r0, v0, s0', 'texkill r0'] and \
    local.index('texkill r0') < gate - 1, 'LocalShadowPS changed'
assert not any(t in l for l in local[5:gate-1] for t in ('c0', 's0', 'v0')) and 'dp3 r0.x, r1, r1' in local[5:gate-1], \
    'LocalShadowPS range clip must read only c1 and v1'
assert 'clip(tex2D(Scene,uv).a-Material.w);return lightClip.zzzz;' in read('world_effects.hlsl')
assert '[branch]if(LocalMaterial.w>=0)clip(tex2D(LocalAlpha,uv).a-LocalMaterial.w);' in read('local_light_effects.hlsl')
# Captured cutoffs are -1 (opaque) or >=0 (alpha test); the skip condition is cutoff<=-1.
assert "p->cutoff=alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:-1.f;" in world
assert 'if(skip_&&material_&&p.cutoff<=-1.f)' in read('replay_draw_state.h')
# Exactly four users (0.3.209: the actor shadow map is a fourth), each with a fixed pixel shader and no other stage-0/PS change inside the loop:
# the directional cascades, the point cube and (0.3.149, diagnostic) DiagReplayProbe, which re-issues
# the sun near pass with the cascade's replayPS still bound.
users = [m.start() for f in (world, point, probe) for m in re.finditer(r'NorthlightReplayDrawState::Cache ', f)]
assert len(users) == 4, users  # 0.3.209 (task 29): the actor shadow map is the fourth (replayPS bound, a fresh Cache per map)
issue = probe[probe.index('NorthlightReplayDrawState::Cache probeBindings(d);'):probe.index('        if(draws==replayProbeList.size())return true;')]
assert 'submitReplay(p,probeBindings,probePoses,rows,mode,' in issue
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in issue, forbidden
    assert forbidden not in probe, forbidden
assert world.index('d->SetPixelShader(replayPS);') < world.index('replayProbe(rows,realLoopMs);') < world.index('d->SetPixelShader(unionPS);')
loop = world[world.index('d->SetPixelShader(replayPS);')+len('d->SetPixelShader(replayPS);'):world.index('replayPosePrepared+=poseConstants.prepared;')]
assert 'NorthlightReplayDrawState::Cache replayBindings(d);' in loop
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in loop, forbidden
cube = point[point.index('NorthlightReplayDrawState::Cache replayBindings(d);'):point.index('// Union: min(scratch, cached static face)')]
for forbidden in ('SetTexture(', 'SetSamplerState(', 'SetPixelShader(', 'SetPixelShaderConstantF(', 'SetRenderState('):
    assert forbidden not in cube, forbidden
assert 'pointCheck(d->SetPixelShader(pointSlot().shadowPS),"cube static PS")' in point
# Directional pass: LESSEQUAL depth, no blend/alpha test/stencil, all colour channels; every texture unbound on entry.
for state in ('{D3DRS_ZFUNC,D3DCMP_LESSEQUAL}', '{D3DRS_ALPHATESTENABLE,FALSE}', '{D3DRS_ALPHABLENDENABLE,FALSE}', '{D3DRS_STENCILENABLE,FALSE}', '{D3DRS_COLORWRITEENABLE,15}'):
    assert state in world, state
assert 'for(int i=0;i<14;++i)d->SetTexture(i,nullptr);' in world
# The float-alpha guard sees every game texture creation.
for method in ('CreateTexture', 'CreateVolumeTexture', 'CreateCubeTexture'):
    line = next(l for l in proxy.splitlines() if f'HRESULT STDMETHODCALLTYPE {method}(' in l and 'ext->' in l)
    assert 'NorthlightReplayDrawState::noteTextureFormat(Format);' in line, method
print('PASS preconditions: replay/local shader kill forms, cutoff encoding, three fixed-PS loops (cascade, cube, probe), pass states, float-alpha guard wiring')

with tempfile.TemporaryDirectory() as tmp:
    for source in ('test_replay_draw_state.cpp', 'test_render_state_caches.cpp'):
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            exe = Path(tmp) / 'test'
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(), str(HERE / source), '-o', str(exe)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
            print(f'{source} {" ".join(flags[:1])}{" asan+ubsan" if len(flags) > 1 else ""}:\n' + out.strip())
