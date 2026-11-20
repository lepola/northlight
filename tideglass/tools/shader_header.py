#!/usr/bin/env python3
"""Wrap compiled Direct3D 9 shader bytecode in a C++ header as a DWORD array."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def render(data: bytes, name: str, *, byte: bool = False, usage: dict | None = None) -> str:
    if len(data) % 4:
        raise ValueError('Shader bytecode is not whole DWORDs')
    words = data if byte else struct.unpack(f"<{len(data) // 4}I", data)
    width = 2 if byte else 8
    lines = [", ".join(f"0x{w:0{width}X}" for w in words[i:i + 8]) for i in range(0, len(words), 8)]
    storage = "BYTE" if byte else "DWORD"
    text = ("#pragma once\n\n#include <windows.h>\n\n"
            f"const {storage} {name}[] = {{\n    " + ",\n    ".join(lines) + "\n};\n")
    if usage is not None:
        if byte or not name.startswith('g_'):
            raise ValueError('Water shader descriptors require named DWORD storage')
        version = 'D3DPS_VERSION(3, 0)' if usage['target'] == 'ps_3_0' else 'D3DVS_VERSION(3, 0)'
        samplers = max(usage['samplers'], default=-1) + 1
        text += ('\n#include "shader_program.h"\n'
                 f'const ShaderProgram p_{name[2:]} = {{{name}, "{name[2:]}", '
                 f'{{{version}, {usage["temporary_registers"]}, {samplers}, {usage["instruction_slots"]}}}}};\n')
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bytecode", type=Path)
    parser.add_argument("name")
    parser.add_argument("output", type=Path)
    parser.add_argument("--byte", action="store_true", help="emit FXC-compatible BYTE storage")
    args = parser.parse_args()

    data = args.bytecode.read_bytes()
    args.output.write_bytes(render(data, args.name, byte=args.byte).encode())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
