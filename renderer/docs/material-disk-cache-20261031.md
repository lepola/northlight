# Persistent generated material maps

Generated normal/height textures shared by world and M2 normal mapping and POM
now survive memory-cache eviction, device Reset and game restart. Files live at
`<client>/Cache/Northlight/Materials-v1/`. The existing GPU cache still bounds
resident maps. A disk hit uploads the finished base texture and regenerates its
hardware mip chain without creating or drawing the normal-generator shader.
CPU displacement meshes and transient render targets are not serialized.

## Identity and limits

Keys hash source pixels, dimensions, format, effective height profile, world/M2
domain, generator bytecode, backend DLL identity and adapter/driver identity.
Equivalent M2 settings reuse the same map; changed inputs and generator/backend
revisions cannot reuse an incompatible file. Source pointers and installation
paths are excluded. Payload length, dimensions, header and SHA-256 are checked
before upload. Interrupted/corrupt entries become misses and are replaced by
atomic publication of a completed temporary file.

The worker maintains a 4 GiB disk target and 16,384-entry target, pruning oldest
files at startup and every 32 writes, down to 7/8 of those targets. Hits refresh
last-use time. Only owned map files and owned temporary files older than a day
are eligible for removal. Temporary files belonging to an active writer survive.

Persistence accepts static MANAGED textures in BGRA8, BGRX8, RGB565, ARGB4444,
ARGB1555, XRGB1555, DXT1, DXT3 and DXT5. Source snapshots are capped at 8 MiB;
finished maps at 512x512 BGRA8. Unsupported, dynamic, render-target and unreadable
sources retain the existing generation/fallback path.

## Scheduling

One worker owns hashing, file reads/writes, validation and pruning. Admission
uses a try-lock and bounded queues (8 jobs, 12 MiB charged CPU data). The render
owner never performs file I/O, helps execute a job, or waits for its completion.
Only owned CPU bytes cross to the worker; D3D resources stay on their owner.

At most one source snapshot and one completed map copy are admitted per frame.
There are at most four pending GPU readbacks. Native D3D9 queues a copy to a
lockable render target; the inspected packaged DXVK 2.7.1/3.1.1 implementations
queue GetRenderTargetData to SYSTEMMEM. Both paths poll an EVENT once per frame
without FLUSH and lock only after completion, using DONOTWAIT and retrying a
busy resource on a later frame. Upload/queue failures retain retryable data.
Reset drops pending GPU resources without joining the disk worker. Final owner
destruction drains the worker, so shutdown can wait for disk I/O.

## Validation

- Separate-process store tests cover restart, changed input/settings/dimensions,
  payload checksum, corrupt/truncated files, recovery, unavailable directories,
  quota pruning, stale temporary files and preservation of retryable input.
  A deliberately blocked I/O worker cannot block owner-side admission; the
  ninth outstanding job is refused while all eight earlier jobs stay pending.
- Native D3D9, DXVK 2.7.1 and DXVK 3.1.1 each pass 23 separate GPU processes:
  nine formats, world and M2 maps, restart, corruption/rebuild, changed settings,
  equivalent M2 settings, failed upload/retry and a real device Reset/reload.
  Base pixels and samples of mip levels 0, 2, 4, 6 and 8 match fresh production
  output exactly. Hits perform zero generator draws and shader creations.
- Busy EVENT and surface wrappers force backoff for eight frames each, asserting
  zero FLUSH flags and DONOTWAIT locks. The native path forbids GetRTData in
  production; test-only reference readbacks may wait.
- The actual Honor Hold `statuebase.blp` is read from the client for the RGBA
  cases; private client data and test binaries are not committed.
- The Windows x86 production DLL builds. O0/O2 cache-state and forced-collision
  checks, material binding/range negative controls, native/DXVK3 visible-set and
  reuse GPU checks, material replay, model profile, mip accounting and profiler
  checks pass. The two pre-existing audit failures remain documented in
  [material cache stability](material-cache-stability-20261031.md).

The mip oracle also found a DXVK 3.1.1/NVIDIA 621.1.0 descriptor-heap failure
without persistence: later fresh textures have blank autogen mips. Disabling
`dxvk.enableDescriptorHeap` fixes the independent reproducer and the full cache
matrix. That default joins the existing process-local DXVK compatibility
options; explicit inline overrides remain last. Native and DXVK 2 are unaffected.
The option is documented in [DXVK's configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf).

These are offscreen correctness and scheduling checks, not an in-game travel
or frame-time recording. No client installation or settings files were changed.
