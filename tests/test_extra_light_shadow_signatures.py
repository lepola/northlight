"""Keep audited extra-light actors recognized after signature regeneration."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'renderer')]
from extract_world_signatures import AUDITED_CAPTURE_SIGNATURES, include_audited_captures

entries = [(int(h, 16), int(kind)) for h, kind in re.findall(
    r'\{UINT64_C\(0x([0-9a-f]+)\), (\d)\}',
    (ROOT / 'src/gamedata/world_shader_signatures.h').read_text())]
assert entries == sorted(entries) and len(dict(entries)) == len(entries)
records = {1: {'kind': 1, 'hash': '0000000000000001', 'bytecode': b'stock'}}
include_audited_captures(records)
assert records[1]['bytecode'] == b'stock'
for identity, (kind, digest) in AUDITED_CAPTURE_SIGNATURES.items():
    assert dict(entries)[identity] == kind == records[identity]['kind'] == 2
    assert records[identity]['sources'][0]['sha256'] == digest
    # extract_wmo_context reads every source path before filtering WMO records.
    assert records[identity]['sources'][0]['path'].startswith('audited-capture/')
    try:
        include_audited_captures({identity: {'kind': kind, 'bytecode': b'wrong shader'}})
    except ValueError:
        pass
    else:
        raise AssertionError('Audited identity accepted different shader bytes')
print('PASS extra-light shadow identities: sorted shared table, regeneration retention, collision refusal')
