#!/usr/bin/env python3
"""Stage optional light-only equipment MPQ from the user's own WotLK client.

No client file is installed or modified. Native lights and unsupported dynamic
colour/replacement/ribbon materials are preserved; detailed exclusions are kept.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import struct
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import client_archives
from world_scene_builder import Assets, array, unpack, string, decode_blp, srgb_to_linear
from world_lights_builder import constant_track
from emitter_tracks import pulse_for

NATIVE_REFERENCE = 'https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/'
REFERENCES = [NATIVE_REFERENCE + 'README.md',
              NATIVE_REFERENCE + 'Engine/Source/Model2/M2Model.cpp#L7394',
              NATIVE_REFERENCE + 'Engine/Source/Model2/M2Model.cpp#L2513',
              NATIVE_REFERENCE + 'Engine/Source/Services/ParticleSystem2.cpp#L804',
              NATIVE_REFERENCE + 'Engine/Source/Gx/Types.h#L23']


def lifetime_track(blob, offset, fmt):
    count, times, keys, values = unpack('<4I', blob, offset)
    if not 0 < count == keys <= 128:
        raise ValueError('Invalid lifetime track counts')
    stamps = [v for (v,) in array(blob, times, count, '<H')]
    if stamps[-1] > 32767 or any(a >= b for a,b in zip(stamps, stamps[1:])):
        raise ValueError('Invalid lifetime timestamps')
    return list(zip(stamps, array(blob, values, keys, fmt)))


def lifetime_colour(colors, alpha):
    """Integrate linearly interpolated RGB × opacity over actual key intervals."""
    if not colors or not alpha or not all(math.isfinite(v) and 0 <= v <= 255.01 for _,c in colors for v in c) or not all(0 <= a[0] <= 32767 for _,a in alpha):
        raise ValueError('Invalid lifetime colour/alpha')
    def sample(curve, t):
        if t <= curve[0][0]:
            return curve[0][1]
        for (a, va), (b, vb) in zip(curve, curve[1:]):
            if t <= b:
                return tuple(x + (y-x)*(t-a)/(b-a) for x,y in zip(va, vb))
        return curve[-1][1]
    times = sorted({0, 32767, *(t for t,_ in colors), *(t for t,_ in alpha)})
    rgb, opacity = [0., 0., 0.], 0.
    for a,b in zip(times, times[1:]):
        ca, cb, aa, ab = sample(colors,a), sample(colors,b), sample(alpha,a)[0], sample(alpha,b)[0]
        opacity += (b-a)*(aa+ab)/2
        for k in range(3):
            dc, da = cb[k]-ca[k], ab-aa
            rgb[k] += (b-a)*(ca[k]*aa + (ca[k]*da+aa*dc)/2 + dc*da/3)
    if not opacity:
        raise ValueError('No visible lifetime emission')
    return tuple(v/opacity/255 for v in rgb), opacity/(32767*32767)


def item_visuals(assets):
    """3.3.5 ItemVisualEffects has exactly ID + model-path string offset."""
    blob = assets.read('dbfilesclient\\itemvisualeffects.dbc')
    magic, count, fields, size, strings = unpack('<4s4I', blob)
    if magic != b'WDBC' or fields != 2 or size != 8 or len(blob) != 20 + count * size + strings:
        raise ValueError('Unsupported ItemVisualEffects schema')
    table = blob[20 + count * size:]
    result = set()
    for _, offset in array(blob, 20, count, '<II'):
        name = string(table, offset)
        if name.endswith('.mdx'):
            name = name[:-4] + '.m2'
        if name.endswith('.m2'):
            result.add(name)
    return result


def texture_rgb(assets, name, alpha_blend=True):
    _, _, rgba = decode_blp(assets.read(name), 64)
    rgb = [0., 0., 0.]
    for p in range(0, len(rgba), 4):
        values = [srgb_to_linear(v / 255.) for v in rgba[p:p+3]]
        alpha = rgba[p+3] / 255.
        w = alpha if alpha_blend else 1.
        for k in range(3):
            rgb[k] += values[k] * w
    if not max(rgb):
        raise ValueError('No visible texture emission')
    # Mean radiance retains dark texels and alpha coverage. Normalizing only
    # the bright pixels turned weak glows into equally strong white emitters.
    return tuple(v / (len(rgba)//4) for v in rgb)


def sources(assets, blob, max_sources=16):
    if len(blob) < 304 or blob[:4] != b'MD20' or unpack('<I', blob, 4)[0] != 264:
        raise ValueError('Not WotLK v264')
    if unpack('<I', blob, 264)[0]:
        raise ValueError('Existing native lights preserved')
    bones, bone_offset = unpack('<II', blob, 44)
    if not bones or bone_offset < 304 or bone_offset + bones * 88 > len(blob):
        raise ValueError('Invalid bones')
    nt, ot = unpack('<II', blob, 80)
    textures = array(blob, ot, nt, '<4I')
    count, offset = unpack('<II', blob, 296)
    stride = 492 if unpack('<I', blob, 16)[0] & 0x200 else 476
    particles = array(blob, offset, count, '<%ds' % stride)
    result, refusals = [], collections.Counter()
    for i, (particle,) in enumerate(particles):
        try:
            flags = unpack('<I', particle, 4)[0]
            bone, texture = unpack('<hH', particle, 20)
            blend, emitter_type, replacement = unpack('<BBh', particle, 40)
            # 12340 CM2Model: particle 3 -> Gx NoAlphaAdd, 4 -> Gx Add;
            # particle 2 is ordinary alpha blending. See REFERENCES.
            # 12340 maps M2 flag 0x10 to emitter 0x200: particles follow
            # the emitter transform at render time. This is suitable for a
            # bone light, not a reason to exclude weapon flame/glow models.
            if blend not in (3, 4) or replacement not in (-1, 0) or flags & (0x8000 | 0x10000000):
                raise ValueError('Unsupported blend, replacement colour, burst or multitexture')
            if emitter_type not in (1, 2) or unpack('<I', particle, 24)[0] > 1 or unpack('<I', particle, 32)[0] > 1:
                raise ValueError('Noncompact, model or child particle emitter')
            if not 0 <= bone < bones or texture >= len(textures):
                raise ValueError('Invalid source bone/texture')
            chain, parent = set(), bone
            while parent >= 0:
                if parent >= bones or parent in chain:
                    raise ValueError('Invalid bone parent chain')
                chain.add(parent)
                parent = unpack('<h', blob, bone_offset + parent * 88 + 8)[0]
            tex = textures[texture]
            if tex[0] != 0 or not tex[2]:
                raise ValueError('Dynamic texture replacement')
            name = string(blob, tex[3])
            p = offset + i * stride
            if constant_track(blob, p + 456, '<B', (1,))[0] != 1:
                raise ValueError('Emitter disabled')
            rate = constant_track(blob, p + 176, '<f')[0]
            if not 0 < rate < 100000:
                raise ValueError('No continuous emission')
            for relative in (200, 220):
                extent = constant_track(blob, p + relative, '<f', (0.,))[0]
                if not 0 <= extent <= 4:
                    raise ValueError('Emitter area is not compact')
            alpha = lifetime_track(blob, p + 276, '<h')
            color, opacity = lifetime_colour(lifetime_track(blob, p + 260, '<3f'), alpha if blend == 4 else [(0, (32767,))])
            tex_rgb = texture_rgb(assets, name, alpha_blend=blend == 4)
            rgb = tuple(color[k] * tex_rgb[k] for k in range(3))
            peak = max(rgb)
            intensity = min(1., peak) * opacity * .35
            if intensity < .00005:
                raise ValueError('Negligible actual emission')
            position = unpack('<3f', particle, 8)
            neighbour = next((e for e in result if e['bone'] == bone and sum((a-b)**2 for a,b in zip(e['position'], position)) < .25**2), None)
            if neighbour:
                combined = [neighbour['rgb'][k]*neighbour['intensity'] + rgb[k]/peak*intensity for k in range(3)]
                power = max(combined)
                neighbour['rgb'], neighbour['intensity'] = tuple(v/power for v in combined), power
                neighbour['particles'].append(i)
                neighbour['particle_blends'].append(blend)
                neighbour['particle_flags'].append(flags)
                if name not in neighbour['textures']:
                    neighbour['textures'].append(name)
                continue
            result.append({'bone': bone, 'position': position, 'rgb': tuple(v/peak for v in rgb),
                           'intensity': intensity, 'start': .5, 'end': 8.,
                           'particles': [i], 'particle_blends': [blend], 'particle_flags': [flags], 'textures': [name], 'basis': 'native 12340 additive particle, lifetime colour × actual texture; original animated bone',
                           'rate': rate})
            result[-1]['emission'] = 'fire' if any(s in name for s in ('flame', 'fire', 'candle')) else 'energy'
        except (ValueError, FileNotFoundError, struct.error) as exc:
            refusals[str(exc)] += 1
    if not result:
        raise ValueError('No proven continuous additive source: ' + json.dumps(refusals, sort_keys=True))
    if max_sources is not None:
        if len(result) > max_sources:
            raise ValueError('Source budget exceeded: %d independent sources (limit %d); none silently omitted' % (len(result), max_sources))
    return result


def validate_rgb(blob, offset, source, index=0):
    pulse = pulse_for(source, index)
    if not pulse:
        if not all(math.isclose(x,y,rel_tol=1e-6,abs_tol=1e-6) for x,y in zip(constant_track(blob, offset, '<3f'), source['rgb'])):
            raise ValueError('Actual emission colour mismatch')
        return
    mode, sequence, nt, ot, nk, ok = unpack('<hh4I', blob, offset)
    tc, to = unpack('<II', blob, ot)
    kc, ko = unpack('<II', blob, ok)
    globals_count, globals_offset = unpack('<II', blob, 20)
    if mode != 1 or nt != 1 or nk != 1 or not 0 <= sequence < globals_count or unpack('<I', blob, globals_offset+sequence*4)[0] != pulse['period_ms']:
        raise ValueError('Native pulse clock mismatch')
    times = [v for (v,) in array(blob,to,tc,'<I')]
    keys = array(blob,ko,kc,'<3f')
    if times != pulse['times_ms'] or kc != len(pulse['factors']) or any(not math.isclose(v, source['rgb'][k]*factor,rel_tol=1e-6,abs_tol=1e-6) for rgb,factor in zip(keys,pulse['factors']) for k,v in enumerate(rgb)):
        raise ValueError('Native pulse colour mismatch')


def validate_lights(original, patched, emitters):
    """Read back every native record through the existing strict track parser."""
    if patched[44:52] != original[44:52]:
        raise ValueError('Source bone array changed')
    count, offset = unpack('<II', patched, 264)
    if count != len(emitters):
        raise ValueError('Native light count mismatch')
    for i, source in enumerate(emitters):
        record = offset + i * 156
        kind, bone, *position = unpack('<HH3f', patched, record)
        if kind != 1 or bone != source['bone'] or not all(math.isclose(a,b,rel_tol=1e-6,abs_tol=1e-6) for a,b in zip(position,source['position'])):
            raise ValueError('Native source anchor mismatch')
        validate_rgb(patched, record+56, source, i)
        for relative, fmt, wanted in ((16,'<3f',(0,0,0)), (36,'<f',(0,)),
                                      (76,'<f',(source['intensity'],)), (96,'<f',(source['start'],)),
                                      (116,'<f',(source['end'],)), (136,'<B',(1,))):
            actual = constant_track(patched, record+relative, fmt)
            if not all(math.isclose(a,b,rel_tol=1e-6,abs_tol=1e-6) for a,b in zip(actual,wanted)):
                raise ValueError('Native source track mismatch')


def build(assets, output):
    from attached_emitter_sources import write_stage
    visual_models = {p for p in item_visuals(assets) if p.startswith(('spells\\', 'item\\'))}
    candidates = {p:{'item_visual':p in visual_models} for p in
        set(n for n in assets.providers if n.startswith('item\\') and n.endswith('.m2')) | visual_models}
    return write_stage(assets,output,candidates,{
        'format':'native WotLK v264 equipment lights with original bones and lifecycle',
        'scope':'Every listed Item/*.m2 plus ItemVisualEffects model paths',
        'format_references':REFERENCES,
        'limitations':['Inline step/linear particle rate and enabled-state are retained; lifetime RGB and alpha are integrated.',
            'Mesh emission requires proven constant unlit additive material, actual UV texture/tint/opacity and rigid original skin bone.',
            'Replacement textures/colors, deforming or display-controlled luminous mesh and unsupported schedules remain reported exclusions.',
            'All independent regions are retained up to sixteen native sources per model; models beyond the budget are explicitly excluded.',
            'Runtime dynamic admission remains capped at eight, shared with local-light limits; no runtime model scans.',
            'Unlisted archive additions cannot be enumerated; native existing lights are preserved.',
            'No installation or in-game test performed.']},'equipment','No supported equipment emitters')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    client_archives.add_arguments(parser)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assets = Assets.from_args(args)
    try:
        result = build(assets, args.output)
    finally:
        assets.close()
    print(json.dumps(result['counts'], indent=2))
