#!/usr/bin/env python3
"""Compact authored fixtures: independent references, boundaries and native preservation."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]),
               str(Path(__file__).resolve().parents[1] / 'renderer')]
import outdoor_light_profiles as profiles

REFERENCES = json.loads((Path(__file__).parent / 'fixtures/compact-emitter-references.json').read_text())['fixtures']


def emitter(x=0., *, rgb=(1., .5, .2), intensity=.8, start=1., end=8., **extra):
    return dict(position=(x, 0., 2.), rgb=rgb, intensity=intensity, start=start, end=end, **extra)


class FixturePolicy(unittest.TestCase):
    def test_actual_catalog_matches_independent_fixture_reference(self):
        catalog = profiles.load()
        before = copy.deepcopy(catalog)
        total_before = total_after = merged = 0
        for name, p in catalog.items():
            original = p.get('emitters', [p])
            result = profiles.fixture_emitters(p)
            if len(original) > 1:
                total_before += len(original)
                total_after += len(result)
            if name not in REFERENCES:
                self.assertEqual(result, original, name)
                continue
            ref = REFERENCES[name]
            self.assertEqual(p['asset_sha256'], ref['asset_sha256'])
            self.assertEqual(len(original), ref['original_count'])
            self.assertEqual(len(result), 1)
            for key in ('position', 'rgb'):
                self.assertEqual(tuple(result[0][key]), tuple(ref[key]))
            for key in ('intensity', 'start', 'end', 'casts_shadows'):
                self.assertEqual(result[0][key], ref[key])
            merged += 1
        self.assertEqual((merged, total_before, total_after), (4, 43, 27))
        self.assertEqual(catalog, before)

    def test_diameter_boundary_and_nontransitive_chain(self):
        for positions, expected in (((0., 3.), 1), ((0., 3.000001), 2), ((0., 2., 4.), 3)):
            p = dict(emitters=[emitter(x) for x in positions])
            before = copy.deepcopy(p)
            self.assertEqual(len(profiles.fixture_emitters(p)), expected)
            self.assertEqual(p, before)

    def test_strongest_actual_radiance_and_range_bounds(self):
        weak = emitter(-1., rgb=(1., .1, .1), intensity=.3, start=.5, end=5.)
        strong = emitter(1., rgb=(.5, .25, .1), intensity=.8, start=2., end=9.)
        p = dict(emitters=[weak, strong])
        before = copy.deepcopy(p)
        result = profiles.fixture_emitters(p)[0]
        self.assertEqual(result['position'], (0., 0., 2.))
        self.assertEqual(result['rgb'], strong['rgb'])
        self.assertEqual(result['intensity'], strong['intensity'])
        self.assertEqual((result['start'], result['end']), (2., 9.))
        self.assertEqual(p, before)
        self.assertEqual(profiles.fixture_emitters(dict(emitters=[strong, weak])), [result])

    def test_shadow_policy_inheritance_and_mixed_policy(self):
        for shadow in (False, True):
            p = dict(casts_shadows=shadow, emitters=[emitter(), emitter(1.)])
            self.assertEqual(profiles.fixture_emitters(p)[0]['casts_shadows'], shadow)
            p['emitters'][1]['casts_shadows'] = not shadow
            self.assertIs(profiles.fixture_emitters(p), p['emitters'])

    def test_single_source_and_separate_fixtures(self):
        single = dict(emitters=[emitter()])
        self.assertIs(profiles.fixture_emitters(single), single['emitters'])
        fixture = dict(emitters=[emitter(-.5), emitter(.5)])
        # Two calls represent different model owners even at the same position.
        first = profiles.fixture_emitters(fixture)
        second = profiles.fixture_emitters(fixture)
        self.assertEqual(len(first) + len(second), 2)
        self.assertIsNot(first[0], second[0])

    def test_native_and_asset_revision_contract(self):
        blob, injected = b'reviewed original fixture', b'known injected fixture'
        original_sha = hashlib.sha256(blob).hexdigest()
        injected_sha = hashlib.sha256(injected).hexdigest()
        p = dict(asset_sha256=original_sha, **emitter(),
                 emitters=[emitter(-.5), emitter(.5)], injected_sha256=injected_sha,
                 variants={injected_sha: dict(position=(0., 0., 2.))})
        catalog = {'world\\fixture.m2': p}
        native = [((0., 0., 2.), (1., .5, .2), 1., 8., 0, 2, 2)]
        for unreviewed in (blob, b'unknown native revision'):
            self.assertIs(profiles.fallback(catalog, 'world\\fixture.m2', unreviewed, native), native)
        expected = profiles.fallback(catalog, 'world\\fixture.m2', blob, [])
        self.assertEqual(len(expected), 1)
        self.assertEqual(profiles.fallback(catalog, 'world\\fixture.m2', injected, native), expected)
        self.assertEqual(expected[0][4:], (0x80000000, 3, 0))
        with self.assertRaises(ValueError):
            profiles.fallback(catalog, 'world\\fixture.m2', b'unpinned fixture', [])
        self.assertIs(profiles.fallback(catalog, 'world\\other.m2', blob, native), native)


if __name__ == '__main__':
    unittest.main()
