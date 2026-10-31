#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Active HD scene discovery, native ownership and original animated-source staging."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from attached_emitter_sources import attached_sources
from build_spell_emitters import append_spell_lights
import build_glue_emitters as glue
from mpq import Archive
from test_spell_emitter_patch import Assets as SourceAssets, append, model, native_sample

SCENE = 'interface\\glues\\models\\ui_scourge\\ui_scourge.m2'


def frozen_native():
    original, _ = model(rate=[([0, 267, 433, 700], [.6, .7, .55, .6])], sequence=0)
    sources, _ = attached_sources(SourceAssets(original), SCENE, original)
    native = bytearray(append_spell_lights(original, [dict(sources[0], position=(i, 2, 3)) for i in range(4)]))
    offset = struct.unpack_from('<I', native, 268)[0]
    # One native RGB key, despite a varying intensity: the original 12340 condition.
    for at in (offset + 156 + 64, offset + 156 + 72):
        array = struct.unpack_from('<I', native, at)[0]
        struct.pack_into('<I', native, array, 1)
    return bytes(native)


def rgb_value(blob, record, time):
    mode, clock, nt, ot, nk, ok = struct.unpack_from('<hh4I', blob, record + 56)
    count, key = struct.unpack_from('<II', blob, ok)
    # This test oracle accepts only an actually constant color function.
    values = [struct.unpack_from('<3f', blob, key + i * 12) for i in range(count)]
    if not all(v == values[0] for v in values):
        raise AssertionError('RGB function changed')
    return values[0]


def native_product(blob, record, time, alpha, settled, previous):
    # Independent transcription of the native producer gate, not the repair writer.
    nt, ot = struct.unpack_from('<II', blob, record + 60)
    key_count = struct.unpack_from('<I', blob, ot)[0] if nt else 0
    if nt > 1 or nt == 1 and key_count > settled:
        intensity = native_sample(blob, record + 76, '<f', time)
        previous = tuple(v * intensity * alpha for v in rgb_value(blob, record, time))
    enabled = native_sample(blob, record + 136, '<B', time)
    return previous if enabled else (0., 0., 0.)


class Assets(SourceAssets):
    def __init__(self, files):
        super().__init__(b'', files)
    def read(self, path):
        if path.endswith(('.m2', '.lua', '.xml')) and path not in self.extra:
            raise FileNotFoundError(path)
        return super().read(path)
    def warnings(self):
        return []


class GlueEmitters(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=fp.output_dir())
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)

    def test_existing_native_scene_is_preserved_and_reported_without_archive(self):
        original, _ = model()
        emitters, _ = attached_sources(SourceAssets(original), SCENE, original)
        native = append_spell_lights(original, emitters)
        assets = Assets({SCENE: native})
        before = dict(assets.extra)
        with self.assertRaisesRegex(ValueError, '^' + glue.EMPTY + '$'):
            glue.build(assets, self.output)
        report = json.loads((self.output / 'glue-lights-report.json').read_text())
        self.assertEqual(report['counts']['native'], 1)
        self.assertEqual(report['counts']['patched'], 0)
        self.assertEqual(report['existing_native_models_preserved'][SCENE]['sha256'], hashlib.sha256(native).hexdigest())
        self.assertEqual(report['existing_native_models_preserved'][SCENE]['native_lights'], 1)
        self.assertEqual(assets.extra, before)
        self.assertFalse((self.output / 'glue-lights-staged.mpq').exists())

    def test_native_four_light_flicker_repair_matches_intensity_and_alpha_without_new_lights(self):
        original = frozen_native()
        patched, repairs = glue.repair_native_flicker(original)
        self.assertEqual([r['light'] for r in repairs], [1])
        self.assertEqual(struct.unpack_from('<I', patched, 264)[0], 4)
        old = struct.unpack_from('<I', original, 268)[0] + 156
        new = struct.unpack_from('<I', patched, 268)[0] + 156
        cached = native_product(original, old, 0, 1, 0, (0, 0, 0))
        changed = False
        for time in (0, 100, 149, 150, 200, 249, 250, 267, 433, 700, 999, 1000, 1267):
            for alpha in (0., .25, .5, 1.):
                expected = tuple(v * native_sample(original, old + 76, '<f', time) * alpha
                                 for v in rgb_value(original, old, time))
                if not native_sample(original, old + 136, '<B', time):
                    expected = (0., 0., 0.)
                actual = native_product(patched, new, time, alpha, 1, cached)
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b, delta=1e-8)
                changed |= actual != native_product(original, old, time, alpha, 1, cached)
                # Before the fully opaque optimization, original and repaired agree.
                self.assertEqual(native_product(patched, new, time, alpha, 0, cached),
                                 native_product(original, old, time, alpha, 0, cached))
        self.assertTrue(changed, 'The destructive frozen-RGB control must differ')
        self.assertEqual(patched[:264], original[:264])
        self.assertEqual(patched[272:len(original)], original[272:])
        glue.verify_native_repair(original, patched, repairs)
        self.assertEqual(glue.repair_native_flicker(patched), (patched, []))

    def test_native_repair_validator_rejects_source_track_bone_color_and_count_mutations(self):
        original = frozen_native()
        patched, repairs = glue.repair_native_flicker(original)
        record = struct.unpack_from('<I', patched, 268)[0] + 156
        mutations = []
        bad = bytearray(patched);bad[304] ^= 1;mutations.append(bad)
        bad = bytearray(patched)
        intensity_keys = struct.unpack_from('<I', bad, record + 92)[0]
        intensity_value = struct.unpack_from('<I', bad, intensity_keys + 4)[0]
        struct.pack_into('<f', bad, intensity_value, .9);mutations.append(bad)
        bad = bytearray(patched);struct.pack_into('<h', bad, record + 2, 99);mutations.append(bad)
        bad = bytearray(patched);struct.pack_into('<I', bad, 264, 5);mutations.append(bad)
        bad = bytearray(patched)
        keys = struct.unpack_from('<I', bad, record + 72)[0]
        key = struct.unpack_from('<I', bad, keys + 4)[0]
        struct.pack_into('<f', bad, key + 12, .25);mutations.append(bad)
        for changed in mutations:
            with self.assertRaises(ValueError):
                glue.verify_native_repair(original, bytes(changed), repairs)

    def test_native_dynamic_rgb_and_static_intensity_are_not_rewritten(self):
        source = frozen_native()
        record = struct.unpack_from('<I', source, 268)[0] + 156
        animated = bytearray(source)
        for at in (record + 64, record + 72):
            array = struct.unpack_from('<I', animated, at)[0]
            struct.pack_into('<I', animated, array, 2)
        array = struct.unpack_from('<I', animated, record + 72)[0]
        key = struct.unpack_from('<I', animated, array + 4)[0]
        struct.pack_into('<3f', animated, key + 12, .25, .5, .75)
        self.assertEqual(glue.repair_native_flicker(bytes(animated)), (bytes(animated), []))
        constant = bytearray(source)
        array = struct.unpack_from('<I', constant, record + 92)[0]
        count, key = struct.unpack_from('<II', constant, array)
        for i in range(count):
            struct.pack_into('<f', constant, key + i * 4, .3)
        self.assertEqual(glue.repair_native_flicker(bytes(constant)), (bytes(constant), []))

    def test_unsupported_external_native_intensity_is_preserved_without_repair(self):
        blob = bytearray(frozen_native())
        animation = append(blob, bytes(64))  # No inline-data bit: values cannot prove its clock.
        struct.pack_into('<II', blob, 28, 1, animation)
        record = struct.unpack_from('<I', blob, 268)[0] + 156
        struct.pack_into('<h', blob, record + 78, -1)
        with self.assertRaisesRegex(ValueError, 'External native light animation'):
            glue.repair_native_flicker(bytes(blob))
        assets = Assets({SCENE: bytes(blob)})
        with self.assertRaisesRegex(ValueError, '^' + glue.EMPTY + '$'):
            glue.build(assets, self.output)
        self.assertEqual(assets.read(SCENE), bytes(blob))
        self.assertFalse((self.output / 'glue-lights-staged.mpq').exists())

    def test_native_repair_roundtrips_only_original_model_and_retains_four_lights(self):
        original = frozen_native()
        report = glue.build(Assets({SCENE: original}), self.output)
        self.assertEqual((report['counts']['patched'], report['counts']['native_repaired'],
                          report['counts']['native_lights_added']), (1, 1, 0))
        with Archive(self.output / 'glue-lights-staged.mpq') as archive:
            self.assertEqual({p for p in archive.names() if not p.startswith('(')}, {SCENE})
            patched = archive.read(SCENE)
        self.assertEqual(struct.unpack_from('<I', patched, 264)[0], 4)
        glue.verify_native_repair(original, patched, report['models'][SCENE]['native_flicker_repairs'])

    def test_changed_hd_model_at_same_path_is_remeasured_and_keeps_native_clock(self):
        stock, _ = model()
        stock_emitters, _ = attached_sources(SourceAssets(stock), SCENE, stock)
        stock_native = append_spell_lights(stock, stock_emitters)
        hd, particle = model(rate=[([0, 100, 200, 300], [0, 20, 10, 0])])
        hd = bytearray(hd)
        struct.pack_into('<3f', hd, particle + 8, 2, 3, 4)
        hd = bytes(hd)
        self.assertNotEqual(hashlib.sha256(stock_native).digest(), hashlib.sha256(hd).digest())
        assets = Assets({SCENE: hd})
        report = glue.build(assets, self.output)
        self.assertEqual(report['counts']['patched'], 1)
        value = report['models'][SCENE]
        self.assertEqual(value['before_sha256'], hashlib.sha256(hd).hexdigest())
        self.assertEqual(value['emitters'][0]['position'], (2, 3, 4))
        self.assertEqual(value['emitters'][0]['rgb'], (0, 0, 1))
        with Archive(self.output / 'glue-lights-staged.mpq') as archive:
            self.assertEqual({p for p in archive.names() if not p.startswith('(')}, {SCENE})
            patched = archive.read(SCENE)
        record = struct.unpack_from('<I', patched, 268)[0]
        for time in (0, 50, 100, 150, 200, 250, 300):
            source = value['emitters'][0]
            self.assertAlmostEqual(native_sample(patched, record + 76, '<f', time),
                                   native_sample(hd, particle + 176, '<f', time) / source['rate'] * source['intensity'])
            self.assertEqual(native_sample(patched, record + 136, '<B', time),
                             native_sample(hd, particle + 456, '<B', time))
        restored = bytearray(patched[:len(hd)])
        for start, end in ((20, 28), (264, 272)):
            restored[start:end] = hd[start:end]
        self.assertEqual(restored, hd)
        self.assertEqual(assets.read(SCENE), hd)

    def test_new_hd_scene_and_literal_external_interface_model_are_discovered(self):
        original, _ = model()
        new = 'interface\\glues\\models\\ui_custom_hd\\ui_custom_hd.m2'
        referenced = 'interface\\custom\\my_backdrop.m2'
        lua = 'interface\\gluexml\\custom.lua'
        assets = Assets({new: original, referenced: original,
                         lua: b'SetCharSelectBackground("Interface\\\\Custom\\\\My_Backdrop.mdx")'})
        found, discovery = glue.candidates(assets)
        self.assertEqual(set(found), {new, referenced})
        self.assertEqual(found[referenced]['references'], [{'file': lua, 'line': 1}])
        self.assertEqual(discovery['glue_source_sha256'][lua], hashlib.sha256(assets.extra[lua]).hexdigest())
        report = glue.build(assets, self.output)
        self.assertEqual(report['counts']['patched'], 2)
        self.assertEqual(set(report['models']), {new, referenced})

    def test_mixed_native_and_hd_scenes_stage_only_the_new_source_owner(self):
        original, _ = model()
        emitters, _ = attached_sources(SourceAssets(original), SCENE, original)
        native = append_spell_lights(original, emitters)
        custom = 'interface\\glues\\models\\ui_new_hd\\ui_new_hd.m2'
        assets = Assets({SCENE: native, custom: original})
        report = glue.build(assets, self.output)
        self.assertEqual((report['counts']['native'], report['counts']['patched']), (1, 1))
        with Archive(self.output / 'glue-lights-staged.mpq') as archive:
            self.assertEqual({p for p in archive.names() if not p.startswith('(')}, {custom})
        self.assertEqual(assets.read(SCENE), native)

    def test_shared_world_references_traversal_and_missing_models_fail_closed(self):
        lua = 'interface\\gluexml\\custom.lua'
        missing = 'interface\\custom\\missing.m2'
        assets = Assets({lua: b'SetCharSelectBackground("World\\\\Shared.m2")\n'
                         b'SetCharSelectBackground("Interface\\\\Glues\\\\..\\\\Escape.m2")\n'
                         b'SetCharSelectBackground("Interface\\\\Custom\\\\Missing.m2")'})
        found, discovery = glue.candidates(assets)
        self.assertEqual(set(found), {missing})
        self.assertEqual(len(discovery['refused_references']), 2)
        with self.assertRaisesRegex(ValueError, '^' + glue.EMPTY + '$'):
            glue.build(assets, self.output)
        report = json.loads((self.output / 'glue-lights-report.json').read_text())
        self.assertEqual(report['counts']['unsupported'], 1)
        self.assertFalse((self.output / 'glue-lights-staged.mpq').exists())

    def test_unrelated_interface_widgets_are_not_treated_as_backdrops(self):
        original, _ = model()
        widget = 'interface\\widgets\\glow.m2'
        assets = Assets({widget: original, 'interface\\gluexml\\widgets.xml':
                         b'<Model file="Interface\\Widgets\\Glow.m2"/>'})
        found, discovery = glue.candidates(assets)
        self.assertEqual(found, {})
        self.assertEqual({p.lower() for p in discovery['refused_references']}, {widget})

    def test_disabled_hd_emission_and_existing_owner_are_not_staged(self):
        disabled, _ = model(enabled=[([0], [0])])
        with self.assertRaisesRegex(ValueError, '^' + glue.EMPTY + '$'):
            glue.build(Assets({SCENE: disabled}), self.output)
        self.assertFalse((self.output / 'glue-lights-staged.mpq').exists())
        original, _ = model()
        with self.assertRaisesRegex(ValueError, '^' + glue.EMPTY + '$'):
            glue.build(Assets({SCENE: original}), self.output, excluded={SCENE})
        report = json.loads((self.output / 'glue-lights-report.json').read_text())
        self.assertEqual(report['counts']['scanned'], 0)
        self.assertEqual(report['excluded_existing_owners'], [SCENE])


if __name__ == '__main__':
    unittest.main()
