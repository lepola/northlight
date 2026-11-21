"""Exercise startup, fallback and concurrent loading against the DLLs in a combined player ZIP."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(package):
    with tempfile.TemporaryDirectory() as temporary, zipfile.ZipFile(package) as archive:
        work = Path(temporary)
        names = archive.namelist()
        # Any DLL without the water API serves as the incompatible core. A macOS package
        # carries no DXVK of its own, so its renderer DLL takes that role there.
        other = '/payload/renderer-backends/dxvk/dxvk_d3d9.dll'
        if not any(name.endswith(other) for name in names):
            other = '/payload/d3d9.dll'
        for output, suffix in [('core.dll', '/payload/water-core.dll'), ('other.dll', other)]:
            member, = [name for name in names if name.endswith(suffix)]
            (work / output).write_bytes(archive.read(member))
        (work / 'scratch').mkdir()
        subprocess.run(['i686-w64-mingw32-clang++', '-std=c++17', '-O2', '-static',
                        '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-I', ROOT / 'tideglass/src',
                        ROOT / 'tests/test_tideglass_host_startup.cpp', '-o', work / 'check.exe'], check=True)
        subprocess.run(['wine', 'check.exe', 'scratch', 'core.dll', 'other.dll'], check=True,
                       cwd=work, env=os.environ | {'WINEDEBUG': '-all'})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zip', type=Path)
    run(parser.parse_args().zip)
