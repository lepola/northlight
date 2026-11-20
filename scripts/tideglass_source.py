"""Check the reviewed Tideglass export before building or packaging it."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'tideglass'


def verify(root=ROOT):
    manifest = json.loads((root / 'source.json').read_bytes())
    approved = set(manifest['files'])
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()
              and not (p.parent.name == '__pycache__' and p.suffix == '.pyc')}
    extra = actual - approved - {'CMakeLists.txt', 'source.json'}
    if extra:
        raise ValueError('Unreviewed Tideglass files: ' + ', '.join(sorted(extra)))
    for name, digest in manifest['files'].items():
        path = root / name
        if not path.resolve().is_relative_to(root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Tideglass export differs: ' + name)
    return manifest


if __name__ == '__main__':
    print('Tideglass source verified at', verify()['revision'])
