# Rotation follow-up: cache capacity and offline replay inputs

This records the earlier `91cfd0b` implementation slice. Subsequent worker
integration, a production snapshot-range correction and normal-view diagnostic
capture are documented in [rotation-snapshot-coherence-20261024.md](rotation-snapshot-coherence-20261024.md).

## Cache decision

Keep the installed settings and current default cache limits unchanged. The
available runtime log is still the run ending on 2026-10-24 at 03:28:59, SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It cannot establish the effect of the newer private builds.

| Cache | Observed evidence | Decision |
| --- | --- | --- |
| Terrain geometry | Frame 18415: 32 MiB full, 571 lookup misses, 573 byte evictions, no count pressure, 8.349 ms capture. A different first-use frame has 137 misses with no evictions at only 9.59 MB. | Prioritize a private 32/48 MiB comparison. Not every lookup miss proves capacity churn; leave the entry limit unchanged. |
| Generated normals | Frames 27000 to 28200 add 279,874 hits, with generations and evictions unchanged. Earlier intervals do contain generation and eviction churn. | A larger cache may reduce cold-turn work, but there is no evidence of continuous regeneration in the late warm intervals. |
| Displacement | The same late interval adds 1,200 hits and zero builds. | Larger retention cannot eliminate pose, draw preparation or admission work. No default increase. |
| Texture shadows | 3,748 cumulative evictions, but only five readbacks attributed to relocking an evicted image; most readbacks have other causes. | Eviction count alone does not justify more retained upload data. |
| GI / snapshots | GI reuses 5,600 static probes with zero solves in one pressure interval; snapshot misses and overflows are zero in the retained final counters. | No evidence supports increasing these limits for heading changes. |

This is a 32-bit process. Observed available virtual address space falls below
the existing 768 MiB geometry admission floor. Adding retention can bring
forward admission refusals and memory relief even when physical RAM is ample.
An isolated terrain comparison must retain the existing 16 MiB low-space
policy, distances, geometry, MSAA and quality, and measure actual free/largest
VA, admission, relief and complete frames alongside capture CPU and exact
geometry. No capacity experiment or FPS gain is claimed here.

The longest retained empty-stream interval is 249.425 ms with recorded,
published and replayed positions equal at entry, only four new commands at
exit, and no observed queue wait flags. Its producer routine remains
unidentified. Additional queue/cache space cannot create absent commands.
Separately, old memory-relief frames take 39.025 / 35.651 ms in total,
including 21.176 / 20.186 ms in material cap transitions. Private source already
clears mandatory source pools before shrinking limits; a fresh run is still
needed to evaluate its subphase telemetry and benefit.

Three agents reviewed five distinct directions each: terrain capacity, VA,
revision/policy work, GI retention and producer attribution; normal generation,
displacement, texture shadows, capture relevance and material pressure policy;
official trace dependencies, isolated capture, layer/resource coverage,
query/MSAA parity and bounded sidecar transport. Detailed evidence is retained
under `northlight-diagnostics/rotation-scene-followup-20261024` in the private
workspace, including `shadows/cache-capacity-decision.md` and
`lighting/cache-policy-audit.md`.

## Implemented offline infrastructure

- A bounded raw PE32 file provider supplies verified immutable client bytes.
  Writable sections and heap addresses require recorded runtime data. The
  client executable is read as a file, never mapped for execution or launched.
- A dedicated `--stand-replay` DLL imports the PE and `NLSNAP1` packets through
  explicit C exports before factory creation. It preserves known-key FIFO and
  last-copy behavior, rejects absent inputs and host-memory fallback, disables
  the client CVar bridge, and refuses D3D9Ex. Its distinct embedded marker is
  rejected by release packaging. The normal DLL is a separate build output.
- An opt-in fixed-slot snapshot spool writes only on its worker. Producer-side
  allocation, file I/O and waits are excluded. Bounds, overflow, record/byte
  budgets and failures leave the session incomplete. Its journal includes
  device, producer frame domain, draw, epoch and thread identity; it is not an
  authenticated trace-call index. The normal callback defaults to null and
  the normal renderer does not construct or enable the spool.
- Four existing FGR consumers accept exactly 68 or 79 float4 registers and
  upload the complete supplied bank. Diagnostic JSON distinguishes actual
  capture inputs from reconstructed depth, authored source/fog data and
  missing per-pass inputs. Existing image assertions are retained.
- Private tool staging includes the official x86 retracer dependencies,
  including its shader compiler DLL, rather than copying the executable alone.

## What the stand does not yet prove

The authored hidden D3D9 smoke is a plumbing oracle, not the user's scene. Its
strict swapchain image gate currently fails after Reset on the exact private
DXVK backend even without the tracer or Northlight. The offscreen scene target
contains the expected pixels while the post-copy swapchain readback is zero.
Fresh backbuffer identities, dimensions and present parameters are correct;
an explicit completion event after the copy does not repair it. The complete
six-case capture/retrace/MSAA acceptance remains unmet. An offscreen readback
does not replace that gate, and no backend defect is established by this alone.

The imported-context scope currently belongs to its host thread. It is not
yet a lifetime-pinned provider for the command-stream replay worker. The
stand's current WorldContext route also needs recording/playback integration
before a streamed run can preserve the imported per-trigger context. An
actual above-Northlight trace host must invoke the exports at independently
verified draw/device/epoch joins and establish context-dependent renderer
activation. Loading the DLL or matching an authored image alone is
insufficient. Streaming or quality must not be disabled to conceal this gap.

Authentic full-scene replay still lacks the original ordered native calls,
resource/pose inputs, verified joins and complete depth/material/per-source
banks. There is no measured whole-scene uplift, stable 120 FPS result or
confirmed elimination of camera-turn stalls in this slice.

## Validation and isolation

Five focused CPU scripts pass: raw PE translation against the actual local
client file, exact 68/79-register transport, bounded spool, archive transport,
and the existing game-snapshot regression. Applicable fixtures exercise O2,
O0 and ASan/UBSan. The original snapshot wiring audit initially rejected a
preprocessor directive inserted between the normal function signature and
body; the stand definition is now separately conditional, preserving the
normal signature/body verbatim. The unchanged existing check then passes.
The failed attempt is retained. Layout and the 316-entry discovery list pass
without executing discovery's tests.

The final x86 host loads the actual dedicated DLL and passes six isolated
C-export scenarios: valid import/update, factory before image, factory before
snapshot, wrong thread, corrupt archive and repeated draw order. No successful
graphics factory or device call occurs. The imported WorldContext CPU scenario
also passes O2/O0 against actual local signature bytes and authored runtime
values. The first C-export fixture compile failed on direct FARPROC casts under
`-Werror`; typed Win32 pointer lookup now copies the checked representation
without disabling warnings or changing any assertion. That failure is retained.

Both DLL variants build from renderer source commit
`91cfd0b91751878237e582d35721d108dd16fb93`; BUILD identity is
`61cdf0213da8bb1b028e41dc2bbdc703d8e2bc972771f3e9f7170fe4323c3932`.
Production inputs remain unchanged through each build. Two existing warnings
remain. Exact PE/PDB GUID and age match for both artifacts. Release packaging
accepts the normal DLL and rejects the dedicated stand DLL.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Normal `frd9.dll` | 5,751,808 | `0b318380f62133e1f2cf892742b6a5067ff755a6574d6616c2ff9dd0e11b6360` |
| Stand `frd9-stand-replay.dll` | 5,752,320 | `9d65f1f126500de7e1d720cbed57c6301e83a8a1477a8dcfee8332a3d7da82b3` |

Private records are `native-chain/changed-path-1`,
`native-chain/snapshot-wiring-retry-1`,
`native-chain/final-hosts-retry-1`, `cache-build-normal` and
`cache-build-stand`. Protected installed inputs still match the original
hashes. The later fixture/report commit does not change renderer build inputs.
No full test suite, old 69-test selection, game execution,
optimizer modification or publication is part of this slice. Installed DLLs,
personal settings and MPQs remain untouched.

One early authored backend smoke unexpectedly created a zero-byte `.bin` and
12-byte `.lut` in the user's DXVK AppData cache. Their names, times and hashes
are preserved in `native-chain/unexpected-appdata-cache-evidence.json`.
They were not deleted or presented as an untouched-cache result. Later backend
experiments redirect their cache/log destinations to private directories.
