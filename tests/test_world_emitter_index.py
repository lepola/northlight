#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Renderer-owned metadata is bounded, source-pinned and separate from native M2 limits."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import world_emitter_index as index
from m2_emitter_patch import append_lights, verify_lights
from test_world_emitter_coverage import fixture


PATH = 'world\\anonymous.m2'


def source():
    return dict(position=[0., 0., 2.], rgb=[.25, .5, 1.], intensity=.175,
                start=1., end=8., emission='energy')


def document(blob, emitters=None):
    return {'version': 1, 'owner': 'northlight-local-direct', 'models': {
        PATH: {'asset_sha256': hashlib.sha256(blob).hexdigest(), 'basis': 'measured',
               'emitters': emitters if emitters is not None else [source()]}}}


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


class WorldEmitterIndex(unittest.TestCase):
    def setUp(self):
        self.assets = fixture()
        self.blob = self.assets.read(PATH)
        self.document = document(self.blob)

    def read(self, value=None, payload=None):
        self.assets.files[index.ARCHIVE_PATH] = payload if payload is not None else encoded(
            self.document if value is None else value)
        return index.Index(self.assets)

    def test_canonical_payload_digest_flags_and_absolute_energy(self):
        value = self.read()
        self.assertEqual(value.origin, 'mpq')
        self.assertEqual(value.payload(), encoded(self.document))
        self.assertEqual(value.digest, hashlib.sha256(value.payload()).hexdigest())
        entry = value.get(PATH, self.blob)
        position, rgb, start, end, identity, kind, flags = index.records(entry)[0]
        self.assertEqual(position, (0., 0., 2.))
        self.assertEqual(rgb, (.25*.175, .5*.175, .175))
        self.assertEqual((start, end, identity, kind, flags), (1., 8., 0x80000000, 3, 0))
        for emission, expected in (('fire', index.CAST_SHADOW|index.FIRE), ('candle', index.CAST_SHADOW|index.CANDLE), ('energy', 0)):
            self.assertEqual(index.flags({'emission': emission}), expected)

    def test_explicit_shadow_intent_survives_compact_build_and_roundtrip(self):
        for emission in ('energy','fire','candle'):
            for shadow in (False,True):
                emitter=dict(source(),emission=emission,casts_shadows=shadow)
                measured={'asset_sha256':hashlib.sha256(self.blob).hexdigest(),'basis':'measured','emitters':[emitter]}
                with patch.object(index,'profile',return_value=measured):payload,_=index.build(self.assets)
                value=self.read(payload=payload);entry=value.get(PATH,self.blob)
                self.assertEqual(entry['emitters'][0]['casts_shadows'],shadow)
                self.assertEqual(bool(index.records(entry)[0][-1]&index.CAST_SHADOW),shadow)
                self.assertEqual(value.payload(),payload)
        for bad in (0,1,None,'false'):
            value=copy.deepcopy(self.document);value['models'][PATH]['emitters'][0]['casts_shadows']=bad
            with self.assertRaises(ValueError):self.read(value)

    def test_reviewed_mixed_shadow_fixtures_keep_their_energy_and_policies(self):
        sources=[dict(source(),casts_shadows=False),dict(source(),position=[.5,0.,2.],casts_shadows=True)]
        reviewed={'emitters':sources}
        with patch.object(index,'pinned',return_value={hashlib.sha256(self.blob).hexdigest():reviewed}):
            value=index.profile(self.assets,PATH,self.blob,{PATH:{'family':'energy'}})
        self.assertEqual(len(value['emitters']),2)
        for actual,expected in zip(value['emitters'],sources):
            for field in ('position','rgb','intensity','start','end','casts_shadows'):
                self.assertEqual(actual[field],expected[field])

    def test_source_hash_changes_are_refused_without_changing_the_pin(self):
        value = self.read()
        with self.assertRaisesRegex(ValueError, 'hash changed'):
            value.get(PATH, self.blob + b'changed')
        self.assertEqual(value.get(PATH, self.blob), self.document['models'][PATH])

    def test_malformed_schema_is_refused(self):
        malformed = [[], None, {}, {'version': 1, 'owner': 'northlight-local-direct'},
                     {**self.document, 'models': []}, {**self.document, 'models': None}]
        for value in (None, [], {}, {'asset_sha256': '0'*64},
                      {**self.document['models'][PATH], 'emitters': None},
                      {**self.document['models'][PATH], 'emitters': {}},
                      {**self.document['models'][PATH], 'emitters': [None]}):
            malformed.append({**self.document, 'models': {PATH: value}})
        for value in malformed:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.read(payload=json.dumps(value).encode())

    def test_owner_and_version_are_explicit(self):
        for field, invalid in (('version', 0), ('version', 2), ('version', True),
                               ('owner', 'client-native'), ('owner', ''), ('owner', None)):
            with self.subTest(field=field, value=invalid), self.assertRaises(ValueError):
                self.read({**self.document, field: invalid})

    def test_paths_must_be_canonical_world_or_spell_models(self):
        invalid = ('item\\torch.m2', 'creature\\torch.m2', 'world\\torch.mdx',
                   'World\\torch.m2', 'world/torch.m2', 'world\\..\\torch.m2',
                   'spells\\.\\torch.m2', 'world\\\\torch.m2', 'world\\bad/../torch.m2',
                   'world\\bad\x00.m2', 'world\\C:torch.m2', 'world\\.m2')
        for path in invalid:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.read({**self.document, 'models': {path: self.document['models'][PATH]}})

    def test_hash_shape_is_strict(self):
        for digest in ('', '0'*63, '0'*65, 'G'*64, 'A'*64, 0, None):
            value = copy.deepcopy(self.document)
            value['models'][PATH]['asset_sha256'] = digest
            with self.subTest(digest=digest), self.assertRaises(ValueError):
                self.read(value)

    def test_invalid_vectors_energy_and_ranges_are_refused(self):
        invalid = [('position', [0, 1]), ('position', [0, 0, 1024.001]),
                   ('position', [0, 0, float('nan')]), ('position', [0, 0, float('inf')]),
                   ('rgb', [0, 0]), ('rgb', [0, 0, 0]), ('rgb', [-.001, .5, 1]),
                   ('rgb', [0, .5, 1.001]), ('intensity', 0), ('intensity', -.1),
                   ('intensity', 4.001), ('intensity', float('inf')),
                   ('intensity', '1'), ('intensity', True), ('start', -1),
                   ('start', 8), ('end', 1), ('end', 32.001), ('emission', 'white')]
        for field, invalid_value in invalid:
            value = copy.deepcopy(self.document)
            value['models'][PATH]['emitters'][0][field] = invalid_value
            with self.subTest(field=field, value=invalid_value), self.assertRaises(ValueError):
                self.read(payload=json.dumps(value).encode())

    def test_metadata_accepts_64_sources_and_1024_anchors_without_weakening_native_limits(self):
        sources = [source() for _ in range(64)]
        sources[-1]['position'] = [-1024., 1024., 1024.]
        value = self.read(document(self.blob, sources))
        self.assertEqual(len(value.get(PATH, self.blob)['emitters']), 64)
        for emitters in ([], sources + [source()]):
            with self.subTest(count=len(emitters)), self.assertRaises(ValueError):
                self.read(document(self.blob, emitters))
        native = [source() for _ in range(16)]
        native[-1]['position'] = [100., -100., 100.]
        patched = append_lights(self.blob, native)
        verify_lights(self.blob, patched, native)
        for emitters in (native + [source()], [dict(source(), position=[100.001, 0, 0])], sources):
            with self.subTest(native_count=len(emitters)), self.assertRaises(ValueError):
                append_lights(self.blob, emitters)

    def test_metadata_byte_and_model_count_limits(self):
        with self.assertRaisesRegex(ValueError, 'size'):
            self.read(payload=b' '*(32*1024*1024+1))
        with self.assertRaisesRegex(ValueError, 'count'):
            self.read({**self.document, 'models': dict.fromkeys('world\\%d.m2' % i for i in range(20001))})

    def test_build_is_deterministic_compact_and_does_not_scan_unreferenced_spells(self):
        self.assets.files.update({'spells\\referenced.m2': self.blob, 'spells\\unused.m2': self.blob,
                                  'creature\\unused.m2': self.blob, 'world\\z.m2': self.blob})
        self.assets.providers = dict.fromkeys(reversed(self.assets.files))
        measured = {'asset_sha256': hashlib.sha256(self.blob).hexdigest(), 'basis': 'measured',
                    'partial': False, 'emitters': [dict(source(), evidence=['private audit'], raw_intensity=2)]}
        with patch.object(index, 'profile', return_value=measured) as profile:
            payload, counts = index.build(self.assets, ['spells\\referenced.m2', 'spells\\..\\bad.m2'])
            called = [call.args[1] for call in profile.call_args_list]
        self.assertEqual(called, ['spells\\referenced.m2', PATH, 'world\\z.m2'])
        self.assertEqual(counts, {'scanned': 3, 'measured': 3, 'emitters': 3})
        self.assertEqual(payload, encoded(json.loads(payload)))
        self.assertEqual(set(json.loads(payload)['models'][PATH]['emitters'][0]),
                         {'position', 'rgb', 'intensity', 'start', 'end', 'emission'})
        self.assets.providers = dict.fromkeys(sorted(self.assets.files))
        with patch.object(index, 'profile', return_value=measured):
            self.assertEqual(index.build(self.assets, ['spells\\referenced.m2'])[0], payload)

    def test_only_referenced_missing_spells_are_discovered_lazily(self):
        value = self.read()
        spell = 'spells\\referenced.m2'
        measured = {'asset_sha256': hashlib.sha256(self.blob).hexdigest(), 'basis': 'measured',
                    'emitters': [dict(source(), evidence=['private audit'])]}
        with patch.object(index, 'profile', return_value=measured) as discover:
            self.assertIsNone(value.get('world\\missing.m2', self.blob))
            self.assertIsNone(value.get('item\\missing.m2', self.blob))
            discover.assert_not_called()
            self.assertEqual(value.get(spell, self.blob), measured)
            self.assertEqual(value.get(spell, self.blob), measured)
            self.assertEqual(discover.call_count, 1)
        payload = value.payload()
        self.assertEqual(set(json.loads(payload)['models']), {PATH, spell})
        self.assertNotIn(b'evidence', payload)
        self.assertEqual(value.digest, hashlib.sha256(payload).hexdigest())
        self.assertEqual(self.read(payload=payload).payload(), payload)
        with self.assertRaises(ValueError):
            value.get(spell, self.blob+b'changed')

    def test_absent_mpq_metadata_is_generated_without_native_model_mutation(self):
        original = dict(self.assets.files)
        value = index.Index(self.assets)
        self.assertEqual(value.origin, 'generated-for-mpq')
        self.assertEqual(set(value.models), {PATH})
        self.assertEqual(value.payload(), index.build(self.assets)[0])
        self.assertEqual(self.assets.files, original)


if __name__ == '__main__':
    unittest.main()
