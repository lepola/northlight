#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""0.3.194: a patch archive without a (listfile) (private-server patches often strip it; the game does not
need it), a custom patch StormLib cannot open or a damaged file no longer fails the world cache build. On a
synthetic client of real StormLib archives (made here; no game bytes): common.MPQ, patch.MPQ, patch-R.MPQ
without a (listfile), patch-S.MPQ, patch-T.MPQ that is not an MPQ and patch-U.MPQ with a damaged texture.
- world_scene_builder.Assets: patch-R still wins every listed name it holds (probed with SFileHasFile,
  Light.dbc and Map.dbc as the art layer reads them) and loses to the later patch-S; a name no listing has
  is read from it and origin() names it; a tile only it adds is listed; patch-T is left out; both are in
  warnings() and fingerprint(); an unopenable common.MPQ or stock patch-3.MPQ still fails; the damaged
  file raises mpq.ReadError (an OSError and a ValueError); an archive that opens on a lock retry is kept;
  with no unlisted archive a missing name is not memoised; client_archives.is_custom_patch spares every
  archive Blizzard shipped.
- the real world_scene_builder on the synthetic tiles (32_48 replaced by patch-R, 33_48 only in patch-R,
  both textured with the damaged texture): exit 0, the terrain comes from patch-R, the texture is
  unsupported, one archive_warning line per archive, the report's archive_warnings.
- install_world_cache end to end (the real scene builder, stand-ins for lights, fog, celestial and
  validation): done, each warning shown once in human progress although every builder reports it, the
  manifest's tolerated archive_warnings; a lights-only update keeps and shows them again."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import contextlib, io, json, shutil, struct, subprocess
import client_archives, install_world_cache as iwc, world_scene_builder
from mpq import Archive, ReadError
from world_scene_builder import Assets

out = fp.output_dir()
ADT = 'World\\Maps\\Azeroth\\Azeroth_32_48.adt'
NEW_ADT = 'World\\Maps\\Azeroth\\Azeroth_33_48.adt'
TEXTURE = 'Textures\\Damaged.blp'


def chunk(tag, payload):
    return tag[::-1].encode() + struct.pack('<I', len(payload)) + payload


def adt(height, texture=b''):
    """A one-MCNK ADT: flat terrain at `height`, its first MTEX texture (if any) as the base layer, no models."""
    header = bytearray(128)
    struct.pack_into('<I', header, 20, 128 + 8)   # MCVT, relative to the MCNK chunk header
    return chunk('MVER', struct.pack('<I', 18)) + (chunk('MTEX', texture + b'\0') if texture else b'') + \
        chunk('MCNK', bytes(header) + chunk('MCVT', struct.pack('<145f', *[height] * 145)))


def archive(path, files, listfile=True):
    path.parent.mkdir(parents=True, exist_ok=True)
    with Archive(path, create=True, capacity=16, listfile=listfile) as a:
        for i, (name, data) in enumerate(files.items()):
            source = out / f'payload-{path.stem}-{i}.bin'
            source.write_bytes(data)
            a.add(source, name)


def make_client(root, broken=None):
    """The synthetic client; `broken` names an archive that is replaced by bytes StormLib cannot open."""
    shutil.rmtree(root, ignore_errors=True)
    data = root / 'Data'
    archive(data / 'common.MPQ', {ADT: adt(0.), 'DBFilesClient\\Light.dbc': b'common', 'DBFilesClient\\Map.dbc': b'common'})
    archive(data / 'patch.MPQ', {'DBFilesClient\\Light.dbc': b'patch'})
    archive(data / 'patch-3.MPQ', {'Interface\\Patch3.txt': b'patch-3'})
    archive(data / 'patch-R.MPQ', {ADT: adt(10., TEXTURE.encode()), NEW_ADT: adt(20., TEXTURE.encode()),
                                   'DBFilesClient\\Light.dbc': b'patch-R', 'DBFilesClient\\Map.dbc': b'patch-R',
                                   'World\\Generic\\new.m2': b'only in patch-R'}, listfile=False)
    archive(data / 'patch-S.MPQ', {'DBFilesClient\\Light.dbc': b'patch-S'})
    (data / 'patch-T.MPQ').write_bytes(b'MPQ\x1a damaged')
    archive(data / 'patch-U.MPQ', {TEXTURE: bytes(range(256)) * 64})
    damaged = bytearray((data / 'patch-U.MPQ').read_bytes())
    for i in range(48, 200):   # inside the texture's compressed sectors; the (listfile) is written after them
        damaged[i] ^= 0x5a
    (data / 'patch-U.MPQ').write_bytes(bytes(damaged))
    archive(data / 'enUS/locale-enUS.MPQ', {'Interface\\Locale.txt': b'enUS'})
    if broken:
        (data / broken).write_bytes(b'not an MPQ')
    return root


client = make_client(out / 'client')
with Archive(client / 'Data/patch-R.MPQ') as a:
    assert not a.has('(listfile)') and a.has(ADT)
    try:
        a.names()
        raise AssertionError('patch-R.MPQ has a (listfile)')
    except FileNotFoundError:
        pass

# 1. Assets: the unlisted archive keeps its place in the priority order.
assets = Assets(client, 'all', 'enUS')
try:
    assert [p.name for p in assets.paths] == ['common.MPQ', 'patch.MPQ', 'patch-3.MPQ', 'patch-R.MPQ', 'patch-S.MPQ', 'patch-U.MPQ',
                                              'locale-enUS.MPQ']
    assert assets.unlisted == ['Data/patch-R.MPQ'] and assets.unreadable == ['Data/patch-T.MPQ'], (assets.unlisted, assets.unreadable)
    assert assets.read('DBFilesClient\\Light.dbc') == b'patch-S'                    # a later listed archive still wins
    assert assets.read('dbfilesclient/map.dbc') == b'patch-R'                       # patch-R replaces common's
    assert assets.origin(ADT) == 'Data/patch-R.MPQ' and assets.read(ADT) == adt(10., TEXTURE.encode())
    assert assets.providers[NEW_ADT.lower()] == assets.providers[ADT.lower()]       # the ADT grid probe
    assert 'world\\generic\\new.m2' not in assets.providers                         # only enumeration misses it
    assert assets.read('World\\Generic\\new.m2') == b'only in patch-R' and assets.origin('world\\generic\\new.m2') == 'Data/patch-R.MPQ'
    try:
        assets.read('World\\Generic\\absent.m2')
        raise AssertionError('absent.m2 was read')
    except FileNotFoundError:
        pass
    try:
        assets.read(TEXTURE)
        raise AssertionError('the damaged texture was read')
    except ReadError as e:
        assert isinstance(e, OSError) and isinstance(e, ValueError) and 'patch-U.MPQ' in str(e), e
    assert [w['problem'] for w in assets.warnings()] == ['unlisted', 'unreadable'], assets.warnings()
    fingerprint = assets.fingerprint()
    assert fingerprint['unlisted'] == ['Data/patch-R.MPQ'] and fingerprint['unreadable'] == ['Data/patch-T.MPQ']
    assert 'data/patch-t.mpq' not in [r['archive'].lower() for r in fingerprint['archives']]
finally:
    assets.close()
for broken in ['common.MPQ', 'patch-3.MPQ']:   # Blizzard's own archives: the client is broken
    try:
        Assets(make_client(out / 'broken-client', broken), 'all', 'enUS').close()
        raise AssertionError(f'an unopenable {broken} was tolerated')
    except OSError as e:
        assert broken in str(e), e

# A custom patch a virus scanner holds for a moment is retried (LOCK_RETRIES is Windows only), not left out.
failed, real_archive, real_retries = set(), world_scene_builder.Archive, world_scene_builder.LOCK_RETRIES
def flaky(path):
    if path.name == 'patch-S.MPQ' and path not in failed:
        failed.add(path)
        raise OSError(f'Cannot open MPQ: {path}')
    return real_archive(path)
world_scene_builder.Archive, world_scene_builder.LOCK_RETRIES = flaky, (0.,)
try:
    assets = Assets(client, 'all', 'enUS')
    assert failed and assets.unreadable == ['Data/patch-T.MPQ'] and assets.read('DBFilesClient\\Light.dbc') == b'patch-S'
    assets.close()
finally:
    world_scene_builder.Archive, world_scene_builder.LOCK_RETRIES = real_archive, real_retries
# The stock view has no unlisted archive: nothing to probe, nothing memoised.
assets = Assets(client, 'stock', 'enUS')
try:
    assert not assets.blind and assets.read(ADT) == adt(0.)
    try:
        assets.read('World\\Generic\\new.m2')
        raise AssertionError('new.m2 was read without patch-R')
    except FileNotFoundError:
        assert assets.probed == {}
finally:
    assets.close()
for name, custom in [('patch.mpq', False), ('patch-2.MPQ', False), ('patch-3.mpq', False), ('patch-enUS.MPQ', False),
                     ('patch-enus-2.mpq', False), ('patch-enUS-3.mpq', False), ('common.mpq', False), ('locale-enUS.MPQ', False),
                     ('patch-4.mpq', True), ('patch-R.MPQ', True), ('patch-enUS-x.mpq', True), ('patch-custom.mpq', False)]:
    assert client_archives.is_custom_patch(Path('Data') / name) == custom, name

# 2. The real scene builder on the synthetic tile.
scene = out / 'scene'
shutil.rmtree(scene, ignore_errors=True)
r = subprocess.run([sys.executable, '-I', '-B', '-X', 'utf8', str(fp.tracked('world_scene_builder.py')), '--instanced',
                    '--map', 'Azeroth', '--client', str(client), '--locale', 'enUS', '--output', str(scene)],
                   capture_output=True, text=True, timeout=300)
assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]
lines = [json.loads(line) for line in r.stdout.splitlines() if line.startswith('{')]
assert [e['archive_warning']['archive'] for e in lines if 'archive_warning' in e] == ['Data/patch-R.MPQ', 'Data/patch-T.MPQ']
(report_path,) = scene.glob('build-Azeroth-*.json')
report = json.loads(report_path.read_text())
assert [w['archive'] for w in report['archive_warnings']] == ['Data/patch-R.MPQ', 'Data/patch-T.MPQ']
assert not report['failures'] and [g['tile'] for g in report['generated']] == [[32, 48], [33, 48]], report['failures']
assert [u.split(':')[0] + ':' + u.split(':')[1] for u in report['unsupported_assets']] == ['texture:textures\\damaged.blp']
for tile, height in [('32_48', 10.), ('33_48', 20.)]:
    terrain_key = (scene / f'Azeroth/{tile}.fg3').read_bytes()[12:44]
    terrain = (scene / 'models' / f'{terrain_key.hex()}.fgs').read_bytes()
    assert struct.unpack_from('<8f', terrain, 20)[2] == height, f'the terrain of {tile} is not patch-R\'s'

# 3. install_world_cache end to end; the post steps are stand-ins that write what the orchestrator checks.
STAND_IN = '''import json, sys
from pathlib import Path
a, name = sys.argv[1:], Path(__file__).name
opt = lambda k: Path(a[a.index(k) + 1])
if name == 'world_lights_builder.py':
    (opt('--output')).mkdir(parents=True, exist_ok=True); (opt('--output') / 'Azeroth.fgl').write_text('lights')
elif name == 'regional_fog_builder.py':
    (opt('--output')).mkdir(parents=True, exist_ok=True)
    (opt('--output') / 'manifest.json').write_text(json.dumps({'maps': {'Azeroth': {'tiles': 2, 'expected': 2}},
                                                               'stats': {'wmo_roots': 0}, 'unreadable_wmos': {}}))
elif name == 'build_celestial_disc_assets.py':
    (opt('--output')).mkdir(parents=True, exist_ok=True)
    for b in ('sun', 'moon'): (opt('--output') / f'{b}.fct').write_text(b)
else:
    opt('--report').write_text('{}')
'''
stand_ins = out / 'stand-ins'
stand_ins.mkdir(exist_ok=True)
for name in ['world_lights_builder.py', 'regional_fog_builder.py', 'build_celestial_disc_assets.py', 'validate_world_cache.py']:
    (stand_ins / name).write_text(STAND_IN)
tracked = fp.tracked
iwc.fp.tracked = lambda name: stand_ins / name if (stand_ins / name).is_file() else tracked(name)
iwc.total_memory = iwc.available_memory = lambda: 16 << 30
cache = out / 'world-cache'
for folder in [cache, *iwc.side_folders(cache)]:
    shutil.rmtree(folder, ignore_errors=True)


def run():
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = iwc.main(['--client', str(client), '--locale', 'enUS', '--output', str(cache), '--maps', 'Azeroth',
                         '--jobs', '1', '--progress', 'human'])
    text = buffer.getvalue()
    return code, json.loads(text.splitlines()[-1]), text


try:
    code, last, text = run()
    assert (code, last['event']) == (0, 'done'), text[-3000:]
    unlisted = 'Warning: Data/patch-R.MPQ has no (listfile), so its files could not be listed.'
    unopened = 'Warning: Data/patch-T.MPQ could not be opened as an MPQ archive, so the world cache is built without it'
    assert str(out) not in text.split('World cache: ')[1].split('World cache ready')[0], 'a client path in a warning'
    assert text.count(unlisted) == 1 and text.count(unopened) == 1, text
    assert 'new files only it adds may be missing from the static shadows and GI' in text
    assert 'to try it again, delete the world-cache folder and run the installer again' in text
    manifest = json.loads((cache / 'install-manifest.json').read_text())
    assert [w['archive'] for w in manifest['tolerated']['archive_warnings']] == ['Data/patch-R.MPQ', 'Data/patch-T.MPQ']
    assert not manifest['problems'] and (cache / 'Azeroth/33_48.fg3').is_file()
    # A lights-only update runs no scene builder: the recorded warnings stay, and are shown again.
    (stand_ins / 'world_lights_builder.py').write_text(STAND_IN + '# changed\n')
    code, last, text = run()
    assert (code, last['event'], last.get('steps')) == (0, 'done', ['lights']), text[-3000:]
    assert text.count(unlisted) == 1 and text.count(unopened) == 1, text
    manifest = json.loads((cache / 'install-manifest.json').read_text())
    assert [w['archive'] for w in manifest['tolerated']['archive_warnings']] == ['Data/patch-R.MPQ', 'Data/patch-T.MPQ']
finally:
    iwc.fp.tracked = tracked
print('unlisted archive: probed, read, reported; scene build and install_world_cache pass with the warning')
