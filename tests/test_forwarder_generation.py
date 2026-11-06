#!/usr/bin/env python3
# northlight-test: requires=zig
"""Regeneration preserves mapped, unchanged headers and repairs changed output."""
import mmap
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import generate_forwarders as gf

output = fp.output_dir()
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='forwarder-generation-', dir=output) as tmp:
    gf.northlight_paths.GENERATED = Path(tmp)
    gf.generate()
    files = [Path(tmp) / name for name in ('forwarders.h', 'command_stream.inl')]
    before = [(p.read_bytes(), p.stat().st_mtime_ns) for p in files]
    with files[0].open('rb') as source, mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as held:
        gf.generate()
        assert held[:] == before[0][0]
        assert [(p.read_bytes(), p.stat().st_mtime_ns) for p in files] == before
        files[1].write_bytes(b'stale output')
        gf.generate()
        assert files[1].read_bytes() == before[1][0]
        assert (files[0].read_bytes(), files[0].stat().st_mtime_ns) == before[0]
print('PASS forwarder generation: unchanged mapped header, stable timestamps, changed output repaired')
