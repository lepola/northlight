"""Client-specific evidence for live emission from non-additive M2 effects.

Scan the winning archive chain, never a stock-model list. Native shader RGBA,
animated UVs and transformed vertices still determine each frame's light; this
catalog only admits luminous alpha materials and identifies environmental bolts.
"""
import collections
import json
import re
import struct
from pathlib import Path

from world_scene_builder import array, blp_mips, decode_blp, string, unpack

FILE = 'northlight-emission.bin'
GLOW, LIGHTNING, SKY_LIGHTNING = 1, 2, 4
LUMINOUS = re.compile(r'flame|fire|candle|lightning|electric|spark|glow|lava|energy|beam|rune', re.I)
BOLT = re.compile(r'lightning|thunderbolt|thunderstrike|thunderflash|stormflash|electricarc', re.I)
SKY_FLASH = re.compile(BOLT.pattern+r'|meteor.*flash', re.I)
NONLIGHT = re.compile(r'smoke|cloud|mist|dust|black32|shadow|blood', re.I)


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def mip_keys(blob):
    """Hash exact CPU-uploaded rows, including every retained small mip."""
    encoding, depth, alpha, _ = unpack('<4B', blob, 8)
    if encoding == 1:
        w,h,rgba = decode_blp(blob,32)
        yield fnv(rgba),w,h,0  # Canonical decoded RGBA, independent of the native palette expansion.
        return
    if encoding not in (2, 3):
        raise ValueError('Unsupported upload encoding')
    fmt = 21 if encoding == 3 else (0x31545844 if depth in (0, 1) else 0x33545844 if alpha == 1 else 0x35545844)
    for _, w, h, offset, size in blp_mips(blob):
        if max(w, h) <= 32:
            expected = w*h*4 if encoding == 3 else ((w+3)//4)*((h+3)//4)*(8 if fmt == 0x31545844 else 16)
            if size != expected:
                raise ValueError('Unexpected upload mip size')
            yield fnv(blob[offset:offset+size]), w, h, fmt
            break  # The cache retains the most detailed small mip; tiny solid tails are ambiguous identities.


def model_textures(assets, path, blob):
    def rows(data, at, fmt):
        return array(data, *reversed(unpack('<II', data, at)), fmt)
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Not WotLK v264')
    textures = rows(blob, 80, '<4I')
    names = [string(blob, t[3]) if t[0] == 0 and t[2] else '' for t in textures]
    found = collections.defaultdict(set)
    # Render batches associate a particular material with a particular texture.
    materials = rows(blob, 112, '<2H')
    if any(flags & 1 and blend in (2, 3, 4) for flags, blend in materials):
        skin = assets.read(path[:-3]+'00.skin')
        if skin[:4] != b'SKIN':
            raise ValueError('Invalid skin')
        combos = rows(blob, 128, '<H')
        for batch in rows(skin, 36, '<2B11H'):
            flags, blend = materials[batch[6]]
            if flags & 1 and blend in (2, 3, 4) and batch[8] in (1,2):
                for slot in range(batch[8]):
                    name = names[combos[batch[9]+slot][0]]
                    if name:
                        found[name].add('unlit-mesh' if batch[8]==1 else 'unlit-mesh-with-mask')
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 512 else 476
    for i in range(count):
        at = offset+i*stride
        if unpack('<B', blob, at+40)[0] in (2, 3, 4):
            name = names[unpack('<H', blob, at+22)[0]]
            if name:
                found[name].add('particle')
    # Ribbons use texture indices in their own array. Their live alpha/lifetime
    # is evaluated by the native draw, not frozen into a permanent model light.
    count, offset = unpack('<II', blob, 288)
    for i in range(count):
        at = offset+i*176
        for index, in rows(blob, at+20, '<H'):
            if index < len(names) and names[index]:
                found[names[index]].add('ribbon')
    return found


def build(assets, output):
    report = {'models': {}, 'textures': {}, 'rejected_textures': {}, 'source_archives': assets.fingerprint()}
    references = collections.defaultdict(list)
    counts = collections.Counter()
    for path in sorted(n for n in assets.providers if n.endswith('.m2')):
        try:
            blob = assets.read(path)
            found = model_textures(assets, path, blob)
            report['models'][path] = {'native_lights': unpack('<I', blob, 264)[0],
                'particles': unpack('<I', blob, 296)[0], 'ribbons': unpack('<I', blob, 288)[0],
                'live_textures': sorted(found)}
            counts['models'] += 1
            for name, evidence in found.items():
                if SKY_FLASH.search(name) or (LUMINOUS.search(name) and not NONLIGHT.search(name)):
                    kind = GLOW
                    if path.startswith('environments\\stars\\') and SKY_FLASH.search(name):
                        kind |= SKY_LIGHTNING
                    elif BOLT.search(name):
                        kind |= LIGHTNING
                    references[name].append({'model': path, 'kind': kind, 'evidence': sorted(evidence)})
        except (ValueError, FileNotFoundError, IndexError, struct.error) as error:
            report['models'][path] = {'rejected': str(error)}
            counts['unavailable_or_unsupported'] += 1
    keys = {}
    for name, uses in sorted(references.items()):
        try:
            blob = assets.read(name)
            _, _, rgba = decode_blp(blob, 32)
            # Native Opaque combiners use vertex/material alpha, not texture
            # alpha. Requiring a bright texel's alpha erased Hellfire/Shadowmoon.
            if not any(max(rgba[i:i+3]) >= (1 if SKY_FLASH.search(name) else 96) and
                       (SKY_FLASH.search(name) or rgba[i+3] >= 8) for i in range(0, len(rgba), 4)):
                raise ValueError('No visible luminous texels')
            kinds = {u['kind'] for u in uses}
            # A shared texture only admits a bolt candidate. Live geometry must
            # also prove a long channel; a small spell using this atlas stays local.
            kind = GLOW
            for value in kinds:
                kind |= value
            mips = list(mip_keys(blob))
            if not mips:
                raise ValueError('No retained CPU mip')
            for key in mips:
                keys[key] = keys.get(key, 0) | kind
            report['textures'][name] = {'kind': kind, 'uses': uses, 'mips': len(mips)}
        except (ValueError, FileNotFoundError, IndexError, struct.error) as error:
            report['rejected_textures'][name] = str(error)
    if len(keys) > 65536:
        raise ValueError('Emission catalog exceeds 65536 upload identities')
    payload = struct.pack('<4s3I', b'NLE1', 1, 24, len(keys))
    payload += b''.join(struct.pack('<Q4I', *key, kind) for key, kind in sorted(keys.items()))
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / FILE).write_bytes(payload)
    counts.update(textures=len(report['textures']), upload_identities=len(keys),
                  lightning_textures=sum(bool(t['kind'] & LIGHTNING) for t in report['textures'].values()),
                  sky_lightning_textures=sum(bool(t['kind'] & SKY_LIGHTNING) for t in report['textures'].values()))
    report['counts'] = dict(counts)
    (output / 'emission-catalog-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report
