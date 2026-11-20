#!/usr/bin/env python3
# northlight-test: requires=client
"""scripts/client_identity.py (stdlib only, reads MPQ headers and tables, never file contents):
- table_sha256 on synthetic archives (format 1; format 2 behind an MPQ\\x1b user-data header with a
  hi-block table; format 4 with HET/BET): stable across a copy with a new mtime, changed by one
  byte of a table or header, blind to file data outside the tables; a non-MPQ is refused;
- match(): extra, missing and changed archives are listed; Blizzard's localized archives match by
  name; load_variants() reads a variants folder;
- the configured client: its stock view matches a stock variant built from it
  (the 7 base archives by table hash), its full chain does not, and patch-y and the HD letters are
  listed as the differences; the identity of the stock view and of the base archives does not
  depend on any installed locale;
- a client of read-only links to the configured client's stock-view archives plus a synthetic foreign
  Data/patch-z.mpq: 'stock' with the default without='z', but 'extra Data/patch-z.mpq' with
  without='' (the caller's verdict for a foreign z); an unreadable archive is a difference;
- with NORTHLIGHT_STOCK_CLIENT (the stock test client, read only): it identifies as 'stock' against a
  variant built from the dev client's stock view, i.e. the base archives of both clients are equal."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json, os, shutil, struct
import client_archives
import client_identity as ci

out = fp.output_dir()


def archive(path, version=0, user_data=False, het_bet=False, data=b'file data' * 10):
    """A minimal archive: header, file data, then the hash, block (and hi-block, HET, BET) tables."""
    header_size = {0: 32, 1: 44, 3: 208}[version]
    base = 0x200 if user_data else 0
    hash_table, block_table = bytes(range(64)), bytes(range(100, 132))            # 4 hash, 2 block entries
    hash_pos = header_size + len(data)
    block_pos = hash_pos + len(hash_table)
    tail = hash_table + block_table
    header = bytearray(header_size)
    struct.pack_into('<4sIIHHIIII', header, 0, b'MPQ\x1a', header_size, 0, version, 3, hash_pos, block_pos, 4, 2)
    if version >= 1:
        hi_pos = block_pos + len(block_table)
        tail += b'\x01\x00\x02\x00'
        struct.pack_into('<QHH', header, 32, hi_pos, 0, 0)
    if het_bet:
        het_pos = header_size + len(data) + len(tail)
        het = struct.pack('<4sII', b'HET\x1a', 1, 8) + b'hetdata!'
        bet = struct.pack('<4sII', b'BET\x1a', 1, 4) + b'bet!'
        struct.pack_into('<QQQ', header, 44, 0, het_pos + len(het), het_pos)      # archive size, BET, HET
        struct.pack_into('<5Q', header, 68, len(hash_table), len(block_table), 4, len(het), len(bet))
        tail += het + bet
    blob = bytes(header) + data + tail
    if user_data:
        blob = struct.pack('<4sIII', b'MPQ\x1b', 0x100, base, 16).ljust(base, b'u') + blob
    path.write_bytes(blob)
    return path, base + hash_pos, base


# 1. table_sha256 on synthetic archives.
for version, user_data, het_bet in [(0, False, False), (1, True, False), (3, False, True)]:
    path, hash_at, base = archive(out / f'v{version}.mpq', version, user_data, het_bet)
    digest = ci.table_sha256(path)
    copy = out / f'v{version}-copy.mpq'
    shutil.copyfile(path, copy)
    os.utime(copy, (1e9, 1e9))
    assert ci.table_sha256(copy) == digest, version
    for offset in (hash_at + 5, base + 13):                                       # a hash table byte, the header
        blob = bytearray(path.read_bytes())
        blob[offset] ^= 1
        copy.write_bytes(blob)
        assert ci.table_sha256(copy) != digest, (version, offset)
    blob = bytearray(path.read_bytes())
    blob[hash_at - 3] ^= 1                                                         # file data, same size
    copy.write_bytes(blob)
    assert ci.table_sha256(copy) == digest, version                                # documented limit (S1c)
    if het_bet:
        blob = bytearray(path.read_bytes())
        blob[-2] ^= 1                                                              # inside the BET table
        copy.write_bytes(blob)
        assert ci.table_sha256(copy) != digest
(out / 'not.mpq').write_bytes(b'PK\x03\x04' + bytes(60))
try:
    ci.table_sha256(out / 'not.mpq')
    raise AssertionError('non-MPQ accepted')
except ValueError:
    pass

# 2. match() and load_variants().
stock = [{'role': 'base', 'name': n, 'bytes': 10, 'table_sha256': n * 2} for n in ('common.mpq', 'patch.mpq')] + \
        [{'role': 'locale', 'name': 'locale-<loc>.mpq'}]
variant = {'format': 'northlight-cache-variant/1', 'variant': 'stock', 'identity': {'chain': stock}}
assert ci.match({'chain': stock}, variant) == (True, [])
extra = stock[:2] + [{'role': 'base', 'name': 'patch-y.mpq', 'bytes': 1, 'table_sha256': 'y'}] + stock[2:]
assert ci.match(extra, variant) == (False, ['extra Data/patch-y.mpq'])
assert ci.match(stock[1:], variant) == (False, ['missing Data/common.mpq'])
changed = [dict(stock[0], table_sha256='other')] + stock[1:]
assert ci.match(changed, variant)[1] == ['changed Data/common.mpq (10 bytes; the variant has 10 bytes, other tables)']
assert ci.match([stock[1], stock[0], stock[2]], variant) == (False, ['the archive order differs'])
folder = out / 'variants'
shutil.rmtree(folder, ignore_errors=True)
(folder / 'stock').mkdir(parents=True)
(folder / 'stock/northlight-cache.json').write_text(json.dumps(variant))
(folder / 'notes.json').write_text('{"not": "a variant"}')
assert list(ci.load_variants(folder)) == ['stock'] and list(ci.load_variants(variant)) == ['stock']

# 3. The configured (dev HD) client.
client = fp.client_root()
locale = client_archives.detect_locale(client)
stock_view = ci.chain_identity(client, 'stock', locale)
assert [e['name'] for e in stock_view['chain'] if e['role'] == 'base'] == \
    ['common.mpq', 'common-2.mpq', 'expansion.mpq', 'lichking.mpq', 'patch.mpq', 'patch-2.mpq', 'patch-3.mpq']
assert all('table_sha256' in e for e in stock_view['chain'] if e['role'] == 'base')
assert all('table_sha256' not in e for e in stock_view['chain'] if e['role'] == 'locale')
dev_variant = {'variant': 'stock', 'identity': stock_view}
assert ci.match(stock_view, dev_variant) == (True, [])
result = ci.identify(client, variants=dev_variant)
names = {e['name'] for e in result['chain']}
if 'patch-y.mpq' in names:                                                        # the dev HD client
    assert result['variant'] is None and result['closest'] == 'stock'
    assert 'extra Data/patch-y.mpq' in result['differences'] and 'extra Data/patch-x.mpq' in result['differences']
    print('dev client: no match;', len(result['differences']), 'differences:', ', '.join(result['differences']))
else:
    print('the configured client has no patch-y: identified as', result['variant'], result['differences'])
assert 'patch-z.mpq' not in names and 'patch-<loc>-z.mpq' not in names            # our art layer is left out
for other_locale in client_archives.installed_locales(client):
    other = ci.chain_identity(client, 'stock', other_locale)
    assert other['chain'] == stock_view['chain'] and other['sha256'] == stock_view['sha256']
    both = [ci.chain_identity(client, 'all', l)['chain'] for l in (locale, other_locale)]
    assert [e for e in both[0] if e['role'] == 'base'] == [e for e in both[1] if e['role'] == 'base']
    print('locale:', locale, other_locale, 'stock identity equal; differing HD locale letters:',
          ', '.join(a['name'] for a, b in zip(*both) if a != b) or 'none')

# 4. A foreign patch-z, and an unreadable archive (symlinks to the client's archives, read only).
links = out / 'linked-client'
shutil.rmtree(links, ignore_errors=True)
(links / 'Data' / locale).mkdir(parents=True)
(links / 'Wow.exe').write_bytes(b'')
for source in client_archives.chain(client, 'stock', locale):
    # Keep the fixture's Data and locale spelling even on a case-sensitive HD installation.
    parent = links / 'Data'
    if len(source.relative_to(client).parts) == 3:
        parent /= locale
    target = parent / source.name
    try:
        os.symlink(source, target)
    except OSError as exc:
        if getattr(exc, 'winerror', None) != 1314:
            raise
        os.link(source, target)   # Read-only archive fixture; no large client copy or symlink privilege needed.
archive(links / 'Data/patch-z.mpq')                                                # someone else's z
assert ci.identify(links, None, dev_variant)['variant'] == 'stock'
foreign = ci.identify(links, None, dev_variant, without='')
assert foreign['variant'] is None and foreign['differences'] == ['extra Data/patch-z.mpq'], foreign['differences']
(links / 'Data/patch-z.mpq').unlink()
(links / 'Data/patch-x.mpq').write_bytes(b'MPQ\x1a')                               # truncated
broken = ci.identify(links, None, {'variant': 'stock', 'identity': ci.chain_identity(links, 'stock')})
assert broken['variant'] is None and broken['differences'][-1].startswith('unreadable Data/patch-x.mpq'), broken
shutil.rmtree(links)

# 5. The stock test client (read only).
stock_client = os.environ.get('NORTHLIGHT_STOCK_CLIENT')
if stock_client:
    result = ci.identify(Path(stock_client), None, dev_variant)                  # its Config.wtf: enUS
    assert result['variant'] == 'stock' and result['differences'] == [], result['differences']
    print(f"stock test client ({result['locale']}): identified as stock")
else:
    print('NORTHLIGHT_STOCK_CLIENT not set: the stock test client was not identified')
print('client_identity: table hash, match, variants and client identification OK')
