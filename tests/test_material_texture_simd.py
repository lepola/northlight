#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compare bounded BGRA box reduction against an independent integer oracle."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix="material-texture-simd-") as tmp:
    root = Path(tmp)
    for label, flags, include in (("scalar", ["-O2", "-mno-sse2"], root), ("current", ["-O2"], root), ("debug", ["-O0"], root)):
        exe = root / (label + ".exe")
        command = [str(fp.zig()), "c++", "-target", "x86-windows-gnu", *flags, "-UNDEBUG", "-std=c++17", "-I", str(include), *fp.test_include_flags(), str(fp.TESTS / "material_texture_simd.cpp"), "-o", str(exe)]
        subprocess.run(command, check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        result = subprocess.run([str(exe)], capture_output=True, text=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        assert "MATERIAL_TEXTURE_SIMD_PASS cases=40" in result.stdout
        print(label, result.stdout.strip())
