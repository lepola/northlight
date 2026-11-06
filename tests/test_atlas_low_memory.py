#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# northlight-test: requires=cxx,zig
"""Exercise atlas lock allocation failures and replay provenance without a GPU."""
from pathlib import Path
import tempfile
import test_command_stream as cs

with tempfile.TemporaryDirectory(prefix='northlight-atlas-oom-') as tmp:
    (Path(tmp)/'d3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n'+cs.stub())
    (Path(tmp)/'cs_generated.inc').write_text(cs.generated_cpp())
    for name, flags in (('o0', ['-O0']), ('o2', ['-O2'])):
        cs.compile_run(tmp, name, flags, source='test_atlas_low_memory.cpp')
print('Atlas allocation failure and upload provenance: passed')
