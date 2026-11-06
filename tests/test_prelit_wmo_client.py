#!/usr/bin/env python3
# northlight-test: requires=client,stormlib,zig,windows-exe
"""Read-only multi-instance/city assets and real WMO shader proofs."""
import hashlib
from contextlib import closing
import json
import re
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from extract_signatures import variants
from extract_signatures import fnv1a64
from extract_wmo_context import lit_wmo
from instance_maps import read_wdt
from world_scene_builder import Assets, chunks, wmo_group

out = fp.output_dir()
report = []
with closing(Assets()) as assets:
    for name in ('IcecrownCitadel', 'HellfireMilitary', 'Shadowfang', 'StormwindJail', 'DeadminesInstance',
                 'OnyxiaLairInstance', 'BlackwingLair', 'Karazahn', 'HellfireRampart', 'Valgarde70',
                 'UtgardePinnacle', 'Nexus70', 'Stratholme Raid', 'UlduarRaid', 'StormwindWorld'):
        wdt = (dict(tiles=[], mwmo='world/wmo/azeroth/buildings/stormwind/stormwind.wmo') if name == 'StormwindWorld'
               else read_wdt(assets.read(f'World/Maps/{name}/{name}.wdt')))
        paths = {wdt['mwmo']} if wdt['mwmo'] else set()
        for x, y in wdt['tiles']:
            root = dict(chunks(assets.read(f'World/Maps/{name}/{name}_{x}_{y}.adt')))
            paths.update(p.decode() for p in root.get('MWMO', b'').split(b'\0') if p)
        groups = []
        for path in sorted(paths):
            root = dict(chunks(assets.read(path)))
            count = struct.unpack_from('<I', root['MOHD'], 4)[0]
            for group in range(count):
                gp = path[:-4] + f'_{group:03}.wmo'
                blob = assets.read(gp)
                top, _ = wmo_group(blob)
                header = top['MOGP']
                flags = struct.unpack_from('<I', header, 8)[0]
                colors = dict(chunks(header, 68)).get('MOCV', b'')
                assert len(colors) % 4 == 0
                if colors:
                    values = list(struct.iter_unpack('<4B', colors))
                    groups.append(dict(path=gp, sha256=hashlib.sha256(blob).hexdigest(),
                                       indoor=not flags & 0x48, vertices=len(values),
                                       mean_bgr=[sum(c[k] for c in values) / len(values) / 255 for k in range(3)]))
        assert paths, name
        if name in ('IcecrownCitadel', 'HellfireMilitary', 'StormwindWorld'):
            assert groups and any(g['indoor'] for g in groups), name
        report.append(dict(map=name, tiles=len(wdt['tiles']), wmos=len(paths), vertex_color_groups=groups))

header = []
calls = []
with closing(Assets(archives='stock')) as assets:
    for sm in (2, 3):
        path = f'shaders/vertex/vs_{sm}_0/MapObjDiffuse_T1.bls'
        code = {i: b for i, _, b in variants(assets.read(path))}
        for variant in (0, 1):
            words = struct.unpack(f'<{len(code[variant]) // 4}I', code[variant])
            name = f'sm{sm}v{variant}'
            header.append(f'static const uint32_t {name}[]={{' + ','.join(hex(w) for w in words) + '};')
            calls.append(f'assert(prelitShader({name},sizeof({name})/4,true)=={str(variant == 0).lower()});')
            calls.append(f'assert(!prelitShader({name},sizeof({name})/4,false));')
    # Audit every registered WMO identity available in the stock archives, not
    # just the representative maps. The Python outdoor-light proof is separate
    # from the production C++ backward RGB slice used for prelit classification.
    registered = {int(h, 16) for h in re.findall(r'UINT64_C\(0x([0-9a-f]+)\)', fp.src('wmo_shader_signatures.h').read_text())}
    seen = set()
    lit_count = 0
    for archive in assets.archives:
        for path in sorted(archive.names()):
            path = path.lower().replace('/', '\\')
            if not (path.startswith(('shaders\\vertex\\vs_1_1\\', 'shaders\\vertex\\vs_2_0\\', 'shaders\\vertex\\vs_3_0\\')) and '\\mapobj' in path and path.endswith('.bls')):
                continue
            for _, _, code in variants(archive.read(path)):
                identity = fnv1a64(code)
                if identity not in registered or identity in seen:
                    continue
                seen.add(identity)
                name = f'wmo{len(seen)}'
                words = struct.unpack(f'<{len(code) // 4}I', code)
                header.append(f'static const uint32_t {name}[]={{' + ','.join(hex(w) for w in words) + '};')
                calls.append(f'accepted+=prelitShader({name},sizeof({name})/4,true);')
                if lit_wmo(code):
                    lit_count += 1
                    calls.append(f'assert(!prelitShader({name},sizeof({name})/4,true));')
    # The generated identity catalog is a union of client versions. Audit all
    # bytecode actually present here, including versions superseded by patches.
    # Missing historical bytecode cannot be reconstructed from its hash.
    assert seen and seen <= registered
    assert lit_count > 0
source = out / 'prelit-client.cpp'
source.write_text('#include "unlit_receiver.h"\n#include <cassert>\n#include <cstdio>\n' + '\n'.join(header) +
                  '\nint main(){using namespace NorthlightUnlitReceiver;unsigned accepted=0;\n' + '\n'.join(calls) +
                  f'\nassert(accepted>0);printf("PRELIT_CLIENT_SHADER_PASS identities={len(seen)} unavailableHistorical={len(registered-seen)} outdoorRejected={lit_count} prelitAccepted=%u sm2 sm3 identity_required\\n",accepted);}}\n', encoding='utf-8')
exe = out / 'prelit-client.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                *fp.include_flags(), str(source), '-o', str(exe)], check=True, env=fp.zig_env())
subprocess.run([str(exe)], check=True)
(out / 'interior-assets.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
for row in report:
    print(row['map'], 'vertex-colour groups:', len(row['vertex_color_groups']),
          'indoor:', sum(g['indoor'] for g in row['vertex_color_groups']))
