#!/usr/bin/env python3
# northlight-test: requires=zig
"""Bounded native-light bridge: executable checks and actual renderer wiring."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-equipment-') as tmp:
    world = fp.src('world_renderer.h').read_text()
    capture = world[world.index('    void captureEquipmentLights('):world.index('    void observeLightAttachments(')]
    (Path(tmp)/'equipment_capture_body.inc').write_text(capture, encoding='utf-8')
    for flags in (['-O0'], ['-O2']):
        exe = Path(tmp)/('test.exe' if sys.platform == 'win32' else 'test')
        for source in ('test_equipment_lights.cpp', 'equipment_capture_mirror.cpp'):
            if source=='equipment_capture_mirror.cpp' and sys.platform!='win32':continue
            target=['-target','x86-windows-gnu'] if sys.platform=='win32' else []
            if source=='equipment_capture_mirror.cpp':target+=['-Wno-microsoft-exception-spec']
            subprocess.run([str(fp.zig()), 'c++', *target, '-std=c++17', *flags, '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                            *fp.test_include_flags(), '-I'+tmp, str(fp.TESTS/source), '-o', str(exe)],
                           check=True, env=fp.zig_env())
            subprocess.run([str(exe)], check=True)

renderer = fp.src('renderer.cpp').read_text()
world = fp.src('world_renderer.h').read_text()
assert 'world->registerEquipmentShader(*out,m2ShaderFamily(h)==3?words.data():nullptr,m2ShaderFamily(h)==3?words.size():0)' in renderer
assert 'if(world->equipmentLightsOn())world->captureEquipmentLights(vs,worldDepth);' in renderer
capture = world[world.index('    void captureEquipmentLights('):world.index('    unsigned spellLayerSetting()')]
assert capture.index('equipmentCapture.take(actor,') < capture.index('GetVertexShaderConstantF(17,readBank,11)')
assert 'const bool actor=isSkinnedShader(shader)' in capture
assert 'equipmentLights.admit(bank,it->second,actor)' in capture
assert capture.index('ds.p!=mainDepth') < capture.index('GetVertexShaderConstantF(17,readBank,11)')
assert capture.index('std::isfinite(q[i])') < capture.index('GetVertexShaderConstantF(17,readBank,11)')
assert 'equipmentLights.output.data()' in world and 'equipmentLights.publish(GetTickCount(),' in world
assert 'GetRenderTargetData' not in capture and 'LockRect' not in capture
assert 'actorCandidates=%u' in world and 'domainRejected=%u' in world and 'selfYards=%.2f' in world
print('actual renderer: cached live banks without admission cutoff, main depth/projection gates, bounded native fallback, source diagnostics, existing local/fog batches, no texture/GPU readback PASS')
