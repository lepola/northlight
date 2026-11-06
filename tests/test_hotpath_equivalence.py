#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Compare optimized production algorithms to the frozen 9537631 implementations."""
import subprocess
import json
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

# Compare the frozen algorithm with the same independently compiled shader costs.
manifest = json.loads((fp.SHADERS / 'world-shader-build.json').read_text())['shaders']
for path in (fp.src('local_light_batches.h'), fp.FIXTURES / 'hotpaths-9537631/local_light_batches.h'):
    text = path.read_text()
    for constant, entry in (('DirectSlots', 'LocalDirect'), ('Direct16Slots', 'LocalDirect16'), ('FogSlots', 'LocalFog')):
        assert int(re.search(r'\b' + constant + r'=(\d+)', text)[1]) == manifest[entry]['static_instruction_slots'], (path, constant)

with tempfile.TemporaryDirectory(prefix='northlight-hotpaths-') as tmp:
    for name, flags in [('O2', ['-O2']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])]:
        exe = Path(tmp) / name
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        *fp.test_include_flags(), str(fp.TESTS / 'test_hotpath_equivalence.cpp'), '-o', str(exe)], check=True)
        print(name, subprocess.check_output([str(exe)], text=True), end='', flush=True)
