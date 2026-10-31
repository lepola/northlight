#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Winning WMO lamp evidence, native deduplication and non-destructive scene tags."""
import collections
import math
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import client_archives,mpq,northlight_paths as fp,wmo_lamp_emission as lamps
from world_scene_builder import Builder
from world_lights_builder import extract_wmo,instance_lights

ROOT='world/wmo/azeroth/buildings/stormwind/stormwind.wmo'.replace('/',chr(92))
GROUP=ROOT[:-4]+'_094.wmo'
TEXTURE='dungeons/textures/doodads/stormwindlampglass.blp'.replace('/',chr(92))

class Assets:
    def __init__(self,files):self.files=files;self.missing=set();self.stats=collections.Counter();self.clamped=set();self.unsupported=set()
    def read(self,path):
        path=path.lower().replace('/',chr(92))
        if path not in self.files:raise FileNotFoundError(path)
        return self.files[path]

class WmoLamps(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        client=fp.client_root();winners={}
        for path in client_archives.chain(client,'all',client_archives.detect_locale(client)):
            try:
                with mpq.Archive(path) as archive:
                    for name in (ROOT,GROUP,TEXTURE):
                        if archive.has(name):winners[name]=archive.read(name)
            except OSError:
                if not client_archives.is_custom_patch(path):raise
        if len(winners)!=3:raise AssertionError('Winning WMO fixture members missing')
        cls.files=winners;cls.root=winners[ROOT];cls.group=winners[GROUP]
    def detect(self,root=None,group=None,files=None):
        assets=Assets(files or self.files)
        return lamps.analyze(self.root if root is None else root,self.group if group is None else group,assets.read,94)
    def test_winning_material_geometry_and_unpinned_variant(self):
        lights,faces=self.detect();self.assertEqual((len(lights),len(faces)),(27,216))
        self.assertEqual(len(set(faces.values())),27)
        self.assertTrue(all(sum(v==s[4] for v in faces.values())==8 for s in lights))
        self.assertTrue(all(s[5:]==(3,18) and .1<s[2]<s[3]<24 for s in lights))
        self.assertTrue(all(.20<s[1][0]<.4 and s[1][0]>s[1][1]>s[1][2]>0 for s in lights))
        changed=bytearray(self.root);changed[changed.index(b'TMOM')+8]^=1
        self.assertEqual(self.detect(bytes(changed)),(lights,faces))
        candidate=next(iter(lamps.candidates(self.root)));changed=bytearray(self.root);changed[changed.index(b'TMOM')+8+64*candidate]&=~0x10
        self.assertEqual(self.detect(bytes(changed)),([],{}))
        with mock.patch('world_scene_builder.decode_blp',return_value=(32,32,bytes([0,0,0,255])*1024)):
            self.assertEqual(self.detect(),([],{}))
        # Later UV sets must not replace the material's first texture coordinates.
        group=bytearray(self.group);header=group.index(b'PGOM');size=struct.unpack_from('<I',group,header+4)[0]
        extra=b'VTOM'+struct.pack('<I',8)+struct.pack('<2f',float('nan'),float('nan'))
        group[header+8+size:header+8+size]=extra;struct.pack_into('<I',group,header+4,size+len(extra))
        self.assertEqual(self.detect(group=bytes(group)),(lights,faces))
        corrupt=bytearray(self.group);offset=min(faces);base=corrupt.index(b'IVOM')+8;struct.pack_into('<H',corrupt,base+2*(offset+1),struct.unpack_from('<H',corrupt,base+2*offset)[0])
        reduced,tagged=self.detect(group=bytes(corrupt));self.assertNotIn(offset,tagged);self.assertLess(len(reduced),27)
    def test_native_same_owner_dedup_and_placement(self):
        sources,_=self.detect();native=extract_wmo(self.root,collections.Counter());self.assertTrue(native)
        self.assertEqual(lamps.merge_native(native,sources),native+sources)
        s=sources[0];matching=(s[0],s[1],s[2],s[3],0,1,3)
        native_source=(*matching[:2],0.,matching[3],*matching[4:])
        merged=lamps.merge_native([native_source],[s]);self.assertEqual(len(merged),1)
        self.assertEqual(merged[0][:4],native_source[:4]);self.assertEqual(merged[0][4:],(s[4],1,19))
        self.assertEqual(lamps.merge_native([matching,matching],[s]),[matching,matching])
        self.assertEqual(lamps.merge_native([matching],[s,s]),[matching])
        offset=((s[0][0]+s[2]*.5,*s[0][1:]),*matching[1:])
        self.assertEqual(lamps.merge_native([offset],[s]),[offset])
        different=(s[0],(0.,0.,1.),s[2],s[3],0,1,3);self.assertEqual(lamps.merge_native([different],[s]),[different,s])
        far=((s[0][0]+3,s[0][1],s[0][2]),s[1],s[2],s[3],0,1,3);self.assertEqual(lamps.merge_native([far],[s]),[far,s])
        matrix=(-.7826081514,.6225146651,0.,-.6225146651,-.7826081514,0.,0.,0.,1.);translation=(-8931.633789,539.293640,101.957024)
        record=(bytes(32),10047,2,*([0.]*6),*matrix,*translation);placed=instance_lights(sources,record,ROOT)
        for point in ((-8743.35,404.60,103.60),(-8737.11,396.75,103.60)):
            self.assertLess(min(math.dist(point,p[:3]) for p in placed),.08)
    def test_shared_geometry_remains_intact_and_tags_are_per_fixture(self):
        assets=Assets(self.files);normal=Builder(assets).wmo(ROOT)
        with mock.patch.object(lamps,'candidates',return_value={}):baseline=Builder(Assets(self.files)).wmo(ROOT)
        self.assertEqual(len(normal[1]),len(baseline[1]));self.assertEqual([t[:3] for t in normal[1]],[t[:3] for t in baseline[1]])
        tagged=collections.Counter(t[3] for t in normal[1] if len(normal[2][t[3]])==4)
        self.assertEqual(len(tagged),27);self.assertTrue(all(n==8 for n in tagged.values()))
        builder=Builder(Assets(self.files));builder.add_mesh(normal[:3],((1,0,0),(0,1,0),(0,0,1)),(0,0,0),'wmo')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'model.fgs';builder.write_scene(path);data=path.read_bytes()
        magic,version,nv,nt,nm=struct.unpack_from('<4s4I',data);self.assertEqual((magic,version),(b'FGS2',2));offset=20+32*nv+16*nt;count=0
        for _ in range(nm):
            *_,cutoff,size=struct.unpack_from('<3fIIfI',data,offset);offset+=28+size
            count+=-8388607<=cutoff<=-1 and cutoff==int(cutoff)
        self.assertEqual(count,27);self.assertEqual(offset,len(data))

if __name__=='__main__':unittest.main()
