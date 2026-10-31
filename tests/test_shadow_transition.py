#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Portable static-shadow transition controller; no game, GPU or asset dependency."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="northlight-shadow-transition-") as tmp:
    for label, flags in (("O2", ["-O2"]),
                         ("ASan+UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        exe = Path(tmp) / label
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I", str(ROOT / "src/world"), str(ROOT / "tests/test_shadow_transition.cpp"),
                        "-o", str(exe)], check=True)
        print(label, subprocess.check_output([str(exe)], text=True), end="", flush=True)
