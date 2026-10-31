#!/usr/bin/env python3
"""Inspect F12 GPU readbacks offline; no graphics device or game process."""
import sys; from pathlib import Path; sys.path.insert(1, str(Path(__file__).resolve().parents[1] / 'scripts'))  # build_environment
import build_environment
from pathlib import Path
import json, math, struct, sys

class Buffer:
    def __init__(self,path):
        self.data=Path(path).read_bytes()
        magic,self.width,self.height,self.format,stride=struct.unpack_from('<5I',self.data)
        layouts={113:('<4e',8),114:('<f',4),21:('<4B',4),22:('<4B',4)}
        if magic!=0x31524746 or self.format not in layouts: raise ValueError('Unsupported FGR')
        self.layout,self.stride=layouts[self.format]
        if not self.width or not self.height or stride!=self.stride or len(self.data)!=20+self.width*self.height*stride:raise ValueError('Invalid FGR length')
    def at(self,x,y):
        return struct.unpack_from(self.layout,self.data,20+(y*self.width+x)*self.stride)
    def sample(self,u,v):
        return self.at(min(self.width-1,max(0,int(u*self.width))),min(self.height-1,max(0,int(v*self.height))))
    def stats(self):
        samples=[self.sample((x+.5)/64,(y+.5)/64) for y in range(64) for x in range(64)]
        finite=[v for v in samples if all(math.isfinite(c) for c in v)]
        return {'size':[self.width,self.height],'format':self.format,'samples':len(samples),'finite':len(finite),
                'min':[min(v[c] for v in finite) for c in range(len(samples[0]))] if finite else [],
                'max':[max(v[c] for v in finite) for c in range(len(samples[0]))] if finite else [],
                'mean':[sum(v[c] for v in finite)/len(finite) for c in range(len(samples[0]))] if finite else []}

def constant_bank(raw):
    if len(raw) not in (68*16,79*16):raise ValueError('Invalid constants length: expected 68 or 79 float4 registers')
    return [struct.unpack_from('<4f',raw,i) for i in range(0,len(raw),16)]

def analyze(directory,capture=1):
    root=Path(directory);prefix=f'capture-{capture}-'
    buffers={p.name[len(prefix):-4]:Buffer(p) for p in root.glob(prefix+'*.fgr')}
    report={'capture':capture,'buffers':{k:v.stats() for k,v in buffers.items()}}
    raw=(root/(prefix+'constants.f32')).read_bytes()
    c=constant_bank(raw)
    # Keep every captured register, including material c73..c78, without treating
    # the final bank as the earlier local-light batches' constants.
    report['constant_bank']={'register_count':len(c),'raw_hex':raw.hex(),'stage':'unversioned'}
    report['input_provenance']={'surface_depth_source':'half-resolution-distance','depth_reconstructed':True,
        'full_resolution_depth_supplied':'depth' in buffers,'invented_fog_environment':False,
        'per_source_bank_missing':True,'material_inputs_complete':False,
        'authentic_source_replay':False,'whole_scene_replay':False}
    report['camera']=c[15][:3];report['source']=c[16];report['source_rgb']=c[17][:3]
    report['legacy_fog']=c[25];report['projection']=c[1]
    def affine(p,first):return tuple(sum(p[k]*c[first+k][j] for k in range(3))+c[first+3][j] for j in range(3))
    points=[]
    for y in range(16):
        for x in range(16):
            u,v=(x+.5)/16,(y+.5)/16
            z=buffers['distance'].sample(u,v)[0]
            if not math.isfinite(z) or not c[0][2]<z<min(128,c[0][3]*.99):continue
            view=((2*(u-.5*c[0][0])-1)/c[1][0]*z,(1-2*(v-.5*c[0][1]))/c[1][1]*z,c[1][2]*z)
            p=affine(view,3);q=affine(p,11);su,sv=q[0]*.5+.5,.5-q[1]*.5
            inside=0<su<1 and 0<sv<1 and 0<q[2]<1
            stored=buffers['shadow-far'].sample(su,sv)[0]
            n=buffers['normals'].sample(u,v)
            points.append({'uv':[u,v],'distance':z,'world':p,'normal':n,'moon_dot':sum(n[i]*c[16][i] for i in range(3)),
              'in_shadow_map':inside,'shadow_receiver':q[2],'shadow_stored':stored,'hard_shadow_visible':not inside or q[2]-.00032<=stored,
              'light':buffers['light'].sample(u,v),'temporal_light':buffers['temporal-light'].sample(u,v),
              'baseline':buffers['baseline'].sample(u,v),'scene':buffers['scene'].sample(u,v),'fog':buffers['fog'].sample(u,v)})
    report['surface_samples']=points;report['surface_count']=len(points)
    report['hard_shadow_lit_count']=sum(p['hard_shadow_visible'] for p in points)
    return report

if __name__=='__main__':
    root=Path(sys.argv[1]) if len(sys.argv)>1 else build_environment.ARCHIVE/'runtime-diagnostics-0.3.63'
    capture=int(sys.argv[2]) if len(sys.argv)>2 else 1
    print(json.dumps(analyze(root,capture),indent=2,allow_nan=False))
