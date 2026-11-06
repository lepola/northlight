# QA address pressure and material uploads

The supplied `northlight-renderer (3).log` has SHA256
`2db9be441ccc7e4ba9cd9bb80ebb3fe18243f4b89a82b046ba31afa5664e958c`.
It identifies 0.3.217, source
`d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`.
The separate diagnostic captures are older; their SDR brightness analysis and
the common SDR/HDR correction are in [the brightness report](qa-native-brightness-20261104.md).

## Evidence and recovery

The large log contains 34 memory-guard trims, 1,274 geometry-memory messages,
174 raised-entry events and 80 expired-world reports. The slowest trim is
57.008 ms, including 54.975 ms clearing replay GPU resources. The last trim's
estimate is 66 MiB, but free address space only changes from 466 to 468 MiB;
the largest hole remains 113 MiB. This is x86 virtual-address pressure and
fragmentation, not evidence of exhausting physical RAM. The log does not
record an explicit master-effects disable or an out-of-memory crash. Expired
world coverage following stalled replacements explains loss of world-dependent
effects, but the log alone does not prove every visible disappearance.

A failed warm chunk build could raise its next entry floor to roughly 556 MiB.
The existing bounded cold path then applied the **same** refusal history to
its own 304 MiB floor. It waited for the warm build's measured peak even though
it uses file-backed temporary geometry and omits optional retained caches.

Cold and warm attempts now keep independent refusal histories. Actual cold
failures still raise the cold floor. Success, map changes and abandoned spatial
regions clear all histories. Admission floors, 256 MiB process reserve,
contiguous-allocation margin, cold headroom, trim thresholds, retirement and
spatial validity limits are unchanged. This avoids a needless recovery stall;
it cannot manufacture address space when the cold path itself does not fit.

## Material reuse

The final material counters show 80,355,309 normal-cache hits and 31,988 disk
map uploads. Every disk hit previously created and released a system-memory
surface. Uploads now reuse one surface with matching dimensions, bounded by
the existing 512x512 generator limit (1 MiB of pixel storage).

Every lock uses `D3DLOCK_DONOTWAIT`. A busy reused surface permits one fresh
nonblocking attempt; failed uploads discard the scratch surface and remain
retryable. Reset and memory-pressure trimming release it. No material identity,
normal-map contents, disk format, world-cache version or cache budget changes.
The log adds `uploadCreates` and `uploadReuses` to make driver-side reuse visible.

## Validation

- The exact production entry-selection and refusal-routing blocks run at O0
  and O2: 17 entry cases, three refusal modes and five allocation-failure cases.
  Restoring the old shared warm/cold lift fails the new regression controls.
- Real Dalaran geometry, seven camera positions, approximately 1.58 million
  local and 2.37 million shadow triangles: a genuinely fragmented x86 process
  with 466 MiB free and a 113 MiB largest hole completes the cold path. Full
  scene, shadow-plan, material, page, ray and GI-bake hashes match ordinary
  builds. Sampled free space remains above 294 MiB; the 258 MiB cold guard is
  retained. These samples are admission checkpoints, not a continuous peak
  memory trace.
- The fragmentation fixture now exercises the actual cold storage policy,
  including texel sharing and spooling. Initial validation exposed its old
  warm-only setup and vector-only material hashes. The fixture now reads all
  texels through the production storage accessors and also compares the full
  plan hash; no parity assertion or reserve threshold was removed.
- Native D3D9 and DXVK 2/3 each verify 72 changing material uploads pixel for
  pixel at 32x32, 64x64 and 512x512: three staging allocations and 69 reuses,
  instead of 72 allocations. Injected busy-lock and failed-copy paths prove
  nonblocking retry, exact pixels and trim/reset release.
- Existing geometry-memory, guard, terrain-pressure, material-memory and
  persistent disk-cache tests pass. Material-pressure transition controls
  retain their exact caps, ownership and recovery assertions.
- The existing extended GPU disk suite passes on native D3D9 and DXVK 2/3:
  RGBA, DXT1, DXT3 and DXT5, restart without regeneration, corrupt-entry repair,
  setting identities, mip pixels, upload failure, busy backoff and device reset.
  Normal-cache regressions on the same backends pass 180 exact map comparisons
  with 48 allocations, plus 96 visible-set frames without map transitions.

DXVK tests load the client's backend DLLs read-only: DXVK 3 SHA256
`265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa`,
DXVK 2 SHA256
`00ecd422b3b12e9d3b309785f3f19431405389d80eb4a37018ebc63e2d87d900`.
An initial material-regression invocation accidentally selected an older
Northlight proxy named `d3d9.dll`; it failed before creating a device and is
not counted as a backend result. The completed regressions use the real
backend DLLs above.

Reproduction:

```text
python tests/test_chunked_local_entry.py
python tests/validate_local_scene_fragmentation.py --output out/qa-memory-cold-parity-20261104 --available-mib 466 --gap-mib 113 --cold-recovery
python tests/test_material_upload_reuse_gpu.py
python tests/test_material_pressure_transition.py
```

The real-scene fixture needs the locally extracted world cache and a fresh
output directory. GPU runners use `NORTHLIGHT_TEST_D3D9` to select a backend.
The new material regression is in the ordinary test-discovery manifest.

This proves reduced allocation work and successful bounded recovery, not a
whole-game FPS increase. `RenderProfile=0` in the supplied session prevents a
reliable before/after frame-cost comparison. Cold scene timings overlap other
correctness work and are not performance measurements. The game was not run
and `Wow.exe` was not modified; its SHA256 remains
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.

The production DLL was rebuilt with `python scripts/build_renderer.py`, pinned
Zig 0.15.2 and the existing shader freshness gates. Its embedded source identity
matches the final source tree:
`59f0eef99c3ff2404bde8683afd2e7583392ea05ce749eaf2784db0b62330add`.
`renderer/frd9.dll` is 9,967,104 bytes, SHA256
`d6c48652fdabbcb856cfd926f915468104d5f29bf7aa17e401559ab83a26939a`.
Source-identity, layout, world-shader and shadow-transition build checks pass.
The installed renderer and previous installer were not replaced.
