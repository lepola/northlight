# northlight-test: requires=client,stormlib
"""Profile safety, native preservation, parent identity and real-cache checks."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib,json,os,pathlib,struct,unittest
import outdoor_light_profiles as p
import world_lights_builder as builder

class Profiles(unittest.TestCase):
    def test_catalog_and_no_guessing(self):
        catalog=p.load();self.assertGreaterEqual(len(catalog),75)
        self.assertIn("world\\generic\\passivedoodads\\lights\\torch.m2",catalog)
        # This brazier contains proved additive fire islands; "smoker" in its
        # name does not make the visible flames a smoke-only source.
        reviewed_smoker='world\\expansion02\\doodads\\generic\\scarletonslaught\\scarleto_brazier_smoker.m2'
        self.assertFalse(any(any(s in name for s in ('unlit','broken','smoke','ember','off.m2'))
                             for name in catalog if name!=reviewed_smoker))
        self.assertIn('t_vfx_fire_anim02',catalog[reviewed_smoker]['position_basis'])
        for name,c in catalog.items():
            self.assertEqual(len(c['position']),3)
            self.assertTrue(c['position_basis'])
        self.assertEqual(p.fallback(catalog,'world\\unknown.m2',b'',[]),[])
    def test_native_wins_and_asset_revision(self):
        blob=b'reviewed';profile={'asset_sha256':hashlib.sha256(blob).hexdigest(),'position':[1,2,3],'rgb':[1,.5,.2],'intensity':1.2,'start':1,'end':12}
        catalog={'world\\lamp.m2':profile};native=[((0,0,0),(1,1,1),0,10,0,2,0)]
        self.assertIs(p.fallback(catalog,'world\\lamp.m2',b'updated',native),native)
        with self.assertRaises(ValueError):p.fallback(catalog,'world\\lamp.m2',b'updated',[])
        light=p.fallback(catalog,'world\\lamp.m2',blob,[])[0]
        self.assertEqual(light[4:],(0x80000000,3,0));self.assertEqual(light[:2],((1,2,3),(1.2,.6,.24)))
        record=(b'x'*32,123,3,*([0]*6),0,-2,0,2,0,0,0,0,2,10,20,30)
        placed=builder.instance_lights([light],record,'world\\lamp.m2')[0]
        self.assertEqual(placed[:3],(6,22,36));self.assertEqual(placed[6:8],(2,24));self.assertEqual(placed[9:],(3,0))
        legacy=builder.instance_lights(native,record,'world\\lamp.m2')[0]
        self.assertNotEqual(legacy[8],placed[8])
    def test_variant_by_asset_hash(self):
        hd,stock=b'hd model',b'stock model'
        profile={'asset_sha256':hashlib.sha256(hd).hexdigest(),'position':[1,2,3],'rgb':[1,.5,.2],'intensity':1.2,'start':1,'end':12,
                 'variants':{hashlib.sha256(stock).hexdigest():{'position':[4,5,6],'rgb':[.5,1,.25],'position_basis':'test'}}}
        catalog={'world\\lamp.m2':profile};native=[((0,0,0),(1,1,1),0,10,0,2,0)]
        self.assertEqual(p.fallback(catalog,'world\\lamp.m2',hd,[]),[((1,2,3),(1.2,.6,.24),1,12,0x80000000,3,0)])
        # Omitted variant fields are the profile's own (intensity, start, end).
        self.assertEqual(p.fallback(catalog,'world\\lamp.m2',stock,[]),[((4,5,6),(.6,1.2,.3),1,12,0x80000000,3,0)])
        self.assertIs(p.fallback(catalog,'world\\lamp.m2',stock,native),native)
        with self.assertRaises(ValueError):p.fallback(catalog,'world\\lamp.m2',b'other',[])
        self.assertEqual(list(p.pinned(profile)),[profile['asset_sha256'],*profile['variants']])
    def test_variant_validation(self):
        base=json.loads(fp.tracked('outdoor-light-profiles.json').read_text())
        name='world\\generic\\human\\passive doodads\\lamps\\stormwindstreetlamp01.m2'
        own=base['profiles'][name]['asset_sha256'];good={'position':[0,0,4],'position_basis':'test'}
        bad=[{own:good},{'A'*64:good},{'a'*63:good},{'a'*64:{'position':[0,0,4]}},{'a'*64:{'position_basis':'test'}},
             {'a'*64:{**good,'colour':[1,1,1]}},{'a'*64:{**good,'position':[0,0,400]}},{'a'*64:{**good,'rgb':[2,0,0]}},
             {'a'*64:{**good,'start':12}}]
        path=fp.output_dir()/'variant-catalog.json'
        for variants in bad:
            data=json.loads(json.dumps(base));data['profiles'][name]['variants']=variants
            path.write_text(json.dumps(data))
            with self.assertRaises((ValueError,KeyError),msg=variants):p.load(path)
        data['profiles'][name]['variants']={'a'*64:good};path.write_text(json.dumps(data))
        self.assertEqual(p.load(path)[name]['variants'],{'a'*64:good})
    def test_catalog_profiles_unchanged_by_variants(self):
        # Without its variants the catalog is byte for byte the reviewed 0.3.110..0.3.162 catalog,
        # so every HD profile places exactly the same emitter as before.
        data=json.loads(fp.tracked('outdoor-light-profiles.json').read_text())
        variants={name:x.pop('variants') for name,x in data['profiles'].items() if 'variants' in x}
        for x in data['profiles'].values():x.pop('casts_shadows',None)
        self.assertEqual(hashlib.sha256((json.dumps(data,indent=2)+'\n').encode()).hexdigest(),
                         '592dfdc945bd7ed81de005570bb18ef69916920f0b04dce0d4d44b4ba88c6f7a')
        self.assertEqual(len(variants),6)
        for name,v in variants.items():
            for sha,x in v.items():self.assertEqual(x['asset'],'stock 3.3.5a (data/common-2.mpq)',name)
    def test_explicit_shadow_policy(self):
        catalog=p.load()
        self.assertTrue(all(type(x['casts_shadows']) is bool for x in catalog.values()))
        self.assertTrue(catalog['world\\generic\\passivedoodads\\lights\\generaltorch01.m2']['casts_shadows'])
        self.assertFalse(catalog['world\\expansion02\\doodads\\crystalsongforest\\sparkles\\crystalsong_sparkles01.m2']['casts_shadows'])
        blob=b'reviewed';path='world\\test.m2';profile=dict(asset_sha256=hashlib.sha256(blob).hexdigest(),position=[0,0,1],rgb=[1,.5,.2],intensity=1,start=1,end=10)
        self.assertEqual(p.fallback({path:profile},path,blob,[])[0][-1],0)
        profile['casts_shadows']=True
        self.assertEqual(p.fallback({path:profile},path,blob,[])[0][-1],2)
        profile['casts_shadows']=False
        self.assertEqual(p.fallback({path:profile},path,blob,[])[0][-1],0)
        profile['casts_shadows']='yes'
        with self.assertRaises(ValueError):p.check(profile)
    def test_parent_identity_required(self):
        native=(0,0,0,1,.5,.2,1,12,1,1,1)
        child=(0,0,0,1,.5,.2,1,12,2,3,0)
        owners={1:(2,17),2:(3,(17<<32)|9)}
        self.assertEqual(p.deduplicate_parent_lights([native,child],owners),([native],1))
        owners[2]=(3,(18<<32)|9)
        self.assertEqual(p.deduplicate_parent_lights([native,child],owners),([native,child],0))
        owners[2]=(1,17)
        self.assertEqual(p.deduplicate_parent_lights([native,child],owners),([native,child],0))
        owners[2]=(3,(17<<32)|9);blue=(*child[:3],.1,.2,1,*child[6:])
        self.assertEqual(p.deduplicate_parent_lights([native,blue],owners),([native,blue],0))
    def test_staged_native_preserved(self):
        staged=os.environ.get('NORTHLIGHT_LIGHTS_STAGE')   # a staged world_lights_builder output to compare with the client's cache
        if not staged or not pathlib.Path(staged).exists():self.skipTest('No staged light cache (NORTHLIGHT_LIGHTS_STAGE)')
        root=fp.client_root();staged=pathlib.Path(staged)
        total=0
        for name in builder.MAPS:
            def records(path):
                b=path.read_bytes();magic,version,size,count=struct.unpack_from('<4sIII',b)
                self.assertEqual((magic,version,size),(b'FGL1',1,48));self.assertEqual(len(b),16+48*count)
                return list(builder.RECORD.iter_unpack(b[16:]))
            before=records(root/'world-cache/lights'/f'{name}.fgl');after=records(staged/f'{name}.fgl')
            by_id={r[8]:r for r in after};self.assertEqual(len(by_id),len(after))
            self.assertTrue(all(by_id[r[8]]==r for r in before));self.assertTrue(all(r[9]==3 and r[10]==0 for r in after if r[8] not in {x[8] for x in before}))
            total+=sum(r[9]==3 for r in after)
        self.assertGreaterEqual(total,14165)

if __name__=='__main__':unittest.main()
