"""Stage gameobjects and unowned world props with their native light lifecycle."""
from attached_emitter_sources import write_stage
from build_mount_emitters import dbc
from world_scene_builder import string

EMPTY = 'No supported gameobject emitters'


def models(assets):
    rows, names = dbc(assets, 'gameobjectdisplayinfo', 19)
    result = {p: {'displays': []} for p in assets.providers
              if p.startswith('world\\') and p.endswith('.m2') and ':' not in p
              and not any(v in ('', '.', '..') for v in p.split('\\'))}
    for row in rows:
        path = string(names, row[1])
        if path.endswith('.mdx'):
            path = path[:-4] + '.m2'
        if (path.startswith(('world\\', 'spells\\', 'creature\\', 'item\\')) and path.endswith('.m2') and ':' not in path and
                not any(p in ('', '.', '..') for p in path.split('\\'))):
            result.setdefault(path, {'displays': []})['displays'].append(row[0])
    return result


def build(assets, output, excluded=()):
    candidates = {p: v for p, v in models(assets).items() if p not in excluded}
    return write_stage(assets, output, candidates, {
        'format': 'native WotLK v264 gameobject/world lights with original bones and lifecycle',
        'scope': 'Every listed World model plus GameObjectDisplayInfo paths without another lighting owner',
        'limitations': ['Native model lifetime follows placement load/unload and server spawn/harvest/despawn.',
                        'Only proven additive emission is staged; ordinary foliage stays unlit.',
                        'Existing native sources and unsupported material animation are preserved.']
    }, 'gameobject', EMPTY)
