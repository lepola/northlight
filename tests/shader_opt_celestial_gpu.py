"""Run the prebuilt exact celestial oracle with four explicitly supplied shader binaries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'old-disc', 'new-disc', 'old-veil', 'new-veil', 'backend', 'work-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--benchmark', type=int, nargs=2, metavar=('WIDTH', 'HEIGHT'))
    parser.add_argument('--timeout', type=int, default=600)
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Requires Windows D3D9; does not launch or install a game.')
    folder = args.work_dir.resolve()
    if fp.is_client(folder):
        raise SystemExit('Refusing to run in a game client directory.')
    paths = [getattr(args, name).resolve() for name in ('old_disc', 'new_disc', 'old_veil', 'new_veil')]
    for path in paths:
        data = path.read_bytes()
        assert len(data) >= 8 and len(data) % 4 == 0 and struct.unpack_from('<I', data)[0] == 0xffff0300, path
    folder.mkdir(parents=True, exist_ok=True)
    print(json.dumps({'shader_files': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
                      'backend_sha256': hashlib.sha256(args.backend.read_bytes()).hexdigest(),
                      'game_launched': False, 'exact_rgba': True, 'exact_per_sample_stencil_coverage': True}), flush=True)
    command = [str(args.executable.resolve()), *(str(path) for path in paths), str(args.backend.resolve())]
    if args.benchmark:
        assert min(args.benchmark) >= 256
        command += ['--benchmark', *(str(value) for value in args.benchmark)]
    environment = dict(os.environ, DXVK_LOG_PATH=str(folder))
    subprocess.run(command, cwd=folder, env=environment, check=True, timeout=args.timeout)


if __name__ == '__main__':
    main()
