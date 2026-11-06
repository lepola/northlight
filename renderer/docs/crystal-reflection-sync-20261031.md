# Dun Niffelem crystal reflection shimmer

The reported Storm Peaks view is near map coordinates 62.4, 60.7. The local
`frostgianticeshard` assets have an opaque unlit base followed by two
SRCALPHA/INVSRCALPHA reflection passes over the same submesh. Displacing only
the base makes those authored layers intersect it as the camera moves.

The existing M2 overlay synchronization admitted only one SM3 lit base and one
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

## Current source identity after unchanged model writes

An unchanged tracked VB/IB rewrite can reuse displacement geometry whose
source snapshot retains an older write revision. Comparing an overlay against
that cached identity rejects the current reflection layer even though the base
drew successfully, leaving the layers on different surfaces.

Retain the current source identity before geometry canonicalization and record
it with each successful base draw, separately from the cached mesh. Overlay
admission still requires the exact source, declaration, draw range, clip matrix
and pose; a buffer write after the base continues to invalidate its overlay.

Fresh validation on this PR checkout passes 576 crystal cases and 576 portal
cases on each of native D3D9, DXVK 2.7.1 and DXVK 3.1.1. The fixture covers
unchanged buffers and alternating equal-content VB/IB writes, verified geometry
aliases, immediate draws and actual production spell-layer replay, constant
restoration, camera/pose changes and writes after the base. The preserved
pre-fix headers fail the same expanded crystal overlay-admission assertion.
The Windows x86 renderer DLL builds.

These hidden-device component tests use native model topology and vertex
programs with an authored camera and depth oracle. They do not replay the full
game scene or establish in-game screenshot acceptance. No new installer or
fresh game session is included in this publication.
