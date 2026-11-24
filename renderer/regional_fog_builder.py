#!/usr/bin/env python3
"""Extract authored regional metadata only; never starts game or Wine.
FRF1: <4s5I magic/version/tileX/tileY/waterCount/indoorCount,256*u32 zone,
water <3fQ cornerWorldX/Y,maxHeight,8x8presence; indoor <6f worldAABB.
Version 2 appends 256 uint32 subzone IDs after the indoor records.
Only MOGP indoor groups contribute exclusion bounds, never root WMO bounds.
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,re,struct,sys,time
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]  # repo root, renderer/
from world_scene_builder import Assets,CLIENT,chunks,wmo_group,unpack,array,placement,matvec
import client_archives
from world_light_placements import names
import northlight_paths
HERE=Path(__file__).resolve().parent
FOREST_REGIONS={int(i):name for i,name in re.findall(r'^FOREST_REGION\((\d+), "([^"]+)"\)$',northlight_paths.src('forest_regions.inc').read_text(),re.M)}
SUPPORTED_REGIONS={10:'Duskwood',33:'Stranglethorn Vale',215:'Mulgore',**FOREST_REGIONS}

def dbc(blob):
    magic,n,fields,size,strings=unpack('<4s4I',blob)
    if magic!=b'WDBC' or size!=fields*4 or len(blob)!=20+n*size+strings:raise ValueError('DBC layout')
    return [unpack('<'+'I'*fields,blob,20+i*size) for i in range(n)],blob[20+n*size:]

def root_zone(areas,area):
    seen=set()
    while area in areas and areas[area][2]:
        if area in seen:return 0
        seen.add(area);area=areas[area][2]
    return area if area in areas else 0

def water_records(root,chunk_positions,liquid_types,stats):
    b=root.get('MH2O',b'');out=[]
    if not b:return out
    if len(b)<256*12:raise ValueError('MH2O header')
    for index in range(256):
        offset,count,_=unpack('<3I',b,index*12)
        if count>16:raise ValueError('MH2O layer count')
        for layer in range(count):
            liquid,fmt,lo,hi,x,y,w,h,mask_offset,vertex=unpack('<2H2f4B2I',b,offset+layer*24)
            if liquid_types.get(liquid) not in (1,3):stats['non_water_liquid_layers']+=1;continue
            if fmt>2 or x+w>8 or y+h>8 or not w or not h or not all(math.isfinite(z) for z in (lo,hi)):
                stats['unsupported_water_layers']+=1;continue
            size=(w*h+7)//8
            if mask_offset and mask_offset+size>len(b):raise ValueError('MH2O presence mask')
            bits=int.from_bytes(b[mask_offset:mask_offset+size],'little') if mask_offset else (1<<(w*h))-1
            mask=0
            for row in range(h):
                for col in range(w):
                    if bits&(1<<(row*w+col)):mask|=1<<((row+y)*8+col+x)
            if mask and index in chunk_positions:out.append((*chunk_positions[index][:2],max(lo,hi),mask))
    return out

def indoor_groups(read,path,stats,clamped):
    """World-local bounds of the interior groups of a root WMO; needs only each MOGP header. Interior is the
    game's own test (CMap::CreateMapObjDefGroups): neither flag 0x8 (outdoor) nor 0x40 (exterior lit)."""
    root=dict(chunks(read(path)));count=unpack('<I',root['MOHD'],4)[0];out=[]
    if count>4096:raise ValueError('WMO group count')
    for i in range(count):
        group=path[:-4]+f'_{i:03d}.wmo'
        try:top,overrun=wmo_group(read(group));g=top['MOGP']
        except (FileNotFoundError,KeyError):stats['missing_wmo_groups']+=1;continue
        if overrun:clamped.add(group)
        flags=unpack('<I',g,8)[0]
        if flags&0x48:continue
        bounds=unpack('<6f',g,12)
        if not all(math.isfinite(v) for v in bounds) or any(bounds[j]>bounds[j+3] for j in range(3)):raise ValueError('WMO bounds')
        out.append(bounds);stats['unique_indoor_groups']+=1
    return out

def wmo_indoors(memo,read,path,stats,clamped,unreadable):
    """indoor_groups(path), read once per root. A WMO that cannot be read adds no bounds and is recorded in
    `unreadable` instead of rejecting the tile: fog may then draw inside that one building."""
    if path not in memo:
        stats['wmo_roots']+=1
        try:memo[path]=indoor_groups(read,path,stats,clamped)
        except (ValueError,FileNotFoundError,KeyError,IndexError,struct.error) as exc:memo[path]=exc
    if isinstance(memo[path],Exception):unreadable[path]=str(memo[path]);return []
    return memo[path]

def placed_indoors(root,groups):
    """World AABBs of the indoor groups of an ADT's MODF placements; groups(path) gives world-local bounds."""
    wmos=names(root,'MWID','MWMO');indoors=[]
    for e in array(root.get('MODF',b''),0,len(root.get('MODF',b''))//64,'<2I12f4H'):
        path=wmos[e[0]];matrix,translation=placement(e[2:5],e[5:8],1.)
        for lowhigh in groups(path):
            points=[]
            for mask in range(8):
                p=tuple(lowhigh[j+3] if mask&(1<<j) else lowhigh[j] for j in range(3));q=matvec(matrix,p);points.append(tuple(q[j]+translation[j] for j in range(3)))
            indoors.append(tuple(min(p[j] for p in points) for j in range(3))+tuple(max(p[j] for p in points) for j in range(3)))
    return indoors

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache',type=Path,help='world cache whose FGS3 tiles select the ADTs (default: <client>/world-cache)')
    parser.add_argument('--output',type=Path,help='default: <cache>/fog')
    client_archives.add_arguments(parser);args=parser.parse_args()
    started=time.time();assets=Assets.from_args(args);cache=args.cache or assets.client/'world-cache';args.output=args.output or cache/'fog';stats=collections.Counter();records,strings=dbc(assets.read('DBFilesClient\\AreaTable.dbc'));areas={r[0]:r for r in records}
    liquid,_=dbc(assets.read('DBFilesClient\\LiquidType.dbc'));types={r[0]:r[14] for r in liquid}   # materialID: the game's visual class (1 water, 2 magma, 3 procedural water)
    # The catalog is proven by id and parent (a top-level zone); the name is in the client's own locale
    # column (11 enUS/enGB, 14 deDE, ...), so it is only compared where the enUS column is filled.
    def area_name(row):
        texts=[strings[o:].split(b'\0',1)[0].decode('utf-8','replace') for o in row[11:27] if 0<o<len(strings)]
        return next((t for t in texts if t),'')
    proof={str(i):{'parent':areas[i][2],'name':area_name(areas[i])} for i in (*SUPPORTED_REGIONS,440) if i in areas}
    for i,name in SUPPORTED_REGIONS.items():
        english=strings[areas[i][11]:].split(b'\0',1)[0].decode('utf-8','replace') if i in areas else ''
        if i not in areas or areas[i][2]!=0 or not proof[str(i)]['name'] or (english and english!=name):
            raise ValueError(f'Forest catalog/AreaTable mismatch: {i} {name}')
    memo={};clamped=set()   # every root WMO's indoor bounds or its read error, once per run
    destination=args.output;destination.mkdir(parents=True,exist_ok=True)
    manifest={'format':'FRF1','zones':proof,'maps':{},'unsupported':{},'unreadable_wmos':{},'sources':['ADT MCNK.areaid','AreaTable.dbc parent','MH2O liquid presence (LiquidType.dbc materialID)','MOGP interior (no flag 0x8/0x40) + group bounds','MODF placement'],
              'limitations':['Indoor group bounds conservatively exclude their footprint; this is not exact portal containment.','No dynamic weather source is inferred.','Unknown maps/zones have zero added fog.','Water max authored height is used for near-water influence; no liquid wave simulation.']}
    for mapname in ('Azeroth','Kalimdor','Expansion01','Northrend'):
        outdir=destination/mapname;outdir.mkdir(exist_ok=True);files=sorted((cache/mapname).glob('*.fg3'));done=0;digest=hashlib.sha256()
        for tile in files:
            tx,ty=map(int,tile.stem.split('_'));name=f'world\\maps\\{mapname}\\{mapname}_{tx}_{ty}.adt'
            try:
                parts=list(chunks(assets.read(name)));root=dict(parts);zones=[0]*256;subzones=[0]*256;positions={}
                for tag,b in parts:
                    if tag!='MCNK':continue
                    ix,iy=unpack('<2I',b,4)
                    if ix>=16 or iy>=16:raise ValueError('MCNK indices')
                    index=iy*16+ix;area=unpack('<I',b,52)[0]
                    zones[index]=root_zone(areas,area)
                    if zones[index] and zones[index]!=area:subzones[index]=area
                    positions[index]=unpack('<3f',b,104)
                waters=water_records(root,positions,types,stats);indoors=[]
                # Non-target zones never add fog: no costly WMO reads needed.
                if any(zone in SUPPORTED_REGIONS for zone in zones):
                    indoors=placed_indoors(root,lambda path:wmo_indoors(memo,assets.read,path,stats,clamped,manifest['unreadable_wmos']))
                blob=struct.pack('<4s5I',b'FRF1',2 if any(subzones) else 1,tx,ty,len(waters),len(indoors))+struct.pack('<256I',*zones)
                blob+=b''.join(struct.pack('<3fQ',*v) for v in waters)+b''.join(struct.pack('<6f',*v) for v in indoors)
                if any(subzones):blob+=struct.pack('<256I',*subzones)
                (outdir/(tile.stem+'.frf')).write_bytes(blob);digest.update(tile.stem.encode()+blob);done+=1;stats['water_layers']+=len(waters);stats['placed_indoor_groups']+=len(indoors)
            except (ValueError,FileNotFoundError,IndexError,KeyError,struct.error) as exc:
                (outdir/(tile.stem+'.frf')).unlink(missing_ok=True)
                manifest['unsupported'][name]=str(exc)
            if done and done%100==0:print(mapname,done,'/',len(files),flush=True)
        manifest['maps'][mapname]={'tiles':done,'expected':len(files),'aggregate_sha256':digest.hexdigest()}
    manifest['clamped_wmo_groups']=sorted(clamped);manifest['archive_fingerprint']=assets.fingerprint();assets.close();manifest['stats']=dict(stats);manifest['seconds']=round(time.time()-started,3)
    manifest['builder_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    manifest['forest_catalog_sha256']=hashlib.sha256(northlight_paths.src('forest_regions.inc').read_bytes()).hexdigest()
    (destination/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2),flush=True)
if __name__=='__main__':main()
