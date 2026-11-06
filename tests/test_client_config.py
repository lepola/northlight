# northlight-test: requires=client,zig
"""Validate factory/reset/package profiles and personal overrides with the runtime parsers.

Different valid personal values are allowed. Missing/malformed profiles and stale factory
defaults still fail. The explicit export checker retains its strict byte comparison.
All mutations use temporary copies; the real client and tracked profiles are read-only.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_packages

FILES = ('celestial-profiles.ini', 'shadow-range-profiles.ini')


def run(validator):
    client, factory = fp.client_root(), fp.REPO / 'client-config'
    before = {root / name: (root / name).read_bytes() for root in (client, factory) for name in FILES}
    for name in FILES:
        assert build_packages.PAYLOAD_COMMON[name] == 'client-config/' + name, name
        assert name in build_packages.PRESERVE, name

    def check(root, mode='--personal', expected=True):
        result = subprocess.run([str(validator), mode, str(root)], capture_output=True, text=True)
        assert result.returncode == (0 if expected else 1), (mode, root, result.returncode, result.stdout, result.stderr)
        return result.stdout.strip()

    try:
        print(check(factory, '--factory'), flush=True)
        print(check(client), flush=True)
        for name in FILES:
            print(('PERSONAL OVERRIDE ' if before[client / name] != before[factory / name] else 'FACTORY MATCH ') + name)
        with tempfile.TemporaryDirectory(prefix='northlight-profile-inputs-') as temporary:
            root = Path(temporary)
            def pair(celestial='[default]\nsun_light_strength=0.5\n[Azeroth:12]\nmoon_light_mix=0.6\n',
                     shadow='[Kalimdor:440]\nterrain_radius=1024\n', encoding='utf-8'):
                for name, text in zip(FILES, (celestial, shadow)):
                    (root / name).write_bytes(text.encode(encoding))

            # An omitted regional override uses the renderer's built-in fallback.
            for encoding in ('utf-8', 'utf-8-sig', 'utf-16'):
                pair(encoding=encoding)
                assert 'Elwynn terrain radius=928' in check(root)
                check(root, '--factory', False)
            pair(celestial='', shadow='')
            check(root)
            check(root, '--factory', False)

            invalid = {
                FILES[0]: ['[default]\nsun_light_strength=nan\n', '[default]\nsun_light_mix=1.1\n',
                           '[default]\nsun_light_strength=1\nsun_light_strength=2\n',
                           '[default]\nunknown=1\n', '[Azeroth:0]\n', '[Azeroth:12]\n[default]\n',
                           '[default]\nsun_light=0,0,0\n', '[default]\nsun_disc=1,1\n'],
                FILES[1]: ['[Azeroth:12]\nterrain_radius=927\n', '[Azeroth:12]\nterrain_radius=4097\n',
                           '[Azeroth:12]\nterrain_radius=nan\n', '[Azeroth:12]\nunknown=2048\n',
                           '[Azeroth:12]\n', '[Unknown:12]\nterrain_radius=2048\n',
                           '[Azeroth:12]\nterrain_radius=2048\nterrain_radius=2048\n',
                           '[Azeroth:12]\nterrain_radius=2048\n[Azeroth:12]\nterrain_radius=2048\n'],
            }
            for name in FILES:
                for bad in invalid[name]:
                    pair()
                    (root / name).write_bytes(bad.encode('utf-8'))
                    check(root, expected=False)
                for bad in (b'\xfe\xff\x00x', b'\xff\xfex', b'#' * 524289):
                    pair()
                    (root / name).write_bytes(bad)
                    check(root, expected=False)
                pair()
                (root / name).unlink()
                check(root, expected=False)

            # Legal factory drift still fails until the embedded reset defaults agree.
            for changed in FILES:
                for name in FILES:
                    (root / name).write_bytes(before[factory / name])
                check(root, '--factory')
                data = (root / changed).read_bytes()
                old, new = ((b'sun_disc_gain = 3.5', b'sun_disc_gain = 3.4') if changed == FILES[0]
                            else (b'terrain_radius = 2048', b'terrain_radius = 2047'))
                assert old in data
                (root / changed).write_bytes(data.replace(old, new, 1))
                check(root)
                check(root, '--factory', False)
    finally:
        assert all(path.read_bytes() == data for path, data in before.items()), 'Profile input modified'
    print('PASS: valid personal overrides, strict factory/reset/package contract, 24 malformed/missing cases; inputs unchanged')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='northlight-profile-validator-') as temporary:
        validator = Path(temporary) / ('profiles.exe' if os.name == 'nt' else 'profiles')
        target = ['-target', 'x86-windows-gnu'] if os.name == 'nt' else []
        subprocess.run([str(fp.zig()), 'c++', *target, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(), str(Path(__file__).with_name('client_config.cpp')),
                        '-o', str(validator)], env=fp.zig_env(), check=True)
        run(validator)
