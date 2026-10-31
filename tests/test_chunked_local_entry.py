#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Run the actual worker entry selection against bounded memory samples."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
source=fp.src('world_renderer.h').read_text()
block=source.split('// BEGIN CHUNKED ENTRY SELECTION')[1].split('// END CHUNKED ENTRY SELECTION')[0]
failure=source.split('                auto noteLocalAllocationFailure=')[1].split('\n',1)[0]
with tempfile.TemporaryDirectory(prefix='local-chunk-entry-') as folder:
    root=Path(folder);(root/'chunked_entry.inl').write_text(block);(root/'chunked_failure.inl').write_text('auto noteLocalAllocationFailure='+failure)
    for opt in ['-O0','-O2']:
        exe=root/'test.exe'
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(root),*fp.test_include_flags(),str(fp.TESTS/'test_chunked_local_entry.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
