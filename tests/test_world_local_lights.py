#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Synthetic format/transform tests plus sanitized native real-cache validation."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import collections,hashlib,json,math,pathlib,struct,subprocess,tempfile,unittest
import world_lights_builder as b
from test_world_light_placements import PlacementTests
ROOT=pathlib.Path(__file__).resolve().parent

def track(blob,value,fmt='<f'):
    # Append constant nested M2 arrays with one value; returns track header.
    timeoff=len(blob);blob.extend(struct.pack('<I',0))
    keyoff=len(blob);blob.extend(struct.pack(fmt,*value))
    timearray=len(blob);blob.extend(struct.pack('<II',1,timeoff))
    keyarray=len(blob);blob.extend(struct.pack('<II',1,keyoff))
    return struct.pack('<hh4I',0,-1,1,timearray,1,keyarray)

class BuilderTests(unittest.TestCase):
    def test_wmo_offsets_and_color(self):
        record=struct.pack('<4BI10f',0,1,1,1,0xff804020,1,2,3,2,0,0,-1,-.5,4,9)
        blob=struct.pack('<4sI',b'TLOM',len(record))+record
        stats=collections.Counter();lights=b.extract_wmo(blob,stats)
        self.assertEqual(len(lights),1);self.assertEqual(lights[0][0],(1,2,3))
        self.assertEqual(lights[0][2:4],(4,9));self.assertAlmostEqual(lights[0][1][0],256/255)
        changed=bytearray(blob);changed[8]=1
        self.assertEqual(b.extract_wmo(changed,stats),[])
        changed[8]=0;changed[10]=0;self.assertEqual(b.extract_wmo(changed,stats),[])
        with self.assertRaises(ValueError):b.extract_wmo(blob[:-1],stats)
    def test_instance_transform_scale(self):
        # 90deg Z rotation and scale2, translation(10,20,30).
        r=(b'x'*32,71,1,*([0]*6),0,-2,0,2,0,0,0,0,2,10,20,30)
        local=[((1,2,3),(.5,.4,.3),4,9,0,1,1)]
        x=b.instance_lights(local,r,'test')[0]
        self.assertEqual(x[:3],(6,22,36));self.assertEqual(x[6:8],(8,18))
        self.assertEqual(x,b.instance_lights(local,r,'test')[0])
        r2=list(r);r2[1]=72;self.assertNotEqual(x[8],b.instance_lights(local,r2,'test')[0][8])
        r2=list(r);r2[11]=-3
        with self.assertRaises(b.Unsupported):b.instance_lights(local,r2,'test')
    def test_m2_tracks_and_bone(self):
        blob=bytearray(304+156);blob[:8]=struct.pack('<4sI',b'MD20',264)
        struct.pack_into('<II',blob,264,1,304);struct.pack_into('<Hh3f',blob,304,1,-1,1,2,3)
        for index,fmt,value in [(2,'<3f',(.5,.25,.125)),(3,'<f',(2,)),(4,'<f',(3,)),(5,'<f',(7,)),(6,'<B',(1,))]:
            header=track(blob,value,fmt);blob[304+16+index*20:304+36+index*20]=header
        stats=collections.Counter();lights=b.extract_m2(blob,stats)
        self.assertEqual(len(lights),1);self.assertEqual(lights[0][:4],((1,2,3),(1,.5,.25),3,7))
        # A nonconstant key must never become an arbitrary frame-zero lamp.
        h=304+76;_,_,_,_,_,ok=struct.unpack_from('<hh4I',blob,h)
        ko=len(blob);blob.extend(struct.pack('<2f',2,4));to=len(blob);blob.extend(struct.pack('<2I',0,100))
        _,_,_,ot,_,_=struct.unpack_from('<hh4I',blob,h)
        struct.pack_into('<II',blob,ok,2,ko);struct.pack_into('<II',blob,ot,2,to)
        self.assertEqual(b.extract_m2(blob,stats),[]);self.assertEqual(stats['unsupported_animated_track'],1)
    def test_m2_native_transport_ignores_placement_scale(self):
        light=((1,2,3),(4,1.882352948,.627451003),16,40,0,2,2)
        for scale in (.25,1,5.3679737,16):
            r=(b'x'*32,71,1,*([0]*6),scale,0,0,0,scale,0,0,0,scale,10,20,30)
            placed=b.instance_lights([light],r,'world\\bonfire.m2')[0]
            self.assertEqual(placed[:3],(10+scale,20+2*scale,30+3*scale))
            self.assertEqual(placed[6:8],(1,12));self.assertEqual(placed[3],1)
            self.assertAlmostEqual(placed[4],1.882352948/4)
    def test_profile_variant_placement(self):
        # A light-less M2 gets its profile's emitter by asset hash; the HD and the stock asset of the
        # same placement share one source id, so caches of the two packs diff as values, not as
        # missing lights.
        import outdoor_light_profiles as profiles
        blob=bytearray(304);blob[:8]=struct.pack('<4sI',b'MD20',264);struct.pack_into('<II',blob,264,0,304)
        hd,stock=bytes(blob),bytes(blob+b'stock')
        self.assertEqual(b.extract_m2(hd,collections.Counter()),[]);self.assertEqual(b.extract_m2(stock,collections.Counter()),[])
        catalog={'world\\lamp.m2':{'asset_sha256':hashlib.sha256(hd).hexdigest(),'position':[0,0,4],'rgb':[1,.5,.25],'intensity':1,'start':1,'end':12,
                 'variants':{hashlib.sha256(stock).hexdigest():{'position':[0,.5,3],'position_basis':'test'}}}}
        r=(b'x'*32,71,1,*([0]*6),0,-2,0,2,0,0,0,0,2,10,20,30)
        a=b.instance_lights(profiles.fallback(catalog,'world\\lamp.m2',hd,[]),r,'world\\lamp.m2')[0]
        c=b.instance_lights(profiles.fallback(catalog,'world\\lamp.m2',stock,[]),r,'world\\lamp.m2')[0]
        self.assertEqual(a[:3],(10,20,38));self.assertEqual(c[:3],(9,20,36))
        self.assertEqual((a[8:],a[3:8]),(c[8:],c[3:8]))
        self.assertEqual(a[9:],(3,0))
    def test_constant_track_invalid(self):
        blob=bytearray(20);blob[:20]=track(blob,(1,))
        self.assertEqual(b.constant_track(blob,0,'<f'),(1,))
        struct.pack_into('<h',blob,0,2)
        with self.assertRaises(b.Unsupported):b.constant_track(blob,0,'<f')
        blob=bytearray(20);struct.pack_into('<hh4I',blob,0,0,-1,0,0,0,0)
        with self.assertRaises(b.Unsupported):b.constant_track(blob,0,'<f')
        self.assertEqual(b.constant_track(blob,0,'<f',(1,)),(1,))

def run_native(cache_dir=None):
    with tempfile.TemporaryDirectory(prefix='northlight-local-lights-') as tmp:
        p=pathlib.Path(tmp)
        records=[(0,0,0,1,.5,.2,2,10,1,1,1),(30,0,0,1,1,1,0,10,2,2,0),(1000,0,0,1,1,1,0,10,3,2,0)]
        good=struct.pack('<4sIII',b'FGL1',1,48,3)+b''.join(b.RECORD.pack(*r) for r in records)
        (p/'Alpha.fgl').write_bytes(good)
        (p/'Beta.fgl').write_bytes(struct.pack('<4sIII',b'FGL1',1,48,1)+b.RECORD.pack(25,0,0,1,1,1,0,5,4,2,0))
        bad=[good[:15],good[:-1],good+b'\0',b'BAD!'+good[4:]]
        for offset,fmt,value in [(4,'<I',3),(8,'<I',52),(12,'<I',1000001),(16,'<f',math.nan),(16+28,'<f',1),(16+48+32,'<Q',1)]:
            x=bytearray(good);struct.pack_into(fmt,x,offset,value);bad.append(bytes(x))
        for i,x in enumerate(bad):(p/f'bad{i}.fgl').write_bytes(x)
        binary=p/'test';command=['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*fp.test_include_flags(),str(ROOT/'test_world_local_lights.cpp'),'-o',str(binary)]
        subprocess.run(command,check=True)
        return json.loads(subprocess.check_output([str(binary),str(p),str(cache_dir or '')],text=True))

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--cache',type=pathlib.Path,default=None,help='optional cache built by this branch; synthetic coverage always runs');args=parser.parse_args()
    suite=unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(cls) for cls in (BuilderTests,PlacementTests))
    result=unittest.TextTestRunner().run(suite)
    if not result.wasSuccessful():raise SystemExit(1)
    native=run_native(args.cache)
    report={'status':'pass','python_tests':result.testsRun,'native':native,'runtime_gpu_test':False,
            'source_sha256':{name:hashlib.sha256(fp.tracked(name).read_bytes()).hexdigest() for name in ['world_lights_builder.py','world_light_placements.py','world_local_lights.h','test_world_local_lights.cpp','test_world_local_lights.py','test_world_light_placements.py']}}
    (fp.output_dir()/'world-local-lights-validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
