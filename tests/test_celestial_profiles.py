#!/usr/bin/env python3
# northlight-test: requires=cxx,client
"""Runner for test_celestial_profiles.cpp (native clang++, plain and ASan/UBSan; no game, Wine or GPU).

The test reads <root>/celestial-profiles.ini and the real zone IDs in <root>/world-cache/fog.
The ini is the tracked client-config copy; world-cache comes from the client root
(--client, default: northlight_paths.client_root()). Both are only read. Writes nothing outside a temporary folder.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import argparse, shutil, subprocess, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--client', type=Path, help='directory containing world-cache/fog')
parser.add_argument('--ini', type=Path, default=fp.REPO/'client-config' / 'celestial-profiles.ini')
args = parser.parse_args()
fog = (args.client or fp.client_root()) / 'world-cache' / 'fog'
assert fog.is_dir(), f'{fog} missing: pass --client <WoW client root>'
with tempfile.TemporaryDirectory(prefix='northlight-celestial-profiles-') as tmp:
    root = Path(tmp) / 'root'
    (root / 'world-cache').mkdir(parents=True)
    shutil.copyfile(args.ini, root / 'celestial-profiles.ini')
    (root / 'world-cache' / 'fog').symlink_to(fog.resolve(), target_is_directory=True)
    for label, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        exe = Path(tmp) / ('test-' + label)
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', *flags, *fp.test_include_flags(),
                        str(HERE / 'test_celestial_profiles.cpp'), '-o', str(exe)], check=True)
        print(label, subprocess.check_output([str(exe), str(root)], text=True), end='', flush=True)
