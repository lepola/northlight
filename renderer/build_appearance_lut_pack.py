#!/usr/bin/env python3
"""Bake the shipped Vibrant.ini into a deterministic, indexed 32^3 RGBA16 pack.

    python3 renderer/build_appearance_lut_pack.py --input client-config/appearance-presets/Vibrant.ini \
        --output Vibrant.nllutpack

The small C++ producer uses the same INI parser, BakeKey and colour-volume baker as the renderer. Python only
adds the deterministic index and integrity metadata; it does not implement or approximate the colour transform.
"""
import argparse
import hashlib
import json
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW_HEADER = struct.Struct('<8sHHI')
PACK_HEADER = struct.Struct('<8sHHHHHHIQQ32s32s')
PACK_RECORD = struct.Struct('<Q74sH QI32s')
PACK_MAGIC = b'NLLUTPK\0'
RAW_MAGIC = b'NLRAW001'
FORMAT_VERSION = 1
VOXEL_RGBA16_UNORM_LE = 1
BAKE_KEY_BYTES = 74
RECORD_BYTES = 128
CUBE_SIZE = 32
VOLUME_BYTES = CUBE_SIZE ** 3 * 4 * 2
INDEX_OFFSET = PACK_HEADER.size
MAX_ENTRIES = 4096


def stable_hash(key_bytes):
    """The renderer's 64-bit FNV-1a BakeKey hash over its canonical little-endian fields."""
    value = 14695981039346656037
    for byte in key_bytes:
        value ^= byte
        value = (value * 1099511628211) & 0xffffffffffffffff
    return value


def _compiler_command(compiler=None):
    selected = compiler or os.environ.get('CXX')
    if selected:
        command = shlex.split(selected)
        if command and shutil.which(command[0]):
            return command
    for name in ('c++', 'clang++', 'g++'):
        path = shutil.which(name)
        if path:
            return [path]
    raise RuntimeError('a C++17 compiler (c++, clang++ or g++) is required to bake the appearance LUT pack')


def _raw_records(data):
    if len(data) < RAW_HEADER.size:
        raise ValueError('C++ LUT producer wrote a truncated raw header')
    magic, raw_version, math_version, count = RAW_HEADER.unpack_from(data)
    if magic != RAW_MAGIC or raw_version != FORMAT_VERSION or math_version == 0:
        raise ValueError('C++ LUT producer wrote an unsupported raw format')
    if count > MAX_ENTRIES:
        raise ValueError(f'C++ LUT producer returned too many unique tables: {count}')
    record_size = 8 + BAKE_KEY_BYTES + VOLUME_BYTES
    if len(data) != RAW_HEADER.size + count * record_size:
        raise ValueError('C++ LUT producer wrote an invalid raw table extent')
    rows, offset, previous = [], RAW_HEADER.size, None
    seen = set()
    for _ in range(count):
        key_hash = struct.unpack_from('<Q', data, offset)[0]
        offset += 8
        key_bytes = data[offset:offset + BAKE_KEY_BYTES]
        offset += BAKE_KEY_BYTES
        table = data[offset:offset + VOLUME_BYTES]
        offset += VOLUME_BYTES
        if stable_hash(key_bytes) != key_hash:
            raise ValueError('C++ BakeKey hash does not match its canonical fields')
        if struct.unpack_from('<H', key_bytes, 40)[0] == 0:
            raise ValueError('identity BakeKey should not have a baked table')
        ordering = (key_hash, key_bytes)
        if previous is not None and ordering <= previous:
            raise ValueError('C++ BakeKey tables are not unique and deterministically ordered')
        previous = ordering
        if key_bytes in seen:
            raise ValueError('C++ BakeKey tables contain a duplicate canonical key')
        seen.add(key_bytes)
        rows.append((key_hash, key_bytes, table))
    return math_version, rows


def make_pack(raw_data, source_ini):
    math_version, rows = _raw_records(raw_data)
    index_offset = PACK_HEADER.size
    tables_offset = index_offset + len(rows) * RECORD_BYTES
    index, tables = bytearray(), bytearray()
    for row, (key_hash, key_bytes, table) in enumerate(rows):
        offset = tables_offset + row * VOLUME_BYTES
        index.extend(PACK_RECORD.pack(key_hash, key_bytes, 0, offset, VOLUME_BYTES,
                                      hashlib.sha256(table).digest()))
        tables.extend(table)
    if len(index) != len(rows) * RECORD_BYTES:
        raise AssertionError('LUT pack index record size changed')
    header = PACK_HEADER.pack(
        PACK_MAGIC, FORMAT_VERSION, math_version, CUBE_SIZE, VOXEL_RGBA16_UNORM_LE,
        BAKE_KEY_BYTES, RECORD_BYTES, len(rows), index_offset, tables_offset,
        hashlib.sha256(source_ini).digest(), hashlib.sha256(index).digest())
    packed = header + index + tables
    validate_pack(packed, source_ini=source_ini, verify_tables=True)
    return packed


def validate_pack(data, *, source_ini=None, verify_tables=False, expected_math_version=None):
    if len(data) < PACK_HEADER.size:
        raise ValueError('LUT pack is truncated before its header')
    (magic, format_version, math_version, cube_size, voxel_format, key_size, record_size,
     count, index_offset, tables_offset, source_hash, index_hash) = PACK_HEADER.unpack_from(data)
    if magic != PACK_MAGIC or format_version != FORMAT_VERSION:
        raise ValueError('unsupported LUT pack format')
    if expected_math_version is not None and math_version != expected_math_version:
        raise ValueError('LUT pack mathematical schema version differs from the renderer')
    if cube_size != CUBE_SIZE or voxel_format != VOXEL_RGBA16_UNORM_LE:
        raise ValueError('unsupported LUT cube dimensions or voxel format')
    if key_size != BAKE_KEY_BYTES or record_size != RECORD_BYTES or count > MAX_ENTRIES:
        raise ValueError('unsupported LUT BakeKey or index record size')
    if index_offset != PACK_HEADER.size or tables_offset != index_offset + count * record_size:
        raise ValueError('LUT pack index offsets are invalid')
    if len(data) != tables_offset + count * VOLUME_BYTES:
        raise ValueError('LUT pack cube extents do not match its file size')
    index = data[index_offset:tables_offset]
    if hashlib.sha256(index).digest() != index_hash:
        raise ValueError('LUT pack index digest mismatch')
    if source_ini is not None and hashlib.sha256(source_ini).digest() != source_hash:
        raise ValueError('LUT pack source INI digest mismatch')
    rows, previous, seen = [], None, set()
    for i in range(count):
        start = index_offset + i * record_size
        key_hash, key_bytes, reserved, offset, length, table_hash = PACK_RECORD.unpack_from(data, start)
        if reserved != 0 or length != VOLUME_BYTES or offset != tables_offset + i * VOLUME_BYTES:
            raise ValueError('LUT pack table index has an invalid reserved field or extent')
        if stable_hash(key_bytes) != key_hash:
            raise ValueError('LUT pack stableHash does not match its full BakeKey')
        ordering = (key_hash, key_bytes)
        if previous is not None and ordering <= previous:
            raise ValueError('LUT pack index is not sorted by hash and canonical key')
        previous = ordering
        if key_bytes in seen:
            raise ValueError('LUT pack index contains a duplicate full BakeKey')
        seen.add(key_bytes)
        if verify_tables:
            table = data[offset:offset + length]
            if hashlib.sha256(table).digest() != table_hash:
                raise ValueError(f'LUT pack table digest mismatch at index {i}')
        rows.append((key_hash, key_bytes, offset, length, table_hash))
    return {
        'format_version': format_version, 'math_schema_version': math_version,
        'cube_size': cube_size, 'voxel_format': 'RGBA16_UNORM_LE', 'entry_count': count,
        'packed_bytes': len(data), 'uncompressed_table_bytes': count * VOLUME_BYTES,
        'source_ini_sha256': source_hash.hex(), 'index_sha256': index_hash.hex(),
        'pack_sha256': hashlib.sha256(data).hexdigest(), 'entries': rows,
    }


def build_pack(input_path, *, compiler=None, source_ini=None):
    input_path = Path(input_path)
    source_ini = input_path.read_bytes() if source_ini is None else bytes(source_ini)
    producer = ROOT / 'src' / 'sky' / 'appearance_lut_pack.cpp'
    if not producer.is_file():
        raise FileNotFoundError(f'missing C++ LUT producer: {producer}')
    with tempfile.TemporaryDirectory(prefix='northlight-lut-pack-') as directory:
        work = Path(directory)
        executable = work / ('appearance_lut_pack.exe' if os.name == 'nt' else 'appearance_lut_pack')
        raw_path = work / 'Vibrant.raw'
        source_snapshot = work / 'Vibrant.ini'
        source_snapshot.write_bytes(source_ini)
        include_dirs = sorted({ROOT / 'src', *(path.parent for path in (ROOT / 'src').rglob('*.h'))})
        command = _compiler_command(compiler) + ['-std=c++17', '-O2']
        for include_dir in include_dirs:
            command.extend(('-I', str(include_dir)))
        command.extend((str(producer), '-o', str(executable)))
        compiled = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError('C++ LUT producer compile failed:\n' + compiled.stderr.strip())
        baked = subprocess.run([str(executable), str(source_snapshot.resolve()), str(raw_path)], cwd=ROOT,
                               capture_output=True, text=True)
        if baked.returncode:
            raise RuntimeError('C++ LUT bake failed:\n' + baked.stderr.strip())
        return make_pack(raw_path.read_bytes(), source_ini)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--input', type=Path, required=True, help='the exact Vibrant.ini to bake')
    parser.add_argument('--output', type=Path, required=True, help='output .nllutpack file')
    parser.add_argument('--compiler', help='C++17 compiler command; default: CXX, c++, clang++ or g++')
    args = parser.parse_args(argv)
    temp_path = None
    try:
        source_ini = args.input.read_bytes()
        packed = build_pack(args.input, compiler=args.compiler, source_ini=source_ini)
        report = validate_pack(packed, source_ini=source_ini, verify_tables=True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=args.output.parent, prefix='.' + args.output.name + '.',
                                         suffix='.tmp', delete=False) as temporary:
            temp_path = Path(temporary.name)
            temporary.write(packed)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temp_path, args.output)
        temp_path = None
        report.pop('entries')
        report['output'] = str(args.output)
        print(json.dumps(report, indent=2))
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        if temp_path is not None:
            temp_path.unlink(missing_ok=True)


if __name__ == '__main__':
    sys.exit(main())
