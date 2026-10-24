#!/usr/bin/env python3
"""Startup source identity follows build inputs, not private settings or line endings."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_renderer as build

with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    for name in ('src/generated/example.h','shaders/example.hlsl','shaders/compiled/example.bin',
                 'scripts/build_renderer.py','northlight_paths.py'):
        path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'input\n')
    original=build.source_identity(root)
    assert len(original)==64 and original==build.source_identity(root)
    header=root/'src/generated/example.h';header.write_bytes(b'input\r\n')
    assert build.source_identity(root)==original
    (root/'northlight-quality.ini').write_text('Personal settings')
    assert build.source_identity(root)==original
    for name in ('src/generated/example.h','shaders/example.hlsl','shaders/compiled/example.bin',
                 'scripts/build_renderer.py','northlight_paths.py'):
        path=root/name;before=path.read_bytes();path.write_bytes(before+b'changed')
        assert build.source_identity(root)!=original,name
        path.write_bytes(before)
    (root/'src/Z.h').write_bytes(b'upper\n')
    (root/'src/a.h').write_bytes(b'lower\n')
    ordered=build.source_identity(root)
    assert ordered=='2999f0c376f87e604ca537470577a54e8d79fc483fd09d0f4ae81428986a1742'
    (root/'src/new.cpp').write_text('Added input')
    assert build.source_identity(root)!=ordered
source=fp.src('renderer.cpp').read_text()
assert 'BUILD identity source=%s' in source and 'NORTHLIGHT_SOURCE_ID' in source
assert '__DATE__' not in source and '__TIME__' not in source
assert 'NORTHLIGHT_SOURCE_ID="' in (fp.SCRIPTS/'build_renderer.py').read_text()
print('PASS source identity: source/shader/binary/tool changes, added file, CRLF equivalence, case-sensitive path order, private settings, startup wiring')
