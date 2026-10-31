#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,stormlib
"""Real Unicode filesystem I/O in the renderer and StormLib, without changing a game client."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from mpq import Archive

scratch = fp.output_dir()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='northlight-utf8-', dir=scratch) as folder:
    root = Path(folder)
    unicode_root = root / 'Игра Ä 世界 🎮'
    unicode_root.mkdir()
    source = unicode_root / 'данные.txt'
    source.write_bytes(b'Unicode MPQ round trip')
    archive = unicode_root / 'архив.mpq'
    with Archive(archive, create=True) as mpq:
        mpq.add(source, 'test.txt')
    with Archive(archive) as mpq:
        assert mpq.read('test.txt') == source.read_bytes()
    exe = root / 'test.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    *fp.test_include_flags(), str(fp.TESTS / 'test_utf8_paths.cpp'), str(fp.src('world_gi.cpp')),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe), str(root)], check=True)
print('PASS StormLib archive creation and reading in a Unicode path')
