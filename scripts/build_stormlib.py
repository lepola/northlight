#!/usr/bin/env python3
"""Build StormLib for the player packages with zig 0.15.2 (never runs a Windows binary).

    python3 scripts/build_stormlib.py                    # every target -> out/stormlib/<target>/
    python3 scripts/build_stormlib.py --target windows   # or mac, linux
    python3 scripts/build_stormlib.py --verify FILE      # check a built library only

Targets (sources from tools/StormLib-master/CMakeLists.txt, read-only):
  windows  x86_64-windows-gnu, UNICODE (paths are wchar_t: CreateFileW, never CreateFileA),
           exports from src/DllMain.def, -lwininet, stripped, no PDB  -> StormLib.dll
  mac      aarch64-macos.12.0, bundled zlib/bzip2 (their headers shadow the SDK's, which
           StormPort.h forces on Apple), install name @rpath/libstorm.dylib -> libstorm.dylib
  linux    x86_64-linux-gnu.2.17 (glibc 2.17 or newer), bundled zlib/bzip2, libc++ linked in,
           soname libstorm.so -> libstorm.so
All: -O2 -D_7ZIP_ST -DBZ_STRICT_ANSI -DNDEBUG, no debug info. The same inputs give the same
bytes (the build runs twice under --check-deterministic). The source tree is identified by
tree_sha256 (every file under src/ plus CMakeLists.txt and LICENSE); renderer/package-pins.json
pins it, and a mismatch refuses the build.

Verification (stdlib parse, nothing is loaded or executed):
  PE32+ AMD64 DLL; exports the 9 functions mpq.py binds; imports only KERNEL32, USER32,
  WININET and api-ms-win-crt-*; KERNEL32 has CreateFileW and no CreateFileA.
  Mach-O arm64 dylib; id @rpath/libstorm.dylib; links only /usr/lib/libSystem.B.dylib; exports
  the 9 functions.
  ELF x86-64 shared object; soname libstorm.so; needs only glibc's own libraries (libc, libm,
  libpthread, libdl, librt, ld-linux) and no symbol version newer than GLIBC_2.17; exports the 9
  functions and nothing outside StormLib's API (LINUX_EXPORTS, a linker version script). None may embed a build path (the unstripped dylib names the zig cache).
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp  # noqa: E402

REQUIRED_EXPORTS = ['SFileOpenArchive', 'SFileCloseArchive', 'SFileOpenFileEx', 'SFileGetFileSize', 'SFileReadFile',
                    'SFileCloseFile', 'SFileHasFile', 'SFileCreateArchive', 'SFileAddFileEx']
IMPORT_ALLOW = re.compile(r'^(kernel32|user32|wininet|api-ms-win-crt-[a-z0-9-]+)\.dll$', re.I)
LISTS = ['SRC_FILES', 'TOMCRYPT_FILES', 'TOMMATH_FILES', 'BZIP2_FILES', 'ZLIB_FILES']
COMMON = ['-O2', '-g0', '-D_7ZIP_ST', '-DBZ_STRICT_ANSI', '-DNDEBUG', '-Isrc', '-Isrc/zlib', '-Isrc/bzip2']
TARGETS = {
    'windows': {'name': 'StormLib.dll', 'zig': 'x86_64-windows-gnu',
                'flags': ['-DUNICODE', '-D_UNICODE', '-s'],
                'extra': ['src/DllMain.c', 'src/DllMain.def'], 'libs': ['-lwininet']},
    'mac': {'name': 'libstorm.dylib', 'zig': 'aarch64-macos.12.0',
            'flags': ['-s', '-Wl,-install_name,@rpath/libstorm.dylib'], 'extra': [], 'libs': []},
    'linux': {'name': 'libstorm.so', 'zig': 'x86_64-linux-gnu.2.17',
              'flags': ['-s', '-fPIC', '-Wl,-soname,libstorm.so'], 'extra': [], 'libs': [], 'exports': 'linux.map'},
}
# ELF exports every global symbol and resolves through the process's global scope: without this, StormLib's
# bundled zlib (inflate, crc32, ...) would bind to the zlib linked into libpython, and libc++ would leak too.
# Only StormLib's API stays visible (SErr*: its POSIX error emulation).
LINUX_EXPORTS = ['SFile*', 'SComp*', 'SListFile*', 'SMem*', 'SErr*']
LINUX_MAP = '{\n  global:\n' + ''.join(f'    {n};\n' for n in LINUX_EXPORTS) + '  local: *;\n};\n'
PINS = fp.RENDERER / 'package-pins.json'


def source_root():
    return fp.tools() / 'StormLib-master'


def sources(root):
    """The library's sources in CMakeLists.txt order (bundled zlib, bzip2, libtomcrypt, libtommath)."""
    text = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    files = []
    for name in LISTS:
        block = re.search(r'set\(' + name + r'\s+(.*?)\)', text, re.S)
        if not block:
            raise ValueError(f'CMakeLists.txt: no set({name} ...)')
        files += [f for f in block.group(1).split() if f.endswith(('.c', '.cpp'))]
    return files


def tree_sha256(root):
    """Identity of the StormLib source tree: sorted (relative path, sha256) of src/, CMakeLists.txt, LICENSE."""
    paths = sorted([p for p in (root / 'src').rglob('*') if p.is_file() and p.name != '.DS_Store'] +
                   [root / 'CMakeLists.txt', root / 'LICENSE'])
    h = hashlib.sha256()
    for p in paths:
        h.update(p.relative_to(root).as_posix().encode() + b'\0' + hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def pinned_tree():
    try:
        return json.loads(PINS.read_text(encoding='utf-8'))['stormlib']['source_tree_sha256']
    except (OSError, KeyError, ValueError):
        return None


def build(target, out_dir, root=None, cache=None):
    """Compile one target into out_dir (cache: a zig cache folder instead of the usual one); returns the library path."""
    root = root or source_root()
    spec = TARGETS[target]
    out_dir.mkdir(parents=True, exist_ok=True)
    result = out_dir / spec['name']
    for stale in out_dir.iterdir():
        if stale.is_file():
            stale.unlink()
    flags = list(spec['flags'])
    if spec.get('exports'):   # a linker version script, written next to the result and removed with the extras
        (out_dir / spec['exports']).write_text(LINUX_MAP, encoding='ascii')
        flags.append(f"-Wl,--version-script={out_dir / spec['exports']}")
    cmd = [str(fp.zig()), 'c++', '-target', spec['zig'], '-shared', *COMMON, *flags,
           *sources(root), *spec['extra'], *spec['libs'], '-o', str(result)]
    env = fp.zig_env()   # both caches outside the (read-only) source tree
    if cache:
        env['ZIG_GLOBAL_CACHE_DIR'] = env['ZIG_LOCAL_CACHE_DIR'] = str(cache)
    env.setdefault('ZIG_LOCAL_CACHE_DIR', env['ZIG_GLOBAL_CACHE_DIR'])
    run = subprocess.run(['nice', '-n', '15', *cmd], cwd=root, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    if run.returncode:
        raise RuntimeError(f'zig failed for {target}:\n' + run.stdout[-4000:])
    for extra in out_dir.iterdir():   # zig may emit an import library or PDB next to the DLL
        if extra != result and extra.is_file():
            extra.unlink()
    return result


# ---- PE32+ ----

def pe_info(data):
    """{'machine','magic','exports','imports': {dll: [names]}} of a PE file (read-only parse)."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    machine, nsections = struct.unpack_from('<HH', data, pe + 4)
    opt_size = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', data, opt)[0]
    dirs = opt + (112 if magic == 0x20b else 96)
    sections = [struct.unpack_from('<IIII', data, opt + opt_size + 40 * i + 8) for i in range(nsections)]

    def off(rva):
        for size, va, raw_size, raw in sections:
            if va <= rva < va + max(size, raw_size):
                return raw + rva - va
        raise ValueError(f'RVA {rva:#x} outside the sections')

    def cstr(rva):
        at = off(rva)
        return data[at:data.index(b'\0', at)].decode('ascii')

    exports = []
    export_rva = struct.unpack_from('<I', data, dirs)[0]
    if export_rva:
        count, names = struct.unpack_from('<I4xI', data, off(export_rva) + 24)
        exports = [cstr(struct.unpack_from('<I', data, off(names) + 4 * i)[0]) for i in range(count)]
    imports = {}
    import_rva = struct.unpack_from('<I', data, dirs + 8)[0]
    at = off(import_rva) if import_rva else None
    wide = magic == 0x20b
    while at is not None:
        lookup, _, _, name, first = struct.unpack_from('<5I', data, at)
        if not (lookup or name or first):
            break
        entries, thunk = [], off(lookup or first)
        while True:
            value = struct.unpack_from('<Q' if wide else '<I', data, thunk)[0]
            if not value:
                break
            if not value >> (63 if wide else 31):
                entries.append(cstr((value & 0x7fffffff) + 2))
            thunk += 8 if wide else 4
        imports[cstr(name)] = entries
        at += 20
    return {'machine': machine, 'magic': magic, 'exports': exports, 'imports': imports}


def verify_windows(path):
    info = pe_info(Path(path).read_bytes())
    problems = []
    if info['machine'] != 0x8664 or info['magic'] != 0x20b:
        problems.append(f"not PE32+ AMD64 (machine {info['machine']:#x}, magic {info['magic']:#x})")
    problems += [f'missing export {n}' for n in REQUIRED_EXPORTS if n not in info['exports']]
    problems += [f'import {d} not allowed' for d in info['imports'] if not IMPORT_ALLOW.match(d)]
    kernel = next((v for d, v in info['imports'].items() if d.lower() == 'kernel32.dll'), [])
    if 'CreateFileW' not in kernel:
        problems.append('CreateFileW not imported (not a UNICODE build)')
    problems += [f'ANSI import {n} (not a UNICODE build)' for n in kernel if n in ('CreateFileA', 'MoveFileA', 'DeleteFileA')]
    return problems, {'format': 'PE32+', 'machine': 'AMD64', 'exports': len(info['exports']),
                      'imports': sorted(info['imports'])}


# ---- Mach-O ----

def macho_info(data):
    """{'cputype','filetype','id','dylibs','exports'} of a thin 64-bit Mach-O (read-only parse)."""
    magic, cputype, _, filetype, ncmds = struct.unpack_from('<IiiII', data, 0)
    if magic != 0xfeedfacf:
        raise ValueError('not a thin 64-bit little-endian Mach-O')
    at, dylibs, ident, exports = 32, [], None, []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', data, at)
        if cmd in (0xc, 0xd, 0x18 | 0x80000000, 0x1f | 0x80000000, 0x23 | 0x80000000):   # LOAD/ID/WEAK/REEXPORT/UPWARD
            name_at = struct.unpack_from('<I', data, at + 8)[0]
            name = data[at + name_at:at + size].split(b'\0', 1)[0].decode()
            if cmd == 0xd:
                ident = name
            else:
                dylibs.append(name)
        elif cmd == 0x2:   # LC_SYMTAB
            symoff, nsyms, stroff, _ = struct.unpack_from('<IIII', data, at + 8)
            for i in range(nsyms):
                strx, ntype, sect, _, _ = struct.unpack_from('<IBBHQ', data, symoff + 16 * i)
                if ntype & 0x01 and ntype & 0x0e == 0x0e and sect:   # N_EXT, N_SECT
                    exports.append(data[stroff + strx:data.index(b'\0', stroff + strx)].decode())
        at += size
    return {'cputype': cputype, 'filetype': filetype, 'id': ident, 'dylibs': dylibs, 'exports': exports}


def verify_mac(path):
    info = macho_info(Path(path).read_bytes())
    problems = []
    if info['cputype'] != 0x0100000c or info['filetype'] != 6:
        problems.append(f"not an arm64 dylib (cputype {info['cputype']:#x}, filetype {info['filetype']})")
    if info['id'] != '@rpath/libstorm.dylib':
        problems.append(f"install name {info['id']}")
    problems += [f'links {d}' for d in info['dylibs'] if d != '/usr/lib/libSystem.B.dylib']
    problems += [f'missing export {n}' for n in REQUIRED_EXPORTS if '_' + n not in info['exports']]
    return problems, {'format': 'Mach-O', 'machine': 'arm64', 'exports': len(info['exports']), 'dylibs': info['dylibs']}


# ---- ELF ----

LINUX_NEEDED = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1', 'ld-linux-x86-64.so.2'}
GLIBC_FLOOR = (2, 17)   # the zig target's glibc: every distribution since 2014 has at least this


def elf_info(data):
    """{'machine','type','soname','needed','exports','glibc'} of a 64-bit little-endian ELF (read-only parse)."""
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise ValueError('not a 64-bit little-endian ELF')
    etype, machine = struct.unpack_from('<HH', data, 16)
    shoff, = struct.unpack_from('<Q', data, 40)
    shentsize, shnum = struct.unpack_from('<HH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + shentsize * i) for i in range(shnum)]

    def cstr(at):
        return data[at:data.index(b'\0', at)].decode('ascii', 'replace')

    needed, soname, exports, glibc = [], None, [], []
    for _, kind, _, _, offset, size, link, _, _, entsize in sections:
        strtab = sections[link][4] if link < len(sections) else 0
        if kind == 6:   # SHT_DYNAMIC
            for at in range(offset, offset + size, entsize or 16):
                tag, value = struct.unpack_from('<qQ', data, at)
                if tag == 1:
                    needed.append(cstr(strtab + value))
                elif tag == 14:
                    soname = cstr(strtab + value)
        elif kind == 11:   # SHT_DYNSYM
            for at in range(offset + 24, offset + size, entsize or 24):   # entry 0 is the null symbol
                name, info, _, shndx = struct.unpack_from('<IBBH', data, at)
                if info >> 4 in (1, 2) and shndx:   # STB_GLOBAL/STB_WEAK, defined here
                    exports.append(cstr(strtab + name))
        elif kind == 0x6ffffffe:   # SHT_GNU_verneed: the symbol versions required from each library
            at = offset
            while True:
                _, count, _, aux, following = struct.unpack_from('<HHIII', data, at)
                vat = at + aux
                for _ in range(count):
                    _, _, _, vname, vnext = struct.unpack_from('<IHHII', data, vat)
                    glibc.append(cstr(strtab + vname))
                    vat += vnext
                if not following:
                    break
                at += following
    return {'machine': machine, 'type': etype, 'soname': soname, 'needed': needed, 'exports': exports, 'versions': glibc}


def verify_linux(path):
    info = elf_info(Path(path).read_bytes())
    problems = []
    if info['machine'] != 62 or info['type'] != 3:
        problems.append(f"not an x86-64 shared object (machine {info['machine']}, type {info['type']})")
    if info['soname'] != 'libstorm.so':
        problems.append(f"soname {info['soname']}")
    problems += [f'links {d}' for d in info['needed'] if d not in LINUX_NEEDED]
    problems += [f'missing export {n}' for n in REQUIRED_EXPORTS if n not in info['exports']]
    problems += [f'exports {n} (not StormLib API)' for n in info['exports']
                 if not any(fnmatch.fnmatchcase(n, pattern) for pattern in LINUX_EXPORTS)]
    for v in info['versions']:
        m = re.fullmatch(r'GLIBC_(\d+)\.(\d+)(?:\.\d+)?', v)
        if m and (int(m.group(1)), int(m.group(2))) > GLIBC_FLOOR:
            problems.append(f'needs {v} (newer than glibc {GLIBC_FLOOR[0]}.{GLIBC_FLOOR[1]})')
    return problems, {'format': 'ELF', 'machine': 'x86-64', 'exports': len(info['exports']), 'needed': info['needed'],
                      'glibc': sorted({v for v in info['versions'] if v.startswith('GLIBC_')})}


BUILD_PATHS = re.compile(rb'/Users/|/private/|/home/|/tmp/|zig-cache|[A-Za-z]:\\\\Users', re.I)  # layout: allow-machine-path


def verify(path):
    data = Path(path).read_bytes()
    problems, info = verify_windows(path) if data[:2] == b'MZ' else verify_linux(path) if data[:4] == b'\x7fELF' \
        else verify_mac(path)
    # A debug map or object path would carry the build machine's folders into the package.
    problems += sorted({'embedded build path ' + m.group().decode('latin-1') for m in BUILD_PATHS.finditer(data)})
    return problems, info


def notices(root):
    """License text for the package: StormLib (MIT) plus the notices of the libraries it bundles."""
    src = root / 'src'
    head = lambda p, n: ''.join(p.read_text(encoding='latin-1').splitlines(keepends=True)[:n])
    return '\n'.join([
        '== StormLib (MIT) ==', (root / 'LICENSE').read_text(encoding='utf-8'),
        '== zlib 1.2.5 (bundled in StormLib) ==', head(src / 'zlib/zlib.h', 24),
        '== bzip2/libbzip2 1.0.5 (bundled in StormLib; BSD-style licence, https://sourceware.org/bzip2/) ==',
        head(src / 'bzip2/bzlib.h', 20),
        '== LibTomCrypt and LibTomMath (bundled in StormLib) ==', head(src / 'libtomcrypt/src/hashes/md5.c', 10),
        '== LZMA SDK (bundled in StormLib; public domain) ==', (src / 'lzma/info.txt').read_text(encoding='latin-1'),
    ])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--target', choices=['all', *TARGETS], default='all')
    ap.add_argument('--out', type=Path, help='default: <out>/stormlib')
    ap.add_argument('--verify', type=Path, help='only verify this built library')
    ap.add_argument('--check-deterministic', action='store_true', help='build again from an empty zig cache and compare the bytes')
    args = ap.parse_args()
    if args.verify:
        problems, info = verify(args.verify)
        print(json.dumps({'file': str(args.verify), 'problems': problems, **info}, indent=2))
        return 1 if problems else 0
    root = source_root()
    tree = tree_sha256(root)
    if pinned_tree() and tree != pinned_tree():
        print(f'StormLib source tree {tree} does not match the pin {pinned_tree()}', file=sys.stderr)
        return 2
    out = args.out or fp.out() / 'stormlib'
    report = {'source_tree_sha256': tree, 'zig': fp.ZIG_VERSION, 'targets': {}}
    failed = False
    for target in TARGETS if args.target == 'all' else [args.target]:
        lib = build(target, out / target, root)
        digest = hashlib.sha256(lib.read_bytes()).hexdigest()
        entry = {'file': str(lib), 'sha256': digest, 'bytes': lib.stat().st_size}
        if args.check_deterministic:
            # A second build from an empty zig cache in another folder must give the same bytes.
            cache = out / (target + '.cache')
            shutil.rmtree(cache, ignore_errors=True)
            again = build(target, out / (target + '.again'), root, cache)
            entry['deterministic'] = again.read_bytes() == lib.read_bytes()
            shutil.rmtree(again.parent)
            shutil.rmtree(cache)
            failed |= not entry['deterministic']
        problems, info = verify(lib)
        entry.update(problems=problems, **info)
        failed |= bool(problems)
        report['targets'][target] = entry
    (out / 'NOTICES.txt').write_text(notices(root), encoding='utf-8')
    (out / 'build-stormlib.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
