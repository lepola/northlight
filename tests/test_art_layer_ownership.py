#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Art ownership survives lost receipts; modified/merged archives remain foreign."""
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_art_layer as art
import northlight_install as installer

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
    for changed in [dict(payload, **{'world\\lamp.m2': b'edited'}), dict(payload, **{'foreign.txt': b'other mod'})]:
        path = client / 'changed.mpq'
        art.write_archive(path, changed)
        assert not art.owns_archive(path)
    art.write_archive(client / 'legacy.mpq', {'world\\lamp.m2': b'no marker'})
    assert not art.owns_archive(client / 'legacy.mpq')
print('PASS art ownership: Unicode paths, no receipts, duplicate archives, tampering, merged mod and missing marker')
