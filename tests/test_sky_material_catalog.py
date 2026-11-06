#!/usr/bin/env python3
# northlight-test: requires=client
"""Every active sky/skin/material must have a current, unambiguous renderer role."""
from pathlib import Path
import json
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from scripts import build_sky_material_catalog as catalog

catalog.check_generated()
subprocess.run([sys.executable,str(fp.REPO/'scripts/build_sky_material_catalog.py'),'--check'],check=True)
roles=json.loads(catalog.ROLES.read_text())['materials']
assert catalog.role(roles,'environments\\stars\\icecrown_skybowla_night.blp',320)=='background'
assert catalog.role(roles,'environments\\stars\\icecrown_skybowla_night.blp',192)=='foreground'
assert catalog.role(roles,'environments\\stars\\dalaranday_dome02.blp',17)=='background'
assert catalog.role(roles,'environments\\stars\\dalaran_night_clouds02.blp',37)=='foreground'
try:
    catalog.role(roles,'new_material.blp',1)
except KeyError:
    pass
else:
    raise AssertionError('Unreviewed material must fail catalog generation')
print('PASS all active sky materials; no asset writes, map IDs or shader identities')
