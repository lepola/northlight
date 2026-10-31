#!/usr/bin/env python3
"""Generated fake clients must be invisible to WoW's loose-file indexer."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with TemporaryDirectory() as temporary:
    root = Path(temporary)
    client = root/'client'
    with patch.object(fp, 'client_root', return_value=client):
        for target in [client/'out', client/'northlight-diagnostics'/'test-prerequisites']:
            protected = fp.protect_output(target)
            assert protected == target/'.svn'
            assert fp.protect_output(protected) == protected
            # Native archive enumeration skips .svn before descending into
            # the fixtures, including fake MPQs and world-cache model files.
            assert '.svn' in protected.relative_to(client).parts
        outside = root/'stand-output'
        assert fp.protect_output(outside) == outside
        nested = client/'.svn'/'reports'
        assert fp.protect_output(nested) == nested
        with patch.object(fp, '_path', return_value=client/'out'):
            assert fp.out() == client/'out'/'.svn'
    with patch.object(fp, 'client_root', return_value=None):
        assert fp.protect_output(client/'out') == client/'out'
print('PASS game-local test outputs protected; external paths and existing .svn unchanged')
