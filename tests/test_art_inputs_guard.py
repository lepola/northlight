#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,stormlib
"""Mod overlay resolution and the real startup guard across MPQ add/remove/replace/rebuild."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import client_archives as ca
from build_art_layer import INPUTS_FILE, write_inputs
from mpq import Archive
from world_scene_builder import Assets, skin_vertex_indices

out = fp.output_dir()
client = Path(tempfile.mkdtemp(prefix='guard-client-', dir=out))
name = r'interface\glues\models\ui_scourge\ui_scourge.m2'
skin_name = name[:-3] + '00.skin'
stock, hd = bytearray(304), bytearray(304)
for model, vertices, views in [(stock, 3, 1), (hd, 6, 4)]:
    model[:4] = b'MD20'
    struct.pack_into('<I', model, 4, 264)
    struct.pack_into('<I', model, 60, vertices)
    struct.pack_into('<I', model, 68, views)
skin = bytearray(48)
skin[:4] = b'SKIN'
struct.pack_into('<II', skin, 4, 3, 48)
struct.pack_into('<II', skin, 12, 3, 54)
skin.extend(struct.pack('<6H', 0, 4, 5, 0, 1, 2))


def archive(relative, files):
    path = client / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    with Archive(path, create=True, capacity=16) as target:
        for i, (key, data) in enumerate(files.items()):
            temp = out / f'guard-payload-{i}.bin'
            temp.write_bytes(data)
            target.add(temp, key)
    return path


archive('Data/common.MPQ', {'test': b'base'})
archive('Data/enGB/locale-enGB.MPQ', {name: bytes(stock)})
mod = archive('Data/patch-I.MPQ', {name: bytes(hd), skin_name: bytes(skin)})
mod_bytes = mod.read_bytes()
assets = Assets(client, locale='enGB', without='z')
try:
    assert assets.origin(name) == 'Data/patch-I.MPQ'
    assert assets.origin(skin_name) == 'Data/patch-I.MPQ'
    assert skin_vertex_indices(assets.read(name), assets.read(skin_name)) == [0, 4, 5]
    try:
        skin_vertex_indices(stock, assets.read(skin_name))
    except ValueError:
        pass
    else:
        raise AssertionError('Stock M2 / HD SKIN destructive control accepted')
finally:
    assets.close()

exe = out / 'art-inputs-guard.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                '-Wall', '-Wextra', '-Werror', '-municode', *fp.include_flags(),
                str(fp.TESTS / 'art_inputs_guard.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)


def snapshot():
    expected = ca.fingerprint(ca.chain(client, locale='enGB', without='z'), client)
    write_inputs(client, client, 'enGB', 'z', 'all', expected)
    return expected


def check(status):
    subprocess.run([str(exe), str(client.resolve()), status], check=True)


check('legacy-no-manifest')
old = snapshot()
check('current')
archive('Data/patch-z.mpq', {name: b'owned generated model'})
archive('Data/enGB/patch-engb-z.mpq', {name: b'owned generated model'})
check('current')
mod.unlink()
check('archive-removed')
mod.write_bytes(mod_bytes)
snapshot()
check('current')
added = client / 'Data/patch-T.mpq'
added.write_bytes(b'new pack')
check('archive-added')
snapshot()
check('current')
stamp = mod.stat()
os.utime(mod, ns=(stamp.st_atime_ns, stamp.st_mtime_ns + 1000000000))
check('archive-changed')
try:
    write_inputs(client, client, 'enGB', 'z', 'all', old)
except ValueError:
    pass
else:
    raise AssertionError('Generation accepted MPQ inputs changed during build')
snapshot()
check('current')
(client / 'Data/patch-custom.mpq').write_bytes(b'not loaded by WoW')
check('current')
(client / INPUTS_FILE).write_text('[NorthlightAssets]\nVersion=99\n', encoding='utf-16')
check('manifest-invalid')
print('PASS: stock/locale/mod overlay, M2/SKIN pair, startup add/remove/replace, rebuild and invalid snapshot')
