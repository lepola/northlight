#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compare terrain depth specialization with the exact interval interpreter."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix="material-terrain-bounds-") as tmp:
    root = Path(tmp)
    fixture = (fp.TESTS / "displacement_state_reuse.cpp").read_text().split("struct Backend:ForwardIDirect3DDevice9")[0]
    fixture = fixture.replace("struct DisplacementStateTestAccess {", "struct DisplacementStateTestAccess {\n static bool terrainCall(const NorthlightActorDeformation::Program&,const float*,const float*,const float*,float&,bool);\n static bool terrainMatch(const NorthlightActorDeformation::Program&);")
    for name in ("auto_bump_renderer.h", "device_mirror.h", "mirror_resource_forwarders.h"):
        fixture = fixture.replace('#include "' + name + '"\n', '')
    (root / "terrain_unique.h").write_text(fixture)
    for opt in ("-O0", "-O2"):
        exe = root / ("terrain" + opt[-1] + ".exe")
        command = [str(fp.zig()), "c++", "-target", "x86-windows-gnu", opt, "-UNDEBUG", "-std=c++17", "-Wno-microsoft-exception-spec", "-Wno-return-type", "-I", str(root), *fp.test_include_flags(), str(fp.TESTS / "material_terrain_bounds.cpp"), "-luser32", "-luuid", "-o", str(exe)]
        subprocess.run(command, check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        result = subprocess.run([str(exe)], capture_output=True, text=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        assert "MATERIAL_TERRAIN_BOUNDS_PASS" in result.stdout
        print(opt, result.stdout.strip())
