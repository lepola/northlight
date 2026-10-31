#!/usr/bin/env python3
"""Build camera-independent static lighting geometry from the active MPQs.

No game or Wine execution. FGS2 contains terrain, render meshes of placed WMO/M2
assets and alpha textures. Models are in bind pose; animated entities, liquids,
particle effects and additive/translucent surfaces are excluded and reported.
"""
from __future__ import annotations
import argparse, collections, functools, hashlib, json, math, os, re, struct, sys, time
from pathlib import Path

HERE=Path(__file__).resolve().parent
sys.path[:0]=[str(HERE.parent),str(HERE)]   # repo root and renderer/: also under python -I
import northlight_paths
CLIENT=northlight_paths.client_root(required=False) or HERE.parent.parent
from mpq import Archive
import client_archives
from m2_visibility import hidden_batches
import instance_maps
ZERO=17066.666666666668
# Windows: a virus scanner or the indexer briefly holds a just-written file; replace() is retried.
LOCK_RETRIES=(.2,.4,.8,1.6,3.) if os.name=='nt' else ()
CONTINENTS=['Azeroth','Kalimdor','Expansion01','Northrend']
UNIT=533.3333333333334/128
# Alpha cutoff of a cutout (blend 1) material in this occluder set. The set feeds shadows, and the game's
# own shadow-map pass tests the full-resolution texture against 128/255 (0.502, CM2Model::
# RenderModelBatchesShadowMap SetAlphaRef(0.50196081)), not against its visible-surface key 224/255 (0.878,
# CGxDevice::s_alphaRef). NL tests the BLP mip of up to 128 px (decode_blp), whose box-filtered alpha has
# softer edges. Measured on 25 stock-client foliage BLPs (leaves, ferns, vines, branches; 128..512 px, cutout
# alpha): the share of texels passing the cutoff on the mip, against the share passing the game's cutoff at
# full resolution, differs on average by 0.006 at 0.5 against the shadow-map 128 (signed -0.002), and by
# 0.039 at 0.5 but 0.013 at 0.75 against the visible 224. A raw 0.878 on the mip erases thin foliage (0.064
# short of the shadow pass, 0.027 of the visible one). 0.5 is kept: it reproduces the shadow casters.
ALPHA_KEY_CUTOFF=.5
CASTER_MIN_ALPHA=.55   # GatherModelBatchesShadowMap: opacity below this casts nothing

class DeletedAsset(ValueError):
    pass

class EmptyTile(ValueError):
    """A tile with no terrain triangles and no placed object, and nothing that failed to load: empty by design."""


def retry(fn,*args):
    """fn(*args), retried on PermissionError after each LOCK_RETRIES delay (6 attempts on Windows)."""
    for delay in LOCK_RETRIES:
        try:return fn(*args)
        except PermissionError:time.sleep(delay)
    return fn(*args)


def open_archive(path):
    """Archive(path); on Windows retried after each LOCK_RETRIES delay, as a virus scanner may briefly hold it."""
    for delay in LOCK_RETRIES:
        try:return Archive(path)
        except OSError:time.sleep(delay)
    return Archive(path)


def unpack(fmt,b,offset=0):
    if offset<0 or offset+struct.calcsize(fmt)>len(b):raise ValueError('Truncated binary structure')
    return struct.unpack_from(fmt,b,offset)

def chunks(b,start=0,clamp=None):
    """(tag, payload) pairs. Only a chunk tagged `clamp` may declare an end past the file: its payload is
    the rest of the file and iteration ends. Any other overrun raises."""
    offset=start
    while offset+8<=len(b):
        tag,size=unpack('<4sI',b,offset)
        if offset+8+size>len(b):
            if clamp is None or tag[::-1]!=clamp.encode('ascii'):raise ValueError('Chunk exceeds file: '+repr(tag))
            yield tag[::-1].decode('ascii'),b[offset+8:];return
        yield tag[::-1].decode('ascii'),b[offset+8:offset+8+size]
        offset+=8+size
    if offset!=len(b):raise ValueError('Trailing chunk data')

def wmo_group(blob):
    """(top-level chunks, overrun bytes) of a WMO group file. Some modified clients have group files whose
    top-level MOGP size passes the end of the file, and the 3.3.5a client evidently loads them; it is believed
    (unverified) to read the MOGP header and sub-chunks in fixed order without bounding them by MOGP's size.
    The MOGP is clamped to the file; its sub-chunks stay strict, so a truly truncated group still raises."""
    top={};offset=0;overrun=0
    for tag,payload in chunks(blob,clamp='MOGP'):
        size=unpack('<I',blob,offset+4)[0];overrun=max(overrun,offset+8+size-len(blob))
        top[tag]=payload;offset+=8+len(payload)
    if overrun and len(top['MOGP'])<68:raise ValueError('Truncated MOGP header')
    return top,overrun

def doodad_indices(sets,count,refs,active):
    """Indices of the doodads the game instantiates for a WMO placement (CMapObj::CreateRefs, GetDoodadSet): only
    those some group lists in its MODR (`refs`; None keeps every index), whose set is the FIRST set whose
    range holds them (an index outside every set never exists) and is set 0 or the placement's `active` set.
    `sets` is [(first,count)] per MODS record. (The game's three per-placement set overrides are not in ADT data.)"""
    result=[]
    for index in sorted(range(count) if refs is None else {i for i in refs if i<count}):
        owner=next((i for i,(first,n) in enumerate(sets) if n and first<=index<first+n),None)
        if owner is not None and owner in (0,active):result.append(index)
    return result

def string(b,offset):
    if offset<0 or offset>=len(b):raise ValueError('Invalid string offset')
    return b[offset:].split(b'\0',1)[0].decode('utf8').replace('/','\\').lower()

def array(b,offset,count,fmt):
    size=struct.calcsize(fmt)
    if count>10000000 or offset+count*size>len(b):raise ValueError('Invalid array')
    return list(struct.iter_unpack(fmt,b[offset:offset+count*size]))

def normalize(v):
    length=math.sqrt(sum(x*x for x in v))
    return tuple(x/length for x in v) if length>1e-8 else (0.,0.,1.)

def srgb_to_linear(value):
    return value/12.92 if value<=.04045 else ((value+.055)/1.055)**2.4

def matmul(a,b):return tuple(tuple(sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)) for i in range(3))
def matvec(a,v):return tuple(sum(a[i][j]*v[j] for j in range(3)) for i in range(3))

def placement(pos,rot,scale):
    # ADT Y-up editing basis -> game Z-up coordinates. Matches the ADT rotation
    # convention (Y, then Z, then X) and the model's X,Z,-Y axis conversion.
    x,y,z=map(math.radians,(-rot[2],rot[1]-90,rot[0]))
    cx,sx,cy,sy,cz,sz=math.cos(x),math.sin(x),math.cos(y),math.sin(y),math.cos(z),math.sin(z)
    rx=((1,0,0),(0,cx,sx),(0,-sx,cx))
    ry=((cy,0,sy),(0,1,0),(-sy,0,cy))
    rz=((cz,sz,0),(-sz,cz,0),(0,0,1))
    fix=((1,0,0),(0,0,1),(0,-1,0))
    game=((0,0,-1),(-1,0,0),(0,1,0))
    matrix=matmul(game,matmul(matmul(ry,matmul(rz,rx)),fix))
    matrix=tuple(tuple(x*scale for x in row) for row in matrix)
    return matrix,(ZERO-pos[2],ZERO-pos[0],pos[1])

def quaternion_matrix(q,scale):
    x,y,z,w=q; length=math.sqrt(x*x+y*y+z*z+w*w)
    if length<1e-8:raise ValueError('Zero quaternion')
    x,y,z,w=(v/length for v in q)
    # Raw MODD quaternion uses the ordinary column-vector rotation. Noggit's
    # conjugation is paired with its transposed quaternion matrix constructor;
    # applying that conjugation here as well would invert the intended rotation.
    a=((1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)),
       (2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)),
       (2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)))
    return tuple(tuple(v*scale for v in row) for row in a)


class Assets:
    """The client's MPQ files by name, resolved in the game's archive order (client_archives.py).
    An archive without a readable (listfile) (private-server patches often strip it; the game does not
    need it) cannot be listed: every name the other archives list is probed in it (SFileHasFile), so a
    file it replaces still resolves to it, read() looks a name no listing has up in it, and the continents'
    64x64 ADT grids are probed in it, so tiles it adds are built (so are the WDT and the MAIN tiles of the
    other `maps` asked for). Other files it adds stay out of providers:
    a builder that enumerates them (world_lights_builder's model names) misses them. A custom patch archive
    (client_archives.is_custom_patch) StormLib cannot open is left out, after the Windows lock retries; any
    other still fails. Both are reported: unlisted, unreadable, warnings(), fingerprint()."""
    def __init__(self,client=None,archives='all',locale=None,without='',maps=()):
        self.client=Path(client) if client else CLIENT;self.view=archives
        self.locale=client_archives.detect_locale(self.client,locale)
        self.archives=[];self.unreadable=[]
        for p in client_archives.chain(self.client,archives,self.locale,without):
            try:self.archives.append(open_archive(p))
            except OSError:
                if not client_archives.is_custom_patch(p):raise   # Blizzard's archives hold the world itself
                self.unreadable.append(p.relative_to(self.client).as_posix())
        self.paths=[a.path for a in self.archives]
        self.providers={};self.blind=[];self.probed={}
        for index,a in enumerate(self.archives):
            try:names=a.names()
            except (OSError,ValueError):self.blind.append(index);continue
            for name in names:self.providers[name.lower()]=index
        tiles=[f'world\\maps\\{m}\\{m}_{x}_{y}.adt'.lower() for m in CONTINENTS for x in range(64) for y in range(64)] if self.blind else []
        for index in self.blind:   # in priority order, so the later archive still wins
            has=self.archives[index].has
            for name,provider in self.providers.items():
                if provider<index and has(name):self.providers[name]=index
            for name in tiles:
                if name not in self.providers and has(name):self.providers[name]=index
        if self.blind:self.probe_maps([m for m in maps if m not in CONTINENTS])
        self.unlisted=[self.paths[i].relative_to(self.client).as_posix() for i in self.blind]
        self.stats=collections.Counter();self.missing=set();self.unsupported=set();self.clamped=set()
    def probe_maps(self,maps):
        """Probe the unlisted archives for the WDT of each map, then for the ADTs its MAIN lists."""
        def probe(names):
            for index in self.blind:   # in priority order, so the later archive still wins
                has=self.archives[index].has
                for name in names:
                    if (name not in self.providers or self.providers[name]<index) and has(name):self.providers[name]=index
        probe([instance_maps.wdt_path(m) for m in maps]);tiles=[]
        for m in maps:
            name=instance_maps.wdt_path(m)
            if name not in self.providers:continue
            try:wdt=instance_maps.read_wdt(self.archives[self.providers[name]].read(name))
            except (OSError,ValueError):continue   # the build reports an unreadable WDT
            tiles+=[f'world\\maps\\{m}\\{m}_{x}_{y}.adt'.lower() for x,y in wdt['tiles']]
        probe(tiles)
    @classmethod
    def from_args(cls,args):
        maps=getattr(args,'map',None) or getattr(args,'maps',None) or ()
        return cls(args.client,args.archives,args.locale,getattr(args,'without',''),[maps] if isinstance(maps,str) else maps)
    def provider(self,name):
        """Index of the archive that provides a lower-case backslash name, or None. A name no listing
        has may still be in an unlisted archive (the highest one that holds it wins)."""
        if name in self.providers:return self.providers[name]
        if not self.blind:return None
        if name not in self.probed:self.probed[name]=next((i for i in reversed(self.blind) if self.archives[i].has(name)),None)
        return self.probed[name]
    def read(self,name):
        name=name.lower().replace('/','\\');index=self.provider(name)
        if index is None:raise FileNotFoundError(name)
        return self.archives[index].read(name)
    def origin(self,name):
        """The archive that provides a name, relative to the client folder."""
        index=self.provider(name.lower().replace('/','\\'))
        if index is None:raise FileNotFoundError(name)
        return self.paths[index].relative_to(self.client).as_posix()
    def warnings(self):
        """The archives this view could not list or open, as the builders report them."""
        return [{'archive':a,'problem':'unlisted'} for a in self.unlisted]+\
               [{'archive':a,'problem':'unreadable'} for a in self.unreadable]
    def fingerprint(self):
        return {'locale':self.locale,'view':self.view,**client_archives.fingerprint(self.paths,self.client),
                'unlisted':self.unlisted,'unreadable':self.unreadable}
    def close(self):
        for a in self.archives:a.close()


def rgb565(value):return ((value>>11)*255//31,((value>>5)&63)*255//63,(value&31)*255//31)

def decode_blp(b,maximum=128):
    if b[:4]!=b'BLP2':raise ValueError('Only BLP2 texture decoding is implemented')
    encoding,alpha_depth,alpha_encoding,has_mips=unpack('<4B',b,8)
    w,h=unpack('<II',b,12); offsets=unpack('<16I',b,20);sizes=unpack('<16I',b,84)
    if not w or not h or max(w,h)>16384:raise ValueError('Invalid BLP dimensions')
    level=0
    while max(w,h)>maximum and level<15 and offsets[level+1]:w=max(1,w//2);h=max(1,h//2);level+=1
    offset,size=offsets[level],sizes[level]
    if not offset or offset+size>len(b):raise ValueError('Invalid BLP mip')
    data=b[offset:offset+size];result=bytearray(w*h*4)
    if encoding==3:
        if len(data)<w*h*4:raise ValueError('Truncated RGBA mip')
        for i in range(w*h):
            bb,g,r,a=data[i*4:i*4+4];result[i*4:i*4+4]=bytes((r,g,bb,a))
    elif encoding==1:
        palette=b[148:1172]
        if len(palette)!=1024 or len(data)<w*h:raise ValueError('Truncated palette mip')
        alpha=data[w*h:]
        for i in range(w*h):
            bb,g,r,_=palette[data[i]*4:data[i]*4+4]
            if alpha_depth==0:a=255
            elif alpha_depth==1:a=((alpha[i//8]>>(i%8))&1)*255
            elif alpha_depth==4:a=((alpha[i//2]>>((i%2)*4))&15)*17
            elif alpha_depth==8:a=alpha[i]
            else:raise ValueError('Unsupported palette alpha')
            result[i*4:i*4+4]=bytes((r,g,bb,a))
    elif encoding==2:
        bc=1 if alpha_depth in (0,1) else (2 if alpha_encoding==1 else 3)
        block_size=8 if bc==1 else 16;offset=0
        for by in range((h+3)//4):
            for bx in range((w+3)//4):
                block=data[offset:offset+block_size];offset+=block_size
                if len(block)!=block_size:raise ValueError('Truncated BC block')
                ca,cb,bits=unpack('<HHI',block,0 if bc==1 else 8)
                a,b=rgb565(ca),rgb565(cb)
                palette=[(*a,255),(*b,255)]
                if ca>cb or bc!=1:
                    palette+=[tuple((2*a[i]+b[i])//3 for i in range(3))+(255,),tuple((a[i]+2*b[i])//3 for i in range(3))+(255,)]
                else:palette+=[tuple((a[i]+b[i])//2 for i in range(3))+(255,),(0,0,0,0 if alpha_depth else 255)]
                alphas=None
                if bc==2:
                    ab=int.from_bytes(block[:8],'little');alphas=[((ab>>(i*4))&15)*17 for i in range(16)]
                elif bc==3:
                    a0,a1=block[:2];ap=[a0,a1]
                    ap+=([(a0*(7-i)+a1*i)//7 for i in range(1,7)] if a0>a1 else [(a0*(5-i)+a1*i)//5 for i in range(1,5)]+[0,255])
                    ab=int.from_bytes(block[2:8],'little');alphas=[ap[(ab>>(i*3))&7] for i in range(16)]
                for i in range(16):
                    xx,yy=bx*4+i%4,by*4+i//4
                    if xx>=w or yy>=h:continue
                    color=palette[(bits>>(i*2))&3]
                    if alphas is not None:color=(*color[:3],alphas[i])
                    result[(yy*w+xx)*4:(yy*w+xx)*4+4]=bytes(color)
    else:raise ValueError('Unsupported BLP encoding')
    return w,h,bytes(result)


class Builder:
    def __init__(self,assets):
        self.assets=assets;self.meshes={};self.mesh_names={};self.textures={};self.skipped_groups=[];self.skip_bad_groups=False;self.reset()
    def reset(self):
        self.vertices=[];self.triangles=[];self.materials=[];self.material_ids={};self.tile_stats=collections.Counter();self.tile_errors=0
    def material(self,path='',blend=0,flat=False,point_glass=False):
        key=(path,blend,flat,point_glass)
        if key in self.material_ids:return self.material_ids[key]
        if path not in self.textures:
            try:self.textures[path]=decode_blp(self.assets.read(path)) if path else (0,0,b'')
            except (ValueError,FileNotFoundError,IndexError) as exc:
                self.assets.unsupported.add('texture:'+path+':'+str(exc));self.textures[path]=(0,0,b'')
        w,h,rgba=self.textures[path]
        color=(1.,1.,1.) if rgba else (.17,.17,.17)
        if flat and rgba:
            count=len(rgba)//4;color=tuple(sum(srgb_to_linear(v/255) for v in rgba[channel::4])/count for channel in range(3));w=h=0;rgba=b''
        result=len(self.materials);self.material_ids[key]=result
        # Negative exact integers carry fixture IDs. Older readers reject them
        # even when the game's floating-point mode flushes subnormal values.
        cutoff=ALPHA_KEY_CUTOFF if blend==1 else 0.
        if point_glass:
            code=point_glass-0x90000000+1
            if not 0<code<=0x7fffff:raise ValueError('Lamp fixture tag range')
            cutoff=-float(code)
        self.materials.append((color,w,h,cutoff,rgba))
        return result
    def add_mesh(self,mesh,matrix,translation,category,instance_id=0):
        if not any(value for row in matrix for value in row):
            self.assets.stats['zero_scale_hidden_instances']+=1
            return
        verts,tris,mats=mesh
        base=len(self.vertices);material_map=[self.material(*m) for m in mats]
        for vertex in verts:
            p=matvec(matrix,vertex[:3]);n=normalize(matvec(matrix,vertex[3:6]))
            self.vertices.append(tuple(p[i]+translation[i] for i in range(3))+n+vertex[6:8])
        for a,b,c,m in tris:self.triangles.append((base+a,base+b,base+c,material_map[m]))
        self.tile_stats[category+'_instances']+=1;self.tile_stats[category+'_triangles']+=len(tris)
    def m2(self,path):
        path=re.sub(r'\.(mdx|mdl)$','.m2',path,flags=re.I)
        if path in self.meshes:return self.meshes[path]
        b=self.assets.read(path)
        if b[:4]!=b'MD20' or unpack('<I',b,4)[0]!=264:raise ValueError('Unsupported M2')
        count,offset=unpack('<II',b,60);data=array(b,offset,count,'<3f4B4B3f4f')
        vertices=[tuple(v[:3])+tuple(v[11:14])+tuple(v[14:16]) for v in data]
        skin=self.assets.read(path[:-3]+'00.skin')
        if skin[:4]!=b'SKIN':raise ValueError('Unsupported skin')
        n,off=unpack('<II',skin,4);lookup=[x[0] for x in array(skin,off,n,'<H')]
        n,off=unpack('<II',skin,12);indices=[lookup[x[0]] for x in array(skin,off,n,'<H')]
        n,off=unpack('<II',skin,28);subsets=array(skin,off,n,'<10H7f')
        n,off=unpack('<II',skin,36);batches=array(skin,off,n,'<2B11H')
        n,off=unpack('<II',b,80);textures=array(b,off,n,'<4I')
        n,off=unpack('<II',b,112);flags=array(b,off,n,'<2H')
        n,off=unpack('<II',b,128);tex_lookup=[x[0] for x in array(b,off,n,'<H')]
        triangles=[];materials=[];selected=set()
        invisible=hidden_batches(b,batches);faint=hidden_batches(b,batches,CASTER_MIN_ALPHA)
        for batch_index,batch in enumerate(batches):
            if batch_index in invisible:
                self.assets.stats['excluded_permanently_invisible_m2_batches']+=1
                continue
            sub=batch[3];rf=batch[6];texcombo=batch[9]
            if sub>=len(subsets) or rf>=len(flags) or texcombo>=len(tex_lookup):raise ValueError('Invalid skin material')
            if sub in selected:continue
            material_flags,blend=flags[rf]
            # The game's shadow casters (CM2Model::GatherModelBatchesShadowMap): not shader 0x8000, batch flag
            # 4, a later material layer or material flag 0x40; blend above 1 only with material flag 0x80; and
            # colour alpha * texture weight of at least 0.55. Animated tracks count by their largest key (m2_visibility).
            if batch[2]==0x8000 or batch[0]&4 or batch[7] or material_flags&0x40:
                self.assets.stats['excluded_non_caster_m2_batches']+=1;continue
            if blend>1 and not material_flags&0x80:self.assets.stats['excluded_translucent_m2_batches']+=1;continue
            if batch_index in faint:self.assets.stats['excluded_faint_m2_batches']+=1;continue
            selected.add(sub);texture=textures[tex_lookup[texcombo]]
            path_tex=string(b,texture[3]) if texture[0]==0 and texture[2] else ''
            mat=len(materials);materials.append((path_tex,min(blend,1),False))   # the game's shadow pass alpha-tests every blended batch
            subset=subsets[sub];first=subset[4]+(subset[1]<<16);size=subset[5]
            if first+size>len(indices) or size%3:raise ValueError('Invalid skin triangle range')
            for i in range(first,first+size,3):
                t=tuple(indices[i:i+3])
                if max(t)>=len(vertices):raise ValueError('M2 vertex range')
                triangles.append((*t,mat))
        mesh=(vertices,triangles,materials);self.meshes[path]=mesh;self.mesh_names[id(vertices)]=path;return mesh
    def wmo(self,path):
        if path in self.meshes:return self.meshes[path]
        rootBlob=self.assets.read(path);root=dict(chunks(rootBlob));groups=unpack('<I',root['MOHD'],4)[0]
        import wmo_lamp_emission
        lampMaterials=wmo_lamp_emission.candidates(rootBlob)
        mt=root.get('MOTX',b'');materials=[]
        for material in array(root['MOMT'],0,len(root['MOMT'])//64,'<16I'):
            materials.append((string(mt,material[3]),material[2],False))
        vertices=[];triangles=[];refs=set()
        for group in range(groups):
            name=path[:-4]+'_%03d.wmo'%group
            try:groupBlob=self.assets.read(name);g,overrun=wmo_group(groupBlob)
            except FileNotFoundError:self.assets.missing.add(name);continue
            glass={}
            if lampMaterials:
                try:_,glass=wmo_lamp_emission.analyze(rootBlob,groupBlob,self.assets.read,group,lampMaterials)
                except (ValueError,FileNotFoundError,IndexError,KeyError,struct.error) as exc:
                    self.assets.unsupported.add('wmo_lamp:'+name+':'+str(exc))
            if overrun:self.assets.clamped.add(name)
            sub={}
            for tag,payload in chunks(g['MOGP'],68):sub.setdefault(tag,payload)
            modr=sub.get('MODR',b'');refs.update(x[0] for x in array(modr,0,len(modr)//2,'<H'))   # the group's doodad refs
            pos=array(sub['MOVT'],0,len(sub['MOVT'])//12,'<3f')
            normals=array(sub['MONR'],0,len(sub['MONR'])//12,'<3f')
            uv=array(sub['MOTV'],0,len(sub['MOTV'])//8,'<2f')
            if self.skip_bad_groups and not all(math.isfinite(v) for t in (pos,normals,uv) for row in t for v in row):
                self.skipped_groups.append({'wmo':path,'group':group,'reason':'nonfinite_vertex'});continue
            inds=[x[0] for x in array(sub['MOVI'],0,len(sub['MOVI'])//2,'<H')]
            props=array(sub['MOPY'],0,len(sub['MOPY'])//2,'<2B');base=len(vertices)
            vertices.extend(tuple(p)+tuple(normals[i])+tuple(uv[i]) for i,p in enumerate(pos))
            # Render batches define visible faces; collision-only triangles outside
            # MOBA ranges must not become artificial lighting occluders.
            batches=array(sub.get('MOBA',b''),0,len(sub.get('MOBA',b''))//24,'<6hI3H2B')
            emitted=set();tagged={}
            for batch in batches:
                first,size,material=batch[6],batch[7],batch[11]
                if material>=len(materials):raise ValueError('WMO material range')
                if materials[material][1]>1:self.assets.stats['excluded_translucent_wmo_batches']+=1;continue
                if first+size>len(inds) or size%3:raise ValueError('WMO index range')
                for i in range(first,first+size,3):
                    if i in emitted:continue
                    emitted.add(i);t=inds[i:i+3]
                    if max(t)>=len(pos):raise ValueError('WMO vertex range')
                    use=material
                    if i in glass:
                        identity=(material,glass[i])
                        if identity not in tagged:tagged[identity]=len(materials);materials.append((*materials[material],glass[i]))
                        use=tagged[identity]
                    triangles.append((base+t[0],base+t[1],base+t[2],use))
        mesh=(vertices,triangles,materials,root,refs);self.meshes[path]=mesh;self.mesh_names[id(vertices)]=path;return mesh
    def wmo_doodads(self,root,refs,doodadset,matrix,translation,root_id=0):
        names=root.get('MODN',b'');data=root.get('MODD',b'');sets=root.get('MODS',b'')
        if not data:return
        selected=doodad_indices([unpack('<II',sets,i*32+20) for i in range(len(sets)//32)],len(data)//40,refs,doodadset)
        for index in selected:
            offset=index*40;packed=unpack('<I',data,offset)[0];name=string(names,packed&0xffffff)
            pos=unpack('<3f',data,offset+4);q=unpack('<4f',data,offset+16);scale=unpack('<f',data,offset+32)[0]
            try:
                mesh=self.m2(name);local=quaternion_matrix(q,scale)
                rotation=matmul(matrix,local);p=matvec(matrix,pos)
                self.add_mesh(mesh,rotation,tuple(p[i]+translation[i] for i in range(3)),'wmo_doodad',(root_id<<32)|index)
            except (ValueError,FileNotFoundError,IndexError) as exc:self.assets.unsupported.add('wmo_doodad:'+name+':'+str(exc))
    def terrain(self,chunk,textures):
        if len(chunk)<128:raise ValueError('Truncated MCNK')
        flags,ix,iy=unpack('<3I',chunk);holes=unpack('<I',chunk,60)[0]
        px,py,pz=unpack('<3f',chunk,104)
        # MCNK child offsets are relative to the MCNK chunk header, eight bytes
        # before this payload. MCNR includes padding not in its declared size.
        def child(header_offset,name):
            offset=unpack('<I',chunk,header_offset)[0]-8
            if offset<0:return b''
            tag,size=unpack('<4sI',chunk,offset)
            if tag[::-1].decode()!=name:raise ValueError('Wrong MCNK child '+name)
            if offset+8+size>len(chunk):raise ValueError('Truncated MCNK child')
            return chunk[offset+8:offset+8+size]
        heights=unpack('<145f',child(20,'MCVT'));layers=child(28,'MCLY')
        texture_id=unpack('<I',layers)[0] if layers else 0
        material=self.material(textures[texture_id] if texture_id<len(textures) else '',0,True)
        base=len(self.vertices);positions=[];grid={};i=0
        for row in range(17):
            for col in range(8 if row&1 else 9):
                # MCNK x/y are the game-world north/west corner (z is altitude).
                p=(px-row*UNIT*.5,py-(col+.5*(row&1))*UNIT,pz+heights[i]);i+=1
                positions.append(p);grid[(row,col)]=len(positions)-1
        local_triangles=[]
        for row in range(8):
            for col in range(8):
                if holes & (1<<((row//2)*4+col//2)):continue
                a,b=grid[(row*2,col)],grid[(row*2,col+1)]
                c,d=grid[(row*2+2,col)],grid[(row*2+2,col+1)]
                center=grid[(row*2+1,col)]
                local_triangles.extend(((a,b,center),(b,d,center),(d,c,center),(c,a,center)))
        normal=[[0.,0.,0.] for p in positions]
        for a,b,c in local_triangles:
            pa,pb,pc=positions[a],positions[b],positions[c]
            u=tuple(pb[i]-pa[i] for i in range(3));v=tuple(pc[i]-pa[i] for i in range(3))
            n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
            if n[2]<0:n=tuple(-x for x in n)
            for index in (a,b,c):
                for j in range(3):normal[index][j]+=n[j]
            self.triangles.append((base+a,base+b,base+c,material))
        self.vertices.extend(p+normalize(normal[i])+(0.,0.) for i,p in enumerate(positions))
        self.tile_stats['terrain_chunks']+=1;self.tile_stats['terrain_triangles']+=len(local_triangles)
    def parse(self,mapname,x,y):
        """Read the ADT: terrain into vertices/triangles, placed M2/WMO through add_mesh."""
        self.reset();name=f'world\\maps\\{mapname}\\{mapname}_{x}_{y}.adt'
        blob=self.assets.read(name)
        if not blob:raise DeletedAsset('Empty highest-priority MPQ deletion marker')
        sections=list(chunks(blob));root=dict(sections)
        if unpack('<I',root['MVER'])[0]!=18:raise ValueError('Unsupported ADT version')
        textures=[s.decode('utf8').lower() for s in root.get('MTEX',b'').split(b'\0') if s]
        for tag,data in sections:
            if tag=='MCNK':self.terrain(data,textures)
        def names(offsetchunk,namechunk):
            return [string(root.get(namechunk,b''),off[0]) for off in array(root.get(offsetchunk,b''),0,len(root.get(offsetchunk,b''))//4,'<I')]
        model_names=names('MMID','MMDX');wmo_names=names('MWID','MWMO');seen=set()
        for entry in array(root.get('MDDF',b''),0,len(root.get('MDDF',b''))//36,'<2I6f2H'):
            name_id,uid=entry[:2]
            if ('m2',uid) in seen:continue
            seen.add(('m2',uid));path=model_names[name_id]
            try:self.add_mesh(self.m2(path),*placement(entry[2:5],entry[5:8],entry[8]/1024),'m2',uid)
            except (ValueError,FileNotFoundError,IndexError) as exc:self.assets.unsupported.add('m2:'+path+':'+str(exc));self.tile_errors+=1
        for entry in array(root.get('MODF',b''),0,len(root.get('MODF',b''))//64,'<2I12f4H'):
            name_id,uid=entry[:2]
            if ('wmo',uid) in seen:continue
            seen.add(('wmo',uid));path=wmo_names[name_id]
            try:
                mesh=self.wmo(path);matrix,translation=placement(entry[2:5],entry[5:8],1.)
                self.add_mesh(mesh[:3],matrix,translation,'wmo',uid);self.wmo_doodads(mesh[3],mesh[4],entry[15],matrix,translation,uid)
            except (ValueError,FileNotFoundError,IndexError,KeyError) as exc:self.assets.unsupported.add('wmo:'+path+':'+str(exc));self.tile_errors+=1
    def build(self,mapname,x,y,destination):
        self.parse(mapname,x,y)
        if not self.vertices or not self.triangles:raise ValueError('Empty world tile')
        return self.finish(mapname,x,y,destination)
    def finish(self,mapname,x,y,destination):
        self.write_scene(destination)
        return {'map':mapname,'tile':[x,y],'vertices':len(self.vertices),'triangles':len(self.triangles),
                'materials':len(self.materials),'bytes':destination.stat().st_size,'geometry':dict(self.tile_stats),
                'sha256':hashlib.sha256(destination.read_bytes()).hexdigest(),
                'bounds':[[min(v[i] for v in self.vertices) for i in range(3)],[max(v[i] for v in self.vertices) for i in range(3)]]}
    def write_scene(self,destination):
        destination.parent.mkdir(parents=True,exist_ok=True)
        temp=destination.with_suffix(f'.{os.getpid()}.tmp')
        with temp.open('wb') as f:
            f.write(struct.pack('<4s4I',b'FGS2',2,len(self.vertices),len(self.triangles),len(self.materials)))
            for vertex in self.vertices:
                if not all(math.isfinite(v) for v in vertex):raise ValueError('Nonfinite vertex')
                f.write(struct.pack('<8f',*vertex))
            for triangle in self.triangles:
                if max(triangle[:3])>=len(self.vertices) or triangle[3]>=len(self.materials):raise ValueError('Invalid output triangle')
                f.write(struct.pack('<4I',*triangle))
            for color,w,h,cutoff,rgba in self.materials:
                f.write(struct.pack('<3fIIfI',*color,w,h,cutoff,len(rgba)));f.write(rgba)
        retry(temp.replace,destination)


class InstancedBuilder(Builder):
    """FGS3 tile instance lists referencing shared FGS2 meshes."""
    def __init__(self,assets,output):
        self.output=output;self.exported={};self.instances=[];self.model_triangles={}
        super().__init__(assets)
    def reset(self):
        super().reset();self.instances=[]
    def add_mesh(self,mesh,matrix,translation,category,instance_id=0):
        if not any(value for row in matrix for value in row):
            self.assets.stats['zero_scale_hidden_instances']+=1
            return
        name=self.mesh_names[id(mesh[0])]
        if name not in self.exported:
            key=hashlib.sha256(('FGS3-v1:'+name).encode()).digest()
            vertices,triangles,materials=mesh
            if not triangles:return
            used=sorted(set(i for triangle in triangles for i in triangle[:3]));remap={old:new for new,old in enumerate(used)}
            compact=([vertices[i] for i in used],[(remap[a],remap[b],remap[c],m) for a,b,c,m in triangles],materials)
            temporary=Builder(self.assets);temporary.textures=self.textures
            temporary.add_mesh(compact,((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'model')
            temporary.write_scene(self.output/'models'/(key.hex()+'.fgs'))
            bounds=tuple(tuple(fn(v[i] for v in compact[0]) for i in range(3)) for fn in (min,max))
            self.exported[name]=(key,bounds);self.model_triangles[key]=len(triangles)
        key,(lo,hi)=self.exported[name]
        corners=[tuple(matvec(matrix,(x,y,z))[i]+translation[i] for i in range(3)) for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])]
        bounds=tuple(fn(v[i] for v in corners) for fn in (min,max) for i in range(3))
        kind={'m2':1,'wmo':2,'wmo_doodad':3}[category]
        self.instances.append((key,instance_id,kind,*bounds,*(v for row in matrix for v in row),*translation))
        self.tile_stats[category+'_instances']+=1;self.tile_stats[category+'_triangles']+=len(mesh[1])
    def build(self,mapname,x,y,destination):
        self.parse(mapname,x,y)
        terrain_key=hashlib.sha256(f'FGS3-v1:terrain:{mapname}:{x}:{y}'.encode()).digest()
        terrain_file=self.output/'models'/(terrain_key.hex()+'.fgs')
        if self.vertices and self.triangles:
            report=self.finish(mapname,x,y,terrain_file)
            lo,hi=report['bounds'];record=(terrain_key,(x<<16)|y,0,*lo,*hi,1.,0.,0.,0.,1.,0.,0.,0.,1.,0.,0.,0.)
            self.instances.insert(0,record);terrain_bytes=terrain_file.stat().st_size
        elif self.instances:   # fully holed terrain (e.g. Icecrown Citadel): the placed objects still make a tile
            lo=[min(r[3+i] for r in self.instances) for i in range(3)];hi=[max(r[6+i] for r in self.instances) for i in range(3)]
            report={'map':mapname,'tile':[x,y],'vertices':0,'triangles':0,'materials':0,'geometry':dict(self.tile_stats),'bounds':[lo,hi]};terrain_bytes=0
        else:raise ValueError('Empty world tile') if self.tile_errors else EmptyTile('Empty world tile')
        self.write_tile(destination,self.instances)
        report.update(format='FGS3',instances=len(self.instances),shared_model_count=len(self.exported),
                      bytes=destination.stat().st_size,terrain_bytes=terrain_bytes,
                      sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
        return report
    def write_tile(self,destination,instances):
        destination.parent.mkdir(parents=True,exist_ok=True);temp=destination.with_suffix(f'.{os.getpid()}.tmp')
        with temp.open('wb') as f:
            f.write(struct.pack('<4sII',b'FGS3',3,len(instances)))
            for record in instances:f.write(struct.pack('<32sQI18f',*record))
        retry(temp.replace,destination)
    def build_global(self,mapname):
        """A map that is one placed WMO (WDT MPHD flag 1, no ADTs): the WMO and its doodads, written into every
        tile their bounds overlap. Returns (per-tile reports, map info). The WDT's MODF position is in the
        ADT axes, but a global WMO has no 17066.67 offset (AreaTrigger.dbc positions lie inside the placed
        bounds only in this frame): translation (-z, -x, y) instead of placement()'s (ZERO-z, ZERO-x, y)."""
        wdt=instance_maps.read_wdt(self.assets.read(instance_maps.wdt_path(mapname)))
        if not wdt['global_wmo']:raise ValueError('Not a global WMO map')
        if not wdt['mwmo'] or not wdt['modf']:raise ValueError('Global WMO map without MWMO/MODF')
        path=wdt['mwmo'].replace('/','\\').lower();entry=wdt['modf'];uid=entry[1]
        self.reset();self.skipped_groups=[];self.skip_bad_groups=True
        try:mesh=self.wmo(path)
        finally:self.skip_bad_groups=False
        matrix,translation=placement(entry[2:5],entry[5:8],1.)
        translation=(0.-entry[4],0.-entry[2],entry[3])
        self.add_mesh(mesh[:3],matrix,translation,'wmo',uid);self.wmo_doodads(mesh[3],mesh[4],entry[15],matrix,translation,uid)
        tiles={};outside=0
        for record in self.instances:
            x0,y0,x1,y1=record[3],record[4],record[6],record[7]
            for tx in range(instance_maps.tile_of(x0,y1)[0],instance_maps.tile_of(x0,y0)[0]+1):
                for ty in range(instance_maps.tile_of(x1,y0)[1],instance_maps.tile_of(x0,y0)[1]+1):
                    if 0<=tx<64 and 0<=ty<64:tiles.setdefault((tx,ty),[]).append(record)
                    else:outside+=1
        if not tiles:raise ValueError('Empty world tile')
        reports=[]
        for (tx,ty),records in sorted(tiles.items()):
            destination=self.output/mapname/f'{tx}_{ty}.fg3'
            self.write_tile(destination,records)
            geometry=collections.Counter({1:'m2_instances',2:'wmo_instances',3:'wmo_doodad_instances'}[r[2]] for r in records)
            reports.append({'map':mapname,'tile':[tx,ty],'vertices':0,'triangles':sum(self.model_triangles[r[0]] for r in records),'materials':0,
                            'geometry':dict(geometry),'format':'FGS3','instances':len(records),'bytes':destination.stat().st_size,
                            'sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
        info={'kind':'global_wmo','wmo':path,'wmo_uid':uid,'instances':len(self.instances),'tiles_outside_grid':outside,
              'bounds':[[min(r[3+i] for r in self.instances) for i in range(3)],[max(r[6+i] for r in self.instances) for i in range(3)]],
              'skipped_wmo_groups':list(self.skipped_groups)}
        return reports,info


def plan_map(assets,mapname):
    """('adt', tiles) for a map built per ADT tile, ('global', wdt) for a single placed WMO. Continents list
    their tiles by ADT name; any other map by its WDT's MAIN (so a flagged tile whose ADT is missing is reported)."""
    if mapname in CONTINENTS:
        pattern=re.compile(r'world\\maps\\'+re.escape(mapname)+r'\\'+re.escape(mapname)+r'_(\d+)_(\d+)\.adt$',re.I)
        return 'adt',sorted(tuple(map(int,match.groups())) for name in assets.providers if (match:=pattern.fullmatch(name)))
    wdt=instance_maps.read_wdt(assets.read(instance_maps.wdt_path(mapname)))
    return ('global',wdt) if wdt['global_wmo'] else ('adt',wdt['tiles'])

def built_global(output,mapname):
    """True when an earlier run's report for a global-WMO map lists tiles that are all still there (a global map
    has no tile list before it is built, so a resumed run cannot tell from the files alone)."""
    for report in output.glob(f'build-{mapname}-*.json'):
        try:tiles=[tuple(r['tile']) for r in json.loads(report.read_text())['generated']]
        except (OSError,ValueError,KeyError,TypeError):continue
        if tiles and all((output/mapname/f'{x}_{y}.fg3').is_file() for x,y in tiles):return True
    return False

def main():
    p=argparse.ArgumentParser();p.add_argument('--map',nargs='+',default=['Azeroth'],help='one or more maps (Map.dbc directory names)');p.add_argument('--tiles',nargs=4,type=int,metavar=('X0','Y0','X1','Y1'))
    p.add_argument('--all-continents',action='store_true');p.add_argument('--output',type=Path,help='default: <client>/world-cache');p.add_argument('--force',action='store_true');p.add_argument('--instanced',action='store_true')
    client_archives.add_arguments(p);args=p.parse_args();assets=Assets.from_args(args);args.output=args.output or assets.client/'world-cache';builder=InstancedBuilder(assets,args.output) if args.instanced else Builder(assets);start=time.time()
    maps=CONTINENTS if args.all_continents else list(dict.fromkeys(args.map))
    for warning in assets.warnings():print(json.dumps({'archive_warning':warning}),flush=True)   # install_world_cache shows these at once
    try:
        plans={};plan_errors={}
        for mapname in maps:
            try:plans[mapname]=plan_map(assets,mapname)
            except (ValueError,FileNotFoundError) as exc:plan_errors[mapname]=str(exc)
            if mapname in plans and args.tiles and plans[mapname][0]=='adt':
                plans[mapname]=('adt',[(x,y) for x,y in plans[mapname][1] if args.tiles[0]<=x<=args.tiles[2] and args.tiles[1]<=y<=args.tiles[3]])
        # one progress total for the process: a global-WMO map counts as one unit (its tile list is known only once built)
        print(json.dumps({'tiles_total':sum(len(v) if kind=='adt' else 1 for kind,v in plans.values()),'maps':maps}),flush=True)
        limitations=['Static M2 bind pose; animated characters absent.','Terrain albedo is first-layer texture mean; terrain geometry is exact including 4x4 holes.',
                f'BLP2 selected mip up to 128px; alpha cutout threshold {ALPHA_KEY_CUTOFF} (game shadow pass: 128/255 at full resolution).','Additive/translucent batches excluded from opaque lighting geometry.',
                'M2 active world geosets use first opaque/cutout material per submesh.','Cross-tile placed object duplicates may exist; runtime should deduplicate triangle geometry.']
        args.output.mkdir(parents=True,exist_ok=True);totals=collections.Counter();seen=dict(stats=collections.Counter(),missing=set(),unsupported=set(),clamped=set())
        for mapname in maps:
            records=[];failures=[];deleted=[];empty=[];info=None
            if mapname in plan_errors:failures.append({'map':mapname,'tile':None,'error':plan_errors[mapname]})
            elif plans[mapname][0]=='global':
                if not args.instanced:failures.append({'map':mapname,'tile':None,'error':'A global-WMO map needs --instanced'})
                elif built_global(args.output,mapname) and not args.force:
                    print(json.dumps({'tile':f'{mapname}/global','triangles':0,'MB':0.,'seconds':round(time.time()-start,1)}),flush=True)
                else:
                    try:
                        records,info=builder.build_global(mapname)
                        print(json.dumps({'tile':f'{mapname}/global','triangles':sum(r['triangles'] for r in records),'MB':round(sum(r['bytes'] for r in records)/1e6,1),'seconds':round(time.time()-start,1)}),flush=True)
                    except (ValueError,FileNotFoundError,IndexError,KeyError) as exc:
                        failures.append({'map':mapname,'tile':None,'error':str(exc)});print('ERROR',failures[-1],flush=True)
            else:
                for x,y in plans[mapname][1]:
                    destination=args.output/mapname/(f'{x}_{y}.fg3' if args.instanced else f'{x}_{y}.fgs')
                    if destination.exists() and not args.force:continue
                    try:
                        record=builder.build(mapname,x,y,destination);records.append(record)
                        print(json.dumps({'tile':f'{mapname}/{x}_{y}','triangles':record['triangles'],'MB':round(record['bytes']/1e6,1),'seconds':round(time.time()-start,1)}),flush=True)
                    except DeletedAsset:
                        deleted.append({'map':mapname,'tile':[x,y]})
                    except EmptyTile:   # fully holed terrain and no objects, e.g. most of NexusRaid
                        empty.append({'map':mapname,'tile':[x,y]});print(json.dumps({'tile':f'{mapname}/{x}_{y}','triangles':0,'MB':0.,'empty':True,'seconds':round(time.time()-start,1)}),flush=True)
                    except (ValueError,FileNotFoundError,IndexError,KeyError) as exc:
                        failures.append({'map':mapname,'tile':[x,y],'error':str(exc)});print('ERROR',failures[-1],flush=True)
            # a report holds what this map added to the shared tallies
            stats=assets.stats-seen['stats'];missing=assets.missing-seen['missing'];unsupported=assets.unsupported-seen['unsupported'];clamped=assets.clamped-seen['clamped']
            seen=dict(stats=collections.Counter(assets.stats),missing=set(assets.missing),unsupported=set(assets.unsupported),clamped=set(assets.clamped))
            report={'format':'FGS3' if args.instanced else 'FGS2','generated':records,'failures':failures,'deleted_source_tiles':deleted,'empty_source_tiles':empty,'missing_assets':sorted(missing),
                'unsupported_assets':sorted(unsupported),'clamped_wmo_groups':sorted(clamped),'archive_warnings':assets.warnings(),'statistics':dict(stats),'elapsed_seconds':time.time()-start,
                'limitations':limitations,'archive_priority':[p.relative_to(assets.client).as_posix() for p in assets.paths],'archive_fingerprint':assets.fingerprint()}
            if info:report['global_wmo']=info
            (args.output/f'build-{mapname}-{int(time.time())}-{os.getpid()}.json').write_text(json.dumps(report,indent=2)+'\n')
            totals.update(generated=len(records),failures=len(failures))
            builder.meshes.clear();builder.mesh_names.clear()   # parsed models are the bulk of the memory; the next map re-reads what it shares
        print(json.dumps({'generated':totals['generated'],'failures':totals['failures'],'unsupported':len(assets.unsupported),'clamped_wmo_groups':len(assets.clamped),'seconds':round(time.time()-start,1)}))
    finally:assets.close()


if __name__=='__main__':main()
