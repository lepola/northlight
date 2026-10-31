#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Check private material depth comparison and state restoration on a CPU mock device."""
import argparse, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path)
args = parser.parse_args()
out = args.output or fp.output_dir() / "material-private-depth"
out.mkdir(parents=True, exist_ok=True)
reference = (fp.TESTS / "displacement_state_reuse.cpp").read_text().split("int main(){")[0]
reference = reference.replace("struct MaterialCaptureStateTestAccess {", "struct MaterialCaptureStateTestAccess {\n static void replayBindings();static void replayConstantRanges();static void privateDepth();")
(out / "material_bindings_reference.h").write_bytes(reference.encode())
for mode, flag in (("O2", "-O2"), ("O0", "-O0")):
    exe = out / (mode + ".exe")
    subprocess.run([str(fp.zig()), "c++", "-target", "x86-windows-gnu", "-std=c++17", "-UNDEBUG", "-Wno-microsoft-exception-spec", "-Wno-return-type", flag, "-I", str(out), *fp.test_include_flags(), str(fp.TESTS / "test_material_private_depth.cpp"), "-luser32", "-luuid", "-o", str(exe)], env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="")
    result.check_returncode()
    assert "PRIVATE_DEPTH_PASS" in result.stdout
