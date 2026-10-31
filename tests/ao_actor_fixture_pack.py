"""Read authored issue-106 JSONL records with compressed numeric textures."""
import base64
import hashlib
import json
from pathlib import Path
import zlib


def load_fixtures(output=None):
    root = Path(__file__).resolve().parent
    records = [json.loads(line) for line in (root / 'fixtures/ao_actor_106.jsonl').read_text().splitlines()]
    manifest = records[0]
    if manifest['type'] == 'manifest':
        assert manifest['schema'] == 'ao_actor_fixture_pack_v2'
        assert manifest['generator_sha256'] == hashlib.sha256((root / 'ao_actor_reference.py').read_bytes()).hexdigest()
        assert len(manifest['fixtures']) == 20 and len(set(manifest['fixtures'])) == 20
        fixtures = {record['name']: record['data'] for record in records[1:] if record['type'] == 'fixture'}
        textures = {record['name']: record['zlib_base64'] for record in records[1:] if record['type'] == 'texture'}
        assert len(records) == 1 + len(fixtures) + len(textures)
        assert set(fixtures) == set(manifest['fixtures'])
        results, expected = [], set()
        if output is not None:
            output = Path(output)
            output.mkdir(parents=True, exist_ok=True)
        for name in manifest['fixtures']:
            assert Path(name).name == name and name.endswith('.json')
            fixture = fixtures[name]
            for key in ('depth', 'actor'):
                filename = fixture[key + '_file']
                assert Path(filename).name == filename
                expected.add(filename)
                pixels = zlib.decompress(base64.b64decode(textures[filename], validate=True))
                assert len(pixels) == fixture['size'][0] * fixture['size'][1] * 4
                assert hashlib.sha256(pixels).hexdigest() == fixture[key + '_sha256']
                if output is not None:
                    (output / filename).write_bytes(pixels)
            if output is not None:
                (output / name).write_text(json.dumps(fixture) + '\n', encoding='utf-8')
            results.append((name, fixture))
        assert set(textures) == expected
    return results
