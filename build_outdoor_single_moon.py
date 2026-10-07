"""Remove exact dedicated lunar batches from active outdoor HD skies.

Keep original models, all retained batches and all lighting/fog curves intact.
Includes clear and weather/underwater profile slots, never substring deletion.
"""
import argparse, hashlib, json, struct
from pathlib import Path
from mpq import Archive
from build_lighting import DBC, u, putu, require
from build_weather_textures import write_blp_bgra, mip_sizes
from build_outdoor_single_sun import texture_names, batch_names, sky_name
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent/'renderer'))
from world_scene_builder import Assets, decode_blp

MOON_TEXTURES = {
    'stormwind_moon.blp', 'orgri_moon01.blp', 'orgri_moonflare01.blp',
    'neksys_moon.blp', 'arathi_moon.blp', 'lazur_moon01.blp',
    'shadowmoon__blue_moon01.blp', 'frostwinddesert_moon_overlay01.blp',
    'shadowmoon_moonatmosphere01.blp',
}

def transparent_moon():
    # Valid uncompressed BLP2, complete 64x64 mip chain. Zero color also
    # suppresses additive draws. Only the dedicated native secondary moon.
    return write_blp_bgra(64,64,[bytes(w*h*4) for w,h in mip_sizes(64,64)])

def build(source,output,assets=None):
    """assets: an Assets of the client (default: the configured client). Raises
    build_lighting.Skip when no outdoor sky has an HD lunar batch."""
    with Archive(source) as a:original={n:a.read(n) for n in a.names() if not n.startswith('(')}
    payload=dict(original);lower={n.lower():v for n,v in original.items()};owned=assets is None;assets=assets or Assets()
    def read(name):return lower[name.lower()] if name.lower() in lower else assets.read(name)
    keys={n:'DBFilesClient\\'+n+'.dbc' for n in ('Light','LightParams','LightSkybox')}
    if not set(keys.values())<=set(original):
        if owned:assets.close()
        require(False,'lighting input lacks LightSkybox')
    light,params,sky=[DBC(original[keys[n]]) for n in keys];refs={}
    for row in light.rows:
        if u(row,1) not in (0,1,530,571):continue
        for col in range(7,15):
            pid=u(row,col)
            if pid not in params.index:continue
            sid=u(params.index[pid],2)
            if sid:refs.setdefault(sid,[]).append((u(row,0),u(row,1),col,pid))
    changes=[];audited=[]
    try:
        for sid in sorted(refs):
            name=sky_name(sky,sky.index[sid]);stem=name[:-4];model=read(stem+'.m2')
            textures,lookup=texture_names(model);count=struct.unpack_from('<I',model,68)[0]
            assert 1<=count<=4
            skins={};removed=[];retained=[];suspects=[]
            for skinid in range(count):
                before=read(stem+f'{skinid:02d}.skin');assert before[:4]==b'SKIN'
                n,off=struct.unpack_from('<II',before,36);assert off>=48 and off+n*24<=len(before)
                kept=[]
                for j in range(n):
                    raw=before[off+j*24:off+(j+1)*24];names=batch_names(raw,textures,lookup)
                    record=dict(skin=skinid,batch=j,textures=names)
                    if set(names)&MOON_TEXTURES:
                        assert set(names)<=MOON_TEXTURES,('Mixed lunar batch',name,names)
                        removed.append(record)
                    else:
                        kept.append(raw);retained.append(record)
                        if any('moon' in x or 'luna' in x for x in names):suspects.append(record)
                assert kept
                skin=bytearray(before);putu(skin,9,len(kept));skin[off:off+24*n]=b''.join(kept)+bytes(24*(n-len(kept)))
                assert skin[:36]==before[:36] and skin[40:off]==before[40:off] and skin[off+24*n:]==before[off+24*n:]
                assert skin[off:off+24*len(kept)]==b''.join(kept)
                skins[skinid]=bytes(skin)
            audited.append(dict(sky=sid,model=name,refs=refs[sid],retained_moon_named_batches=suspects))
            if not removed:continue
            custom=f'Environments\\Stars\\fr_single_moon_105_{sid}'
            assert custom.lower()+'.m2' not in lower
            payload[custom+'.m2']=model
            for k,skin in skins.items():payload[custom+f'{k:02d}.skin']=skin
            for asset in set(assets.providers)|set(lower):
                if asset.startswith(stem.lower()) and asset.endswith('.anim'):payload[custom+asset[len(stem):]]=read(asset)
            # Stable IDs preserve every lighting-band reference. The shared
            # sky entry changes only its model path, retaining every sky flag.
            row=sky.index[sid];old=bytes(row);putu(row,1,len(sky.strings));sky.strings+=(custom+'.mdx').encode()+b'\0'
            assert row[:4]==old[:4] and row[8:]==old[8:]
            changes.append(dict(sky=sid,original_model=name,clone=custom,refs=refs[sid],removed_batches=removed,retained_batches=retained))
        require(changes,'no outdoor sky has an HD lunar batch')
        payload[keys['LightSkybox']]=sky.bytes()
        assert {n for n in original if payload[n]!=original[n]}=={keys['LightSkybox']}
        native='Textures\\moon02.blp';payload[native]=transparent_moon()
        for size in (64,32,16,8,4,2,1):
            w,h,rgba=decode_blp(payload[native],size);assert w==h==size and not any(rgba)
    finally:
        if owned:assets.close()
    output.mkdir(parents=True,exist_ok=True)
    target=output/'patch-z.mpq';assert not target.exists()
    with Archive(target,create=True,capacity=len(payload)*2) as a:
        for i,(name,data) in enumerate(payload.items()):
            temp=output/f'payload-{i}.bin';temp.write_bytes(data);a.add(temp,name);temp.unlink()
    with Archive(target) as a:
        for name,data in payload.items():assert a.read(name)==data,name
        finalSky=DBC(a.read(keys['LightSkybox']))
        for change in changes:
            name=sky_name(finalSky,finalSky.index[change['sky']]);model=a.read(name[:-4]+'.m2');textures,lookup=texture_names(model)
            for k in range(struct.unpack_from('<I',model,68)[0]):
                skin=a.read(name[:-4]+f'{k:02d}.skin');n,off=struct.unpack_from('<II',skin,36)
                for j in range(n):assert not set(batch_names(skin[off+j*24:off+(j+1)*24],textures,lookup))&MOON_TEXTURES
    report=dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),archive_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),audited=audited,changes=changes,changed_existing_assets=[keys['LightSkybox']],added_assets=sorted(set(payload)-set(original)),lighting_and_retained_batches_identical=True,native_secondary_moon_transparent=True,game_launched=False,visual_verified=False)
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Audited',len(refs),'skies; changed',len(changes),'skies; removed',sum(len(c['removed_batches']) for c in changes),'lunar batches. Native secondary moon transparent. All payloads verified.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();build(args.source,args.output)
