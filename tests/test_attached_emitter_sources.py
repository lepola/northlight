#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Rigid skin anchors, actual mesh radiance, complete bounded source coverage."""
from pathlib import Path
import math
import struct
import sys
import tempfile
import unittest
sys.path[:0] = [str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]
import northlight_paths
northlight_paths.use_source_modules()
from attached_emitter_sources import attached_sources,consolidate,mesh_sources
from build_spell_emitters import append_spell_lights
from build_equipment_emitters import build as build_equipment
from build_mount_emitters import build_creatures,creature_models
from equipment_emitter_patch import append_bone_lights
from test_spell_emitter_patch import Assets,append,model,native_sample,raw_track
from test_mount_emitter_patch import assets as mount_assets,table
from mpq import Archive
import build_gameobject_emitters as gameobjects
import world_emitter_index


def mesh_model():
    blob = bytearray(512)
    blob[:4] = b'MD20'
    struct.pack_into('<I',blob,4,264)
    struct.pack_into('<II',blob,44,2,304)
    struct.pack_into('<iIh',blob,304,-1,0x200,-1)
    struct.pack_into('<iIh',blob,392,-1,0x200,0)
    def header(at,fmt,rows):
        offset = append(blob,b''.join(struct.pack(fmt,*r) for r in rows))
        struct.pack_into('<II',blob,at,len(rows),offset)
    header(60,'<3f4B4B3f4f',[(x,y,0,255,0,0,0,0,0,0,0,0,0,1,u,v,0,0) for x,y,u,v in ((0,0,0,0),(1,0,1,0),(0,1,0,1))])
    name = b'textures\\actual-blue.blp\0'
    name_offset = append(blob,name)
    header(80,'<4I',[(0,0,len(name),name_offset)])
    header(112,'<2H',[(1,4)])
    header(120,'<H',[(1,)])  # Actual model bone1, not vertex's index0.
    header(128,'<H',[(0,)])
    header(136,'<h',[(0,)])
    header(152,'<h',[(-1,)])
    skin = bytearray(48)
    skin[:4] = b'SKIN'
    def skin_header(at,fmt,rows):
        offset = append(skin,b''.join(struct.pack(fmt,*r) for r in rows))
        struct.pack_into('<II',skin,at,len(rows),offset)
    skin_header(4,'<H',[(0,),(1,),(2,)])
    skin_header(12,'<H',[(0,),(1,),(2,)])
    skin_header(20,'<4B',[(0,0,0,0)]*3)
    skin_header(28,'<10H7f',[(0,0,0,3,0,3,1,0,1,0,*([0.]*7))])
    skin_header(36,'<2B11H',[(0,0,0,0,0,65535,0,0,1,0,0,65535,0)])
    assets = Assets(bytes(blob),{'creature\\glow00.skin':bytes(skin)})
    return assets,bytes(blob)


class AttachedEmission(unittest.TestCase):
    def test_billboard_mesh_retains_original_bone_and_position(self):
        assets, original = mesh_model()
        billboard = bytearray(original)
        struct.pack_into('<I', billboard, 392+4, 8)
        struct.pack_into('<3f', billboard, 392+76, 3, 4, 5)
        sources, rejected = attached_sources(assets, 'creature\\glow.m2', billboard)
        self.assertEqual(rejected['mesh_batches'], {})
        self.assertEqual((sources[0]['bone'], sources[0]['position']), (1, (1/3, 1/3, 0)))
        patched = append_spell_lights(bytes(billboard), sources)
        self.assertEqual(patched[304:480], billboard[304:480])
        light = struct.unpack_from('<I', patched, 268)[0]
        self.assertEqual(struct.unpack_from('<H', patched, light+2)[0], 1)

    def test_gameobject_dbc_resolution_staging_and_herb_ownership(self):
        assets, original = mesh_model()
        herb = 'world\\skillactivated\\tradeskillnodes\\bush_ragveil.m2'
        prop = 'world\\prop.m2'
        names = b'\0World\\SkillActivated\\TradeskillNodes\\Bush_Ragveil.mdx\0World\\prop.m2\0Spells\\orb.mdx\0world\\..\\bad.mdx\0world\\house.wmo\0'
        paths = [v for v in names.split(b'\0') if v]
        rows = [[i+1, names.index(p), *([0]*17)] for i, p in enumerate(paths)]
        rows.append([6949, 1, *([0]*17)])
        assets.extra.update({herb: original, prop: original, 'spells\\orb.m2': original,
                            herb[:-3]+'00.skin': assets.extra['creature\\glow00.skin'],
                            prop[:-3]+'00.skin': assets.extra['creature\\glow00.skin'],
                            'dbfilesclient\\gameobjectdisplayinfo.dbc': table(rows, 19, names)})
        ambient = 'world\\unlisted_by_dbc.m2'
        assets.extra[ambient] = original
        assets.extra[ambient[:-3]+'00.skin'] = assets.extra['creature\\glow00.skin']
        assets.providers[ambient] = 0
        self.assertEqual(set(gameobjects.models(assets)), {herb, prop, 'spells\\orb.m2', ambient})
        self.assertEqual(gameobjects.models(assets)[ambient]['displays'], [])
        self.assertEqual(gameobjects.models(assets)[herb]['displays'], [1, 6949])
        self.assertIsNone(world_emitter_index.profile(assets, herb, original, {}))
        before = dict(assets.extra)
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            report = gameobjects.build(assets, Path(folder), excluded={prop, 'spells\\orb.m2'})
            self.assertEqual(set(report['models']), {herb, ambient})
            with Archive(Path(folder)/'gameobject-lights-staged.mpq') as archive:
                self.assertEqual({n for n in archive.names() if not n.startswith('(')}, {herb, ambient})
        self.assertEqual(assets.extra, before)

    def test_world_particle_without_display_row_keeps_original_lifecycle(self):
        original, particle = model()
        path = 'world\\ambient\\animated_lamp.m2'
        source = Assets(original, {'dbfilesclient\\gameobjectdisplayinfo.dbc': table([], 19)})
        source.providers[path] = 0
        before = dict(source.extra)
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            report = gameobjects.build(source, Path(folder))
            self.assertEqual(set(report['models']), {path})
            self.assertEqual(report['models'][path]['displays'], [])
            with Archive(Path(folder)/'gameobject-lights-staged.mpq') as archive:
                patched = archive.read(path)
            light = struct.unpack_from('<I', patched, 268)[0]
            for t in (0, 50, 149, 150, 249, 250, 300, 1100):
                self.assertAlmostEqual(native_sample(patched, light+76, '<f', t),
                                       native_sample(original, particle+176, '<f', t)/10*.35)
                self.assertEqual(native_sample(patched, light+136, '<B', t),
                                 native_sample(original, particle+456, '<B', t))
        self.assertEqual(source.blob, original)
        self.assertEqual(source.extra, before)

    def test_mesh_uses_real_skin_bone_and_uv_texture_not_white(self):
        assets,original = mesh_model()
        emitters,rejected = attached_sources(assets,'creature\\glow.m2',original)
        self.assertEqual(rejected['mesh_batches'],{})
        self.assertEqual(len(emitters),1)
        source = emitters[0]
        self.assertEqual((source['bone'],source['rgb']),(1,(0,0,1)))
        self.assertAlmostEqual(source['intensity'],.5*.35)
        self.assertEqual(source['emission'],'energy')
        self.assertEqual(source['position'],(1/3,1/3,0))
        patched = append_spell_lights(original,emitters)
        self.assertEqual(struct.unpack_from('<II',patched,44),struct.unpack_from('<II',original,44))
        restored = bytearray(patched[:len(original)])
        for a,b in ((20,28),(264,272)):
            restored[a:b] = original[a:b]
        self.assertEqual(restored,original)

    def test_mesh_proof_rejects_lit_replaceable_hidden_and_deforming(self):
        assets,original = mesh_model()
        cases=[]
        bad=bytearray(original)
        struct.pack_into('<H',bad,struct.unpack_from('<I',bad,116)[0],0)
        cases.append((bad,assets))
        bad=bytearray(original)
        struct.pack_into('<I',bad,struct.unpack_from('<I',bad,84)[0],11)
        cases.append((bad,assets))
        skin=bytearray(assets.extra['creature\\glow00.skin'])
        struct.pack_into('<H',skin,struct.unpack_from('<I',skin,32)[0],102)
        cases.append((original,Assets(original,{'creature\\glow00.skin':bytes(skin)})))
        bad=bytearray(original)
        vertices=struct.unpack_from('<I',bad,64)[0]
        bad[vertices+12:vertices+16]=bytes((128,127,0,0))
        skin=bytearray(assets.extra['creature\\glow00.skin'])
        skin[struct.unpack_from('<I',skin,24)[0]+1]=1
        cases.append((bad,Assets(bad,{'creature\\glow00.skin':bytes(skin)})))
        for blob,source in cases:
            with self.assertRaisesRegex(ValueError,'No proven attached'):
                attached_sources(source,'creature\\glow.m2',blob)

    def test_disabled_or_zero_scale_particles_cannot_emit(self):
        for original,_ in (model(enabled=[([0],[0])]),):
            with self.assertRaisesRegex(ValueError,'No proven attached'):
                attached_sources(Assets(original),'creature\\glow.m2',original)
        original,particle=model()
        blob=bytearray(original)
        _,_,_,keys=struct.unpack_from('<4I',blob,particle+292)
        struct.pack_into('<2f',blob,keys,0,0)
        with self.assertRaisesRegex(ValueError,'No proven attached'):
            attached_sources(Assets(blob),'creature\\glow.m2',blob)
        original,_=model()
        blob=bytearray(original)
        animation=append(blob,bytes(64))  # External animation with plausible inline-looking offsets.
        struct.pack_into('<II',blob,28,1,animation)
        with self.assertRaisesRegex(ValueError,'External animation'):
            attached_sources(Assets(blob),'creature\\glow.m2',blob)

    def test_consolidation_preserves_energy_and_distinct_hooves(self):
        make=lambda bone,x,rgb,power:{'bone':bone,'position':(x,0,0),'rgb':rgb,'intensity':power,'textures':['actual.blp'],'particles':[],'start':.5,'end':8.}
        inputs=[make(0,0,(1,0,0),.3),make(0,.1,(0,0,1),.7),make(1,0,(0,1,0),.02)]
        result=consolidate(inputs)
        self.assertEqual(len(result),2)
        for k in range(3):
            self.assertAlmostEqual(sum(s['rgb'][k]*s['intensity'] for s in inputs),sum(s['rgb'][k]*s['intensity'] for s in result))
        self.assertEqual({s['bone'] for s in result},{0,1})
        self.assertEqual(len(consolidate([make(i,0,(1,0,0),.01) for i in range(16)])),16)
        with self.assertRaisesRegex(ValueError,'none silently omitted'):
            consolidate([make(i,0,(1,0,0),.01) for i in range(17)])

    def test_weak_glow_remains_weaker_than_strong_actual_texture(self):
        assets,original=mesh_model()
        strong=attached_sources(assets,'creature\\glow.m2',original)[0][0]
        class Weak(Assets):
            def read(self,path):
                data=super().read(path)
                if path.endswith('.blp'):
                    data=bytearray(data)
                    data[-4:]=bytes((30,0,0,100))
                return data
        weak=attached_sources(Weak(original,assets.extra),'creature\\glow.m2',original)[0][0]
        self.assertLess(weak['intensity'],strong['intensity']*.01)
        self.assertEqual(weak['rgb'],(0,0,1))

    def test_fire_pulse_multiplies_rgb_without_changing_lifecycle(self):
        original,particle=model()
        assets=Assets(original)
        emitters,_=attached_sources(assets,'creature\\glow.m2',original)
        emitters[0]['emission']='fire'
        patched=append_spell_lights(original,emitters)
        light=struct.unpack_from('<I',patched,268)[0]
        self.assertEqual(struct.unpack_from('<h',patched,light+56)[0],1)
        for t in (0,50,100,149,150,200,249,250,300):
            self.assertAlmostEqual(native_sample(patched,light+76,'<f',t),native_sample(original,particle+176,'<f',t)/10*.35)
            self.assertEqual(native_sample(patched,light+136,'<B',t),native_sample(original,particle+456,'<B',t))

    def test_creature_scope_needs_no_server_cache_and_keeps_dbc_bytes(self):
        assets,_=mount_assets()
        before=dict(assets.extra)
        self.assertEqual(set(creature_models(assets)),{'creature\\real_mount.m2','creature\\wrong_ghost.m2'})
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            report=build_creatures(assets,Path(folder))
            self.assertEqual(report['counts']['scanned'],2)
            self.assertEqual(report['counts']['patched'],2)
            self.assertFalse(report['server_creature_mappings_inferred'])
            with Archive(Path(folder)/'creature-lights-staged.mpq') as mpq:
                self.assertEqual({p for p in mpq.names() if not p.startswith('(')},set(report['models']))
        self.assertEqual(assets.extra,before)

    def test_equipment_scope_covers_item_paths_outside_objectcomponents(self):
        original,_=model()
        source=Assets(original,{'dbfilesclient\\itemvisualeffects.dbc':table([],2)})
        source.providers['item\\misc\\actual-glow.m2']=0
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            report=build_equipment(source,Path(folder))
            self.assertEqual(set(report['models']),{'item\\misc\\actual-glow.m2'})


if __name__=='__main__':
    unittest.main()
