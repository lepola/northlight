#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""An unsupported mod sky is preserved, never partly cloned or allowed to fail the art build."""
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from build_lighting import Skip
import build_outdoor_single_moon as moon
from build_art_layer import write_archive


def dbc(row, strings=b'\0'):
    return b'WDBC' + struct.pack('<4I', 1, len(row), len(row)*4, len(strings)) + struct.pack('<%dI'%len(row), *row) + strings


with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
    root = Path(folder)
    source = root / 'source.mpq'
    light = [1, 0, *([0]*13)]
    light[7] = 10
    write_archive(source, {'DBFilesClient\\Light.dbc': dbc(light),
                           'DBFilesClient\\LightParams.dbc': dbc([10, 0, 20]),
                           'DBFilesClient\\LightSkybox.dbc': dbc([20, 1, 0], b'\0Environments\\Stars\\test.mdx\0')})
    skin = bytearray(72)
    skin[:4] = b'SKIN'
    struct.pack_into('<II', skin, 36, 1, 48)
    for views in (1, 2):
        model = bytearray(304)
        struct.pack_into('<I', model, 68, views)
        files = {'environments\\stars\\test.m2': bytes(model), 'environments\\stars\\test00.skin': bytes(skin)}
        def read(name):
            try:return files[name.lower()]
            except KeyError:raise FileNotFoundError(name)
        assets = SimpleNamespace(read=read, providers=files)
        with patch.object(moon, 'texture_names', return_value=([], [])), \
             patch.object(moon, 'batch_names', return_value=['arathi_moon.blp', 'neksys_stars.blp']):
            try:
                moon.build(source, root / f'output-{views}', assets)
            except Skip:
                pass
            else:
                raise AssertionError('Unsupported sky clone was published')
        assert not (root / f'output-{views}' / 'patch-z.mpq').exists()
print('PASS: mixed lunar batches and missing LOD skins preserve the original sky')
