#!/usr/bin/env python3
# northlight-test: requires=cxx
"""CPU checks for endpoint baking, immutable results, cache hits and coalescing."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

HERE = Path(__file__).resolve().parent
shader = fp.SHADERS.joinpath('world_effects.hlsl').read_text()
renderer = (fp.REPO / 'src/world/world_renderer.h').read_text()
manifest = json.loads(fp.SHADERS.joinpath('world-shader-build.json').read_text())
assert 'float3 from=samplePreviousGrade(base),to=sampleSelectedGrade(base);' in shader
assert 'GradeTime.x>0' in shader and 'GradeTime.y))-base' in shader
assert 'PreviousGradeB : register(s7)' in shader
assert 'PreviousGradeDusk' not in shader and 'PreviousGradeNight' not in shader
assert 'tex2D(WorldCurves,curveUV(luminance)).a' in shader
assert 'gradeRequestValid=gradeLutWorker.request(wanted)!=0;' in renderer
assert 'if(gradeAnchorSource!=gradeAnchor)' in renderer
assert 'parent->second==area.zone' in renderer
assert '!gradeVolumeSupported||gradeVolumeUnavailable' in renderer
assert 'gradeSampler(4,grade.to[timePair.first]' in renderer
assert 'gradeSampler(7,grade.from[timePair.second]' in renderer
assert 'DWORD(now-gradeMismatchSince)>=500' in renderer
assert 'cpu_bakes=%u cache_hits=%u worker_ms=%.2f gpu_uploads=%u upload_ms=%.2f' in renderer
assert manifest['source_sha256'] == hashlib.sha256(fp.SHADERS.joinpath('world_effects.hlsl').read_bytes()).hexdigest()
for name, slots in (('WorldCompositeGrade', 422), ('WorldCompositeWaterGrade', 429),
                    ('WorldCompositeGradeBlend', 440), ('WorldCompositeWaterGradeBlend', 447)):
    info = manifest['shaders'][name]
    assert info['static_instruction_slots'] == slots and info['static_instruction_slots'] <= 512
    assert info['temporary_registers'] <= 32
assert 'bindGradeVolume()' not in renderer and 'volumePS[2][2]' not in renderer
with tempfile.TemporaryDirectory(prefix='northlight-world-grade-worker-') as tmp:
    exe = Path(tmp) / 'test-world-grade-worker'
    subprocess.run([
        'clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
        *fp.test_include_flags(), str(HERE / 'test_world_grade_lut_worker.cpp'), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)
