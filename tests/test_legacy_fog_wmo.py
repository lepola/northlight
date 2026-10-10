#!/usr/bin/env python3
# northlight-test: requires=cxx
"""PS3 fog epilogue proof (legacy_fog.h ps3FogColorRegister) on real MapObj token sequences: native test, clang++ plain and ASan/UBSan.
No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

with tempfile.TemporaryDirectory(prefix='nl-fog-wmo-') as temp:
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
        binary = Path(temp)/'test'
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                        str(HERE/'test_legacy_fog_wmo.cpp'), '-o', str(binary)], check=True)
        out = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout
        assert 'legacy fog wmo ok' in out, out
print('legacy fog wmo: ok')
