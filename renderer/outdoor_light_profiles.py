"""Offline, exact-asset emitter catalog. No runtime model guessing or I/O.

A profile pins one model asset by sha256 (asset_sha256, position, rgb, intensity, start, end).
Optional "variants" pin other assets of the same model path, e.g. the stock 3.3.5a model where the
profile was authored on an HD pack: {sha256: {position, position_basis, [rgb, intensity, start,
end, family]}}; omitted fields are the profile's own.
"""
import hashlib,json,math,re
from pathlib import Path

EMITTER=('position','rgb','intensity','start','end')
VARIANT_FIELDS={'asset','family','position','position_basis','rgb','intensity','start','end','emitters','casts_shadows'}

def check(p):
    if 'casts_shadows' in p and type(p['casts_shadows']) is not bool:raise ValueError('Invalid shadow policy')
    if len(p['position'])!=3 or len(p['rgb'])!=3:raise ValueError('Emitter vector size')
    if not all(math.isfinite(v) for v in [*p['position'],*p['rgb'],p['intensity'],p['start'],p['end']]):raise ValueError('Nonfinite emitter')
    if any(abs(v)>100 for v in p['position']) or not all(0<=v<=1 for v in p['rgb']) or max(p['rgb'])<=0 or not 0<p['intensity']<=4 or not 0<=p['start']<p['end']<=32:raise ValueError('Emitter outside bounded policy')

def load(path=None):
    extension=Path(__file__).with_name('wotlk-emitter-profiles.json') if path is None else None
    path=Path(path) if path else Path(__file__).with_name('outdoor-light-profiles.json')
    data=json.loads(path.read_text())
    if data.get('version')!=1 or not isinstance(data.get('profiles'),dict):raise ValueError('Invalid emitter catalog')
    if extension and extension.exists():
        extra=json.loads(extension.read_text())
        if extra.get('version')!=1:raise ValueError('Invalid WotLK emitter extension')
        for name,p in extra['profiles'].items():
            previous=data['profiles'].get(name,{})
            data['profiles'][name]={**previous,**p,'variants':{**previous.get('variants',{}),**p.get('variants',{})}}
    for name,p in data['profiles'].items():
        if name!=name.lower().replace('/','\\') or not name.startswith('world\\') or not name.endswith('.m2'):raise ValueError('Noncanonical emitter asset')
        check(p)
        if 'emitters' in p:
            if not 1<=len(p['emitters'])<=16:raise ValueError('Emitter region count')
            for emitter in p['emitters']:check(emitter)
        if len(p['asset_sha256'])!=64:raise ValueError('Missing reviewed asset hash')
        for sha,v in p.get('variants',{}).items():
            if not re.fullmatch('[0-9a-f]{64}',sha) or sha==p['asset_sha256']:raise ValueError('Invalid variant asset hash: '+name)
            if set(v)-VARIANT_FIELDS or 'position' not in v or not v.get('position_basis'):raise ValueError('Invalid variant fields: '+name)
            check({**p,**v})
            if 'emitters' in v:
                if not 1<=len(v['emitters'])<=16:raise ValueError('Variant emitter region count')
                for emitter in v['emitters']:check(emitter)
        if 'injected_sha256' in p and p['injected_sha256'] not in p.get('variants',{}):raise ValueError('Unpinned injected model: '+name)
    return data['profiles']

def pinned(p):
    """{asset sha256: emitter fields} of every asset a profile covers, the profile's own first."""
    fields=(*EMITTER,*(['emitters'] if 'emitters' in p else []),*(['casts_shadows'] if 'casts_shadows' in p else []))
    result={p['asset_sha256']:{k:p[k] for k in fields}}
    for sha,v in p.get('variants',{}).items():
        result[sha]={k:v.get(k,p[k]) for k in EMITTER}
        if 'casts_shadows' in v or 'casts_shadows' in p:result[sha]['casts_shadows']=v.get('casts_shadows',p.get('casts_shadows'))
        if 'emitters' in v or 'emitters' in p:result[sha]['emitters']=v.get('emitters',p.get('emitters'))
    return result

def fallback(catalog,path,blob,native):
    # A supported native light always wins. Profiles never add a second emitter
    # to it, nor silently replace an updated model with the old bulb position.
    if path not in catalog:return native
    sha=hashlib.sha256(blob).hexdigest()
    # Exact generated models retain the authored float values, stable source
    # ID and parent-WMO duplicate suppression. Other native lights still win.
    if native and catalog[path].get('injected_sha256')!=sha:return native
    p=pinned(catalog[path]).get(sha)
    if p is None:raise ValueError('Emitter profile asset changed: '+path)
    return [(tuple(e['position']),tuple(c*e['intensity'] for c in e['rgb']),e['start'],e['end'],0x80000000+i,3,2 if e.get('casts_shadows',p.get('casts_shadows',False)) else 0)
            for i,e in enumerate(p.get('emitters',[p]))]

def deduplicate_parent_lights(records,owners):
    # Only compare a WMO child against native lights from ITS OWN parent.
    # Spatial/color resemblance alone must not erase a different nearby lamp.
    parents={}
    for r in records:
        kind,uid=owners[r[8]]
        if kind==2 and r[9]==1:parents.setdefault(uid,[]).append(r)
    out=[];removed=0
    for r in records:
        kind,uid=owners[r[8]];duplicate=False
        if r[9]==3 and kind==3:
            for native in parents.get(uid>>32,[]):
                distance=sum((r[i]-native[i])**2 for i in range(3))
                a=r[3:6];b=native[3:6];cosine=sum(x*y for x,y in zip(a,b))/math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
                if distance<=1.5**2 and cosine>=.92 and native[7]>=r[7]*.5:duplicate=True;break
        if duplicate:removed+=1
        else:out.append(r)
    return out,removed
