#!/usr/bin/env python3
"""Certify compiled Water shaders from actual reads, resources and pinned tool bytes.

prepare runs before compilation; reuse fills in products from the shader cache whose recorded
compiler, driver, entry and actual include reads all still match that snapshot, and lists the
shaders left to compile; publish checks the actual include reads against the snapshot, writes
headers/manifest and stores what it certified in the cache. validate rejects stale or modified products.
Instruction slots are the compiler's static estimate, not GPU timing or loop iterations.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path
import shader_header

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = 2
CACHE = ROOT / 'build/shader-cache'
CACHE_VERSIONS = 3  # per shader: enough to switch between a few branches or settings without recompiling
PRODUCTS = ('.bin', '.asm', '.inputs')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_bytes((json.dumps(value, indent=2, sort_keys=True) + '\n').encode())


def write_if_changed(path, data):
    # Unchanged generated headers keep their timestamps, so Ninja recompiles only what they changed.
    path = Path(path)
    if not path.is_file() or path.read_bytes() != data:
        path.write_bytes(data)


def local_path(value):
    if value[:3].lower() == 'z:\\':
        value = value[2:].replace('\\', '/')
    return Path(value).resolve()


def resources(assembly, target):
    if target not in ('ps_3_0', 'vs_3_0'):
        raise ValueError('Unsupported Water shader target: ' + target)
    if not re.search(r'^\s*' + target + r'\s*$', assembly, re.M):
        raise ValueError('Disassembly target mismatch')
    match = re.search(r'// approximately (\d+) instruction slots? used', assembly)
    if not match:
        raise ValueError('Compiler instruction-slot report missing')
    code = '\n'.join(line.split('//', 1)[0] for line in assembly.splitlines())
    temporaries = max(map(int, re.findall(r'\br(\d+)\b', code)), default=-1) + 1
    samplers = sorted(set(map(int, re.findall(r'\bs(\d+)\b', code))))
    slots = int(match[1])
    sampler_limit = 16 if target.startswith('ps') else 4
    if temporaries > 32 or any(n >= sampler_limit for n in samplers) or slots > 32768:
        raise ValueError('Shader exceeds SM3 resource limits')
    instructions = sum(bool(line.strip()) and not re.match(r'\s*(?:[pv]s_|dcl|def|label)', line)
                       for line in code.splitlines())
    return {'target': target, 'temporary_registers': temporaries, 'samplers': samplers,
            'instruction_count': instructions, 'instruction_slots': slots,
            'portable_512_slots': slots <= 512,
            'headroom': {'temporary_registers': 32 - temporaries,
                         'samplers': sampler_limit - len(samplers)},
            'pressure': temporaries >= 29 or len(samplers) >= sampler_limit - 1}


def check_water_bindings(name, assembly, dependencies):
    # The two surface programs have different optimized-out inputs. Validate the
    # named parameters actually present, including the last row of each array.
    if name not in ('ps_water', 'ps_water_fine', 'ps_water_moments', 'ps_water_moments_fine', 'ps_surface_detail_moments'):
        return
    contracts = [Path(p) for p in dependencies if Path(p).name == 'water_registers.hlsli']
    if len(contracts) != 1:
        raise ValueError(name + ': missing or ambiguous water binding declarations')
    contract = contracts[0]
    slots = {key: int(value) for key, value in re.findall(
        r'^#define WATER_SLOT_(\w+) (\d+)$', contract.read_text(), re.M)}
    bound = re.findall(r'^//\s+(\w+)\s+([cs])(\d+)\s+(\d+)\s*$', assembly, re.M)
    required = {'cWaveMomentSamples'} if name == 'ps_surface_detail_moments' else {'cGameFog', 'cSurfaceVisibility'}
    if not required.issubset({key for key, _, _, _ in bound}):
        raise ValueError(name + ': missing compiled water controls')
    for key, kind, offset, count in bound:
        if slots.get(key) != int(offset) or key[0] != kind:
            raise ValueError(name + ': unexpected binding for ' + key)
        limit = slots['cSurfaceVisibility'] + 1 if kind == 'c' else 16
        if int(offset) + int(count) > limit:
            raise ValueError(name + ': capture cannot hold ' + key)


def validate_bytecode(data, target):
    version = 0xffff0300 if target == 'ps_3_0' else 0xfffe0300
    if len(data) < 8 or len(data) % 4 or struct.unpack_from('<I', data)[0] != version or data[-4:] != b'\xff\xff\0\0':
        raise ValueError('Invalid shader bytecode markers: ' + target)


def prepare(output, lists, compiler, root=ROOT):
    entries = {}
    for path in sorted(lists.glob('*.list')):
        fields = path.read_text().strip().split('\t')
        name, source, target = fields[:3]
        if name != path.stem or not re.fullmatch(r'[a-zA-Z0-9_]+', name):
            raise ValueError('Invalid shader name')
        entries[name] = {'source': str(local_path(source)), 'target': target,
                         'definitions': fields[3] if len(fields) > 3 else ''}
    if not entries:
        raise ValueError('No shader entries')
    candidates = {str(p.resolve()): digest(p) for folder in ('shaders', 'tests')
                  for p in (root / folder).rglob('*') if p.suffix in ('.hlsl', '.hlsli')}
    compiler = compiler.resolve()
    candidates[str(compiler)] = digest(compiler)
    for entry in entries.values():
        if entry['source'] not in candidates:
            raise ValueError('Source outside snapshotted shader inputs: ' + entry['source'])
    record = {'version': SCHEMA, 'entries': entries, 'candidates': candidates,
              'compiler': {'path': str(compiler), 'sha256': digest(compiler),
                           'flags': ['D3DCOMPILE_OPTIMIZATION_LEVEL3'], 'entry': 'main'},
              'driver_sha256': digest(output / 'compile_shaders.exe')}
    write_json(output / 'shader-inputs.json', record)
    return record


def cache_key(record, entry):
    identity = {'compiler': record['compiler']['sha256'], 'driver': record['driver_sha256'],
                'entry': {k: entry[k] for k in ('source', 'target', 'definitions')},
                'dependencies': entry['dependencies']}
    return hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:16]


def store(cache, name, key, record, generated):
    folder = cache / name / key
    if not (folder / 'record.json').is_file():
        folder.mkdir(parents=True, exist_ok=True)
        for suffix in PRODUCTS:
            shutil.copyfile(generated / (name + suffix), folder / (name + suffix))
        write_json(folder / 'record.json', record)  # last: a folder without it is incomplete
    (folder / 'record.json').touch()
    versions = sorted((p for p in (cache / name).iterdir() if (p / 'record.json').is_file()),
                      key=lambda p: (p / 'record.json').stat().st_mtime, reverse=True)
    for old in versions[CACHE_VERSIONS:]:
        shutil.rmtree(old)


def reuse(output, cache=CACHE):
    """Copy cached products whose every recorded read still matches the snapshot; return the shaders left to compile."""
    before = json.loads((output / 'shader-inputs.json').read_bytes())
    generated = output / 'generated'
    pending = []
    for name, entry in before['entries'].items():
        found = None
        for folder in (cache / name).iterdir() if (cache / name).is_dir() else ():
            path = folder / 'record.json'
            if not path.is_file():
                continue
            if not all((folder / (name + suffix)).is_file() for suffix in PRODUCTS):
                # Incomplete (a product removed by a storage closeout): drop it so a fresh compile can take its place.
                shutil.rmtree(folder)
                continue
            saved = json.loads(path.read_bytes())
            if saved['entry'] != entry or any(before['candidates'].get(p) != h for p, h in saved['dependencies'].items()):
                continue
            if folder.name == cache_key(before, {**entry, 'dependencies': saved['dependencies']}):
                found = folder
                break
        if found is None:
            pending.append(name)
            continue
        for suffix in PRODUCTS:
            write_if_changed(generated / (name + suffix), (found / (name + suffix)).read_bytes())
    return pending


def publish(output, cache=CACHE):
    before = json.loads((output / 'shader-inputs.json').read_bytes())
    if digest(output / 'compile_shaders.exe') != before['driver_sha256']:
        raise ValueError('Shader compiler driver changed during compilation')
    compiler = before['compiler']
    if digest(compiler['path']) != compiler['sha256']:
        raise ValueError('Shader compiler changed during compilation')
    generated = output / 'generated'
    result = {'version': SCHEMA, 'compiler': compiler, 'driver_sha256': before['driver_sha256'], 'shaders': {}}
    for name, entry in before['entries'].items():
        dependencies = {}
        for line in (generated / (name + '.inputs')).read_text().splitlines():
            path, actual = line.rsplit('\t', 1)
            path = str(local_path(path))
            if actual != before['candidates'].get(path) or digest(path) != actual:
                raise ValueError(name + ': source/compiler changed during compilation: ' + path)
            dependencies[path] = actual
        if entry['source'] not in dependencies or compiler['path'] not in dependencies:
            raise ValueError(name + ': missing actual source/compiler reads')
        binary = generated / (name + '.bin')
        data = binary.read_bytes()
        validate_bytecode(data, entry['target'])
        assembly = generated / (name + '.asm')
        disassembly = assembly.read_text()
        check_water_bindings(name, disassembly, dependencies)
        usage = resources(disassembly, entry['target'])
        header = generated / (name + '.h')
        write_if_changed(header, shader_header.render(data, 'g_' + name, usage=usage).encode())
        result['shaders'][name] = {**entry, 'dependencies': dependencies, 'resources': usage,
                                  'products': {p.name: digest(p) for p in (binary, assembly, header)}}
    write_json(output / 'shader-build.json', result)
    for name, entry in result['shaders'].items():
        store(cache, name, cache_key(before, entry), {'entry': before['entries'][name], 'dependencies': entry['dependencies']},
              generated)
    # A successful rebuild retires products of removed entries too. Otherwise a
    # stale generated header can hide a missing recipe entry on the next build.
    for path in generated.iterdir():
        if (path.name.startswith(('ps_', 'vs_')) and path.suffix in ('.bin', '.asm', '.inputs', '.h')
                and path.stem not in result['shaders']):
            path.unlink()
    return result


def validate(output):
    record = json.loads((output / 'shader-build.json').read_bytes())
    if record.get('version') not in (1, SCHEMA):
        raise ValueError('Unsupported shader manifest')
    compiler = record['compiler']
    if digest(compiler['path']) != compiler['sha256'] or digest(output / 'compile_shaders.exe') != record['driver_sha256']:
        raise ValueError('Shader compiler identity changed')
    generated = output / 'generated'
    for name, entry in record['shaders'].items():
        for path, expected in entry['dependencies'].items():
            if digest(path) != expected:
                raise ValueError(name + ': stale dependency: ' + path)
        for path, expected in entry['products'].items():
            if Path(path).name != path or digest(generated / path) != expected:
                raise ValueError(name + ': modified generated product: ' + path)
        data = (generated / (name + '.bin')).read_bytes()
        validate_bytecode(data, entry['target'])
        disassembly = (generated / (name + '.asm')).read_text()
        if record['version'] >= 2:
            check_water_bindings(name, disassembly, entry['dependencies'])
        usage = resources(disassembly, entry['target'])
        if usage != entry['resources'] or (generated / (name + '.h')).read_bytes() != shader_header.render(data, 'g_' + name, usage=usage).encode():
            raise ValueError(name + ': resource/header mismatch')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'reuse', 'publish', 'validate'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--lists', type=Path)
    parser.add_argument('--compiler', type=Path)
    args = parser.parse_args()
    if args.action == 'prepare':
        if args.lists is None or args.compiler is None:
            parser.error('prepare requires --lists and --compiler')
        prepare(args.output, args.lists, args.compiler)
    elif args.action == 'reuse':
        for name in reuse(args.output):
            print(name)
    else:
        record = publish(args.output) if args.action == 'publish' else validate(args.output)
        entries = record['shaders']
        pressure = [name for name, entry in entries.items() if entry['resources']['pressure']]
        print(f"{args.action}: {len(entries)} shaders verified; {len(pressure)} near register/sampler limits")
        print('Resource report: ' + str(args.output / 'shader-build.json'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
