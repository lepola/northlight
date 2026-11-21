#!/usr/bin/env python3
# northlight-test:
"""A player package carries only a Tideglass core that cannot change the client: renderer/build_packages.py
check_water_core refuses a core importing FlushInstructionCache or WriteProcessMemory, or carrying the
development picture clock's marker, and accepts one whose only memory-protection import is the C runtime's
VirtualProtect. The marker matches the Tideglass source, and Northlight's core build never asks for the clock.
The core states its version as the Tideglass source does, and a combined package's label is both parts' versions.
Synthetic PE files; nothing is compiled or run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
fp.use_source_modules()
import re
import struct
import unittest

import build_packages as bp  # noqa: E402

HOST = b'MZ host WaterHostGetAPI\0'


def pe32(imports, extra=b''):
    """A minimal 32-bit PE whose one section holds a KERNEL32 import table with these names."""
    pe, opt_size, section, raw, va = 0x80, 224, 0x80 + 24 + 224, 0x200, 0x1000
    body = bytearray(0x400)
    descriptor, thunks = 0, 40                                  # one descriptor and the zero terminator
    names_at = thunks + 4 * (len(imports) + 1)
    rvas, at = [], names_at
    for name in imports:
        entry = b'\0\0' + name.encode() + b'\0'
        body[at:at + len(entry)] = entry
        rvas.append(va + at); at += len(entry)
    dll = at
    body[dll:dll + 13] = b'KERNEL32.dll\0'
    for i, rva in enumerate(rvas):
        struct.pack_into('<I', body, thunks + 4 * i, rva)
    struct.pack_into('<5I', body, descriptor, va + thunks, 0, 0, va + dll, va + thunks)
    head = bytearray(raw)
    head[0:2] = b'MZ'
    struct.pack_into('<I', head, 0x3c, pe)
    head[pe:pe + 4] = b'PE\0\0'
    struct.pack_into('<HH', head, pe + 4, 0x14c, 1)
    struct.pack_into('<H', head, pe + 20, opt_size)
    struct.pack_into('<H', head, pe + 24, 0x10b)
    struct.pack_into('<II', head, pe + 24 + 96 + 8, va, 40)   # the import directory
    head[section:section + 8] = b'.idata\0\0'
    struct.pack_into('<IIII', head, section + 8, len(body), va, len(body), raw)
    return bytes(head) + bytes(body) + b'WaterHostGetAPI\0Tideglass 0.1.0; cooperative core started\0' + extra


class PlayerWaterCore(unittest.TestCase):
    def test_runtime_page_protection_alone_is_accepted(self):
        self.assertEqual(bp.check_water_core(pe32(['VirtualProtect', 'ReadProcessMemory', 'GetTickCount']), HOST), '0.1.0')

    def test_a_core_without_a_version_is_refused(self):
        with self.assertRaises(SystemExit) as refused:
            bp.check_water_core(pe32(['VirtualProtect']).replace(b'Tideglass 0.1.0;', b'Tideglass core  '), HOST)
        self.assertIn('version', str(refused.exception))

    def test_version_matches_the_source(self):
        header = (fp.REPO / 'tideglass/src/version.h').read_text()
        version = re.search(r'#define TIDEGLASS_VERSION "([^"]+)"', header).group(1)
        logged = (fp.REPO / 'tideglass/src/host_api.cpp').read_text()
        self.assertIn('"Tideglass " TIDEGLASS_VERSION "; cooperative core started', logged)
        self.assertTrue(bp.TIDEGLASS_MARKER.fullmatch(f'Tideglass {version};'.encode()))
        self.assertEqual(bp.combo_version('0.3.217', version), f'0.3.217-tideglass-{version}')

    def test_a_core_that_can_rewrite_code_is_refused(self):
        for name in sorted(bp.CODE_WRITE_IMPORTS):
            with self.subTest(name=name), self.assertRaises(SystemExit) as refused:
                bp.check_water_core(pe32(['VirtualProtect', name]), HOST)
            self.assertIn(name, str(refused.exception))

    def test_the_development_clock_is_refused(self):
        with self.assertRaises(SystemExit) as refused:
            bp.check_water_core(pe32(['VirtualProtect'], b'x' + bp.CLOCK_MARKER + b'\0'), HOST)
        self.assertIn('picture clock', str(refused.exception))

    def test_not_a_cooperative_pair_is_refused(self):
        with self.assertRaises(SystemExit):
            bp.check_water_core(pe32(['VirtualProtect']).replace(b'WaterHostGetAPI', b'OtherHostGetAPI'), HOST)
        with self.assertRaises(SystemExit):
            bp.check_water_core(pe32(['VirtualProtect']), b'MZ plain renderer\0')

    def test_marker_and_release_build_match_the_source(self):
        engine = (fp.REPO / 'tideglass/src/engine.cpp').read_text()
        self.assertEqual(re.search(r'kVisualClockMarker\[\]="([^"]+)"', engine).group(1).encode(), bp.CLOCK_MARKER)
        self.assertIn('defined(WATER_VISUAL_CLOCK)', engine)
        self.assertNotIn('WATER_VISUAL_CLOCK', (fp.REPO / 'tideglass/CMakeLists.txt').read_text(),
                         'the player core build never asks for the picture clock')


if __name__ == '__main__':
    unittest.main()
