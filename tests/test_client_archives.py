#!/usr/bin/env python3
# northlight-test:
"""client_archives: the game's MPQ order, locale detection and the stock view, on fake client
folders (empty files; no StormLib, no client)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import shutil
import client_archives as ca


def client(root, files, config=None):
    shutil.rmtree(root, ignore_errors=True)
    for name in ['Wow.exe', *files]:
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_bytes(b'')
    if config is not None:
        (root / 'WTF').mkdir()
        (root / 'WTF/Config.wtf').write_text(config)
    return root


def names(root, *args, **kw):
    return [p.relative_to(root).as_posix() for p in ca.chain(root, *args, **kw)]


out = fp.output_dir()
base = ['Data/common.MPQ', 'Data/common-2.MPQ', 'Data/expansion.MPQ', 'Data/lichking.MPQ']
patches = ['Data/patch.MPQ', 'Data/patch-2.MPQ', 'Data/patch-3.MPQ', 'Data/patch-x.MPQ', 'Data/patch-5.MPQ']
local = ['Data/enUS/locale-enUS.MPQ', 'Data/enUS/expansion-locale-enUS.MPQ', 'Data/enUS/lichking-locale-enUS.MPQ',
         'Data/enUS/speech-enUS.MPQ', 'Data/enUS/base-enUS.MPQ', 'Data/enUS/backup-enUS.MPQ',
         'Data/enUS/patch-enUS.MPQ', 'Data/enUS/patch-enUS-2.MPQ', 'Data/enUS/patch-enUS-3.MPQ', 'Data/enUS/patch-enUS-z.MPQ']
ignored = ['Data/patch-y.MPQ.disabled', 'Data/patch-custom.MPQ', 'Data/patch-1.MPQ', 'Data/enUS/patch-enUS-9.MPQ.disabled',
           'Data/deDE/locale-deDE.MPQ', 'Data/deDE/patch-deDE-3.MPQ']
root = client(out / 'client-a', base + patches + local + ignored, 'SET gxApi "d3d9"\nSET locale "enUS"\n')

# 1. Game order, lowest priority first: patch.mpq under patch-2 (the 0.3.161 builder sorted
#    patch-enus.mpq last, so it overrode patch-enus-2/3/x/z), digits before letters, locale
#    base archives in expansion order, the speech/base/backup archives and inactive names left out.
expected = base + ['Data/patch.MPQ', 'Data/patch-2.MPQ', 'Data/patch-3.MPQ',
                   'Data/enUS/locale-enUS.MPQ', 'Data/enUS/expansion-locale-enUS.MPQ', 'Data/enUS/lichking-locale-enUS.MPQ',
                   'Data/enUS/patch-enUS.MPQ', 'Data/enUS/patch-enUS-2.MPQ', 'Data/enUS/patch-enUS-3.MPQ',
                   'Data/patch-5.MPQ', 'Data/patch-x.MPQ', 'Data/enUS/patch-enUS-z.MPQ']
assert names(root) == expected, names(root)

# 2. Stock view: only Blizzard's 3.3.5a archives.
stock = [n for n in expected if n not in ('Data/patch-5.MPQ', 'Data/patch-x.MPQ', 'Data/enUS/patch-enUS-z.MPQ')]
assert names(root, 'stock') == stock, names(root, 'stock')
assert names(root, without='z') == [n for n in expected if n != 'Data/enUS/patch-enUS-z.MPQ']   # under our art layer

# 3. Locale: Config.wtf wins over the folder scan, an explicit locale wins over both, a missing one fails.
assert ca.detect_locale(root) == 'enUS' and sorted(ca.installed_locales(root)) == ['deDE', 'enUS']
assert names(root, locale='deDE') == base + ['Data/patch.MPQ', 'Data/patch-2.MPQ', 'Data/patch-3.MPQ',
                                             'Data/deDE/locale-deDE.MPQ', 'Data/deDE/patch-deDE-3.MPQ',
                                             'Data/patch-5.MPQ', 'Data/patch-x.MPQ']
for wanted, error in [('frFR', FileNotFoundError)]:
    try:
        ca.detect_locale(root, wanted)
        raise AssertionError('missing locale accepted')
    except error:
        pass

# 4. No Config.wtf: the only installed locale; two installed locales and no config is an error.
lower = client(out / 'client-b', [n.lower() for n in base + ['Data/deDE/locale-deDE.MPQ', 'Data/deDE/patch-deDE.MPQ']])
assert ca.detect_locale(lower) == 'deDE'
assert names(lower) == [n.lower() for n in base + ['Data/deDE/locale-deDE.MPQ', 'Data/deDE/patch-deDE.MPQ']]
two = client(out / 'client-c', base + ['Data/enUS/locale-enUS.MPQ', 'Data/ruRU/locale-ruRU.MPQ'])
try:
    ca.detect_locale(two)
    raise AssertionError('ambiguous locale accepted')
except ValueError:
    pass

# 5. The fingerprint follows names, sizes and mtimes.
before = ca.fingerprint(ca.chain(root), root)
(root / 'Data/patch-x.MPQ').write_bytes(b'changed')
after = ca.fingerprint(ca.chain(root), root)
assert before['sha256'] != after['sha256'] and len(after['archives']) == len(expected)

# 6. --without: an add_arguments option (default none), case-insensitive, patch letters only.
import argparse
ap = argparse.ArgumentParser()
ca.add_arguments(ap)
assert ap.parse_args([]).without == '' and ap.parse_args(['--without', 'yz']).without == 'yz'
assert names(root, without='Z') == names(root, without='z') and names(root, 'stock', without='z') == stock
try:
    ca.chain(root, without='-')
    raise AssertionError('bad --without accepted')
except ValueError:
    pass
print('client_archives: order, stock view, locale detection, fingerprint and --without OK')
