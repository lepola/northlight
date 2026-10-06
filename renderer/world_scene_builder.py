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
ZERO=17066.666666666668
# Windows: a virus scanner or the indexer briefly holds a just-written file; replace() is retried.
LOCK_RETRIES=(.2,.4,.8,1.6,3.) if os.name=='nt' else ()
CONTINENTS=['Azeroth','Kalimdor','Expansion01','Northrend']
CUSTOM_PATCH=re.compile(r'patch(?:-[a-z]{4})?-[4-9a-z]\.mpq')   # not one Blizzard shipped (client_archives.STOCK_SUFFIXES)
UNIT=533.3333333333334/128

class DeletedAsset(ValueError):
    pass


def retry(fn,*args):
    """fn(*args), retried on PermissionError after each LOCK_RETRIES delay (6 attempts on Windows)."""
    for delay in LOCK_RETRIES:
        try:return fn(*args)
        except PermissionError:time.sleep(delay)
    return fn(*args)


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
    64x64 ADT grids are probed in it, so tiles it adds are built. Other files it adds stay out of providers:
    a builder that enumerates them (world_lights_builder's model names) misses them. A custom patch archive
    (patch-4..9, -a..z) StormLib cannot open is left out; any other still fails. Both are reported:
    unlisted, unreadable, warnings(), fingerprint()."""
    def __init__(self,client=None,archives='all',locale=None,without=''):
        self.client=Path(client) if client else CLIENT;self.view=archives
        self.locale=client_archives.detect_locale(self.client,locale)
        self.archives=[];self.unreadable=[]
        for p in client_archives.chain(self.client,archives,self.locale,without):
            try:self.archives.append(Archive(p))
            except OSError:
                if not CUSTOM_PATCH.fullmatch(p.name.lower()):raise   # Blizzard's archives hold the world itself
                self.unreadable.append(p.relative_to(self.client).as_posix())
        self.paths=[a.path for a in self.archives]
        self.providers={};self.blind=[];self.probed={}
        for index,a in enumerate(self.archives):
            try:names=a.names()
            except (OSError,ValueError):self.blind.append(index);continue
            for name in names:self.providers[name.lower()]=index
        tiles=[f'world\\maps\\{m}\\{m}_{x}_{y}.adt'.lower() for m in CONTINENTS for x in range(64) for y in range(64)]
        for index in self.blind:   # in priority order, so the later archive still wins
            has=self.archives[index].has
            for name,provider in self.providers.items():
                if provider<index and has(name):self.providers[name]=index
            for name in tiles:
                if name not in self.providers and has(name):self.providers[name]=index
        self.unlisted=[self.paths[i].relative_to(self.client).as_posix() for i in self.blind]
        self.stats=collections.Counter();self.missing=set();self.unsupported=set();self.clamped=set()
    @classmethod
    def from_args(cls,args):
        return cls(args.client,args.archives,args.locale,getattr(args,'without',''))
    def provider(self,name):
        """Index of the archive that provides a lower-case backslash name, or None. A name no listing
        has may still be in an unlisted archive (the highest one that holds it wins)."""
        if name in self.providers:return self.providers[name]
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
        self.assets=assets;self.meshes={};self.mesh_names={};self.textures={};self.reset()
    def reset(self):
        self.vertices=[];self.triangles=[];self.materials=[];self.material_ids={};self.tile_stats=collections.Counter()
    def material(self,path='',blend=0,flat=False):
        key=(path,blend,flat)
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
        self.materials.append((color,w,h,.5 if blend==1 else 0.,rgba))
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
        invisible=hidden_batches(b,batches)
        for batch_index,batch in enumerate(batches):
            if batch_index in invisible:
                self.assets.stats['excluded_permanently_invisible_m2_batches']+=1
                continue
            sub=batch[3];rf=batch[6];texcombo=batch[9]
            if sub>=len(subsets) or rf>=len(flags) or texcombo>=len(tex_lookup):raise ValueError('Invalid skin material')
            if sub in selected:continue
            blend=flags[rf][1]
            if blend>1:self.assets.stats['excluded_translucent_m2_batches']+=1;continue
            selected.add(sub);texture=textures[tex_lookup[texcombo]]
            path_tex=string(b,texture[3]) if texture[0]==0 and texture[2] else ''
            mat=len(materials);materials.append((path_tex,blend,False))
            subset=subsets[sub];first=subset[4]+(subset[1]<<16);size=subset[5]
            if first+size>len(indices) or size%3:raise ValueError('Invalid skin triangle range')
            for i in range(first,first+size,3):
                t=tuple(indices[i:i+3])
                if max(t)>=len(vertices):raise ValueError('M2 vertex range')
                triangles.append((*t,mat))
        mesh=(vertices,triangles,materials);self.meshes[path]=mesh;self.mesh_names[id(vertices)]=path;return mesh
    def wmo(self,path):
        if path in self.meshes:return self.meshes[path]
        root=dict(chunks(self.assets.read(path)));groups=unpack('<I',root['MOHD'],4)[0]
        mt=root.get('MOTX',b'');materials=[]
        for material in array(root['MOMT'],0,len(root['MOMT'])//64,'<16I'):
            materials.append((string(mt,material[3]),material[2],False))
        vertices=[];triangles=[]
        for group in range(groups):
            name=path[:-4]+'_%03d.wmo'%group
            try:g,overrun=wmo_group(self.assets.read(name))
            except FileNotFoundError:self.assets.missing.add(name);continue
            if overrun:self.assets.clamped.add(name)
            sub={}
            for tag,payload in chunks(g['MOGP'],68):sub.setdefault(tag,payload)
            pos=array(sub['MOVT'],0,len(sub['MOVT'])//12,'<3f')
            normals=array(sub['MONR'],0,len(sub['MONR'])//12,'<3f')
            uv=array(sub['MOTV'],0,len(sub['MOTV'])//8,'<2f')
            inds=[x[0] for x in array(sub['MOVI'],0,len(sub['MOVI'])//2,'<H')]
            props=array(sub['MOPY'],0,len(sub['MOPY'])//2,'<2B');base=len(vertices)
            vertices.extend(tuple(p)+tuple(normals[i])+tuple(uv[i]) for i,p in enumerate(pos))
            # Render batches define visible faces; collision-only triangles outside
            # MOBA ranges must not become artificial lighting occluders.
            batches=array(sub.get('MOBA',b''),0,len(sub.get('MOBA',b''))//24,'<6hI3H2B')
            emitted=set()
            for batch in batches:
                first,size,material=batch[6],batch[7],batch[11]
                if batch[10]&2:material=batch[5] # extended material index convention
                if material>=len(materials):raise ValueError('WMO material range')
                if materials[material][1]>1:self.assets.stats['excluded_translucent_wmo_batches']+=1;continue
                if first+size>len(inds) or size%3:raise ValueError('WMO index range')
                for i in range(first,first+size,3):
                    if i in emitted:continue
                    emitted.add(i);t=inds[i:i+3]
                    if max(t)>=len(pos):raise ValueError('WMO vertex range')
                    triangles.append((base+t[0],base+t[1],base+t[2],material))
        mesh=(vertices,triangles,materials,root);self.meshes[path]=mesh;self.mesh_names[id(vertices)]=path;return mesh
    def wmo_doodads(self,root,doodadset,matrix,translation,root_id=0):
        names=root.get('MODN',b'');data=root.get('MODD',b'');sets=root.get('MODS',b'')
        if not data:return
        selected=set()
        for index in {0,doodadset}:
            if (index+1)*32>len(sets):continue
            first,count=unpack('<II',sets,index*32+20)
            selected.update(range(first,first+count))
        for index in sorted(selected):
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
    def build(self,mapname,x,y,destination):
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
            except (ValueError,FileNotFoundError,IndexError) as exc:self.assets.unsupported.add('m2:'+path+':'+str(exc))
        for entry in array(root.get('MODF',b''),0,len(root.get('MODF',b''))//64,'<2I12f4H'):
            name_id,uid=entry[:2]
            if ('wmo',uid) in seen:continue
            seen.add(('wmo',uid));path=wmo_names[name_id]
            try:
                mesh=self.wmo(path);matrix,translation=placement(entry[2:5],entry[5:8],1.)
                self.add_mesh(mesh[:3],matrix,translation,'wmo',uid);self.wmo_doodads(mesh[3],entry[15],matrix,translation,uid)
            except (ValueError,FileNotFoundError,IndexError,KeyError) as exc:self.assets.unsupported.add('wmo:'+path+':'+str(exc))
        if not self.vertices or not self.triangles:raise ValueError('Empty world tile')
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
        self.output=output;self.exported={};self.instances=[]
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
            self.exported[name]=(key,bounds)
        key,(lo,hi)=self.exported[name]
        corners=[tuple(matvec(matrix,(x,y,z))[i]+translation[i] for i in range(3)) for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])]
        bounds=tuple(fn(v[i] for v in corners) for fn in (min,max) for i in range(3))
        kind={'m2':1,'wmo':2,'wmo_doodad':3}[category]
        self.instances.append((key,instance_id,kind,*bounds,*(v for row in matrix for v in row),*translation))
        self.tile_stats[category+'_instances']+=1;self.tile_stats[category+'_triangles']+=len(mesh[1])
    def build(self,mapname,x,y,destination):
        terrain_key=hashlib.sha256(f'FGS3-v1:terrain:{mapname}:{x}:{y}'.encode()).digest()
        terrain_file=self.output/'models'/(terrain_key.hex()+'.fgs')
        report=super().build(mapname,x,y,terrain_file)
        lo,hi=report['bounds'];record=(terrain_key,(x<<16)|y,0,*lo,*hi,1.,0.,0.,0.,1.,0.,0.,0.,1.,0.,0.,0.)
        self.instances.insert(0,record)
        destination.parent.mkdir(parents=True,exist_ok=True);temp=destination.with_suffix(f'.{os.getpid()}.tmp')
        with temp.open('wb') as f:
            f.write(struct.pack('<4sII',b'FGS3',3,len(self.instances)))
            for record in self.instances:f.write(struct.pack('<32sQI18f',*record))
        retry(temp.replace,destination)
        report.update(format='FGS3',instances=len(self.instances),shared_model_count=len(self.exported),
                      bytes=destination.stat().st_size,terrain_bytes=terrain_file.stat().st_size,
                      sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
        return report


def main():
    p=argparse.ArgumentParser();p.add_argument('--map',default='Azeroth');p.add_argument('--tiles',nargs=4,type=int,metavar=('X0','Y0','X1','Y1'))
    p.add_argument('--all-continents',action='store_true');p.add_argument('--output',type=Path,help='default: <client>/world-cache');p.add_argument('--force',action='store_true');p.add_argument('--instanced',action='store_true')
    client_archives.add_arguments(p);args=p.parse_args();assets=Assets.from_args(args);args.output=args.output or assets.client/'world-cache';builder=InstancedBuilder(assets,args.output) if args.instanced else Builder(assets);start=time.time();records=[];failures=[];deleted=[]
    maps=CONTINENTS if args.all_continents else [args.map]
    for warning in assets.warnings():print(json.dumps({'archive_warning':warning}),flush=True)   # install_world_cache shows these at once
    try:
        for mapname in maps:
            pattern=re.compile(r'world\\maps\\'+re.escape(mapname)+r'\\'+re.escape(mapname)+r'_(\d+)_(\d+)\.adt$',re.I)
            tiles=sorted(tuple(map(int,match.groups())) for name in assets.providers if (match:=pattern.fullmatch(name)))
            if args.tiles:tiles=[(x,y) for x,y in tiles if args.tiles[0]<=x<=args.tiles[2] and args.tiles[1]<=y<=args.tiles[3]]
            print(json.dumps({'map':mapname,'tiles_total':len(tiles)}),flush=True)
            for x,y in tiles:
                destination=args.output/mapname/(f'{x}_{y}.fg3' if args.instanced else f'{x}_{y}.fgs')
                if destination.exists() and not args.force:continue
                try:
                    record=builder.build(mapname,x,y,destination);records.append(record)
                    print(json.dumps({'tile':f'{mapname}/{x}_{y}','triangles':record['triangles'],'MB':round(record['bytes']/1e6,1),'seconds':round(time.time()-start,1)}),flush=True)
                except DeletedAsset:
                    deleted.append({'map':mapname,'tile':[x,y]})
                except (ValueError,FileNotFoundError,IndexError,KeyError) as exc:
                    failures.append({'map':mapname,'tile':[x,y],'error':str(exc)});print('ERROR',failures[-1],flush=True)
        report={'format':'FGS3' if args.instanced else 'FGS2','generated':records,'failures':failures,'deleted_source_tiles':deleted,'missing_assets':sorted(assets.missing),
                'unsupported_assets':sorted(assets.unsupported),'clamped_wmo_groups':sorted(assets.clamped),'archive_warnings':assets.warnings(),'statistics':dict(assets.stats),'elapsed_seconds':time.time()-start,
                'limitations':['Static M2 bind pose; animated characters absent.','Terrain albedo is first-layer texture mean; terrain geometry is exact including 4x4 holes.',
                'BLP2 selected mip up to 128px; alpha cutout threshold 0.5.','Additive/translucent batches excluded from opaque lighting geometry.',
                'M2 active world geosets use first opaque/cutout material per submesh.','Cross-tile placed object duplicates may exist; runtime should deduplicate triangle geometry.'],
                'archive_priority':[p.relative_to(assets.client).as_posix() for p in assets.paths],'archive_fingerprint':assets.fingerprint()}
        args.output.mkdir(parents=True,exist_ok=True)
        (args.output/f'build-{args.map}-{int(time.time())}-{os.getpid()}.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({'generated':len(records),'failures':len(failures),'unsupported':len(assets.unsupported),'clamped_wmo_groups':len(assets.clamped),'seconds':round(time.time()-start,1)}))
    finally:assets.close()


if __name__=='__main__':main()
