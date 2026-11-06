#!/usr/bin/env python3
# northlight-test: requires=client,windows-exe,zig
"""Execute the actual 12340 light selector and the installed source-priority thunk."""
from pathlib import Path
import subprocess
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

for mode in ('-O0', '-O2'):
    exe = fp.output_dir() / ('glue-lighting' + mode + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', mode,
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS / 'glue_lighting_selection.cpp'), '-Wl,--image-base,0x400000',
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    image = bytearray(exe.read_bytes())
    optional = struct.unpack_from('<I', image, 0x3c)[0] + 24
    flags = struct.unpack_from('<H', image, optional + 70)[0]
    struct.pack_into('<H', image, optional + 70, flags & ~0x40)
    exe.write_bytes(image)
    subprocess.run([str(exe), str(fp.client_root() / 'Wow.exe')], check=True, timeout=30)
