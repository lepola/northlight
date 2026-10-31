#!/usr/bin/env python3
"""Explicit developer export check: compare factory profiles with a tuned client.

The renderer reads personal profiles from the client root. Packages and settings reset
use the factory profiles in this folder. They may intentionally differ. Normal validation
is tests/test_client_config.py, which uses the renderer's parsers and checks factory/reset
consistency. Use this stricter byte comparison only when reviewing an explicit tuning export.

  python3 check_client_config.py           exit 1 if a copy differs; SKIP (exit 0) when the
                                           client-root files are absent (scratch copies)
  python3 check_client_config.py --update  copy client root -> this folder (never the reverse)
"""
from pathlib import Path
import hashlib, shutil, sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import northlight_paths  # noqa: E402
CLIENT = northlight_paths.client_root(required=False) or HERE.parents[1]
FILES = ('celestial-profiles.ini', 'shadow-range-profiles.ini')

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'missing'

def main():
    if '--update' in sys.argv:
        for name in FILES:
            shutil.copyfile(CLIENT / name, HERE / name)
            print('updated', name, sha(HERE / name))
        return 0
    if not all((CLIENT / n).is_file() for n in FILES):
        print('SKIP client-root profiles not found under', CLIENT, '(scratch copy?)')
        return 0
    bad = [n for n in FILES if sha(CLIENT / n) != sha(HERE / n)]
    for n in FILES:
        print(('DIFFERS ' if n in bad else 'OK      ') + n, 'client', sha(CLIENT / n)[:16], 'tracked', sha(HERE / n)[:16])
    if bad:
        print('Personal profiles differ from factory defaults. This is valid for local tuning. '
              'Use --update only to intentionally export these personal profiles as reviewed factory defaults.')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
