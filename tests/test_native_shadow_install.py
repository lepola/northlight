#!/usr/bin/env python3
# northlight-test:
"""Detailed native shadows are disabled transactionally without rewriting other WoW settings."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

spec = importlib.util.spec_from_file_location('shadow_installer', fp.RENDERER/'windows-package/install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class NativeShadows(unittest.TestCase):
    def test_only_native_detailed_shadow_value_changes(self):
        source = b'// SET extShadowQuality "4"\r\nSET locale "ruRU"\r\n\tset\tEXTSHADOWQUALITY\t"5" // keep\r\nSET shadowLevel "0"\r\nSET extShadowQuality 3\r\n'
        expected = source.replace(b'"5"', b'"0"').replace(b'Quality 3', b'Quality 0')
        self.assertEqual(installer.native_shadow_config(source), expected)
        self.assertEqual(installer.native_shadow_config(expected), expected)

    def test_missing_key_bom_newline_and_legacy_bytes(self):
        for source in (b'', b'\xef\xbb\xbf', b'SET locale "enGB"', b'SET note "\xff\xfe"\r\n'):
            result = installer.native_shadow_config(source)
            self.assertTrue(result.startswith(source))
            self.assertIn(b'SET extShadowQuality "0"', result)
            self.assertEqual(installer.native_shadow_config(result), result)
        self.assertNotIn(b'\n', installer.native_shadow_config(b'SET locale "enGB"\r\n').replace(b'\r\n', b''))

    def test_utf16_encoding_is_preserved_and_malformed_input_refused(self):
        for bom, encoding in ((b'\xff\xfe','utf-16-le'), (b'\xfe\xff','utf-16-be')):
            source = bom+'SET locale "ruRU"\r\nSET extShadowQuality "5"\r\n'.encode(encoding)
            expected = bom+'SET locale "ruRU"\r\nSET extShadowQuality "0"\r\n'.encode(encoding)
            self.assertEqual(installer.native_shadow_config(source), expected)
        with self.assertRaises(UnicodeDecodeError): installer.native_shadow_config(b'\xff\xfeX')

    def test_fresh_upgrade_repeat_and_restore(self):
        with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
            base = Path(folder)
            package = base/'package'
            (package/'payload').mkdir(parents=True)
            blob = b'MZ Northlight renderer 0.3.216; PROXY module=%ls root=%ls'
            (package/'payload/d3d9.dll').write_bytes(blob)
            (package/'payload-manifest.json').write_text(json.dumps([{'path':'d3d9.dll','sha256':hashlib.sha256(blob).hexdigest()}]))
            for index, old in enumerate((None, b'SET extShadowQuality "5"\r\nSET locale "enGB"\r\n')):
                client = base/str(index)
                client.mkdir()
                (client/'wow.exe').write_bytes(b'untouched')
                config = client/installer.GAME_CONFIG
                if old is not None:
                    config.parent.mkdir()
                    config.write_bytes(old)
                backup = installer.install(client, package, backend='native')
                self.assertIn(b'extShadowQuality "0"', config.read_bytes())
                plan, _, _ = installer.payload_plan(client, package, backend='native')
                self.assertFalse(plan)
                with self.assertRaisesRegex(ValueError, 'generated locally'):
                    installer.payload_plan(client, package, backend='native', extra={'wtf/config.wtf':b'foreign'})
                installer.restore(client, backup)
                self.assertEqual(config.read_bytes() if config.exists() else None, old)
            client = base/'0'
            backup = installer.install(client, package, backend='native')
            config = client/installer.GAME_CONFIG
            edited = config.read_bytes()+b'SET locale "deDE"\n'
            config.write_bytes(edited)
            installer.restore(client, backup)
            self.assertEqual(config.read_bytes(), edited)


if __name__ == '__main__': unittest.main()
