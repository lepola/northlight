#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Old-working-NDC shadow reprojection using production frame math; CPU only."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="northlight-shadow-transition-math-") as tmp:
    for label, flags in (("O2", ["-O2"]),
                         ("ASan+UBSan", ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        exe = Path(tmp) / label
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I", str(ROOT / "src/world"), "-I", str(ROOT / "src/gi"),
                        str(ROOT / "tests/test_shadow_transition_math.cpp"), str(ROOT / "src/gi/world_gi.cpp"),
                        "-o", str(exe)], check=True)
        print(label, subprocess.check_output([str(exe)], text=True), end="", flush=True)
