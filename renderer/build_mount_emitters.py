#!/usr/bin/env python3
"""Stage proven creature/mount model lights without server mappings or DBC/WDB edits."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths
northlight_paths.use_source_modules()
import client_archives
from world_scene_builder import Assets, array, string, unpack
from build_spell_emitters import spell_sources, append_spell_lights, validate_spell_lights
from mpq import Archive

# Build 12340: Spell.EffectAura[95..97] -> creature entry [110..112],
# server QueryCreatureResponse -> display IDs, then native DBC model rows.
REFERENCES = [
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Spells/Auras/SpellAuraEffects.cpp',
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/game/Server/Packets/QueryPackets.cpp',
    'https://github.com/TrinityCore/TrinityCore/blob/3.3.5/src/server/shared/DataStores/DBCStructure.h',
]


def creature_cache(blob):
    if len(blob) < 24 or blob[:4] != b'BOMW' or unpack('<I', blob, 4)[0] != 12340:
        raise ValueError('Requires build 12340 creaturecache.wdb')
    result, at = {}, 24
    while at < len(blob):
        entry, size = unpack('<II', blob, at)
        at += 8
        if not entry and not size:
            if at != len(blob):
                raise ValueError('Trailing WDB data')
            break
        if size > 65536 or at + size > len(blob):
            raise ValueError('Invalid WDB record length')
        body = blob[at:at+size]
        at += size
        if entry & 0x80000000:
            continue
        cursor = 0
        for _ in range(6):  # Four names, title, cursor icon. No Lua execution.
            end = body.find(b'\0', cursor)
            if end < cursor or end-cursor > 4096:
                raise ValueError('Invalid WDB string')
            cursor = end+1
        if len(body)-cursor != 77:
            raise ValueError('Unknown 12340 creature record layout')
        result[entry] = tuple(v for v in unpack('<4I', body, cursor+24) if v)
    return result


def dbc(assets, name, fields):
    blob = assets.read('dbfilesclient\\'+name+'.dbc')
    magic, count, actual, size, strings = unpack('<4s4I', blob)
    if magic != b'WDBC' or actual != fields or size != fields*4 or len(blob) != 20+count*size+strings:
        raise ValueError('Unsupported 3.3.5 '+name+' schema')
    rows = array(blob, 20, count, '<%dI' % fields)
    if len({r[0] for r in rows}) != count:
        raise ValueError('Duplicate '+name+' IDs')
    return rows, blob[20+count*size:]


def mount_models(assets, cache):
    spells, _ = dbc(assets, 'spell', 234)
    displays, _ = dbc(assets, 'creaturedisplayinfo', 16)
    models, names = dbc(assets, 'creaturemodeldata', 28)
    entries = collections.defaultdict(list)
    for row in spells:
        for i in range(3):
            if row[71+i] == 6 and row[95+i] == 78 and 0 < row[110+i] < 0x80000000:
                entries[row[110+i]].append(row[0])
    display_by_id, model_by_id = {r[0]:r for r in displays}, {r[0]:r for r in models}
    def model_path(row):
        path = string(names, row[2]).lower().replace('/', '\\')
        return path[:-4]+'.m2' if path.endswith('.mdx') else path
    colors = collections.defaultdict(set)
    for row in displays:
        if row[13] and row[1] in model_by_id:
            colors[model_path(model_by_id[row[1]])].add(row[13])
    candidates, missing = {}, {}
    for entry, spells in sorted(entries.items()):
        if entry not in cache:
            missing[entry] = 'No current-server creature-cache record'
            continue
        for display in cache[entry]:
            row = display_by_id.get(display)
            model = model_by_id.get(row[1]) if row else None
            if not model:
                missing[entry] = 'Display/model ID absent in this client'
                continue
            path = model_path(model)
            if not path.startswith('creature\\') or not path.endswith('.m2') or any(p in ('.','..') for p in path.split('\\')):
                missing[entry] = 'Unsupported mount model path'
                continue
            item = candidates.setdefault(path, {'references': [], 'replacement_colors': set()})
            item['references'].append({'creature':entry, 'display':display, 'spells':sorted(set(spells))})
            item['replacement_colors'].update(colors[path])
    for item in candidates.values():
        item['replacement_colors'] = sorted(item['replacement_colors'])
    return candidates, missing


def build(assets, cache_blob, output, excluded=()):
    from attached_emitter_sources import write_stage
    candidates, missing = mount_models(assets,creature_cache(cache_blob))
    candidates = {p:i for p,i in candidates.items() if p not in excluded}
    return write_stage(assets,output,candidates,{
        'format':'native WotLK v264 cached mount lights',
        'unresolved_creatures':missing,'excluded_owned_models':sorted(excluded),'creature_cache_sha256':hashlib.sha256(cache_blob).hexdigest(),
        'references':REFERENCES,'limitations':[
            'Cached creature-entry mappings annotate this legacy scope only; build_creatures audits all creature model paths without WDB.',
            'Every supported independent anchor is preserved up to sixteen sources; unsupported regions remain reported.',
            'Shared model paths also emit when used by NPCs; original native model lifetime owns the lights.',
            'No installation or in-game validation performed.']},'mount','No proven mount emitters in the resolved client/server models')


def creature_models(assets):
    """All listed and client-DBC creature paths; never infer server creature IDs."""
    models,names = dbc(assets,'creaturemodeldata',28)
    displays,_ = dbc(assets,'creaturedisplayinfo',16)
    result = {p:{'model_ids':[],'replacement_colors':[]} for p in assets.providers
              if p.startswith('creature\\') and p.endswith('.m2')}
    by_id = {}
    for row in models:
        path = string(names,row[2])
        if path.endswith('.mdx'):
            path = path[:-4]+'.m2'
        if not path.startswith('creature\\') or not path.endswith('.m2') or any(p in ('.','..') for p in path.split('\\')):
            continue
        by_id[row[0]] = path
        result.setdefault(path,{'model_ids':[],'replacement_colors':[]})['model_ids'].append(row[0])
    for row in displays:
        if row[1] in by_id and row[13]:
            result[by_id[row[1]]]['replacement_colors'].append(row[13])
    for info in result.values():
        info['replacement_colors'] = sorted(set(info['replacement_colors']))
    return result


def build_creatures(assets, output, excluded=()):
    from attached_emitter_sources import write_stage
    candidates = {p:i for p,i in creature_models(assets).items() if p not in excluded}
    return write_stage(assets,output,candidates,{
        'format':'native WotLK v264 creature lights with original bone anchors and lifecycle',
        'scope':'Every listed Creature/*.m2 plus every CreatureModelData creature model path; independent of server cache',
        'server_creature_mappings_inferred':False,'excluded_owned_models':sorted(excluded),
        'references':REFERENCES,'limitations':[
            'Actual model proof applies to mounts and creatures sharing that path; no server creature-entry mapping is guessed.',
            'Particle rate/enabled tracks and original bones are retained; actual lifetime color, texture coverage and alpha determine energy.',
            'Rigid constant unlit additive meshes are supported; replacement, deforming, hidden-geoset and animated-material uncertainty is reported.',
            'All supported independent anchors are preserved up to sixteen; bridge admission remains capped at eight.',
            'Unlisted archive additions cannot be enumerated; existing native lights win.',
            'No installation or in-game validation performed.']},'creature','No supported creature emitters')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    client_archives.add_arguments(parser)
    parser.add_argument('--creature-cache', type=Path)
    parser.add_argument('--cached-mounts-only',action='store_true',help='Legacy scope: resolve only mounts known by this server cache')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assets = Assets.from_args(args)
    try:
        if args.creature_cache or args.cached_mounts_only:
            cache = args.creature_cache or assets.client/'Cache/WDB'/assets.locale/'creaturecache.wdb'
            result = build(assets, cache.read_bytes(), args.output)
        else:
            result = build_creatures(assets,args.output)
    finally:
        assets.close()
    print(json.dumps(result['counts'],indent=2))
