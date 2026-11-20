#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Standalone model generation and release-cache freshness, without changing a client."""
from pathlib import Path
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_art_layer as art
import build_packages as packages
import build_emitter_patch
import test_emitter_patch


class Delivery(unittest.TestCase):
    def test_higher_locale_reed_keeps_lighting_and_other_repairs(self):
        local = Path('Data/enUS')
        reed, flower = list(art.SCENERY_INPUTS)[:2]
        light = r'DBFilesClient\Light.dbc'
        sky = r'DBFilesClient\LightSkybox.dbc'
        class Assets:
            paths = [Path('Data/patch-z.mpq'), local / 'patch-enus-z.mpq']
            providers = {reed.lower(): 1}
            provider = providers.get
        payload = {name: b'local' for name in (reed, flower, light, sky)}
        effective, overrides = art.respect_locale_patches(Assets(), payload, local, 'enUS', 'y')
        self.assertEqual(set(effective), {flower, light, sky})
        self.assertEqual(overrides, [reed])
        Assets.providers[light.lower()] = 1
        effective, overrides = art.respect_locale_patches(Assets(), payload, local, 'enUS', 'y')
        self.assertEqual(set(effective), {flower})
        self.assertEqual(set(overrides), {light, reed})

    def test_release_rejects_stale_or_missing_builder_sources(self):
        manifest = {'variant': 'stock', 'build': {'sources': packages.install_world_cache.source_hashes()}}
        packages.check_variant_sources(manifest)
        for changed in ('outdoor-light-profiles.json', 'wotlk-emitter-profiles.json', 'world_scene_builder.py'):
            stale = copy.deepcopy(manifest)
            stale['build']['sources'][changed] = 'old'
            with self.assertRaisesRegex(SystemExit, changed):
                packages.check_variant_sources(stale)
        with self.assertRaisesRegex(SystemExit, 'regenerate'):
            packages.check_variant_sources({'variant': 'stock'})

    def test_packaged_imports_are_complete(self):
        packages.check_closure(packages.APP_FILES)

    def test_reviewed_generation_never_guesses_unknown_models_or_revisions(self):
        original = test_emitter_patch.LightPatch().model()
        name = 'world\\torch.m2'
        class Assets:
            providers = {name: None, 'world\\unknown_lantern.m2': None}
            def read(self, path):
                return original
            def fingerprint(self):
                return {}
        profile = {'position': [0, 0, 1], 'rgb': [1, .5, .1], 'intensity': 1,
                   'start': 1, 'end': 10, 'asset_sha256': '0' * 64,
                   'family': 'test', 'position_basis': 'reviewed fixture'}
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            path = Path(folder) / 'profiles.json'
            path.write_text(json.dumps({'version': 1, 'profiles': {name: profile}}))
            with patch.object(build_emitter_patch, 'profile', side_effect=AssertionError('must not guess')):
                with self.assertRaisesRegex(ValueError, 'No supported model changes'):
                    build_emitter_patch.build(Assets(), Path(folder) / 'out', path, reviewed_only=True)
            self.assertFalse((Path(folder) / 'out/patch-y.mpq').exists())

    def test_generated_model_matches_the_reviewed_light_cache_source(self):
        from outdoor_light_profiles import fallback, load
        from world_lights_builder import extract_m2
        from collections import Counter
        original = test_emitter_patch.LightPatch().model()
        name = 'world\\torch.m2'
        class Assets:
            providers = {name: None}
            def read(self, path):
                return original
            def fingerprint(self):
                return {}
            def origin(self, path):
                return 'fixture.mpq'
        profile = {'position': [0, 0, 1], 'rgb': [1, .5, .1], 'intensity': 1,
                   'start': 1, 'end': 10, 'asset_sha256': hashlib.sha256(original).hexdigest(),
                   'family': 'test', 'position_basis': 'reviewed fixture'}
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            folder = Path(folder)
            path = folder / 'profiles.json'
            path.write_text(json.dumps({'version': 1, 'profiles': {name: profile}}))
            build_emitter_patch.build(Assets(), folder / 'out', path, reviewed_only=True)
            payload = art.read_archive(folder / 'out/patch-y.mpq')
            self.assertEqual(set(payload), {name})
            catalog = load(folder / 'out/outdoor-light-profiles.json')
            self.assertEqual(fallback(catalog, name, original, []),
                             fallback(catalog, name, payload[name], extract_m2(payload[name], Counter())))


if __name__ == '__main__':
    unittest.main()
