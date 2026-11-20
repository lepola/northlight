#!/usr/bin/env python3
# northlight-test: requires=stormlib,client
"""Offline zero-opacity regressions, including the actual Orgrimmar emitters."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import argparse,struct
from m2_visibility import hidden_batches
from world_scene_builder import Assets, Builder, array, unpack


def fixture(values=(0,), kind=0, sequences=1, global_id=-1):
    b=bytearray(512);b[:4]=b'MD20';struct.pack_into('<I',b,4,264)
    struct.pack_into('<II',b,20,1,420);struct.pack_into('<I',b,420,1000)
    struct.pack_into('<I',b,28,sequences);struct.pack_into('<II',b,88,1,180)
    struct.pack_into('<II',b,144,1,210);struct.pack_into('<h',b,210,0)
    struct.pack_into('<Hh4I',b,180,kind,global_id,1,220,1,228)
    struct.pack_into('<II',b,220,len(values),240);struct.pack_into('<II',b,228,len(values),300)
    for i,v in enumerate(values):struct.pack_into('<I',b,240+4*i,i*100);struct.pack_into('<h',b,300+2*i,v)
    batch=(16,0,0,0,0,65535,0,0,1,0,0,0,0)
    return b,batch


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--locale',help='Client locale when several are installed')
    args=parser.parse_args()
    b,batch=fixture();assert hidden_batches(b,[batch])=={0}
    b,_=fixture((0,0),kind=1);assert hidden_batches(b,[batch])=={0}
    for kwargs in [dict(values=(32767,)),dict(values=(0,32767),kind=1),dict(kind=2),dict(kind=3),dict(values=()),dict(sequences=2),dict(global_id=1),dict(global_id=-2)]:
        b,_=fixture(**kwargs);assert not hidden_batches(b,[batch]),kwargs
    b,_=fixture(global_id=0,sequences=4);assert hidden_batches(b,[batch])=={0}
    b,_=fixture();struct.pack_into('<I',b,232,999999);assert not hidden_batches(b,[batch])
    b,_=fixture();struct.pack_into('<h',b,210,-1);assert not hidden_batches(b,[batch])
    b,_=fixture();struct.pack_into('<I',b,188,999999);assert not hidden_batches(b,[batch])
    b,_=fixture();assert not hidden_batches(b[:200],[batch])
    for field,value in [(11,65535),(11,2)]:
        altered=list(batch);altered[field]=value;assert not hidden_batches(b,[altered])
    # The game applies the texture weight to any textured batch, not only to a single-texture one.
    b,_=fixture();altered=list(batch);altered[8]=2;assert hidden_batches(b,[altered])=={0}
    altered[8]=0;assert not hidden_batches(b,[altered])   # untextured: the weight is not applied
    # Shadow casters need colour alpha * weight >= 0.55: bounded by each track's largest key over all sequences.
    b,_=fixture((16384,),kind=1);assert not hidden_batches(b,[batch]) and hidden_batches(b,[batch],.55)=={0}
    b,_=fixture((16384,32767),kind=1,sequences=2);assert not hidden_batches(b,[batch],.55)   # a key reaches 1
    b,_=fixture((32767,));assert not hidden_batches(b,[batch],.55)
    # Colour-track alpha (header +72, alpha track at +20 of a 40-byte colour; batch colour index at [5]).
    def coloured(value):
        b,_=fixture((32767,));struct.pack_into('<II',b,72,1,440);struct.pack_into('<Hh4I',b,460,0,-1,1,480,1,488)
        struct.pack_into('<II',b,480,1,496);struct.pack_into('<II',b,488,1,500);struct.pack_into('<I',b,496,0);struct.pack_into('<h',b,500,value)
        return b
    colored=list(batch);colored[5]=0
    assert hidden_batches(coloured(0),[colored])=={0}
    assert hidden_batches(coloured(16384),[colored],.55)=={0} and not hidden_batches(coloured(16384),[colored])
    assert not hidden_batches(coloured(32767),[colored],.55)
    assert not hidden_batches(coloured(0),[batch])   # colour index 65535: no colour alpha
    # Hidden first pass cannot suppress a later visible pass sharing the submesh.
    b,_=fixture();visible=list(batch);visible[11]=65535;assert hidden_batches(b,[batch,visible])=={0}
    assets=Assets(locale=args.locale)
    try:
        builder=Builder(assets)
        for leaf in ['orgrimmarfloatingembers','orgrimmarsmokeemitter']:
            path='world\\kalimdor\\orgrimmar\\passivedoodads\\orgrimmarbonfire\\'+leaf+'.m2'
            data=assets.read(path);skin=assets.read(path[:-3]+'00.skin');n,o=unpack('<II',skin,36)
            assert hidden_batches(data,array(skin,o,n,'<2B11H'))=={0}
            mesh=builder.m2(path);assert len(mesh[1])==0
        assert assets.stats['excluded_permanently_invisible_m2_batches']==2
    finally:assets.close()
    print('PASS visibility: constant/global zero omitted; visible, animated, missing, external/out-of-bounds, nonlinear and multi-texture retained; actual smoke/ember hidden geometry excluded.')


if __name__=='__main__':main()
