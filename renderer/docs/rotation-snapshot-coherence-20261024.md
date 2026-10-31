# Camera rotation: snapshot coherence and faithful offline inputs

The runtime evidence is still the run ending at 03:28:59 on 2026-10-24,
SHA-256 `b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It predates these changes. No whole-frame FPS improvement or elimination of
rotation stalls is established by the checks below. The game, wow_optimize,
personal settings and installed binaries were not modified or launched.

## Production defect fixed

The snapshot reader treats immutable client code as live signature bytes and
records mutable client memory for the command-stream worker. Its PE parser
previously accepted writable executable sections. In the actual local client
file, RWX `.zdata` extended the contiguous bypass from the end of `.text`
(`0x9de3b3`) to `0xdd2000`. This accidentally classified camera, map, sky and
weather globals as code. Those globals could therefore come from the live
producer frame rather than the queued draw's snapshot.

Writable sections no longer extend the signature bypass. The entire requested
read must fit inside the range, and section-end arithmetic is checked. Existing
live signature checks remain live. Startup diagnostics report the actual
in-process range and whether four representative runtime globals fall inside;
this is needed because the raw file alone cannot prove its loaded headers.

The focused regression uses the actual executable's PE section metadata as
data, captures runtime values, overwrites the live values and verifies frozen
playback. It includes the complete sky-reader sequence, failed reads, repeated
keys, reads crossing the code boundary and live successful/failed signatures.
O2, O0 and ASan/UBSan pass. Reinstating the exact old section predicate makes
the same fixture fail with `SNAPSHOT_WRITABLE_GLOBAL_MISMATCH`. Neither a game
launch nor an authored replacement PE is used for this proof.

This establishes a coherence bug, not its contribution to the observed
249.425 ms producer gap. Old sidecars that omitted these globals are incomplete;
initial PE data or values from another run cannot repair them faithfully.

## Worker replay inputs

The dedicated stand now pins immutable PE bytes and validity/counters in a
shared context. The producer's imported packet cursor stays separate from the
queue-owned snapshot. Recording consumes the exact admitted trigger/draw;
playback reads its queued copy, even if the producer has already imported the
next packet. The worker installs its context before startup callbacks and
releases it after playback TLS teardown. Missing runtime input, wrong admission
or worker startup failure leaves the stand invalid rather than silently using
host memory or an unprofiled fallback.

Five focused cases execute the actual StreamDevice/Replayer with a fake D3D
target: ordered updates, retired producer ownership, initialization failure,
missing admission and a throwing startup callback. O2 and ASan/UBSan pass.
The actual WorldContext signature/read route also passes O2/O0 on Windows.
These are CPU correctness and lifetime checks; fake D3D timings are not FPS.
The C export status remains version 1 with exactly eight 64-bit words.

## Bounded normal-view lighting capture

Ctrl+Shift+Alt+F12 requests one diagnostic capture in the normal world view;
Ctrl+Shift+F12 retains debug-view cycling. Capture is explicit, capped at four
attempts and writes into an exclusively created session directory. It does not
force shadow, GI, temporal-history or slicing settings. Attempted capture frames
are tagged by renderer/world frame and excluded from FRAME interval sampling.
Other GPU timing aggregates still require filtering using those tags.

The capture exports the actual full-size sampled depth, ordered per-pass
constant banks, shader bytecode, source slots, bound textures, viewport,
samplers, render/blend/stencil state and target descriptors. Immutable input
groups may share a file only after checking their actual binding. Mutable
accumulation/color inputs get fresh snapshots. Readback failure produces an
incomplete journal, never a reconstructed substitute.

Depth is point-copied to a private R32F target at exact pixel centers. All RTs,
depth binding, viewport, state block and VS/PS constants are restored. MSAA
color exports describe their single-sample resolve; per-sample color, stencil,
material replay and physical source ownership remain explicitly unproved.
No BeginScene/EndScene boundary is added. This is useful input capture, not
complete native-scene replay.

The helper acceptance uses actual 37x23 DF24 and INTZ checkerboard depth,
verifies all 1,702 depth pixels exactly, 116 float4 registers, all captured
state and 256 VS / 224 PS constant registers. Twenty-two injected API failure
cases must leave incomplete output. Three complete exports and all failure
controls pass on the real AMD Radeon integrated adapter using a private copy
of the installed DXVK backend. NVIDIA rejects DF24 at format admission in this
backend; those failed attempts are retained. No vendor ID is spoofed and the
AMD helper result is not claimed as a NVIDIA or full-scene benchmark. Compiler
temporary files and DXVK logs/caches are redirected into each private run.

## Cache capacity decision

Do not increase every cache. The late warm normal-cache interval adds 279,874
hits with no generations or evictions, and displacement adds 1,200 hits with
no builds. More retention cannot reduce that already-reused work. Texture
shadow eviction counts likewise overstate useful eviction-return readbacks.

Terrain is the justified capacity candidate: one recorded frame is byte-full
at 32 MiB, with 571 misses, 573 byte evictions and 8.349 ms capture. Another
frame has 137 misses without any eviction, so not every miss is capacity churn.
A private 32/48 MiB comparison must preserve the 16 MiB low-space policy,
entry limit, distances, geometry and MSAA, and measure free/largest 32-bit VA,
GI admission and relief cost alongside exact geometry and capture CPU.
Available VA already falls below the existing 768 MiB geometry admission
floor. The extra retention can make overall stalls worse. No default cap is
changed and no cache-derived FPS uplift is claimed.

## Remaining scene replay gate

The strict authored swapchain image check still fails after Reset on the exact
private DXVK backend, including without Northlight. Further diagnostics retain
the original failure: the source target is correct; a sentinel fill of the
destination reads back correctly; copying the source to either an ordinary RT
or the backbuffer leaves the sentinel unchanged despite successful HRESULTs.
Both null and explicit rectangles fail. A normally closed official trace
confirms the correct device, new surface identities, live references and exact
StretchRect arguments. It contains four rendered frames and three Presents,
not the required six-frame acceptance. No backend defect is established yet.

Authentic whole-scene throughput still requires original ordered native calls,
resource/pose/depth inputs, independently verified device/draw/epoch joins,
context-dependent WorldRenderer activation and matching quality/MSAA. None of
the component checks above substitutes for that gate or proves stable 120 FPS.
Detailed immutable logs, commands, source guards and failed attempts are kept
under `northlight-diagnostics/rotation-scene-followup-20261024`.

## Build and focused acceptance record

Both x86 DLLs build from renderer source commit
`e1b642e9d3c455d45741e33c21e89a6c5912d058`, BUILD identity
`b34e68b47d1d5934b04f9a87c296d037dd59782599668b76be74e28d72df9200`.
Production inputs remain unchanged through each build, and both PE/PDB GUIDs
and ages match. The six actual-DLL CPU-host C ABI cases pass. Two pre-existing
compiler warnings remain; no new format warnings remain. The first integration
build failed on an incorrectly scoped diagnostics call; its corrected rerun
and the original failure are preserved. The helper's subsequent format-argument
casts change no pixel/state operation; its GPU acceptance predates those casts.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Normal `frd9.dll` | 5,774,336 | `b0e46244d4009b2f84cbf12e059dcf321a794666a40e76ff43ad4c265d109908` |
| Stand `frd9-stand-replay.dll` | 5,776,896 | `54578873561e1c4352c417a08cb43b6f4f694953ed056a19b88551e2783df843` |

The separate manual package is
`northlight-builds/northlight-rotation-coherence-e1b642e-20261024`.
Its `d3d9.dll` is the normal artifact; the replay DLL is excluded. Nothing is
installed automatically. The installed DLL, optimizer DLL/symbols, log, personal
INI files and Config.wtf still match the previous recorded SHA-256 values.
The focused evidence locations are `shadows/snapshot-client-range-final`,
`native-chain/worker-import`, `native-chain/lighting-capture`,
`native-chain/coherence-dll-api`, `coherence-build-normal-final` and
`coherence-build-stand`. No full suite, old 69-test selection, game launch,
publication or FPS benchmark was run for this slice.
