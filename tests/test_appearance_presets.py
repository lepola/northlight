# northlight-test: requires=cxx
"""CPU checks for portable regional appearance palettes; no client or GPU."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()

spec = spec_from_file_location('northlight_build_packages', fp.RENDERER / 'build_packages.py')
builder = module_from_spec(spec)
spec.loader.exec_module(builder)

with tempfile.TemporaryDirectory(prefix='northlight-appearance-presets-') as tmp:
    root = Path(tmp)
    folder = root / 'appearance-presets'
    folder.mkdir()
    (folder / 'Default.ini').write_bytes(builder.default_appearance_preset())
    (folder / 'Vibrant.ini').write_bytes((fp.REPO / 'client-config/appearance-presets/Vibrant.ini').read_bytes())
    exe = root / 'appearance-presets.exe'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    *fp.test_include_flags(), str(Path(__file__).with_suffix('.cpp')), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(root), str(fp.REPO / 'client-config/celestial-profiles.ini')], check=True)
