#!/usr/bin/env python3
# northlight-test:
"""World-fixed probe atlas interpolation reference; CPU only, no game/GPU.

Run directly to write world-probe-coverage-validation.json. This checks world
key matching, negative modulo, spatial missing-probe fade and independence
from camera/request-grid origin. Residency scheduling is tested separately.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import hashlib
import itertools
import json
import math

HERE=Path(__file__).resolve().parent
N=16
SPACING=8
BITS=tuple(itertools.product((0,1),repeat=3))


def wrapped(key):
    return tuple(v-N*math.floor(v/N) for v in key)


def insert(atlas,key,valid=True):
    # Positive, spatially varying constant-radiance SH fixtures.
    radiance=tuple(.3+.001*key[i] for i in range(3))
    atlas[wrapped(key)]=(key,valid,radiance)


def sample(atlas,position,grid_origin=(0,0,0),mean=32):
    # Retained ABI value has no effect on the world's probe coordinates.
    del grid_origin
    grid=tuple(v/SPACING for v in position)
    base=tuple(math.floor(v) for v in grid)
    fraction=tuple(v-b for v,b in zip(grid,base))
    total=coverage=0.0
    irradiance=[0.0]*3
    for bit in BITS:
        key=tuple(v+b for v,b in zip(base,bit))
        record=atlas.get(wrapped(key))
        if record is None or record[0]!=key or not record[1]:
            continue
        weight=math.prod(f if b else 1-f for f,b in zip(fraction,bit))
        coverage+=weight
        # Spatial visibility may change normalized interpolation weights, but
        # it must not act as the residency/ambient replacement coverage mask.
        distance=math.sqrt(sum((p-k*SPACING)**2 for p,k in zip(position,key)))
        variance=.02
        excess=max(distance-mean,0)
        visibility=variance/(variance+excess*excess)
        weight*=visibility*visibility
        for channel in range(3):
            irradiance[channel]+=math.pi*record[2][channel]*weight
        total+=weight
    if total<=.00001:
        return (0.,0.,0.),0.
    return tuple(v/total for v in irradiance),min(1.,max(0.,coverage))


def main():
    shader=fp.src('world_effects.hlsl')
    source=shader.read_text()
    assert 'sampler2D ProbeMetadata : register(s10)' in source
    assert 'float3 grid=p/GridOrigin.w;' in source
    assert 'cell-GridInfo.x*floor(cell/GridInfo.x)' in source
    assert 'if(all(metadata.xyz==cell)&&metadata.w>=0){' in source
    body=source.split('float4 probeIrradiance(',1)[1].split('float4 WorldLighting(',1)[0]
    assert 'GridOrigin.xyz' not in body and 'float3 edge=' not in body

    # 0.3.197: same-key re-publication blends on the GPU from the previous SH (task 13); born's fade stays as is.
    assert 'sampler2D ProbePrevious : register(s8)' in source
    assert 'float blend=saturate((PassInfo.w-moment.w)*(1/.3));' in body and '[branch]if(blend<1)' in body
    assert 'weight*=saturate((PassInfo.w-metadata.w)*(1/.45));' in body
    blend_header=fp.src('probe_blend.h').read_text()
    assert 'BlendSeconds=.3f' in blend_header and '(1/.3)' in body
    renderer=fp.src('world_renderer.h').read_text()
    gi_pass=renderer.index('"world GI pass"')
    assert renderer.index('SetTexture(8,probePrev)')<gi_pass<renderer.index('SetTexture(8,textures[8])')
    assert renderer[gi_pass:].split('SetTexture(8,textures[8])',1)[0].count('\n')<=2  # restored right after the GI quad

    atlas={}
    for key in itertools.product(range(-20,-12),range(-4,4),range(-2,6)):
        insert(atlas,key)
    position=(-128.8,-.8,15.2)
    reference=sample(atlas,position)
    assert reference[1]>.999999
    camera_cases=0
    for origin in itertools.product(range(-192,-64,8),range(-48,48,8),range(-24,48,8)):
        assert sample(atlas,position,origin)==reference
        camera_cases+=1

    # Only x=0 probes exist. Coverage is exactly 1-x/spacing, continuously
    # fading to legacy ambient through the missing neighbor's world cell.
    incomplete={}
    for y,z in itertools.product((0,1),repeat=2):insert(incomplete,(0,y,z))
    fade=[]
    for i in range(33):
        x=SPACING*i/32
        _,coverage=sample(incomplete,(x,.8,1.6))
        assert abs(coverage-(1-i/32))<1e-12
        fade.append(coverage)
    assert all(a>=b for a,b in zip(fade,fade[1:]))
    occluded={}
    for key in BITS:insert(occluded,key)
    assert sample(occluded,(4,4,4),mean=6)[1]==1
    assert sample(occluded,(4,4,4),mean=32)[1]==1

    wrap_cases=0
    for key in itertools.product((-33,-32,-17,-16,-1,0,15,16,31,32),repeat=3):
        slot=wrapped(key)
        assert all(0<=v<N for v in slot)
        assert slot==tuple(v%N for v in key)
        wrap_cases+=1
    # Metadata prevents an aliased slot from representing another world point.
    alias={}
    insert(alias,(16,0,0))
    assert sample(alias,(0,0,0))==((0.,0.,0.),0.)
    insert(alias,(0,0,0),False)
    assert sample(alias,(0,0,0))==((0.,0.,0.),0.)
    insert(alias,(0,0,0),True)
    assert sample(alias,(0,0,0))[1]==1

    # Crossing negative world-cell and atlas-wrap boundaries has no coverage
    # seam when both adjacent cells' probes are resident.
    boundary_cases=0
    max_jump=0.
    for boundary in (-128.,-8.,0.,128.):
        nearby={}
        center=int(boundary/SPACING)
        for key in itertools.product(range(center-1,center+2),(0,1),(0,1)):
            insert(nearby,key)
        a,ca=sample(nearby,(boundary-1e-6,.8,1.6))
        b,cb=sample(nearby,(boundary+1e-6,.8,1.6))
        assert abs(ca-1)<1e-12 and abs(cb-1)<1e-12
        jump=max(abs(x-y) for x,y in zip(a,b));max_jump=max(max_jump,jump)
        assert jump<1e-6
        boundary_cases+=1

    report={'result':'pass','game_launched':False,'gpu_test':False,
            'shader_sha256':hashlib.sha256(shader.read_bytes()).hexdigest(),
            'test_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'atlas_dimension':N,'spacing':SPACING,'camera_grid_origins_tested':camera_cases,
            'missing_neighbor_fade_samples':fade,'negative_wrap_cases':wrap_cases,
            'coverage_independent_of_moment_visibility':'pass',
            'world_boundaries_tested':boundary_cases,'maximum_boundary_irradiance_jump':max_jump,
            'stale_slot_and_invalid_probe_rejection':'pass',
            'limitations':['CPU interpolation reference, not GPU execution.',
                           'Camera invariance assumes resident metadata and probe values remain unchanged.',
                           'No claim about directional shadows or runtime residency coverage.']}
    output=fp.output_dir()/'world-probe-coverage-validation.json'
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(f'PASS: {camera_cases} camera origins, {wrap_cases} wrap cases; {output}')


if __name__=='__main__':main()
