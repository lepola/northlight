"""Append native v264 point lights to original animated equipment bones.

All original bytes and bone/skin/material/particle data are preserved. A missing
source or an unsupported material is an exclusion, never a white fallback.
"""
import math
import struct
from emitter_tracks import NativeTracks, pulse_for

MAX_ATTACHED_SOURCES = 16


def append_bone_lights(blob, emitters):
    if len(blob) < 304 or blob[:4] != b'MD20' or struct.unpack_from('<I', blob, 4)[0] != 264:
        raise ValueError('Requires MD20/v264')
    if struct.unpack_from('<I', blob, 264)[0]:
        raise ValueError('Preserve existing native lights')
    bones, bone_offset = struct.unpack_from('<II', blob, 44)
    globals, global_offset = struct.unpack_from('<II', blob, 20)
    if not bones or bones > 32767 or bone_offset < 304 or bone_offset + bones * 88 > len(blob):
        raise ValueError('Invalid original bones')
    if globals >= 32767 or globals and (global_offset < 304 or global_offset + globals * 4 > len(blob)):
        raise ValueError('Invalid global sequences')
    if not 1 <= len(emitters) <= MAX_ATTACHED_SOURCES:
        raise ValueError('At most sixteen compact attached sources')
    for source in emitters:
        if not isinstance(source['bone'], int) or not 0 <= source['bone'] < bones:
            raise ValueError('Light must use an existing source bone')
        seen, parent = set(), source['bone']
        while parent >= 0:
            if parent >= bones or parent in seen:
                raise ValueError('Invalid source bone parent chain')
            seen.add(parent)
            parent = struct.unpack_from('<h', blob, bone_offset+parent*88+8)[0]
        if len(source['position']) != 3 or len(source['rgb']) != 3 or not all(math.isfinite(x) for x in (*source['position'], *source['rgb'])):
            raise ValueError('Invalid source vectors')
        if max(map(abs, source['position'])) > 100 or not all(0 <= x <= 1 for x in source['rgb']) or not max(source['rgb']):
            raise ValueError('Invalid source position/colour')
        if not 0 < source['intensity'] <= 4 or not 0 <= source['start'] < source['end'] <= 12:
            raise ValueError('Invalid source energy/range')
    out = bytearray(blob)
    tracks = NativeTracks(out)
    def add(data):
        out.extend(b'\0' * (-len(out) % 4))
        offset = len(out)
        out.extend(data)
        return offset
    records = []
    for source in emitters:
        record = struct.pack('<HH3f', 1, source['bone'], *source['position'])
        for field, (fmt, values) in enumerate([('<3f', (0, 0, 0)), ('<f', (0,)), ('<3f', source['rgb']),
                            ('<f', (source['intensity'],)), ('<f', (source['start'],)), ('<f', (source['end'],)), ('<B', (1,))]):
            record += tracks.track(fmt, values, pulse=pulse_for(source, len(records)) if field == 2 else None)
        assert len(record) == 156
        records.append(record)
    tracks.finish()
    struct.pack_into('<II', out, 264, len(records), add(b''.join(records)))
    restored = bytearray(out[:len(blob)])
    for start, end in ((20, 28), (264, 272)):
        restored[start:end] = blob[start:end]
    if restored != blob:
        raise ValueError('Original model changed')
    return bytes(out)
