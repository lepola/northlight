#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Production mirror and deferred capture batch-state regression, CPU only."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

with tempfile.TemporaryDirectory(prefix="material-raster-batch-") as tmp:
    root = Path(tmp)
    client_fixtures.actor_client_programs(root)
    fixture = (fp.TESTS / "displacement_state_reuse.cpp").read_text().split("int main(")[0]
    fixture = fixture.replace("struct MaterialCaptureStateTestAccess {", "struct MaterialCaptureStateTestAccess {\n static void cpuReuse();\n static void batchCapture();\n static void admissionBatch();\n static void replayBindings();\n static void replayConstantRanges();")
    fixture = fixture.replace("struct DisplacementStateTestAccess {", "struct DisplacementStateTestAccess {\n static void cpuReuse();")
    fixture = fixture.replace("static int failAllocationAfter=-1;", "static size_t allocationCount=0;static int failAllocationAfter=-1;")
    fixture = fixture.replace("void* operator new(std::size_t n){", "void* operator new(std::size_t n){++allocationCount;")
    (root / "material_bindings_reference.h").write_text(fixture)
    binding = (fp.TESTS / "test_material_batch_bindings.cpp").read_text().rsplit("int main(", 1)[0]
    (root / "material_packet_bindings.h").write_text(binding)
    packet = (fp.TESTS / "test_material_packet_auxiliary.cpp").read_text().split("void MaterialCaptureStateTestAccess::packetAuxiliary()")[0]
    (root / "packet_batch_fixture.h").write_text(packet)
    for name, expected in (("material_mirror_batch", "MATERIAL_MIRROR_BATCH_PASS"), ("material_sampler_batch", "MATERIAL_SAMPLER_BATCH_PASS"), ("material_batch_capture", "MATERIAL_BATCH_CAPTURE_PASS"), ("material_admission_batch", "MATERIAL_ADMISSION_BATCH_PASS"), ("material_cpu_reuse", "MATERIAL_CPU_REUSE_PASS")):
        exe = root / (name + ".exe")
        command = [str(fp.zig()), "c++", "-target", "x86-windows-gnu", "-O2", "-UNDEBUG", "-std=c++17", "-Wno-microsoft-exception-spec", "-I", str(root), *fp.test_include_flags(), str(fp.TESTS / (name + ".cpp")), "-luser32", "-luuid", "-o", str(exe)]
        subprocess.run(command, check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        result = subprocess.run([str(exe)], capture_output=True, text=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        assert expected in result.stdout, result.stdout
        print(result.stdout.strip())
