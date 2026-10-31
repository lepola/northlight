#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""scripts/build_cache_variant.py on four Azeroth tiles of the client's stock view (NORTHLIGHT_STOCK_CLIENT
if set, else the configured client; read only), run as the installer runs it (python -I -X utf8):
- the zip holds northlight-cache.json first, then only allowlisted cache files (no install manifest,
  validation, logs, build reports, *.fcm or fog/lights manifests), deflated, with fixed timestamps;
- northlight-cache.json (northlight-cache-variant/1) lists exactly the zipped files with their sha256 and
  size, its cache digest recomputes, and its identity matches the client's stock-view chain;
- a rerun finds the cache up to date and writes a byte-identical zip."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import hashlib, json, os, subprocess, tempfile, zipfile
import build_cache_variant as bcv
import client_identity as ci

client = Path(os.environ.get('NORTHLIGHT_STOCK_CLIENT') or fp.client_root())
# The builder deliberately refuses an output inside the client, even when the checkout is there.
with tempfile.TemporaryDirectory(prefix='northlight-cache-variant-') as work:
    # The builder reports resolved paths; macOS temporary directories sit behind the /var symlink.
    output = Path(work).resolve() / 'variant'
    command = [sys.executable, '-I', '-X', 'utf8', str(fp.REPO / 'scripts/build_cache_variant.py'), '--client', str(client),
               '--archives', 'stock', '--without', 'z', '--variant', 'test', '--output', str(output), '--maps', 'Azeroth',
               '--tiles', '31', '48', '32', '49', '--progress', 'json']


    def build():
        r = subprocess.run(command, capture_output=True, encoding='utf-8', errors='replace', timeout=1800)
        lines = r.stdout.strip().splitlines()
        assert r.returncode == 0, (r.returncode, lines[-5:], r.stderr[-1500:])
        return [json.loads(line) for line in lines if line.startswith('{')]


    events = build()
    done = events[-1]
    assert done['event'] == 'done' and any(e.get('event') == 'done' and e.get('mode') == 'full' for e in events)
    zip_path = Path(done['zip'])
    assert zip_path.parent == output and zip_path.name == f"Northlight-cache-test-{done['cache_digest'][:12]}.zip"
    first = hashlib.sha256(zip_path.read_bytes()).hexdigest()

    with zipfile.ZipFile(zip_path) as z:
        infos = z.infolist()
        names = [i.filename for i in infos]
        assert names[0] == bcv.MANIFEST and names[1:] == sorted(names[1:])
        assert all(bcv.ALLOWLIST.fullmatch(n) for n in names[1:]), [n for n in names[1:] if not bcv.ALLOWLIST.fullmatch(n)]
        assert 'lights/Azeroth.fga' in names and z.read('lights/Azeroth.fga').startswith(b'FGA1')
        assert all(i.compress_type == zipfile.ZIP_DEFLATED and i.date_time == bcv.ZIP_TIME for i in infos)
        kinds = {n.split('/')[0] for n in names[1:]}
        assert kinds == {'Azeroth', 'models', 'lights', 'fog', 'celestial'}, kinds
        manifest = json.loads(z.read(bcv.MANIFEST))
        assert z.read(bcv.MANIFEST) == (output / bcv.MANIFEST).read_bytes()
        assert manifest['format'] == bcv.FORMAT and manifest['variant'] == 'test'
        assert sorted(manifest['files']) == names[1:] and manifest['file_count'] == len(names) - 1
        for name in names[1:]:
            data = z.read(name)
            assert manifest['files'][name] == {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}, name
    assert manifest['cache_digest'] == bcv.cache_digest(manifest['files']) == done['cache_digest']
    cache = output / 'world-cache'
    left_out = sorted(p.relative_to(cache).as_posix() for p in cache.rglob('*') if p.is_file()
                      and p.relative_to(cache).as_posix() not in manifest['files'])
    assert 'install-manifest.json' in left_out and 'validation.json' in left_out and 'celestial/sun.fcm' in left_out
    assert not any(n.endswith(('.json', '.log', '.fcm')) for n in manifest['files'])
    build_info = manifest['build']
    assert build_info['archives'] == 'stock' and build_info['without'] == 'z' and build_info['maps'] == ['Azeroth']
    assert set(build_info['digests']) == {'scene:Azeroth', 'lights', 'fog', 'celestial'} and build_info['sources']
    identity = ci.chain_identity(client, 'stock', manifest['identity']['locale'])
    assert ci.match(identity, manifest) == (True, []) and identity['sha256'] == manifest['identity']['sha256']

    events = build()
    assert any(e.get('event') == 'up_to_date' for e in events) and events[-1]['event'] == 'done'
    assert hashlib.sha256(zip_path.read_bytes()).hexdigest() == first, 'the zip is not reproducible'
    print(f"cache variant: {len(names) - 1} files, {zip_path.stat().st_size} zip bytes, allowlist, manifest sha and "
          'reproducible zip OK')
