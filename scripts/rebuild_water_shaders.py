"""Recompile the exported water shaders and compare their headers and include sets with the branch."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from tideglass_source import ROOT, verify
sys.path.insert(0, str(ROOT / 'tools'))
import shader_build


def wine_path(path):
    return 'Z:' + str(path.resolve()).replace('/', '\\')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True, help='pinned D3DCompiler_43.dll')
    parser.add_argument('--shader', action='append', help='rebuild specific symbols; defaults to all')
    args = parser.parse_args()
    manifest = verify()
    if shader_build.digest(args.compiler) != manifest['compiler_sha256']:
        parser.error('D3DCompiler DLL differs from the source manifest')
    selected = args.shader or sorted(manifest['shaders'])
    if set(selected) - manifest['shaders'].keys():
        parser.error('unknown shader name')
    with tempfile.TemporaryDirectory() as temp:
        build = Path(temp)
        lists, generated = build / 'lists', build / 'generated'
        lists.mkdir()
        generated.mkdir()
        driver = build / 'compile_shaders.exe'
        subprocess.run(['i686-w64-mingw32-clang++', '-std=c++17', '-O2', '-static', '-DWIN32_LEAN_AND_MEAN',
                        '-DNOMINMAX', '-Wl,--no-insert-timestamp', ROOT / 'tools/compile_shaders.cpp',
                        '-ladvapi32', '-o', driver], check=True)
        for name in selected:
            record = manifest['shaders'][name]
            (lists / (name + '.list')).write_text('\t'.join([name, wine_path(ROOT / record['source']), record['target']]) + '\n')
        shader_build.prepare(build, lists, args.compiler)
        env = os.environ | {'WINEDEBUG': '-all', 'WINEDLLOVERRIDES': 'd3dcompiler_43=n;mscoree,mshtml='}
        for name in selected:
            subprocess.run(['wine', driver, wine_path(args.compiler), wine_path(lists / (name + '.list')),
                            wine_path(generated)], env=env, check=True)
        rebuilt = shader_build.publish(build, cache=build / 'cache')
        for name, record in rebuilt['shaders'].items():
            dependencies = {Path(p).relative_to(ROOT).as_posix(): digest for p, digest in record['dependencies'].items()
                            if p != str(args.compiler.resolve())}
            expected = manifest['shaders'][name]
            if record['products'][name + '.h'] != expected['sha256'] or dependencies != expected['dependencies']:
                raise ValueError('Recompiled shader differs: ' + name)
        print('Verified', len(selected), 'compiled water shaders')


if __name__ == '__main__':
    main()
