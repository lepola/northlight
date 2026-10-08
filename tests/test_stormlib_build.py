#!/usr/bin/env python3
# northlight-test: requires=zig,stormlib-src slow
"""scripts/build_stormlib.py: the source tree matches its pin; both targets build, verify (Windows: PE32+
AMD64, the 9 exports mpq.py binds, only KERNEL32/USER32/WININET/UCRT imports, CreateFileW and no ANSI
file calls; macOS: arm64 @rpath dylib linking only libSystem; neither embeds a build path) and equal
the pinned bytes when rebuilt from an empty zig cache. On an arm64 Mac the dylib writes and reads back an
MPQ through ctypes. With a client configured, the art layer built with the new dylib equals our known
layers (0.3.199, storm bands and weather textures, rain alpha .65, colour .80, a one-texel core at every mip down to 4 wide: the dev HD client cc2e6520..., the stock client ffd666f0...); its game-derived output is deleted
after hashing. The Windows DLL is never loaded or run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
import ctypes as C
import hashlib
import json
import os
import platform
import shutil
import tempfile
import subprocess
import unittest

import build_stormlib as bs  # noqa: E402

PINS = json.loads((fp.RENDERER / 'package-pins.json').read_text())['stormlib']
KNOWN_ART = {'cc2e6520f24e1799ab8a35f925b1a3d27750261436919e310bd847e169e2e299',   # 0.3.199 storm bands + weather textures (rain colour .80, alpha .65, one-texel cores): HD-2
             'ffd666f0101ad7e9bc3d9192f1299e70e06ef91bca5ab97dfa54785650314278'}   # stock
OUT = fp.output_dir()
built = {}


def build(target):
    if target not in built:
        built[target] = bs.build(target, OUT / target)
    return built[target]


class StormLibBuild(unittest.TestCase):
    def test_source_tree_pinned(self):
        self.assertEqual(bs.tree_sha256(bs.source_root()), PINS['source_tree_sha256'])

    def test_targets_verify_and_match_pins(self):
        for target in bs.TARGETS:
            with self.subTest(target=target):
                lib = build(target)
                problems, info = bs.verify(lib)
                self.assertEqual(problems, [])
                self.assertEqual(hashlib.sha256(lib.read_bytes()).hexdigest(), PINS[target + '_sha256'])

    def test_rebuild_from_empty_cache_is_identical(self):
        for target in bs.TARGETS:
            with self.subTest(target=target):
                cache = OUT / (target + '.cache')
                shutil.rmtree(cache, ignore_errors=True)
                again = bs.build(target, OUT / (target + '.again'), cache=cache)
                self.assertEqual(again.read_bytes(), build(target).read_bytes())
                shutil.rmtree(cache)

    def test_verifier_rejects_an_ansi_build(self):
        info = {'machine': 0x8664, 'magic': 0x20b, 'exports': bs.REQUIRED_EXPORTS,
                'imports': {'KERNEL32.dll': ['CreateFileA', 'MoveFileA'], 'MSVCRT.dll': []}}
        original = bs.pe_info
        try:
            bs.pe_info = lambda data: info
            problems, _ = bs.verify_windows(build('windows'))
        finally:
            bs.pe_info = original
        self.assertIn('CreateFileW not imported (not a UNICODE build)', problems)
        self.assertIn('ANSI import CreateFileA (not a UNICODE build)', problems)
        self.assertIn('import MSVCRT.dll not allowed', problems)

    @unittest.skipUnless(sys.platform == 'darwin' and platform.machine() == 'arm64', 'needs an arm64 Mac')
    def test_dylib_writes_and_reads_an_mpq(self):
        lib = C.CDLL(str(build('mac')))
        H, U = C.c_void_p, C.c_uint32
        for name, args, res in [('SFileCreateArchive', [C.c_char_p, U, U, C.POINTER(H)], C.c_bool),
                                ('SFileAddFileEx', [H, C.c_char_p, C.c_char_p, U, U, U], C.c_bool),
                                ('SFileCloseArchive', [H], C.c_bool),
                                ('SFileOpenArchive', [C.c_char_p, U, U, C.POINTER(H)], C.c_bool),
                                ('SFileOpenFileEx', [H, C.c_char_p, U, C.POINTER(H)], C.c_bool),
                                ('SFileGetFileSize', [H, C.POINTER(U)], U),
                                ('SFileReadFile', [H, H, U, C.POINTER(U), H], C.c_bool),
                                ('SFileCloseFile', [H], C.c_bool)]:
            getattr(lib, name).argtypes, getattr(lib, name).restype = args, res
        archive, source = OUT / 'roundtrip.mpq', OUT / 'payload.bin'
        archive.unlink(missing_ok=True)
        payload = bytes(range(256)) * 400
        source.write_bytes(payload)
        handle = H()
        self.assertTrue(lib.SFileCreateArchive(bytes(archive), 0x00100000, 16, C.byref(handle)))
        self.assertTrue(lib.SFileAddFileEx(handle, bytes(source), b'DBFilesClient\\Test.dbc', 0x200, 2, 2))
        self.assertTrue(lib.SFileCloseArchive(handle))
        self.assertTrue(lib.SFileOpenArchive(bytes(archive), 0, 0x100, C.byref(handle)))
        f = H()
        self.assertTrue(lib.SFileOpenFileEx(handle, b'DBFilesClient\\Test.dbc', 0, C.byref(f)))
        size = lib.SFileGetFileSize(f, None)
        data, count = C.create_string_buffer(size), U()
        self.assertTrue(lib.SFileReadFile(f, data, size, C.byref(count), None))
        self.assertEqual(data.raw, payload)
        lib.SFileCloseFile(f)
        lib.SFileCloseArchive(handle)

    @unittest.skipUnless(sys.platform == 'darwin' and platform.machine() == 'arm64' and fp.client_root(required=False),
                         'needs an arm64 Mac and a client')
    def test_art_layer_with_new_dylib_matches_known_layers(self):
        client = fp.client_root()
        # build_art_layer refuses an output inside the client, and this repository may live inside it
        out = Path(tempfile.mkdtemp(prefix='northlight-art-layer-')) / 'art-layer'
        env = dict(os.environ, NORTHLIGHT_STORMLIB=str(build('mac')))
        try:
            r = subprocess.run([sys.executable, str(fp.REPO / 'build_art_layer.py'), '--client', str(client),
                                '--output', str(out)], capture_output=True, text=True, env=env, timeout=600)
            self.assertEqual(r.returncode, 0, r.stdout[-2000:] + r.stderr[-2000:])
            report = json.loads((out / 'art-layer-manifest.json').read_text())
            self.assertIn(report['archive_sha256'], KNOWN_ART)
        finally:
            shutil.rmtree(out.parent, ignore_errors=True)   # game-derived: hashed, never kept


if __name__ == '__main__':
    unittest.main()
