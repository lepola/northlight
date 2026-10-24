# northlight-test: requires=cxx
"""Transactional INI persistence and bounded settings transport, without running the game."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-settings-') as tmp:
    exe = Path(tmp) / 'settings.exe'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(Path(tmp) / 'config')], check=True)
