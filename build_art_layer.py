#!/usr/bin/env python3
"""Build the lighting art layer (patch-z) from a client's own Light*.dbc; never starts the game.

    python3 build_art_layer.py --output DIR [--client C] [--archives all|stock] [--world-cache W] [--letter z]

The input is the client's archive chain without our own letter (so a rebuild never stacks on an
installed art layer). Steps, each on the previous step's archive, as the HD chain was built:

1. relighting   build_lighting.relight: new outdoor profiles with the tuned colour and fog bands,
   then build_lighting.retime: their bands' key times moved onto Northlight's sun and moon
   (sunset colour at 20:15, night by 21:00, dawn from 04:30; two-key, constant and unrecognised
   bands unchanged), reported as its own step `retime`
2. mulgore      warm prairie bands (any client) + the HD Mulgore sky without its sun (HD sky only)
3. stormwind    denser day fog (any client) + the HD Stormwind sky without its sun   (HD sky only)
4. orgrimmar    the HD Orgrimmar sky without its sun                                 (HD sky only)
5. outdoor_sun  HD outdoor skies without their solar batches                         (HD skies only)
6. outdoor_moon HD outdoor skies without lunar batches + transparent moon02          (HD skies only)
7. storm        a private storm profile (Light column 9) per outdoor row, made after the sky steps:
   a copy of the row's final clear profile when its stock storm profile is its clear profile, else the
   stock storm profile relit and retimed; then the grey, readable storm look (fog end x0.7, 300 yard floor) (build_lighting.stormify)
8. weather_textures  procedural rain streak (1:16), red rain and snow flake (1:2) BLPs
   (build_weather_textures), replacing the client's; generated, no client bytes. Plus the client's
   weather mist puffs resampled to 1:4 (the renderer's signature to skip them in rain)

Steps 2-6 hold the HD sky cleanup: a step whose HD sky ids, profiles or textures are absent is
skipped with the reason (build_lighting.Skip). Mulgore and Stormwind find their profiles through
their Light rows and apply their bands without the HD sky (the step records sky_clone_skipped), so
a stock client gets the relighting plus those two zones' bands. Files the chain already resolves to
the same bytes are left out. The archive is written twice, as
DIR/<Data>/patch-<letter>.mpq and DIR/<Data>/<locale>/patch-<locale>-<letter>.mpq (the locale
patches override the base patches' DBCs), with DIR/art-layer-manifest.json. DIR is a staging
folder: nothing is written into the client.
"""
import argparse
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # the repo root, also under python -I
import northlight_paths as fp  # noqa: E402
fp.use_source_modules()
import client_archives  # noqa: E402
from mpq import Archive  # noqa: E402
from world_scene_builder import Assets, decode_blp  # noqa: E402
import build_lighting  # noqa: E402
import build_mulgore_lighting  # noqa: E402
import build_stormwind_single_sun  # noqa: E402
import build_outdoor_single_sun  # noqa: E402
import build_outdoor_single_moon  # noqa: E402
import build_weather_textures  # noqa: E402
from build_lighting import DBC, Skip  # noqa: E402

DBC_NAME = 'DBFilesClient\\{}.dbc'
SKYBOX = DBC_NAME.format('LightSkybox')


def write_archive(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=path.parent) as temp:
        staging = Path(temp) / path.name
        with Archive(staging, create=True, capacity=max(16, len(payload) * 2)) as archive:
            for i, (name, data) in enumerate(payload.items()):
                source = Path(temp) / f'payload-{i}.bin'
                source.write_bytes(data)
                archive.add(source, name)
        with Archive(staging) as archive:
            for name, data in payload.items():
                assert archive.read(name) == data, name
        staging.replace(path)


def read_archive(path):
    with Archive(path) as archive:
        return {n: archive.read(n) for n in archive.names() if not n.startswith('(')}


def build(client, output, world_cache, locale=None, letter='z', archives='all'):
    locale = client_archives.detect_locale(client, locale)
    fog = world_cache / 'fog'
    assets = Assets(client, archives, locale, without=letter)
    report = {'client_archives': assets.fingerprint(), 'letter': letter, 'steps': []}
    work = Path(tempfile.mkdtemp(prefix='art-layer-', dir=output.parent))
    try:
        tables = {n: DBC(assets.read(DBC_NAME.format(n))) for n in build_lighting.TABLES}
        stock = {n: DBC(assets.read(DBC_NAME.format(n))) for n in build_lighting.TABLES}   # untouched, for the storm sources
        chain_skybox = assets.read(SKYBOX)
        changes, skipped, zones = build_lighting.relight(tables)
        retimed = build_lighting.retime(tables, [c['new'] for c in changes.values()])
        payload = {DBC_NAME.format(n): t.bytes() for n, t in tables.items()}
        last = None   # the last step's archive while payload is exactly its content
        report['steps'].append({'step': 'relighting', 'profiles': len(changes), 'skipped_incomplete_profiles': skipped,
                                'light_volumes': zones,
                                'sources': {n: assets.origin(DBC_NAME.format(n)) for n in build_lighting.TABLES}})
        report['steps'].append({'step': 'retime', **{k: v for k, v in retimed.items() if k != 'rows'}})
        report['retimed_rows'] = retimed['rows']
        steps = [
            ('mulgore', lambda s, o: build_mulgore_lighting.build(s, o, client, assets, chain_skybox, fog)),
            ('stormwind', lambda s, o: build_stormwind_single_sun.build(s, o, 'stormwind', assets, fog)),
            ('orgrimmar', lambda s, o: build_stormwind_single_sun.build(s, o, 'orgrimmar', assets, fog)),
            ('outdoor_sun', lambda s, o: build_outdoor_single_sun.build(s, o, assets)),
            ('outdoor_moon', lambda s, o: build_outdoor_single_moon.build(s, o, assets)),
        ]
        for index, (name, step) in enumerate(steps, 1):
            source, out = work / f'{index}-{name}-input.mpq', work / f'{index}-{name}'
            # The relighting archive carries only the band tables; the sky steps after Mulgore also
            # need the client's LightSkybox when Mulgore (which adds it) was skipped.
            if name != 'mulgore' and SKYBOX not in payload:
                payload, last = {**payload, SKYBOX: chain_skybox}, None
            write_archive(source, payload)
            try:
                step(source, out)
            except Skip as skip:
                report['steps'].append({'step': name, 'skipped': str(skip)})
                print(f'{name}: skipped ({skip})', flush=True)
                continue
            payload, last = read_archive(out / 'patch-z.mpq'), work / f'{index}-{name}.mpq'
            (out / 'patch-z.mpq').replace(last)
            manifest = json.loads((out / 'manifest.json').read_text())
            report['steps'].append({'step': name, 'added_assets': len(manifest.get('added_assets', [])),
                                    'archive_sha256': manifest.get('archive_sha256')})
            if manifest.get('sky_clone_skipped'):
                report['steps'][-1]['sky_clone_skipped'] = manifest['sky_clone_skipped']
                print(f"{name}: sky clone skipped ({manifest['sky_clone_skipped']})", flush=True)
        # 0.3.198 (rain): after the sky steps, so their skybox edits are kept: private storm profiles, then the weather textures.
        final = {n: DBC(payload[DBC_NAME.format(n)]) for n in build_lighting.TABLES}
        storm = build_lighting.stormify(final, stock)
        payload.update({DBC_NAME.format(n): t.bytes() for n, t in final.items()})
        report['steps'].append({'step': 'storm', **storm})
        textures = build_weather_textures.weather_textures()
        textures.update(build_weather_textures.mist_textures(assets.read, decode_blp))   # 0.3.199 (rain mist)
        payload.update(textures)
        report['steps'].append({'step': 'weather_textures', 'files': sorted(textures)})
        last = None   # the payload is no longer a step's archive
        unchanged = sorted(n for n, data in payload.items() if n.lower() in assets.providers and assets.read(n) == data)
        payload = {n: data for n, data in payload.items() if n not in unchanged}
        data = client_archives.data_dir(client).name
        local = client_archives.locale_dir(client, locale).name
        targets = [f'{data}/patch-{letter}.mpq', f'{data}/{local}/patch-{local.lower()}-{letter}.mpq']
        for target in targets:
            if last and not unchanged:   # keep the step's archive bytes (reproducible against the HD chain)
                (output / target).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(last, output / target)
            else:
                write_archive(output / target, payload)
    finally:
        assets.close()
        shutil.rmtree(work, ignore_errors=True)
    report.update(targets=targets, locale=locale, dropped_unchanged=unchanged,
                  files={n: hashlib.sha256(d).hexdigest() for n, d in sorted(payload.items())},
                  letter_already_in_client=[t for t in targets if (client / t).exists()],
                  archive_sha256=hashlib.sha256((output / targets[0]).read_bytes()).hexdigest(),
                  game_launched=False)
    (output / 'art-layer-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    client_archives.add_arguments(ap)
    ap.add_argument('--world-cache', type=Path, help="the client's world cache, for its fog zones (default: <client>/world-cache)")
    ap.add_argument('--output', type=Path, required=True, help='staging folder; gets <Data>/... archives and a manifest')
    ap.add_argument('--letter', default='z', help='patch letter of the art layer (default z)')
    args = ap.parse_args()
    client = (args.client or fp.client_root()).resolve()
    if args.letter not in client_archives.SUFFIXES or not args.letter.isalpha():
        ap.error('--letter must be one lower-case letter')
    output = args.output.resolve()
    if output == client or client in output.parents:
        ap.error('--output must be a staging folder outside the client')
    output.mkdir(parents=True, exist_ok=True)
    report = build(client, output, (args.world_cache or client / 'world-cache').resolve(), args.locale, args.letter, args.archives)
    print(json.dumps({'steps': report['steps'], 'files': len(report['files']), 'targets': report['targets']}, indent=2))


if __name__ == '__main__':
    main()
