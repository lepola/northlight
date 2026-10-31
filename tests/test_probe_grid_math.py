#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact production-kernel comparison against an independent reference; no timings."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix="northlight-kernel-") as tmp:
    for mode,flags in (("O2",["-O2"]),("scalar",["-O2","-U__SSE2__"]),("sanitized",["-O1","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all"])):
        exe=Path(tmp)/mode
        subprocess.run(["clang++","-std=c++17","-UNDEBUG","-ffp-contract=off","-frounding-math","-Wall","-Wextra","-Werror",*flags,*fp.test_include_flags(),str(Path(__file__).with_suffix(".cpp")),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
