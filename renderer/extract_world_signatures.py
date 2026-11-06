#!/usr/bin/env python3
"""Prove projection register layouts of original world vertex shaders.

No game execution. Only the listed world shader families are candidates. Unknown
position code is excluded. Skinning stays in the original shader for replay.
"""
import json
import hashlib
import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import northlight_paths  # noqa: E402
from mpq import Archive
from extract_signatures import variants, register, fnv1a64, position_prefix, EXPECTED_POSITION, SM1_LENGTHS

PATTERN = re.compile(r'\\vertex\\vs_[123]_\d+\\(Terrain|DetailDoodad|MapObj[^\\]+|[C]?Diffuse[^\\]*)\.bls$', re.I)

# Extra-light VS3 variants audited from the Cathedral Square captures. Only
# identities ship; original shader/mesh bytes remain in the private report.
AUDITED_CAPTURE_SIGNATURES = {
    0x9fc660afba57ebef: (2, 'da2769e84bf1985e93642d21cb9675b0bc118ab5dd921309c8461568a4ac3d8b'),
    0xbc62083b7435826c: (2, 'e8acd0c5029d777718457fa381db7b8afd5862074f2a653b97d925fe0f5973bc'),
    0xd26558bb3994fc81: (2, '316786218a44b6715993cd165006fc5e92ac2aeb111fea3b0878bdb409ed7ad4'),
}


def include_audited_captures(records):
    for value, (kind, digest) in AUDITED_CAPTURE_SIGNATURES.items():
        if value in records:
            record = records[value]
            if record['kind'] != kind or hashlib.sha256(record['bytecode']).hexdigest() != digest:
                raise ValueError('Audited capture signature collision')
        else:
            records[value] = {'kind': kind, 'hash': f'{value:016x}',
                              'sources': [{'capture': 'Cathedral Square 2026-11-01', 'sha256': digest,
                                           'path': f'audited-capture/extra-light-vs-{value:016x}'}]}


def instructions(code):
    words = struct.unpack('<%dI' % (len(code)//4), code)
    major = (words[0] >> 8) & 255
    offset = 1
    while offset < len(words):
        token = words[offset]
        op = token & 65535
        if op == 65535:
            return
        if op == 65534:
            offset += 1 + ((token >> 16) & 32767)
            continue
        count = (token >> 24) & 15 if major >= 2 else SM1_LENGTHS[op]
        args = tuple(words[offset+1:offset+1+count])
        if len(args) != count:
            raise ValueError('Truncated instruction')
        yield op, args
        offset += 1 + count


def classify(code):
    """Return 1 for terrain column matrix c4..7, 2 for dot rows c2..5."""
    try:
        if position_prefix(code) == EXPECTED_POSITION:
            return 1
    except (ValueError, KeyError):
        pass
    major = code[1]
    pos = (4, 0) if major < 3 else None
    ops = list(instructions(code))
    for op, args in ops:
        if major == 3 and op == 31 and args[0] & 31 == 0 and register(args[1])[0] == 6:
            pos = register(args[1])
    writes = [(i, op, a) for i, (op,a) in enumerate(ops)
              if op not in (31,81) and a and register(a[0]) == pos]
    if len(writes) != 4 or [a[0] >> 16 & 15 for _,_,a in writes] != [1,2,4,8]:
        return 0
    if writes[-1][0] - writes[0][0] != 3:
        return 0
    source = None
    for channel, (_,op,args) in enumerate(writes):
        if op != 9 or len(args) != 3:
            return 0
        constant = 0xa0e40002 + channel
        operands = list(args[1:])
        if constant not in operands:
            return 0
        operands.remove(constant)
        value = operands[0]
        if register(value)[0] != 0 or value & 0x0ffff800 != 0x00e40000:
            return 0
        if source is not None and value != source:
            return 0
        source = value
    # Never modify a projection constant that the shader itself defines.
    if any(op == 81 and 2 <= register(a[0])[1] <= 5 for op,a in ops):
        return 0
    return 2


def main():
    inventory = json.loads((HERE.parent/'inspection/inventory.json').read_text())
    records = {}
    rejected = []
    variants_seen = 0
    for path,entry in sorted(inventory.items()):
        if not entry['enabled']:
            continue
        listing = HERE.parent/'inspection'/(Path(path).name+'.list')
        names = [name for name in listing.read_text().splitlines() if PATTERN.search(name)]
        if not names:
            continue
        with Archive(HERE.parent.parent/path) as archive:
            for name in names:
                for index,flags,code in variants(archive.read(name)):
                    variants_seen += 1
                    try:
                        kind = classify(code)
                    except (KeyError, ValueError) as exc:
                        kind = 0
                    source = {'archive':path,'path':name,'variant':index}
                    if not kind:
                        rejected.append(source)
                        continue
                    value = fnv1a64(code)
                    if value in records and records[value]['bytecode'] != code:
                        raise ValueError('FNV collision')
                    record = records.setdefault(value, {'kind':kind,'hash':f'{value:016x}',
                        'bytecode':code,'sources':[]})
                    record['sources'].append(source)
    include_audited_captures(records)
    header = ['// Generated by extract_world_signatures.py; exact original bytecode identities.',
              '#pragma once','#include <cstdint>',
              'struct WorldShaderSignature { std::uint64_t hash; unsigned projectionKind; };',
              '// Kind 1: c4..7, clip = view.x*c4 + view.y*c5 + view.z*c6 + view.w*c7.',
              '// Kind 2: c2..5, clip.xyzw = dot(c2..5, view).',
              'static constexpr WorldShaderSignature kWorldShaderSignatures[] = {']
    for value,record in sorted(records.items()):
        header.append('    {UINT64_C(0x%016x), %d},' % (value,record['kind']))
    header.append('};')
    (northlight_paths.GAMEDATA/'world_shader_signatures.h').write_text('\n'.join(header)+'\n')
    report = {'variants_seen':variants_seen,'unique_count':len(records),
              'kind_counts':{str(k):sum(r['kind']==k for r in records.values()) for k in (1,2)},
              'rejected_variants':rejected,
              'shaders':[{k:v for k,v in record.items() if k!='bytecode'} for _,record in sorted(records.items())]}
    (HERE/'world_shader_signatures.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('rejected_variants','shaders')}))
    print('Excluded unproved variants:',len(rejected))


if __name__ == '__main__':
    main()
