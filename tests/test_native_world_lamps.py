#!/usr/bin/env python3
# northlight-test: requires=zig,stormlib,client
"""Actual native inventory -> ownership -> light selection, with no model draw."""
from pathlib import Path
import argparse
import subprocess
import sys
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
from world_scene_builder import Assets, Builder
from native_fixture_catalog import qualify, shell_geometry, write

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixtures', type=Path, default=fp.output_dir()/'native-world-lamps')
args = parser.parse_args()
args.fixtures.mkdir(parents=True, exist_ok=True)
assets = Assets()
builder = Builder(assets)
proof = []
try:
    for name in ('dalaran_lamppost01', 'dalaran_lampwall01'):
        path = 'world\\expansion02\\doodads\\dalaran\\'+name+'.m2'
        blob = assets.read(path)
        record = qualify(assets, path, blob, builder)
        assert record and record[3] == 1, name
        proof.append((record, shell_geometry(builder, path, blob, record[3])))
        (args.fixtures/(name+'.m2')).write_bytes(blob)
    # The same provenance cannot be obtained by a native weapon or spell.
    for path in ('item\\objectcomponents\\weapon\\knife_1h_utgarde_d_03.m2',
                 'spells\\enchantments\\shaman_fire.m2'):
        assert qualify(assets, path, assets.read(path)) is None, path
finally:
    assets.close()
catalog = args.fixtures/'native-fixtures.bin'
write(catalog, proof)
exe = args.fixtures/'native-world-lamps.exe'
command = [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
           *fp.test_include_flags(), fp.TESTS/'test_native_world_lamps.cpp', '-o', exe]
subprocess.run(list(map(str, command)), check=True, env=fp.zig_env())
data = catalog.read_bytes()
for label, invalid in (('truncated', data[:-1]), ('trailing', data+b'\0'), ('version', data[:4]+bytes(4)+data[8:]),
                       ('source-mask', data[:48]+bytes(8)+data[56:])):
    path = args.fixtures/(label+'.bin')
    path.write_bytes(invalid)
    subprocess.run([str(exe), '--invalid-catalog', str(path)], check=True)
for case, name in (('dalaran-post', 'dalaran_lamppost01'), ('dalaran-wall', 'dalaran_lampwall01')):
    output = args.fixtures/case
    output.mkdir(exist_ok=True)
    subprocess.run([str(exe), str(args.fixtures/(name+'.m2')), str(catalog), '-', '-', str(output/'sources.fgl')], check=True)
for case in ('orc-street', 'orc-hanging'):
    output = args.fixtures/case
    if not (output/'source.m2').exists():
        continue
    proof_path = output/'source-proofs.fgl'
    if not proof_path.exists():
        (output/'sources.fgl').replace(proof_path)
    subprocess.run([str(exe), str(output/'source.m2'), str(catalog), str(proof_path), str(output/'sources.fga'), str(output/'sources.fgl')], check=True)
