#!/usr/bin/env python3
# northlight-test:
"""Actor shader dependencies, embedded payloads and unchanged legacy effects."""
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
shader = ROOT / 'shaders'
manifest = json.loads((shader / 'shader-build.json').read_text())
legacy = json.loads((ROOT / 'tests/fixtures/ao_actor_legacy_sha256.json').read_text())
embedded = (ROOT / 'src/generated/compiled_shaders.h').read_text()
actors = {'AOActorPrepare': 'kAoActorPrepareShader', 'HBAOActorContactBloom': 'kHbaoActorContactBloomShader',
          'GTAOActorContactBloom': 'kGtaoActorContactBloomShader', 'HBAOActorOnly': 'kHbaoActorOnlyShader',
          'GTAOActorOnly': 'kGtaoActorOnlyShader'}
source_hash = hashlib.sha256((shader / 'effects.hlsl').read_bytes() + b'\n' + (shader / 'ao_actor.hlsl').read_bytes()).hexdigest()
assert manifest['actor_source_sha256'] == source_hash
for name, digest in legacy.items():
    assert hashlib.sha256((shader / 'compiled' / (name + '.bin')).read_bytes()).hexdigest() == digest, name + ' baseline changed'
for name, symbol in actors.items():
    data = (shader / 'compiled' / (name + '.bin')).read_bytes()
    record = manifest['shaders'][name]
    assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
    assert data[:4] == b'\x00\x03\xff\xff' and data[-4:] == b'\xff\xff\x00\x00'
    assert record['source_sha256'] == source_hash, name + ' dependency changed without recompilation'
    assert record['static_instruction_slots'] <= 512 and record['temporary_registers'] <= 32
    assert max(record['samplers']) < 16
    array = re.search(r'static const DWORD ' + symbol + r'\[\] = \{(.*?)\};', embedded, re.S)
    assert array, symbol
    words = [int(word, 16) for word in re.findall(r'0x[0-9a-fA-F]+', array.group(1))]
    assert struct.pack('<' + 'I' * len(words), *words) == data, name + ' embedded payload differs'
print('PASS 10 original effect payloads unchanged; 5 actor dependency/SM3/embedded-payload gates')
