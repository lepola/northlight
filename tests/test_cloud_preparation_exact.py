#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Frozen cloud ranks, cubes, Worley padding and regional water masks; no benchmark."""
import subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='cloud-preparation-exact-') as tmp:
    for name in ('test_fog_perlin_cubes','test_fog_quantile_samples','test_fog_worley_reference','test_regional_fog_water_mask'):
        for mode,flags in (('O2',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])):
            exe=Path(tmp)/(name+'-'+mode)
            subprocess.run(['clang++','-std=c++17','-UNDEBUG','-ffp-contract=off','-frounding-math',*flags,*fp.test_include_flags(),str(fp.TESTS/(name+'.cpp')),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        print(name+' PASS',flush=True)
