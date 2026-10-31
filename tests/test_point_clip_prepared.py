#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Prepared cube planes preserve scalar masks, overflow guards, and fail-open inputs."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
source=Path(__file__).with_suffix('.cpp')
with tempfile.TemporaryDirectory(prefix='northlight-point-clip-') as tmp:
 for mode,flags in [('release',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
  exe=Path(tmp)/mode
  subprocess.run(['clang++','-std=c++17','-UNDEBUG',*flags,*fp.test_include_flags(),str(source),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
renderer=fp.src('world_point_rendering.inl').read_text()
assert 'const NorthlightPointShadow::FaceClip clip(faceMatrices);' in renderer
assert renderer.count('pointMask(batch.boundsLow,batch.boundsHigh,clip)')==2
assert 'pointMask(batch.low,batch.high,clip)' in renderer
assert 'clip.mask(bounds.low,bounds.high,bounds.valid)' in renderer
assert 'const FaceClip clip(matrices);' in fp.src('point_light_shadow.h').read_text()
print('PASS prepared clipping is shared by static, terrain, live, replay, and content paths')
