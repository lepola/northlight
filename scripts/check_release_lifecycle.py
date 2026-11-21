"""Rehearse upgrade, log collection and uninstall on a disposable copy of an enUS client."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

OUT = Path(__file__).resolve().parents[1] / 'out'


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def unpack(archive, folder):
    subprocess.run(['unzip', '-q', archive, '-d', folder], check=True)
    inner, = [p for p in folder.iterdir() if p.is_dir()]
    return inner


def clone_file(source, target):
    subprocess.run(['cp', '--preserve=mode,timestamps', '--reflink=always', '--', source, target], check=True)
    return target


def run(options, scratch):
    platform = options.platform
    target = scratch / 'client'
    def ignore(folder, names):
        path = Path(folder).relative_to(options.client)
        private = {'logs', 'world-cache'} if not path.parts else {'account'} if str(path).lower() == 'wtf' else set()
        omitted = [n for n in names if n.lower() in private]
        for name in set(names) - set(omitted):
            if (Path(folder) / name).is_symlink():
                if path.as_posix().lower() == 'interface/addons':
                    omitted.append(name)
                    continue
                raise ValueError('Use a disposable base client without symlinks: ' + str(path / name))
        return omitted
    shutil.copytree(options.client, target, copy_function=clone_file, ignore=ignore)
    if options.fog:
        shutil.copytree(options.fog, target / 'world-cache/fog', copy_function=clone_file)
    packages = {name: unpack(archive, scratch / name) for name, archive in
                [('previous', options.previous), ('new', options.release)]}
    foreign = b'private rollback fixture'
    (target / 'd3d9.dll').write_bytes(foreign)
    addon = target / 'interface/addons/BetaRollbackProbe'
    addon.mkdir(parents=True)
    (addon / 'BetaRollbackProbe.toc').write_bytes(b'## Interface: 30300\n')
    protected = [p for p in (target / n for n in ('wow.exe', 'WTF/Config.wtf', 'data/patch-a.mpq', 'data/enus/realmlist.wtf',
                                                    'interface/addons/BetaRollbackProbe/BetaRollbackProbe.toc')) if p.exists()]
    before = {str(p): sha(p) for p in protected}

    def tree():
        return {str(p.relative_to(target)): (p.stat().st_mtime_ns, p.stat().st_size)
                for p in (target / 'data').rglob('*') if p.is_file()}
    inventory = tree()
    results = {'platform': platform, 'protected': len(protected), 'steps': []}
    edited = None
    for name, action in [('previous', 'install'), ('new', 'install'), ('new', 'logs'), ('new', 'uninstall')]:
        pkg = packages[name]
        env = dict(os.environ)
        if platform == 'linux':
            script = {'install': 'install.sh', 'uninstall': 'uninstall.sh', 'logs': 'collect-logs.sh'}[action]
            args = ['bash', str(pkg / script), '--client', str(target)]
        else:
            script = {'install': 'Install.cmd', 'uninstall': 'Uninstall.cmd', 'logs': 'Collect Logs.cmd'}[action]
            args = ['wine', 'cmd', '/c', script, '--client', 'Z:' + str(target).replace('/', '\\')]
            env.update(WINEPREFIX=str(scratch / 'wine'), WINEDEBUG='-all')
        if action != 'logs':
            args.append('--yes')
        if action == 'install':
            args += ['--no-world-cache', '--locale', 'enUS']
        report = scratch / f'{platform}-{name}-{action}.log'
        with report.open('wb') as log:
            code = subprocess.run(args, cwd=pkg, env=env, input=b'\r\n', stdout=log, stderr=subprocess.STDOUT,
                                  timeout=3600).returncode
        if code:
            raise RuntimeError(f'{name} {action} exited {code}\n' + report.read_text(errors='replace')[-4096:])
        assert all(sha(Path(p)) == v for p, v in before.items()), 'a protected file changed'
        step = {'package': name, 'action': action}
        if action == 'install':
            for dll in ('d3d9.dll', 'water-core.dll'):
                assert sha(target / dll) == sha(pkg / 'payload' / dll), dll
            setting = target / 'water.ini'
            if name == 'previous':
                edited, n = re.subn(rb'(?im)^WaveCrestSharpness[ \t]*=[^\r\n]*', b'WaveCrestSharpness=0.44', setting.read_bytes())
                assert n == 1
                edited += b'\n; tester keeps this comment\n'
                setting.write_bytes(edited)
            else:
                assert setting.read_bytes() == edited, 'the upgrade replaced edited water settings'
                if options.expected_art:
                    for rel in ('patch-y.mpq', 'enus/patch-enus-y.mpq'):
                        assert sha(target / 'data' / rel) == sha(options.expected_art / rel), rel
                    step['art_matches'] = True
        elif action == 'logs':
            archive, = sorted(pkg.glob('northlight-logs-*.zip'))[-1:]
            with zipfile.ZipFile(archive) as z:
                names = z.namelist()
            assert 'report.json' in names and 'game/water.ini' in names, names
            assert not [n for n in names if 'Account' in n or 'WTF' in n], names
            step['logs'] = names
        else:
            assert tree() == inventory, 'data/ differs after uninstall'
            assert (target / 'd3d9.dll').read_bytes() == foreign, 'the foreign d3d9.dll was not restored'
            assert not (target / 'water-core.dll').exists()
            assert (target / 'water.ini').read_bytes() == edited, 'uninstall dropped edited water settings'
        results['steps'].append(step)
        print(platform, name, action, 'passed', flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=('linux', 'windows'))
    parser.add_argument('--client', type=Path, required=True, help='read-only enUS base client on the scratch filesystem')
    parser.add_argument('--previous', type=Path, required=True, help='previous installer ZIP for this platform')
    parser.add_argument('--release', type=Path, required=True, help='release installer ZIP for this platform')
    parser.add_argument('--fog', type=Path, help='optional world-cache fog zones for art comparison')
    parser.add_argument('--expected-art', type=Path, help='directory containing the two expected patch-y archives')
    if not __debug__:
        parser.error('release verification requires Python assertions')
    options = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='release-rehearsal-', dir=OUT) as folder:
        print(json.dumps(run(options, Path(folder)), indent=2))


if __name__ == '__main__':
    main()
