"""Portable shader provenance validation, shared by all generators and host builds."""
import hashlib
import json
from pathlib import Path
import re
import struct

# Checksum-pinned Microsoft profiles used by this repository and its integration.
TRUSTED_COMPILERS = {
    '3c5f066a121a446e55db9c699b392da5d3aca6ad9019437b722360d09698e3c3',
    '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3',
    'e407b9ffadef47e87994dc488214f75405cf04ee99589f2315fc7ab99fd1dfe2',
}
TOOLS = ('scripts/shaders/compile_shaders.cpp', 'scripts/shaders/shader_inputs.h',
         'scripts/shaders/shader_compiler.py', 'scripts/shaders/shader_manifest.py')
MANIFESTS = ('shader-build.json', 'world-shader-build.json', 'celestial-disc-shader-build.json',
             'local-light-shader-build.json', 'static-shadow-shader-build.json')


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def safe_path(root, name):
    path = Path(name)
    if path.is_absolute() or '..' in path.parts or not (root / path).resolve().is_relative_to(root.resolve()):
        raise ValueError('Unsafe shader input path: ' + name)
    return root / path


def snapshot(root, generator):
    paths = [p for p in (root / 'shaders').rglob('*') if p.suffix in ('.hlsl', '.hlsli')]
    paths += [root / name for name in TOOLS]
    if generator.is_relative_to(root):
        paths.append(generator)
    return {p.relative_to(root).as_posix(): sha256(p) for p in paths}


def actual_inputs(root, readset, before, source):
    inputs = {}
    for line in readset.read_text().splitlines():
        name, digest = line.rsplit('\t', 1)
        if name[:3].lower() == 'z:\\':
            name = name[2:].replace('\\', '/')
        path = Path(name).resolve()
        if not path.is_relative_to(root.resolve()):
            raise ValueError('Shader include outside repository: ' + name)
        relative = path.relative_to(root).as_posix()
        if digest != before.get(relative) or sha256(path) != digest:
            raise ValueError('Shader input changed during compilation: ' + relative)
        inputs[relative] = digest
    if source.resolve().relative_to(root).as_posix() not in inputs:
        raise ValueError('Main shader input missing from actual read set')
    return inputs


def validate_entry(root, name, entry):
    if entry.get('compiler_sha256') not in TRUSTED_COMPILERS:
        raise ValueError(name + ': unrecognised compiler identity')
    if not entry.get('dependencies') or not entry.get('compiler_inputs') or not entry.get('compiler_driver_sha256'):
        raise ValueError(name + ': incomplete shader provenance; rebuild this family')
    for key in ('dependencies', 'compiler_inputs'):
        for relative, expected in entry[key].items():
            if sha256(safe_path(root, relative)) != expected:
                raise ValueError(name + ': stale ' + relative)
    if entry.get('source') not in entry['dependencies']:
        raise ValueError(name + ': missing source dependency')
    binary = root / 'shaders/compiled' / (name + '.bin')
    if not re.fullmatch(r'\w+', name) or sha256(binary) != entry['sha256'] or sha256(Path(str(binary) + '.asm')) != entry.get('assembly_sha256'):
        raise ValueError(name + ': changed compiled product')
    data = binary.read_bytes();target = entry['target']
    if target not in ('ps_2_0', 'ps_3_0', 'vs_3_0'):
        raise ValueError(name + ': invalid target')
    version = (0xffff0000 if target.startswith('ps') else 0xfffe0000) | int(target[3]) << 8
    if len(data) < 8 or len(data) % 4 or struct.unpack_from('<I', data)[0] != version or data[-4:] != b'\xff\xff\0\0':
        raise ValueError(name + ': malformed bytecode')
    resources = validate_assembly(Path(str(binary)+'.asm'), target,
                                  96 if target == 'ps_2_0' else 512,
                                  12 if target == 'ps_2_0' else 32)
    if any(entry.get(key) != value for key,value in resources.items()):
        raise ValueError(name + ': resource report differs from disassembly')
    return data


def validate_headers(root, manifest, headers):
    for name, entry in manifest['shaders'].items():
        data = validate_entry(root, name, entry)
        symbol = entry['symbol']
        pattern = rb'\b' + re.escape(symbol.encode()) + rb'\s*\[\s*\]\s*=\s*\{([^}]+)\}'
        bodies = [m[1] for contents in headers.values() for m in re.finditer(pattern, contents)]
        if len(bodies) != 1:
            raise ValueError(name + ': generated symbol missing or duplicated')
        values = re.findall(rb'0[xX]([0-9a-fA-F]+)', bodies[0])
        if re.sub(rb'0[xX][0-9a-fA-F]+|[\s,]', b'', bodies[0]):
            raise ValueError(name + ': malformed generated array')
        packed = struct.pack('<' + 'I' * len(values), *(int(v,16) for v in values))
        if packed != data:
            raise ValueError(name + ': embedded header differs from bytecode')


def publish(root, manifest_path, manifest, headers):
    validate_headers(root, manifest, headers)
    manifest['provenance_version'] = 1
    manifest['headers'] = {name: hashlib.sha256(data).hexdigest() for name,data in headers.items()}
    for name, data in headers.items():
        safe_path(root, name).write_bytes(data)
    # Publish the complete manifest last. Interrupted publication fails validation.
    manifest_path.write_bytes((json.dumps(manifest, indent=2) + '\n').encode())


def validate_manifest(root, manifest):
    if manifest.get('provenance_version') != 1 or not manifest.get('headers'):
        raise ValueError('Incomplete shader provenance; rebuild all shader families')
    headers = {}
    for name, expected in manifest['headers'].items():
        path = safe_path(root, name)
        if sha256(path) != expected:
            raise ValueError('Changed generated shader header: ' + name)
        headers[name] = path.read_bytes()
    validate_headers(root, manifest, headers)


def validate_all(root):
    result = {}
    for name in MANIFESTS:
        path = root / 'shaders' / name
        manifest = json.loads(path.read_bytes())
        validate_manifest(root, manifest)
        result[name] = sha256(path)
    return result


def instructions(path):
    """D3DDisassemble text without comments, blank lines and the /* n */ or n: instruction numbers."""
    lines = (re.sub(r"^(?:/\*.*?\*/|\d+:)\s*", "", line.split("//", 1)[0].strip()) for line in Path(path).read_text().rstrip("\0").splitlines())
    return [line for line in lines if line]


def validate_assembly(path, target, slot_limit=512, register_limit=32):
    """Baseline SM3 (or ps_2_0 with smaller limits) instruction slots and register limits.

    Cost references: Microsoft dx9-graphics-reference-asm-{ps,vs}-instructions-
    {ps,vs}-3-0, including the cube sampling surcharge. A break_ne/lt/gt with
    identical operands can never exit its loop.
    """
    assembly = path.read_text().rstrip("\0")
    costs = dict.fromkeys("abs add break cmp dp3 dp4 else endif exp expp frc log logp mad max min mov mova mul nop rcp ret rsq setp sge slt sub texld".split(), 1)
    costs.update(dict.fromkeys("call crs dp2add dsx dsy endloop endrep lrp m3x2 texkill texldl".split(), 2))
    costs.update(dict.fromkeys("breakp callnz if ifc loop m3x3 m4x3 nrm pow rep texldd texldp lit sgn".split(), 3))
    costs.update(m3x4=4, m4x4=4, texldb=6, sincos=8)
    slots = 0; counts = {}
    for line in instructions(path):
        tokens = line.split()
        if tokens[0] in {"break_ne", "break_lt", "break_gt"}:
            operands = [s.strip() for s in line[len(tokens[0]):].split(",")]
            if len(operands) == 2 and operands[0] == operands[1]:
                raise ValueError(f"{path.name}: non-exiting loop branch: {line}")
        op = tokens[0].split("_")[0]
        if op in {"ps", "vs", "def", "defi", "defb", "dcl", "label"}: continue
        if op not in costs: raise ValueError(f"{path.name}: unknown opcode {tokens[0]}")
        slots += 3 if op == "break" and "_" in tokens[0] else costs[op]
        counts[op] = counts.get(op, 0) + 1
    cube = {int(n) for n in re.findall(r"dcl_cube\s+s(\d+)", assembly)}
    cube_extra = 0
    for line in instructions(path):
        sample = re.match(r"(texldl|texldp|texld)\s+.*\bs(\d+)\b", line)
        if sample and int(sample[2]) in cube:
            cube_extra += 1 if sample[1] == "texldp" else 3
    slots += cube_extra
    if slots > slot_limit: raise ValueError(f"{path.name}: {slots} static slots exceeds {target} {slot_limit}")
    temps = [int(n) for n in re.findall(r"\br(\d+)\b", assembly)]
    if max(temps, default=-1) >= register_limit: raise ValueError(f"{path.name}: temporary register limit")
    samplers = sorted({int(n) for n in re.findall(r"\bs(\d+)\b", assembly)})
    if samplers and max(samplers) >= (4 if target.startswith("vs") else 16):
        raise ValueError(f"{path.name}: sampler register limit")
    return {"static_instruction_slots": slots, "temporary_registers": max(temps, default=-1) + 1,
            "samplers": samplers, "instructions": counts, "instruction_count": sum(counts.values()), "cube_instruction_extra_slots": cube_extra}
