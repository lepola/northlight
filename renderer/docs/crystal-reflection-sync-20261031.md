# Dun Niffelem crystal reflection shimmer

The reported Storm Peaks view is near map coordinates 62.4, 60.7. The local
`frostgianticeshard` assets have an opaque unlit base followed by two
SRCALPHA/INVSRCALPHA reflection passes over the same submesh. Displacing only
the base makes those authored layers intersect it as the camera moves.

The combined implementation previously admitted only one SM3 lit base and one
unlit additive overlay. It now also admits the exact audited SM2/SM3 single-bone
lit/unlit `diffuse_t1` and `diffuse_env` programs and alpha blending. Reflection
UVs still come from the original shader; the displaced mesh, height texture and
strength come from the successful base draw. Environment UV generation need
not qualify for independent height inference. Source identity, declaration,
draw range, clip matrix, used bone transforms and frame lifetime must match.

`tests/test_crystal_overlay_gpu.py --backend <plain-D3D9-or-DXVK-DLL>` extracts
the original shard04 topology, texture and shader variants from local MPQs.
Its hidden-device oracle checks 12 camera rotations, SM2 CPU height and SM3
CPU/GPU height, lit/unlit shaders, alpha/additive blending, exact overlay depth,
constant restoration and refusal after a pose change. Each of native D3D9,
DXVK 2.7.1 and DXVK 3.1.1 passes 144 cases. Unsynchronized native geometry
fails the depth comparison in all 144 cases on each backend. Compiling the same
test against the saved pre-change production headers fails overlay admission.

Geometry, displacement state reuse, private capture depth and material batch
binding regressions pass. The x86 DLL also builds from isolated commit
`66d0e6ed` plus this fix. Isolation avoids unrelated edits occurring in the
shared working directory during validation.

The fixture scales the authentic mesh into an authored camera setup and uses a
simple UV pixel shader for the depth oracle. It does not replay the complete
game frame or certify the screenshots' final appearance. No game was launched;
installed DLLs, INIs and MPQs were not changed. Extracted assets remain ignored
local test output.

## Independent material PR port

The standalone material branch at `0d1b15a8` did not yet contain base/overlay
retention. This port also brings the bounded per-frame mesh references,
shader identity retention and deferred-draw binding from the combined path.
Temporarily bound geometry bypasses the original game-state mirror while it
is recorded; ordinary draws retain the existing mirror path.

The independent oracle additionally records and replays the actual displaced
overlay through the production spell layer. All 144 immediate/deferred cases
pass on each of native D3D9, DXVK 2 and DXVK 3, with native constants restored
and the original mirror never entered during the temporary binding. CPU
geometry, snapshot and atlas-upload checks pass.

`test_displacement_state_reuse.py` still fails the existing range-admission
assertion at `displacement_state_reuse.cpp:546`. Recompiling against the unchanged
`0d1b15a8` production headers reproduces the identical assertion. This check is
not counted as passing for the standalone port.
