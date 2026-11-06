#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Current winning MPQs -> upload identity -> actual bolt draw -> transient light."""
from pathlib import Path
import json
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from emission_catalog import FILE, build, mip_keys
from world_scene_builder import Assets, array, blp_mips, string, unpack
from extract_signatures import variants
from build_glue_emitters import native_track
import bisect


def sky_frames(blob, batch, ticks=None):
    """Sample actual material keys, ramp interiors, flash edges and loop wrap."""
    constant=lambda value:(0,-1,(((0,),b'',(tuple(value),)),))
    color = unpack('<I', blob, 76)[0]+batch[5]*40
    tracks = [native_track(blob, color, '<3f'), native_track(blob, color+20, '<h')] if batch[5]!=65535 else [constant((1,1,1)),constant((32767,))]
    weight = array(blob, *reversed(unpack('<II', blob, 144)), '<h')[batch[11]][0] if batch[11]!=65535 else -1
    tracks.append(native_track(blob, unpack('<I', blob, 92)[0]+weight*20, '<h') if weight>=0 else constant((32767,)))
    tracks=[t if t[2] and t[2][0][0] else constant((1,1,1) if i==0 else (32767,)) for i,t in enumerate(tracks)]
    globals_ = [v for v, in array(blob, *reversed(unpack('<II', blob, 20)), '<I')]
    period = max((globals_[clock] for _,clock,_ in tracks if clock >= 0),default=1000)
    times = {0, period-1, period, period+1, period*2}
    for _,_,curves in tracks:
        for keys,_,_ in curves:
            times.update(t+d for t in keys for d in (-1,0,1) if 0 <= t+d <= period)
            times.update((a+b)//2 for a,b in zip(keys,keys[1:]))
    def sample(track, tick):
        mode,clock,curves = track
        keys,_,values = curves[0]
        tick = tick%globals_[clock] if clock >= 0 else tick
        if tick <= keys[0]: return values[0]
        if tick >= keys[-1]: return values[-1]
        index = bisect.bisect_right(keys,tick)-1
        amount = (tick-keys[index])/(keys[index+1]-keys[index]) if mode else 0
        return tuple(a+(b-a)*amount for a,b in zip(values[index],values[index+1]))
    frames=[]
    for tick in sorted(times if ticks is None else ticks):
        rgb = sample(tracks[0],tick)
        alpha = sample(tracks[1],tick)[0]/32767*sample(tracks[2],tick)[0]/32767
        frames.append((tick,*rgb,alpha))
    return period,frames

def main():
    out=fp.output_dir()/'emission-sources';out.mkdir(parents=True,exist_ok=True)
    assets=Assets(without='z')
    try:
        report=build(assets,out)
        assert len(report['models'])==sum(n.endswith('.m2') for n in assets.providers)
        data=bytearray()
        for name,index in ((r'shaders\vertex\vs_3_0\CDiffuse_T1.bls',0),(r'shaders\vertex\vs_3_0\Diffuse_T1.bls',0),(r'shaders\pixel\ps_3_0\Combiners_Mod.bls',0)):
            code=list(variants(assets.read(name)))[index][2];data+=struct.pack('<I',len(code)//4)+code
        data+=struct.pack('<I',4)
        for path in (r'world\expansion01\doodads\netherstorm\lightning\netherstormlightning01.m2',r'world\expansion02\doodads\stormpeaks\lightning\stormpeakslightning01.m2',r'environments\stars\icecrownsky.m2',r'environments\stars\netherstormskybox.m2'):
            b=assets.read(path);skin=assets.read(path[:-3]+'00.skin')
            rows=lambda blob,offset,fmt:array(blob,*reversed(unpack('<II',blob,offset)),fmt)
            textures=rows(b,80,'<4I');combos=rows(b,128,'<H')
            batch=next(u for u in rows(skin,36,'<2B11H') if u[8]==1 and 'lightning' in string(b,textures[combos[u[9]][0]][3]))
            tex=textures[combos[batch[9]][0]];name=string(b,tex[3]);assert report['textures'][name]['kind']&(4 if path.startswith('environments') else 2)
            blob=assets.read(name);_,w,h,offset,size=next(m for m in blp_mips(blob) if max(m[1:3])<=32)
            key=next(k for k in mip_keys(blob) if k[1:3]==(w,h));fmt=key[3];pitch=((w+3)//4)*(8 if fmt==0x31545844 else 16) if fmt!=21 else w*4
            data+=struct.pack('<5I',w,h,fmt,pitch,size)+blob[offset:offset+size]
            vertices=rows(b,60,'<3f4B4B3f4f');lookup=rows(skin,4,'<H');indices=rows(skin,12,'<H')
            subset=rows(skin,28,'<10H7f')[batch[3]];first=subset[4]+(subset[1]<<16);indices=indices[first:first+subset[5]]
            data+=struct.pack('<I',len(indices))
            for i, in indices:
                v=vertices[lookup[i][0]];data+=struct.pack('<3fI2f',*v[:3],0xffffffff,*v[14:16])
            period,frames=sky_frames(b,batch) if path.startswith('environments') else (0,[])
            data+=struct.pack('<2I',period,len(frames))
            for frame in frames:data+=struct.pack('<I4f',*frame)
        fixture=out/'native.bin';fixture.write_bytes(data)
    finally:assets.close()
    for mode in ('O0','O2'):
        exe=out/f'emission-{mode}.exe'
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','-Wno-microsoft-exception-spec','-Wno-misleading-indentation','-Wno-return-type',*fp.test_include_flags(),str(fp.TESTS/'emission_sources.cpp'),'-o',str(exe)],check=True,env=fp.zig_env())
        subprocess.run([str(exe),str(fixture),str(out/FILE)],check=True)
    print('PASS full active-model catalog:',json.dumps(report['counts']))


if __name__ == '__main__':
    main()
