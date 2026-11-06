"""Offline identity proof for native lamps which may be server-spawned.

GameObjectDisplayInfo ownership and a rigid owned luminous region qualify a
source. Names alone never do. Runtime compares the exact vertex data and the
pointer-independent authored structure before granting shadow intent.
"""
import hashlib
import struct

from world_scene_builder import Builder, unpack

RECORD = struct.Struct('<4Q4I')
SHELL = struct.Struct('<I12f')
COUNTS = (20, 28, 44, 60, 80, 112, 120, 128, 240, 264, 288, 296)


def digest(data, value=14695981039346656037):
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def identity(blob):
    if blob[:8] != b'MD20\x08\x01\0\0':
        raise ValueError('Native fixture M2 version')
    n, at = unpack('<II', blob, 8)
    name = blob[at:at+n]
    if not 1 < n <= 256 or len(name) != n or name[-1] or b'\0' in name[:-1]:
        raise ValueError('Native fixture name')
    count, at = unpack('<II', blob, 60)
    vertices = blob[at:at+count*48]
    if not 1 <= count <= 4096 or len(vertices) != count*48:
        raise ValueError('Native fixture vertex budget')
    shape = blob[4:8]+blob[16:20]
    shape += b''.join(blob[p:p+4] for p in COUNTS)+blob[160:212]
    for p, width in ((44, 88), (264, 156)):
        size, at = unpack('<II', blob, p)
        if size > (512 if p == 44 else 64) or at+size*width > len(blob):
            raise ValueError('Native fixture structure')
        for i in range(size):
            row = blob[at+i*width:at+(i+1)*width]
            shape += row[:16]+(row[76:88] if p == 44 else b'')
    return digest(name[:-1].lower()), digest(vertices), digest(shape), count


def qualify(assets, path, blob, builder=None, owner=None):
    """An exact DBC owner and existing source-owned geometry, never a path inference."""
    from build_gameobject_emitters import models
    from light_attachments import native_sources
    if not (owner if owner is not None else models(assets).get(path, {})).get('displays'):
        return None
    sources = native_sources(blob)
    if not sources:
        return None
    mesh = (builder or Builder(assets)).m2(path)
    owned = {mesh[2][t[3]][3]-0x800000 for t in mesh[1]
             if len(mesh[2][t[3]]) > 3 and 0x800000 <= mesh[2][t[3]][3] < 0x800040}
    mask = sum(1 << s[4] for s in sources if s[4] in owned and s[6] & 64)
    if not mask:
        return None
    name, vertices, shape, count = identity(blob)
    return name, vertices, shape, mask, count, 0, 0, 0


def shell_geometry(builder, path, blob, mask):
    from light_attachments import native_sources
    points = {s[4]: s[0] for s in native_sources(blob)}
    vertices, triangles, materials = builder.m2(path)
    result = []
    for triangle in triangles:
        material = materials[triangle[3]]
        source = material[3]-0x800000 if len(material)>3 else -1
        if 0 <= source < 64 and mask >> source & 1:
            result.append((source, *points[source], *(v for i in triangle[:3] for v in vertices[i][:3])))
    return result


def write(path, entries):
    records, shells = [], []
    for record, geometry in sorted(entries, key=lambda entry: entry[0]):
        records.append((*record[:5], len(shells), len(geometry), 0))
        shells.extend(geometry)
    payload = struct.pack('<4s5I', b'NLF1', 2, RECORD.size, len(records), SHELL.size, len(shells))
    payload += b''.join(RECORD.pack(*r) for r in records)+b''.join(SHELL.pack(*s) for s in shells)
    path.write_bytes(payload)
    return payload


def build(assets, output):
    from build_gameobject_emitters import models
    try:
        candidates = models(assets)
    except FileNotFoundError:
        candidates = {}
    builder = Builder(assets)
    records, proof, errors = {}, [], []
    for path, owner in sorted(candidates.items()):
        if not owner['displays']:
            continue
        try:
            blob = assets.read(path)
            if len(blob) < 304 or not unpack('<I', blob, 264)[0]:
                continue
            record = qualify(assets, path, blob, builder, owner)
            if record:
                key = record[:3]
                if key in records and records[key][0] != record:
                    raise RuntimeError('Ambiguous native fixture identity: '+path)
                records[key] = record, shell_geometry(builder, path, blob, record[3])
                proof.append({'asset': path, 'displays': owner['displays'],
                              'sources': [i for i in range(64) if record[3] >> i & 1],
                              'sha256': hashlib.sha256(blob).hexdigest()})
        except (ValueError, FileNotFoundError, IndexError, KeyError, struct.error) as exc:
            errors.append({'asset': path, 'error': str(exc)})
    payload = write(output/'native-fixtures.bin', records.values())
    return {'sha256': hashlib.sha256(payload).hexdigest(), 'fixtures': proof, 'errors': errors}
