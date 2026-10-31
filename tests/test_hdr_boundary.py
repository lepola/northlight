#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the production HDR boundary methods with a CPU-only colour ownership mock."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
start = source.index('    bool hdrUiInputs(){')
end = source.index('    // 0.3.154: the per-draw timing scopes;', start)
methods = source[start:end]
spell = fp.src('spell_layer.h').read_text()
start = spell.index('inline bool isRenderTargetTexture(')
end = spell.index('\nenum class DrawType:', start)
texture_test = 'namespace NorthlightSpellLayer {\n' + spell[start:end] + '\n}\n'
assert source.count('if(spellOn||hdr.active)shaderReads[static_cast<void*>(*out)]=') == 2
assert 'NorthlightShaderReads::scan(' in source
runs = []
with tempfile.TemporaryDirectory(prefix='northlight-hdr-boundary-') as tmp:
    root = Path(tmp)
    (root / 'hdr_boundary_texture.h').write_text(texture_test)
    for label, flags, production in [
        ('optimized', ['-O2'], methods),
        ('sanitizers', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'], methods),
        ('before-fix', ['-O2', '-DNORTHLIGHT_HDR_BOUNDARY_COUNTERFACTUAL'], methods.replace('&&hdrUiInputs()', '')),
    ]:
        (root / 'hdr_boundary_methods.h').write_text(production)
        exe = root / label
        command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *flags,
                   '-I', str(root), *fp.test_include_flags(), str(fp.TESTS / 'test_hdr_boundary.cpp'), '-o', str(exe)]
        subprocess.run(command, check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        assert run.returncode == 0, run.stdout + run.stderr
        print(label, run.stdout.strip())
        runs.append(dict(label=label, output=run.stdout.strip()))
(fp.output_dir() / 'hdr-boundary-validation.json').write_text(json.dumps(dict(
    scope='Production HDR draw/admission methods and texture usage predicate; mock device, no GPU or game.',
    limitation='An intentional logical legacy read after genuine atlas UI still sees the pre-UI legacy scene; this check prevents scene passes from triggering that migration.',
    runs=runs), indent=2) + '\n')
