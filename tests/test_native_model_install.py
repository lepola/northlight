#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Normal installation stages safe model generators, without external commands or game writes."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import native_model_lights as native
import build_packages


class NativeInstall(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=fp.output_dir())
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.assets = SimpleNamespace(client=self.root, locale='enUS')
        self.work = self.root / 'work'
        absent_creatures = patch.object(native.build_mount_emitters, 'build_creatures', side_effect=ValueError('No supported creature emitters'))
        absent_creatures.start()
        self.addCleanup(absent_creatures.stop)
        absent_glue = patch.object(native.build_glue_emitters, 'build', side_effect=ValueError(native.build_glue_emitters.EMPTY))
        absent_glue.start()
        self.addCleanup(absent_glue.stop)
        absent_objects = patch.object(native.build_gameobject_emitters, 'build', side_effect=ValueError(native.build_gameobject_emitters.EMPTY))
        absent_objects.start()
        self.addCleanup(absent_objects.stop)

    def test_fresh_client_stages_equipment_and_safe_spells_without_wdb(self):
        equipment = 'spells\\enchant.m2'
        blobs = {'equipment-lights-staged.mpq': {equipment: b'equipment'},
                 'spell-lights-staged.mpq': {'spells\\missile.m2': b'missile'}}
        with patch.object(native.build_equipment_emitters, 'build', return_value={'counts': {'patched': 1}}) as items, \
             patch.object(native.build_spell_emitters, 'build', return_value={'counts': {'patched': 1}}) as spells, \
             patch.object(native.build_mount_emitters, 'build') as mounts:
            payload, steps = native.build(self.assets, self.work, lambda path: blobs[path.name])
        self.assertEqual(payload, {equipment: b'equipment', 'spells\\missile.m2': b'missile'})
        self.assertEqual(spells.call_args.kwargs, {'excluded': {equipment}})
        self.assertEqual(items.call_count, 1)
        mounts.assert_not_called()
        self.assertIn('live particle', steps[-1]['skipped'])
        self.assertFalse((self.root / 'Data').exists())

    def test_no_qualified_models_is_a_supported_client(self):
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
             patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')):
            payload, steps = native.build(self.assets, self.work, lambda _: self.fail('No archive expected'))
        self.assertEqual(payload, {})
        self.assertEqual(len(steps), 6)

    def test_creature_sources_need_no_server_cache_and_keep_model_ownership(self):
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
             patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')), \
             patch.object(native.build_mount_emitters, 'build_creatures', return_value={'counts': {'patched': 1}}) as creatures:
            payload, steps = native.build(self.assets, self.work, lambda _: {'creature\\deathknightmount.m2': b'hoof-lights'})
        self.assertEqual(payload, {'creature\\deathknightmount.m2': b'hoof-lights'})
        self.assertEqual(creatures.call_args.kwargs, {'excluded': set()})
        self.assertEqual(next(s['models'] for s in steps if s['step']=='creature_model_lights'), 1)

    def test_dbc_payload_and_conflicting_model_owners_are_refused(self):
        for spell in ({'dbfilesclient\\spell.dbc': b'bad'}, {'spells\\same.m2': b'different'}):
            with self.subTest(spell=spell), \
                 patch.object(native.build_equipment_emitters, 'build', return_value={'counts': {}}), \
                 patch.object(native.build_spell_emitters, 'build', return_value={'counts': {}}):
                def read(path):
                    return {'spells\\same.m2': b'equipment'} if 'equipment' in path.name else spell
                with self.assertRaises(ValueError):
                    native.build(self.assets, self.work, read)

    def test_dynamic_glue_models_share_art_transaction_without_touching_installed_assets(self):
        path='interface\\glues\\models\\ui_hd\\ui_hd.m2'
        with patch.object(native.build_equipment_emitters,'build',side_effect=ValueError('No supported equipment emitters')), \
             patch.object(native.build_spell_emitters,'build',side_effect=ValueError('No supported spell emitters')), \
             patch.object(native.build_glue_emitters,'build',return_value={'counts':{'patched':1}}) as glue:
            payload,steps=native.build(self.assets,self.work,lambda _: {path:b'qualified-hd-backdrop'})
        self.assertEqual(payload,{path:b'qualified-hd-backdrop'})
        self.assertEqual(glue.call_args.kwargs,{'excluded':set()})
        self.assertEqual(next(s['models'] for s in steps if s['step']=='glue_model_lights'),1)
        self.assertFalse((self.root/'Data').exists())

    def test_supported_server_cache_adds_mounts_and_corrupt_cache_is_skipped(self):
        from test_mount_emitter_patch import cache
        source = self.root / 'Cache/WDB/enUS/creaturecache.wdb'
        source.parent.mkdir(parents=True)
        for blob in (cache(), b'BOMW' + bytes(21)):
            source.write_bytes(blob)
            with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
                 patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')), \
                 patch.object(native.build_mount_emitters, 'build', return_value={'counts': {'patched': 1}}) as mounts:
                payload, steps = native.build(self.assets, self.work, lambda _: {'creature\\mount.m2': b'mount'})
            self.assertEqual(bool(payload), blob == cache())
            self.assertEqual(mounts.call_count, int(blob == cache()))
            self.assertEqual(source.read_bytes(), blob)

    def test_unexpected_generator_failure_is_not_silently_hidden(self):
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('bad archive')):
            with self.assertRaisesRegex(ValueError, 'bad archive'):
                native.build(self.assets, self.work, lambda _: {})

    def test_every_generator_is_packaged_on_every_platform(self):
        build_packages.check_closure(build_packages.APP_FILES)
        for module in ('native_model_lights', 'build_equipment_emitters', 'build_spell_emitters', 'build_mount_emitters', 'build_gameobject_emitters', 'equipment_emitter_patch'):
            self.assertIn('renderer/' + module + '.py', build_packages.APP_FILES)

    def test_gameobjects_stage_with_disjoint_world_ownership(self):
        path = 'world\\skillactivated\\tradeskillnodes\\bush_ragveil.m2'
        owned = 'world\\lamp.m2'
        with patch.object(native.build_equipment_emitters, 'build', side_effect=ValueError('No supported equipment emitters')), \
             patch.object(native.build_spell_emitters, 'build', side_effect=ValueError('No supported spell emitters')), \
             patch.object(native.build_gameobject_emitters, 'build', return_value={'counts': {'patched': 1}}) as objects:
            payload, steps = native.build(self.assets, self.work, lambda _: {path: b'herb', owned: b'suppressed'}, excluded={owned})
        self.assertEqual(payload, {path: b'herb'})
        self.assertEqual(objects.call_args.kwargs, {'excluded': {owned}})
        self.assertEqual(next(s['models'] for s in steps if s['step'] == 'gameobject_model_lights'), 1)


if __name__ == '__main__':
    unittest.main()
