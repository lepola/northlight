#!/usr/bin/env python3
# northlight-test: requires=zig
"""Portable static-shadow transition controller; no game, GPU or asset dependency."""
import os
import sys
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix="northlight-shadow-transition-") as tmp:
    for label, flags in ([("O2", ["-O2"])] if os.name=="nt" else [("O2", ["-O2"]),
                         ("ASan+UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])]):
        exe = Path(tmp) / (label+".exe" if os.name=="nt" else label)
        compiler=([str(fp.zig()),"c++","-target","x86-windows-gnu"] if os.name=="nt" else ["clang++"])
        subprocess.run([*compiler, "-std=c++17", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I", str(ROOT / "src/world"), str(ROOT / "tests/test_shadow_transition.cpp"),
                        "-o", str(exe)], env=fp.zig_env(),check=True)
        print(label, subprocess.check_output([str(exe)], text=True), end="", flush=True)
