#!/usr/bin/env python3
"""Build a prebuilt world-cache variant: the cache zip and its northlight-cache.json (developer machine,
offline; reads the client only, writes only under --output).

    python3 scripts/build_cache_variant.py --client C --archives stock --without z --variant stock \\
        --output out/variants/stock [--maps ...] [--tiles X0 Y0 X1 Y1] [--jobs N]

1. scripts/install_world_cache.py builds <output>/world-cache from the client's chain (--archives,
   --without), or finds it up to date.
2. northlight-cache.json (format northlight-cache-variant/1): the variant name; the identity of the chain
   the cache was built from (client_identity.chain_identity, which the installer matches against
   the player's client); every cache file's sha256 and size; the cache digest; the build's
   archive view, maps, tiles, builder source sha256s and per-step digests.
3. Northlight-cache-<variant>-<cache digest 12>.zip: northlight-cache.json first, then only the files the
   DLL reads (ALLOWLIST), sorted, deflate, Zip64, fixed timestamps and modes, so the same cache
   gives the same zip. A copy of northlight-cache.json sits next to the zip for the package builder.

The art layer is not part of a variant: the installer always builds it locally from the player's
own Light*.dbc.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE.parent), str(HERE)]
import client_archives  # noqa: E402
import client_identity  # noqa: E402
import northlight_paths  # noqa: E402

FORMAT = 'northlight-cache-variant/1'
MANIFEST = 'northlight-cache.json'
MAPS = ['Azeroth', 'Kalimdor', 'Expansion01', 'Northrend']
_MAP = '(?:' + '|'.join(MAPS) + ')'
# What the DLL reads from world-cache/ (src/world, src/lights, src/sky); nothing else is shipped.
# Continents only, on purpose: the dungeon and raid maps (instance_maps.py) are an optional layer the installer builds
# on the player's machine on top of this cache; a prebuilt variant never carries them.
ALLOWLIST = re.compile(rf'{_MAP}/\d+_\d+\.fg3|models/[0-9a-f]{{64}}\.fgs|lights/{_MAP}\.fg[la]'
                       rf'|fog/{_MAP}/\d+_\d+\.frf|celestial/(?:sun|moon)\.fct')
ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def cache_files(cache):
    """Allowlisted files under cache, as sorted posix paths."""
    return sorted(rel for rel in (p.relative_to(cache).as_posix() for p in cache.rglob('*') if p.is_file())
                  if ALLOWLIST.fullmatch(rel))


def cache_digest(files):
    rows = ''.join(f"{name}\0{f['sha256']}\0{f['bytes']}\n" for name, f in sorted(files.items()))
    return hashlib.sha256(rows.encode()).hexdigest()


def entry(name):
    info = zipfile.ZipInfo(name, ZIP_TIME)
    info.compress_type, info.create_system, info.external_attr = zipfile.ZIP_DEFLATED, 3, 0o644 << 16
    return info


def write_zip(path, cache, names, manifest_bytes, level):
    """The zip, northlight-cache.json first, written to a temporary name and renamed when complete."""
    temp = path.with_name(path.name + '.tmp')
    with zipfile.ZipFile(temp, 'w', zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        z.writestr(entry(MANIFEST), manifest_bytes, compresslevel=level)
        for name in names:
            z.writestr(entry(name), (cache / name).read_bytes(), compresslevel=level)
    temp.replace(path)


def run_install(args, cache):
    command = [sys.executable, '-I', '-B', '-X', 'utf8', str(HERE / 'install_world_cache.py'), '--client', str(args.client),
               '--archives', args.archives, '--output', str(cache), '--progress', args.progress]
    command += ['--without', args.without] if args.without else []
    command += ['--locale', args.locale] if args.locale else []
    command += ['--maps', *args.maps] if args.maps else []
    command += ['--tiles', *map(str, args.tiles)] if args.tiles else []
    command += ['--jobs', str(args.jobs)] if args.jobs else []
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8', errors='replace')
    last = None
    for line in proc.stdout:
        print(line, end='', flush=True)
        if line.startswith('{'):
            try:
                last = json.loads(line)
            except ValueError:
                pass
    proc.wait()
    return proc.returncode, last or {}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    client_archives.add_arguments(ap)
    ap.set_defaults(without='z')
    ap.add_argument('--variant', required=True, help="variant name, e.g. 'stock'")
    ap.add_argument('--output', type=Path, required=True, help='variant folder: world-cache/, the zip, northlight-cache.json')
    ap.add_argument('--maps', nargs='+', choices=MAPS)
    ap.add_argument('--tiles', nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'), help='only these tiles (testing)')
    ap.add_argument('--jobs', type=int)
    ap.add_argument('--level', type=int, default=6, help='deflate level (default 6)')
    ap.add_argument('--progress', choices=['json', 'human'], default='human')
    args = ap.parse_args(argv)
    if not re.fullmatch(r'[a-z0-9-]+', args.variant):
        ap.error('--variant must be lower-case letters, digits and dashes')
    started = time.time()
    args.client = (args.client or northlight_paths.client_root()).resolve()
    output = args.output.resolve()
    if output == args.client or args.client in output.parents:
        ap.error('--output must be outside the client')
    output.mkdir(parents=True, exist_ok=True)
    cache = output / 'world-cache'

    code, last = run_install(args, cache)
    if code != 0 or last.get('event') not in ('done', 'up_to_date'):
        print(json.dumps({'event': 'failed', 'step': 'install_world_cache', 'exit': code}), flush=True)
        return code or 1
    build = json.loads((cache / 'install-manifest.json').read_text(encoding='utf-8'))
    identity = client_identity.chain_identity(args.client, args.archives, build['inputs']['locale'], args.without)
    built_chain = [Path(a['archive']).name for a in build['inputs']['archives']]
    identity_chain = [Path(p).name.lower() for p in client_archives.chain(args.client, args.archives,
                                                                         build['inputs']['locale'], args.without)]
    if built_chain != identity_chain:
        print(json.dumps({'event': 'failed', 'error': 'the cache was built from another chain than the identity',
                          'built': built_chain, 'identity': identity_chain}), flush=True)
        return 1

    names = cache_files(cache)
    files = {}
    for name in names:
        data = (cache / name).read_bytes()
        files[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
    digest = cache_digest(files)
    manifest = {'format': FORMAT, 'variant': args.variant, 'identity': identity, 'cache_digest': digest,
                'file_count': len(files), 'total_bytes': sum(f['bytes'] for f in files.values()),
                'build': {'archives': args.archives, 'without': args.without, 'maps': build.get('maps_built'),
                          'tiles': build['inputs']['tiles'], 'fingerprint': build['fingerprint'],
                          'digests': build.get('digests'), 'sources': build['inputs']['sources'],
                          'built_unix': build.get('built_unix'), 'python': sys.version.split()[0]},
                'files': files}
    manifest_bytes = (json.dumps(manifest, indent=1, sort_keys=True) + '\n').encode()
    zip_path = output / f'Northlight-cache-{args.variant}-{digest[:12]}.zip'
    for old in output.glob(f'Northlight-cache-{args.variant}-*.zip'):
        if old != zip_path:
            old.unlink()
    zip_started = time.time()
    write_zip(zip_path, cache, names, manifest_bytes, args.level)
    (output / MANIFEST).write_bytes(manifest_bytes)
    zip_seconds = round(time.time() - zip_started, 1)
    print(json.dumps({'event': 'done', 'variant': args.variant, 'zip': str(zip_path), 'zip_bytes': zip_path.stat().st_size,
                      'files': len(files), 'cache_bytes': manifest['total_bytes'], 'cache_digest': digest,
                      'identity_sha256': identity['sha256'], 'zip_seconds': zip_seconds,
                      'seconds': round(time.time() - started, 1)}), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
