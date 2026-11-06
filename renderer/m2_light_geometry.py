"""Find a small source-enclosing component inside a rigid M2 fixture.

Native light ownership supplies the emission hint even when a replacement packs
the bulb and frame into the same material. No asset names or colour thresholds.
"""
import collections
import math
import itertools


def dot(a,b):return sum(x*y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])


def encloses(vertices,triangles,group,point,extent,visible=None):
    """A nearly closed inward view, not a nearby frame or a flat glow card."""
    prepared=[]
    for i in group:
        a,b,c=(vertices[v] for v in triangles[i][:3])
        prepared.append((i,a[:3],sub(b[:3],a[:3]),sub(c[:3],a[:3]),a[3:6],b[3:6],c[3:6]))
    hits=outward=0;epsilon=max(extent*1e-6,1e-8)
    for i in range(64):
        z=1-2*(i+.5)/64;r=math.sqrt(1-z*z);phi=i*2.399963229728653
        direction=(r*math.cos(phi),r*math.sin(phi),z);nearest=math.inf;back=False
        for index,a,e1,e2,na,nb,nc in prepared:
            h=cross(direction,e2);det=dot(e1,h)
            if abs(det)<1e-12:continue
            offset=sub(point,a);u=dot(offset,h)/det
            if u<0 or u>1:continue
            q=cross(offset,e1);v=dot(direction,q)/det
            if v<0 or u+v>1:continue
            distance=dot(e2,q)/det
            if epsilon<distance<nearest:
                if visible is not None and not visible(index,u,v):continue
                nearest=distance
                normal=tuple((1-u-v)*na[k]+u*nb[k]+v*nc[k] for k in range(3))
                back=dot(normal,direction)>0
        if math.isfinite(nearest):hits+=1;outward+=back
    return hits>=56 and outward*16>=hits*15


def source_components(vertices,triangles,materials,lights,static_vertex,alpha_at=None):
    """Return triangle -> source index; preserve the surrounding fixture."""
    if not triangles or len(triangles)>65536 or not 1<=len(lights)<=64:return {}
    referenced={v for t in triangles for v in t[:3]}
    low=[min(vertices[v][k] for v in referenced) for k in range(3)]
    high=[max(vertices[v][k] for v in referenced) for k in range(3)]
    volume=math.prod(high[k]-low[k] for k in range(3))
    if not math.isfinite(volume) or volume<=0:return {}
    parents=list(range(len(triangles)));points={};opaque=[]
    def root(i):
        while parents[i]!=i:parents[i]=parents[parents[i]];i=parents[i]
        return i
    for i,t in enumerate(triangles):
        material=materials[t[3]]
        if material[1] not in (0,1) or len(material)>3 or (material[1]==1 and alpha_at is None):continue
        opaque.append(i)
        for v in t[:3]:
            key=(t[3],*(round(x,5) for x in vertices[v][:3]))
            if key in points:parents[root(i)]=root(points[key])
            else:points[key]=i
    groups=collections.defaultdict(list)
    for i in opaque:groups[root(i)].append(i)
    result={}
    for group in groups.values():
        if not 4<=len(group)<=4096 or len(group)==len(triangles):continue
        ids={v for i in group for v in triangles[i][:3]}
        lo=[min(vertices[v][k] for v in ids) for k in range(3)]
        hi=[max(vertices[v][k] for v in ids) for k in range(3)]
        span=[hi[k]-lo[k] for k in range(3)]
        # A room/body occupying the fixture is not a bulb. Bounds and tests use
        # model units, so a uniformly scaled mod asset retains the same policy.
        if min(span)<=0 or math.prod(span)>volume*.25:continue
        inside=[s for s in lights if all(lo[k]<s[0][k]<hi[k] for k in range(3))]
        if len(inside)!=1:continue
        light=inside[0]
        if not light[6]&2 or max(span)>light[3]*.25:continue
        if not all(static_vertex(v) for v in ids):continue
        if encloses(vertices,triangles,group,light[0],max(span),alpha_at if materials[triangles[group[0]][3]][1]==1 else None):
            result.update((i,light[4]) for i in group)
    return result


def paired_source_layers(vertices,triangles,materials,lights,static_vertex,owners=None):
    """An exact UV/normal copy of a complete owned luminous region shares it.

    Some opaque base meshes duplicate an unlit source layer with a small depth
    separation. Prove the whole owned source-containing material region, rather than
    dropping its connected base mesh (which can also contain the metal frame).
    """
    if not triangles or len(triangles)>65536:return {}
    owned=dict(owners or {})
    for i,t in enumerate(triangles):
        material=materials[t[3]]
        if len(material)>3:
            tag=material[3]
            if 0x800000<=tag<0x800040:owned[i]=tag-0x800000
            elif 0x801000<=tag<0x801040:owned[i]=0x80000000+tag-0x801000
    if not owned:return {}
    sources={s[4]:s for s in lights if s[6]&2}
    groups=collections.defaultdict(list)
    for i,source in owned.items():groups[(source,triangles[i][3])].append(i)
    def uv_key(t):return tuple(sorted((round(vertices[v][6],5),round(vertices[v][7],5)) for v in t[:3]))
    candidates=collections.defaultdict(list)
    for i,t in enumerate(triangles):
        material=materials[t[3]]
        if i not in owned and len(material)==3 and material[1]==0:
            candidates[(material,uv_key(t))].append(i)
    def parallel(a,b):
        product=dot(a,b)
        return product>0 and product*product>=.999**2*dot(a,a)*dot(b,b)
    def face(t):
        a,b,c=(vertices[v][:3] for v in t[:3]);return cross(sub(b,a),sub(c,a))
    proofs=collections.defaultdict(list)
    for group in groups.values():
        if not 4<=len(group)<=4096:continue
        source=owned[group[0]];light=sources.get(source);material=materials[triangles[group[0]][3]]
        if light is None or material[1]!=0:continue
        ids={v for i in group for v in triangles[i][:3]}
        low=[min(vertices[v][k] for v in ids) for k in range(3)];high=[max(vertices[v][k] for v in ids) for k in range(3)]
        if not all(low[k]<light[0][k]<high[k] for k in range(3)) or max(high[k]-low[k] for k in range(3))>light[3] or not all(static_vertex(v) for v in ids):continue
        # Classification tolerance for an authored duplicate, never a ray bias
        # or a vertex move. It scales with the luminous region, not the model.
        tolerance=max(high[k]-low[k] for k in range(3))*.005
        matched=[];work=0
        for i in group:
            a=triangles[i];found=[]
            for j in candidates[(material[:3],uv_key(a))]:
                work+=1
                if work>65536:break
                b=triangles[j]
                if not all(static_vertex(v) for v in b[:3]) or not parallel(face(a),face(b)):continue
                for order in itertools.permutations(range(3)):
                    pairs=[(vertices[a[k]],vertices[b[order[k]]]) for k in range(3)]
                    if all(math.dist(x[:3],y[:3])<=tolerance and max(abs(x[k]-y[k]) for k in (6,7))<=1e-6 and parallel(x[3:6],y[3:6]) for x,y in pairs):
                        found.append(j);break
            if work>65536 or len(found)!=1:break
            matched.extend(found)
        if len(matched)!=len(group) or len(set(matched))!=len(group):continue
        for i in matched:proofs[i].append(source)
    return {i:owners[0] for i,owners in proofs.items() if len(owners)==1}
