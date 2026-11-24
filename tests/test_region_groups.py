#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native checks for geographic grouping of AreaTable regions."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-region-groups-') as tmp:
    exe = Path(tmp) / 'region-groups'
    subprocess.run([
        'clang++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-uninitialized-const-pointer',
        '-DIMGUI_USER_CONFIG="northlight_imconfig.h"', *fp.test_include_flags(),
        str(Path(__file__).with_suffix('.cpp')),
        *[str(fp.src(name)) for name in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp')],
        '-o', str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
