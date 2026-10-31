#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact guarded mirror-epoch capture and untrusted reader controls."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix="material-epoch-guard-") as tmp:
    root = Path(tmp)
    fixture = (fp.TESTS / "displacement_state_reuse.cpp").read_text().split("int main(){")[0]
    fixture = fixture.replace("struct DisplacementStateTestAccess {", "struct DisplacementStateTestAccess {\n static void guardedChecks();")
    (root / "epoch_guard_fixture.h").write_text(fixture)
    exe = root / "guard.exe"
    command = [str(fp.zig()), "c++", "-target", "x86-windows-gnu", "-O2", "-UNDEBUG", "-std=c++17", "-Wno-microsoft-exception-spec", "-I", str(root), *fp.test_include_flags(), str(fp.TESTS / "material_epoch_guard.cpp"), "-luser32", "-luuid", "-o", str(exe)]
    subprocess.run(command, check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
    result = subprocess.run([str(exe)], capture_output=True, text=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    assert "MATERIAL_EPOCH_GUARD_PASS" in result.stdout
    print(result.stdout.strip())
