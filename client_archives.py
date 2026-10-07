"""The MPQ archives a 3.3.5a client loads, in the game's priority order (stdlib only, no StormLib).

chain(client) lists them lowest priority first, so the last archive that holds a name wins:

    common, common-2, expansion, lichking           Data/
    patch, patch-2..9, patch-a..z                    Data/
    locale, expansion-locale, lichking-locale        Data/<locale>/
    patch-<locale>, patch-<locale>-2..9, -a..z       Data/<locale>/

The same order as Noggit's loader (application.cpp loadMPQs, last loaded wins). Only one-character
patch suffixes load, so patch-5.mpq.disabled or patch-custom.mpq are ignored. The speech, base and
backup archives are left out: they hold only sounds and launcher files, no world or DBC data.

The locale is the client's `SET locale` in WTF/Config.wtf, else the only Data/<xxXX>/ folder that
holds locale-<xxXX>.mpq. Folder and file names match case-insensitively (Data/enUS on Windows,
data/enus in a copied client).

archives='stock' keeps only the archives Blizzard shipped with 3.3.5a (patch, patch-2, patch-3 and
their locale counterparts), which is how an unmodified client resolves; 'all' is what this client
loads. without='z' (--without z) leaves our own art layer out of either view, so a build never
reads its own earlier output and installing the art layer does not change the chain.
"""
import hashlib
import re
from pathlib import Path

BASE = ('common', 'common-2', 'expansion', 'lichking')
LOCALE_BASE = ('locale', 'expansion-locale', 'lichking-locale')
SUFFIXES = '23456789abcdefghijklmnopqrstuvwxyz'   # patch-?.mpq, in load order
STOCK_SUFFIXES = ('', '2', '3')
VIEWS = ('all', 'stock')
LOCALES = ('enUS', 'enGB', 'deDE', 'frFR', 'esES', 'esMX', 'ruRU', 'koKR', 'zhCN', 'zhTW')


def _entries(folder):
    """{lowercase name: actual path} of a folder's entries; {} if it does not exist."""
    try:
        return {p.name.lower(): p for p in Path(folder).iterdir()}
    except (FileNotFoundError, NotADirectoryError):
        return {}


def data_dir(client):
    folder = _entries(client).get('data')
    if folder is None or not folder.is_dir():
        raise FileNotFoundError(f'{client}: no Data folder')
    return folder


def locale_dir(client, locale):
    """Data/<locale>/ as written on disk."""
    folder = _entries(data_dir(client)).get(locale.lower())
    if folder is None or not folder.is_dir():
        raise FileNotFoundError(f'{client}: no Data/{locale} folder')
    return folder


def configured_locale(client):
    """The locale in WTF/Config.wtf (SET locale "enUS"), or None."""
    config = _entries(_entries(client).get('wtf') or Path(client, 'WTF')).get('config.wtf')
    if config is None:
        return None
    match = re.search(r'^\s*SET\s+locale\s+"([A-Za-z]{4})"', config.read_text(encoding='latin-1'), re.M | re.I)
    return match.group(1) if match else None


def installed_locales(client):
    """Locales with a Data/<xxXX>/locale-<xxXX>.mpq, as written on disk."""
    found = []
    for name, folder in sorted(_entries(data_dir(client)).items()):
        if len(name) == 4 and folder.is_dir() and f'locale-{name}.mpq' in _entries(folder):
            found.append(folder.name)
    return found


def detect_locale(client, locale=None):
    """The locale the client runs with: the argument, WTF/Config.wtf, else the only installed one."""
    installed = installed_locales(client)
    wanted = locale or configured_locale(client)
    if wanted:
        for name in installed:
            if name.lower() == wanted.lower():
                return next((l for l in LOCALES if l.lower() == name.lower()), name)
        raise FileNotFoundError(f'{client}: locale {wanted} is not installed (found: {", ".join(installed) or "none"})')
    if len(installed) == 1:
        return next((l for l in LOCALES if l.lower() == installed[0].lower()), installed[0])
    raise ValueError(f'{client}: cannot tell the locale (no SET locale in WTF/Config.wtf; installed: '
                     f'{", ".join(installed) or "none"}); pass --locale')


def chain(client, archives='all', locale=None, without=''):
    """The archives the game loads, lowest priority first (the last one holding a name wins).
    without: patch letters to leave out, e.g. 'z' to read the client under our own art layer."""
    if archives not in VIEWS:
        raise ValueError(f'archives must be one of {VIEWS}, not {archives!r}')
    without = (without or '').lower()
    if any(c not in SUFFIXES for c in without):
        raise ValueError(f'without must be patch letters or digits ({SUFFIXES}), not {without!r}')
    data = data_dir(client)
    locale = detect_locale(client, locale).lower()
    suffixes = STOCK_SUFFIXES if archives == 'stock' else ('',) + tuple(SUFFIXES)
    suffixes = [s for s in suffixes if not s or s not in without]   # '' (patch.mpq) is never left out
    top, local = _entries(data), _entries(locale_dir(client, locale))
    names = [(top, n + '.mpq') for n in BASE]
    names += [(top, 'patch' + (f'-{s}' if s else '') + '.mpq') for s in suffixes]
    names += [(local, f'{n}-{locale}.mpq') for n in LOCALE_BASE]
    names += [(local, f'patch-{locale}' + (f'-{s}' if s else '') + '.mpq') for s in suffixes]
    return [folder[n] for folder, n in names if n in folder and folder[n].is_file()]


def is_custom_patch(path):
    """True for a patch-<x>.mpq or patch-<locale>-<x>.mpq whose suffix Blizzard never shipped (not in
    STOCK_SUFFIXES): a mod's or a private server's archive, not one the world itself depends on."""
    match = re.fullmatch(r'patch(?:-[a-z]{4})?-([0-9a-z])\.mpq', Path(path).name.lower())
    return bool(match) and match.group(1) in SUFFIXES and match.group(1) not in STOCK_SUFFIXES


def fingerprint(paths, client=None):
    """Identity of an archive chain: relative names, sizes and mtimes in priority order."""
    rows = []
    for p in paths:
        st = p.stat()
        rows.append({'archive': (p.relative_to(client) if client else p).as_posix(), 'bytes': st.st_size,
                     'mtime_ns': st.st_mtime_ns})
    digest = hashlib.sha256(repr([(r['archive'].lower(), r['bytes'], r['mtime_ns']) for r in rows]).encode()).hexdigest()
    return {'archives': rows, 'sha256': digest}


def add_arguments(parser):
    """--client, --archives, --locale and --without for builders that read the client's MPQs."""
    parser.add_argument('--client', type=Path, help='client folder (default: the configured client)')
    parser.add_argument('--archives', choices=VIEWS, default='all',
                        help="'all': the archives this client loads; 'stock': only Blizzard's 3.3.5a archives")
    parser.add_argument('--locale', help='client locale (default: WTF/Config.wtf, else the only installed one)')
    parser.add_argument('--without', default='', metavar='LETTERS',
                        help="patch letters to leave out of the chain, e.g. 'z' for our own art layer (default: none)")


if __name__ == '__main__':
    import argparse
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import northlight_paths
    ap = argparse.ArgumentParser(description='Print the archive chain, highest priority last.')
    add_arguments(ap)
    a = ap.parse_args()
    client = a.client or northlight_paths.client_root()
    print('locale', detect_locale(client, a.locale))
    for p in chain(client, a.archives, a.locale, a.without):
        print(p.relative_to(client).as_posix())
