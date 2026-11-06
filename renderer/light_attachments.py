"""Match placed world lights to the client's live M2 bone palette.

An unchanged palette bone proves the placement; a submitted triangle proves
the skin section. Neither proximity nor animation time identifies a source.
"""
import math
import struct

from world_scene_builder import array, unpack
from world_lights_builder import constant_track, static_bone_offset, static_bone_point, Unsupported, valid_light

RECORD = struct.Struct('<QII27fI')
REQUIRES_NATIVE, NATIVE_SOURCE = 64, 1


def physical_bone(blob, bone, point):
    """Camera-facing glow cards do not move the physical light source."""
    count, offset = unpack('<II', blob, 44)
    seen = set()
    while bone >= 0:
        if bone >= count or bone in seen:
            raise ValueError('Invalid attachment bone chain')
        seen.add(bone)
        off = offset + bone*88
        _, flags, parent = unpack('<iIh', blob, off)
        if flags & 0x7f != 8:
            break
        translation = (0., 0., 0.)
        if flags & 0x200:
            try:
                translation = constant_track(blob, off+16, '<3f', translation)
            except Unsupported:
                break
        point = tuple(point[k]+translation[k] for k in range(3))
        bone = parent
    return bone, point


def native_sources(blob):
    """World-placement sources whose current native clock must remain authoritative."""
    from build_spell_emitters import lifecycle_track
    result = []
    count, offset = unpack('<II', blob, 264)
    bones, bone_offset = unpack('<II', blob, 44)
    if count > 64:
        return result
    for index in range(count):
        off = offset + index*156
        kind, bone, *point = unpack('<Hh3f', blob, off)
        if kind != 1 or not 0 <= bone < bones:
            continue
        seen, parent = set(), bone
        while parent >= 0 and parent < bones and parent not in seen:
            seen.add(parent);parent = unpack('<h', blob, bone_offset+parent*88+8)[0]
        if parent >= 0:
            continue
        try:
            translation = static_bone_offset(blob, bone)
        except Unsupported:
            translation = None
        try:
            rgb = constant_track(blob, off+56, '<3f')
            start = constant_track(blob, off+96, '<f')[0]
            end = constant_track(blob, off+116, '<f')[0]
            flags = 2
            try:
                power = constant_track(blob, off+76, '<f')[0]
                enabled = constant_track(blob, off+136, '<B', (1,))[0]
                if enabled != 1 or translation is not None:
                    continue
            except Unsupported:
                intensity = lifecycle_track(blob, off+76, '<f', 1.)
                enabled = lifecycle_track(blob, off+136, '<B', 1)
                powers = [v for curve in intensity['curves'] for v in curve['values']]
                states = [v for curve in enabled['curves'] for v in curve['values']]
                if not powers or not 0 < max(powers) <= 10000 or min(powers) < 0 or not max(states) or any(v not in (0,1) for v in states):
                    continue
                # This is an ownership/hue witness, never an exported steady
                # light. Only the native scene supplies its current intensity.
                power, flags = 1., 2 | REQUIRES_NATIVE
                if translation is not None:
                    point = tuple(point[k]+translation[k] for k in range(3))
            color = tuple(c*power for c in rgb)
            if valid_light(point, color, start, end):
                result.append((tuple(point), color, start, end, index, 2, flags))
        except (Unsupported, ValueError):
            continue
    return result


def native_proofs(blob, sources, offsets=None):
    """Native record index and RAW anchor, independent of bone translation."""
    count, at = unpack('<II', blob, 264)
    return [(source[4], tuple(unpack('<3f',blob,at+source[4]*156+4)),
             tuple((offsets or {}).get(source[4],{}).get('offset',(0.,0.,0.))))
            for source in sources if source[5] == 2 and (source[6] & REQUIRES_NATIVE or source[4] in (offsets or {})) and source[4] < count]


def placed_native(proofs, placement, lights):
    matrix = placement[9:18] + placement[18:21]
    ids = {source: light[8] for source, light in lights}
    def transform(point, translated):
        return tuple(sum(point[j]*matrix[j*3+k] for j in range(3))+(matrix[9+k] if translated else 0) for k in range(3))
    return [(ids[index], index, 0, *([0.]*9), *transform(delta,False), *transform(anchor,True), *matrix, NATIVE_SOURCE)
            for index,anchor,delta in proofs if index in ids]


def templates(assets, path, blob, lights):
    from attached_emitter_sources import attached_sources
    skin = assets.read(path[:-3]+'00.skin')
    if skin[:4] != b'SKIN':
        raise ValueError('Unsupported attachment skin')
    def rows(data, at, fmt):
        return array(data, *reversed(unpack('<II', data, at)), fmt)
    vertices = rows(blob, 60, '<3f4B4B3f4f')
    lookup = [v for (v,) in rows(skin, 4, '<H')]
    indices = [v for (v,) in rows(skin, 12, '<H')]
    subsets = rows(skin, 28, '<10H7f')
    combos = [v for (v,) in rows(blob, 120, '<H')]
    # Older light patches added an identity bone, unrelated to the bulb. Recover
    # the original emitting bone from the original skin/particle evidence.
    evidence_blob = bytearray(blob)
    struct.pack_into('<II', evidence_blob, 264, 0, 0)
    try:
        evidence, _ = attached_sources(assets, path, bytes(evidence_blob))
    except ValueError:
        evidence = []
    result = []
    native_count, native_offset = unpack('<II', blob, 264)
    for point, color, _, _, index, kind, _ in lights:
        bone = -1
        if kind == 2 and index < native_count:
            bone = unpack('<h', blob, native_offset+index*156+2)[0]
        # A native light on an animated bone is already an explicit attachment.
        try:
            static_bone_point(blob, bone, point)
            moving = False
        except Unsupported:
            moving = bone >= 0
        peak = max(color)
        compatible = [s for s in evidence if max(abs(s['rgb'][k]/max(s['rgb'])-color[k]/peak) for k in range(3)) < .35]
        if not moving and compatible:
            nearest = min(compatible, key=lambda s: math.dist(s['position'], point))
            if math.dist(nearest['position'], point) <= 1:
                bone = nearest['bone']
        if bone < 0:
            continue
        if not moving:
            bone, point = physical_bone(blob, bone, point)
        if bone < 0:
            continue
        try:
            static_bone_point(blob, bone, point)
            continue
        except Unsupported:
            pass
        for subset in subsets:
            palette = combos[subset[7]:subset[7]+subset[6]]
            if len(palette) > 75:
                continue
            source_bone, source_point = bone, point
            seen = set()
            # Particle-only children need not occur in the skin palette. An
            # inactive child inherits its parent's complete current transform.
            while source_bone not in palette and source_bone >= 0:
                if source_bone in seen:
                    raise ValueError('Attachment bone cycle')
                seen.add(source_bone)
                off = unpack('<I', blob, 48)[0] + source_bone*88
                _, flags, parent = unpack('<iIh', blob, off)
                if flags & 0x27f:
                    break
                source_bone = parent
            if source_bone not in palette:
                continue
            reference = None
            for slot, candidate in enumerate(palette):
                try:
                    reference = slot, static_bone_offset(blob, candidate)
                    break
                except Unsupported:
                    continue
            if reference is None:
                continue
            first = subset[4] + (subset[1] << 16)
            if subset[5] < 3 or first+3 > len(indices):
                continue
            triangle=indices[first:first+3]
            if any(i>=len(lookup) or lookup[i]>=len(vertices) for i in triangle):
                continue
            witness = tuple(x for i in triangle for x in vertices[lookup[i]][:3])
            if not all(math.isfinite(x) and abs(x) < 100000 for x in (*witness, *point, *reference[1])):
                raise ValueError('Nonfinite attachment geometry')
            result.append((index, palette.index(source_bone), reference[0], witness, tuple(source_point), reference[1]))
    return result


def placed(templates, placement, lights):
    # instance_lights retains the local source order.
    matrix = placement[9:18] + placement[18:21]
    ids = {source: light[8] for source, light in lights}
    return [(ids[index], bone, reference, *witness, *point, *offset, *matrix)
            for index, bone, reference, witness, point, offset in templates if index in ids]


def write(path, records):
    records = [tuple(record)+(0,) if len(record)==30 else tuple(record) for record in records]
    records = sorted(set(records), key=lambda r: (r[3:12], r[0], r[1:3]))
    with path.open('wb') as output:
        output.write(struct.pack('<4sIII', b'FGA1', 2 if any(r[-1] for r in records) else 1, RECORD.size, len(records)))
        for record in records:
            output.write(RECORD.pack(*record))
