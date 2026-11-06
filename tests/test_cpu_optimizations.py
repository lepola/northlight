#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""CPU preparation: exhaustive packed decoders and storage/cache boundary oracles."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

for optimization in ["-O0", "-O2"]:
    exe = fp.output_dir() / ("cpu-optimizations" + optimization + ".exe")
    subprocess.run([str(fp.zig()), "c++", "-target", "x86-windows-gnu", "-std=c++17",
                    optimization, "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    *fp.test_include_flags(), str(fp.TESTS / "cpu_optimizations.cpp"),
                    "-o", str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
