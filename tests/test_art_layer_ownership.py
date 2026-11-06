#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Art ownership survives lost receipts; modified/merged archives remain foreign."""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_art_layer as art
import northlight_install as installer
from build_packages import APP_FILES


PACKAGED_PROBE = r'''
from pathlib import Path
import hashlib, io, os, sys
app, client = map(Path, sys.argv[1:])
sys.path.insert(0, str(app))
import northlight_paths as fp
fp.use_source_modules()
import northlight_install as installer
import build_art_layer as art
assert not fp.LOCAL_INI.exists()
try:
    fp.stormlib()
except fp.Missing:
    pass
else:
    raise AssertionError('Probe inherited a developer StormLib configuration')
package = installer.Package(app.parent, installer.host_platform())
inst = installer.Installer(package, out=io.StringIO())
assert fp.stormlib() == package.stormlib
assert os.environ['NORTHLIGHT_STORMLIB'] == inst.child_env()['NORTHLIGHT_STORMLIB'] == str(package.stormlib)
assert not inst.art_shas
before = {p.relative_to(client).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in client.rglob('*') if p.is_file()}
assert inst.foreign_z(client, 'enUS') == []
for name in ('changed.mpq', 'merged.mpq', 'legacy.mpq'):
    digest = hashlib.sha256((client/name).read_bytes()).hexdigest()
    assert not inst.owns_art(client, name, digest), name
assert before == {p.relative_to(client).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in client.rglob('*') if p.is_file()}
print('PASS packaged parent ownership: bundled StormLib, clean configuration, complete manifest, foreign/tampered/merged rejection, read-only client')
'''

scratch = fp.output_dir()
scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=scratch) as folder:
    client = Path(folder) / 'Игра 世界 🎮'
    (client / 'Data/enUS').mkdir(parents=True)
    payload = art.owned_payload({'DBFilesClient\\Light.dbc': b'our lighting', 'world\\lamp.m2': b'our model'})
    for name in ['Data/patch-z.mpq', 'Data/enUS/patch-enus-z.mpq']:
        art.write_archive(client / name, payload)
        assert art.owns_archive(client / name)
    inst = installer.Installer.__new__(installer.Installer)
    inst.art_shas = set()
    assert inst.foreign_z(client, 'enUS') == []  # no backups or known digest
    for name, changed in [('changed.mpq', dict(payload, **{'world\\lamp.m2': b'edited'})),
                          ('merged.mpq', dict(payload, **{'foreign.txt': b'other mod'}))]:
        path = client / name
        art.write_archive(path, changed)
        assert not art.owns_archive(path)
    art.write_archive(client / 'legacy.mpq', {'world\\lamp.m2': b'no marker'})
    assert not art.owns_archive(client / 'legacy.mpq')
    # Recreate the packaged app, excluding the repository's machine config.
    # A fresh -I process cannot reuse this test's already-loaded StormLib DLL.
    package = Path(folder) / 'package'
    app = package / 'app'
    for name in APP_FILES:
        target = app / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(fp.REPO / name, target)
    bundled = installer.Package(package, installer.host_platform()).stormlib
    bundled.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(fp.stormlib(), bundled)
    probe = package / 'probe.py'
    probe.write_text(PACKAGED_PROBE, encoding='utf-8')
    clean = {key:value for key,value in os.environ.items() if not key.startswith('NORTHLIGHT_')}
    for inherited in ({}, {'NORTHLIGHT_STORMLIB': str(package/'missing-stormlib')}):
        subprocess.run([sys.executable, '-I', '-B', '-X', 'utf8', str(probe), str(app), str(client)],
                       cwd=package, env={**clean, **inherited}, check=True)
print('PASS art ownership: Unicode paths, no receipts, duplicate archives, tampering, merged mod and missing marker')
