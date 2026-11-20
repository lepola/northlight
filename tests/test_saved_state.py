#!/usr/bin/env python3
# northlight-test: requires=cxx
"""State restoration, pooled resource lifetime and injected API failures; no GPU."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix="northlight-saved-state-") as directory:
    for name, flags in (("optimized", ["-O2"]),
                        ("sanitized", ["-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"])):
        exe = Path(directory) / name
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                        *flags, *fp.test_include_flags(), str(fp.TESTS / "test_saved_state.cpp"),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
