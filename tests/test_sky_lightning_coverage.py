#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Every active sky flash batch, its actual masks and native material timeline."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from emission_catalog import FILE, SKY_FLASH, build
from world_scene_builder import Assets,array,unpack,string,blp_mips,decode_blp
from extract_signatures import variants
from test_emission_sources import sky_frames

out=fp.output_dir()/'all-sky-flashes';out.mkdir(parents=True,exist_ok=True)
assets=Assets(without='z')
try:
    catalog=build(assets,out)
    records=[];metadata=[]
    for path in sorted(n for n in assets.providers if n.startswith('environments\\stars\\') and n.endswith('.m2')):
        blob=assets.read(path)
        if len(blob)<304 or blob[:4]!=b'MD20':continue
        rows=lambda b,o,f:array(b,*reversed(unpack('<II',b,o)),f)
        textures=rows(blob,80,'<4I');names=[string(blob,t[3]) if t[0]==0 and t[2] else '' for t in textures]
        if not any(SKY_FLASH.search(n) for n in names):continue
        skin=assets.read(path[:-3]+'00.skin');combos=rows(blob,128,'<H');materials=rows(blob,112,'<2H')
        vertices=rows(blob,60,'<3f4B4B3f4f');lookup=rows(skin,4,'<H');indices=rows(skin,12,'<H');subsets=rows(skin,28,'<10H7f')
        for index,batch in enumerate(rows(skin,36,'<2B11H')):
            used=[names[combos[i][0]] for i in range(batch[9],batch[9]+batch[8])]
            if not any(SKY_FLASH.search(n) for n in used):continue
            assert batch[8] in (1,2) and materials[batch[6]][0]&1,(path,index)
            for n in used:
                if SKY_FLASH.search(n):assert catalog['textures'][n]['kind']&4,(path,index,n)
            payload=bytearray(struct.pack('<I',len(used)))
            for name in used:
                b=assets.read(name);_,w,h,at,size=next(m for m in blp_mips(b) if max(m[1:3])<=32)
                encoding,depth,alpha,_=unpack('<4B',b,8)
                fmt=21 if encoding in (1,3) else 0x31545844 if depth in (0,1) else 0x33545844 if alpha==1 else 0x35545844
                if encoding==1:
                    _,_,rgba=decode_blp(b,32);raw=bytes(v for i in range(0,len(rgba),4) for v in (rgba[i+2],rgba[i+1],rgba[i],rgba[i+3]))
                else:raw=b[at:at+size]
                pitch=w*4 if fmt==21 else ((w+3)//4)*(8 if fmt==0x31545844 else 16)
                payload+=struct.pack('<5I',w,h,fmt,pitch,len(raw))+raw
            sub=subsets[batch[3]];first=sub[4]+(sub[1]<<16);selected=indices[first:first+sub[5]]
            payload+=struct.pack('<I',len(selected))
            for i, in selected:
                v=vertices[lookup[i][0]];payload+=struct.pack('<3fI4f',*v[:3],0xffffffff,*v[14:18])
            period,frames=sky_frames(blob,batch)
            payload+=struct.pack('<2I',period,len(frames))
            for frame in frames:payload+=struct.pack('<I4f',*frame)
            records.append(payload);metadata.append({'model':path,'batch':index,'textures':used,'period_ms':period,'timeline_samples':len(frames),'blend':materials[batch[6]][1]})
    data=bytearray()
    for name in ('Diffuse_T1','Diffuse_T1_T2'):
        code=list(variants(assets.read('shaders\\vertex\\vs_3_0\\'+name+'.bls')))[0][2];data+=struct.pack('<I',len(code)//4)+code
    for name in ('Combiners_Mod','Combiners_Opaque','Combiners_Mod_Mod','Combiners_Opaque_Mod'):
        code=list(variants(assets.read('shaders\\pixel\\ps_3_0\\'+name+'.bls')))[0][2];data+=struct.pack('<I',len(code)//4)+code
    data+=struct.pack('<I',len(records))+b''.join(records)
    fixture=out/'all-sky.bin';fixture.write_bytes(data)
    (out/'cases.json').write_text(json.dumps(metadata,indent=2)+'\n')
finally:assets.close()
results=[]
for mode in ('O0','O2'):
    exe=out/f'all-sky-{mode}.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17','-Wall','-Wextra','-Werror','-Wno-microsoft-exception-spec','-Wno-misleading-indentation','-Wno-return-type',*fp.test_include_flags(),str(fp.TESTS/'sky_lightning_coverage.cpp'),'-o',str(exe)],check=True,env=fp.zig_env())
    r=subprocess.run([str(exe),str(fixture),str(out/FILE)],capture_output=True,text=True)
    (out/f'{mode}.log').write_text(r.stdout+r.stderr)
    assert r.returncode==0,r.stdout+r.stderr
    print(r.stdout.splitlines()[-1],flush=True);results.append({'build':mode,'passed':True})
report={'models':len({m['model'] for m in metadata}),'batches':len(metadata),'timeline_samples':sum(m['timeline_samples'] for m in metadata),'masked_batches':sum(len(m['textures'])==2 for m in metadata),'fixture_sha256':hashlib.sha256(data).hexdigest(),'runs':results,'game_launched':False}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
