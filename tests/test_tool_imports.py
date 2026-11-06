#!/usr/bin/env python3
# northlight-test:
"""Pure asset/shader helpers import without loading optional native toolchains."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
code=r'''
import northlight_paths as fp
fp.use_source_modules()
calls=[]
def unavailable(*args,**kwargs):
    calls.append(True)
    raise fp.Missing('intentional unavailable toolchain')
fp.stormlib=fp.tools=fp.zig=unavailable
import mpq,extract_signatures,build_lighting,shader_compiler
assert mpq.lib is None and not calls
assert extract_signatures.fnv1a64(b'')==0xcbf29ce484222325
assert len(build_lighting.rgb(build_lighting.transform_color(1,0xc8c8c8,0,2,None)))==3
assert shader_compiler.instructions(fp.COMPILED/'WorldFog.bin.asm')
assert not calls
try:mpq.Archive('unused.mpq')
except fp.Missing:pass
else:raise AssertionError('archive access must still require StormLib')
assert calls
calls.clear()
try:shader_compiler.Compiler()
except fp.Missing:pass
else:raise AssertionError('compilation must still require its toolchain')
assert calls
print('PASS pure imports and helpers need no native library; actual archive/compiler use still checks dependencies')
'''
subprocess.run([sys.executable,'-c',code],cwd=fp.REPO,check=True)
