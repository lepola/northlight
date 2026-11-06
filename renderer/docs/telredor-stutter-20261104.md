# Telredor periodic shadow loss, 2026-11-04

The tester's `qa_report/northlight-renderer.log` identifies renderer source
`59f0eef99c3ff2404bde8683afd2e7583392ea05ce749eaf2784db0b62330add`, built at
17:18:55 UTC. The scene is Expansion01, zone 3521, at camera
`(281.12, 5984.81, 152.29)`. This is the preceding build, not the later
Stormwind HDR report.

## Recorded cause

Four successful, game-requested device resets occur at game frames 897, 1030,
1151 and 2477. The first two gaps are 6,015 and 6,000 ms; the last is 34,985 ms.
Each reset keeps the same window, 2560×1440 back buffer, presentation interval,
MSAA mode and focus. The device reports `D3D_OK` before every reset. The
requested D24X8 depth format is upgraded to D24S8 by Northlight; that difference
alone does not establish a feedback loop or explain the reset trigger.

All four recorded stacks contain `Wow.exe+0x2a04cf` and
`Wow.exe+0x2902c6`. Read-only disassembly of a local client copy maps these to
the `WM_SIZE` handler and its `IDirect3DDevice9::Reset` call respectively.
The message dispatch uses entry 2 at VA `0x6a0798`, selecting handler
`0x6a0488`, whose call returns at `0x6a04cf`. The device reset call is at
`0x6902c4`. No executable modification is involved. The exact sender and
message parameters were not captured; an OPENGL32 frame in the stack does
not identify the responsible external application.

The resets invalidate shadow targets and lighting history. The static scene
stays at generation 1 with 254 CPU models, but its GPU model count falls to
zero and the same 12,901,464 bytes are uploaded again after each reset.
Following frames reach 366.153, 396.026 and 364.616 ms. Diagnostic capture
frame 1141 takes 1,019.104 ms and is excluded from those figures: its products
explicitly say `fps_eligible=false`.

The seven memory samples report no pressure and no trims, with at least
1,335 MiB of available virtual address space. No periodic whole-cache eviction
is evidenced. The CPU texture cache does suffer a separate reset defect:
retained entries fall from 4,362 to 5; decoded textures stay at 156 while
misses rise from 379,077 to 1,880,129. Once-uploaded managed textures survive
a device reset, but their cleared CPU copies cannot be recovered from an
upload that the game has no reason to repeat.

Local evidence: `out/telredor-stutter-20261104/log-evidence.json` and the
reproducible parser beside it. The native call-path mapping is from a copy
of the client's executable, whose hash is included in that evidence.

## API contracts

[WM_SIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-size)
distinguishes restored, minimized and maximized windows. Its packed dimensions
contain only 16 bits each. A duplicate-size guard must preserve real state
transitions and must not mistake truncated dimensions for equality.

[D3D9 Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset)
still has to execute when requested: default-pool resources and device state
are invalidated. [Managed resources](https://learn.microsoft.com/en-us/windows/win32/direct3d9/managing-resources)
survive the reset without application re-upload. CPU cache retention must
follow that lifetime and continue invalidating writes, releases and identity
reuse.

## Duplicate resize notifications

An independent window-stability owner shares the existing subclass with the
settings overlay. It remains active when F11 settings input is disabled.
Only repeated `SIZE_RESTORED` notifications are coalesced, after a size has
actually reached the previous window procedure and a successful primary,
windowed Present has confirmed the same dimensions. The live client rectangle,
window rectangle, styles, focus and non-minimized/non-maximized state must
also agree. The first message and all unproven cases pass through.

Reset, failed presentation, focus, display, DPI, style, size/move and lifetime
transitions invalidate that proof. A synchronous Reset inside the original
window procedure cannot prevent the guard from arming afterward; nested size
messages cannot overwrite newer geometry with an older outer message.
Additional swap chains cannot publish a healthy primary-window state.
Teardown reserves ownership until shared cleanup finishes. Late publications
carry the original session and invalidation epoch, and use a bounded writer
claim; they cannot declare a replacement window ready. UI-only cleanup retains
the stability owner, while concurrent window destruction retires it.
Explicit device Reset calls still execute normally. Window changes that cannot
be rebound safely fail open.

The window procedure makes no D3D calls and waits on no renderer locks. Present
publishes atomic metadata; client/window queries occur on size messages only.
There are no new GPU passes, cache limits or quality reductions. Bounded
diagnostics distinguish forwarded message states from suppressed duplicates.
Since the historical log lacks `wParam`, this fix does not claim that every
recorded reset was specifically a duplicate `SIZE_RESTORED`; other resize
states deliberately continue to reach the client.

The Win32 fixture covers 50 scenarios plus two enabled-menu ownership cases on
each of native D3D9, DXVK 2 and DXVK 3.
It uses real ANSI/Unicode window procedures, subclass chaining, geometry queries
and a private GPU marker. A dummy client Reset invalidates the guard and clears
that marker; duplicates preserve the marker bit-for-bit with the guard, while
the no-guard control resets and loses it. A compile-only foreground fact seam
allows hidden windows without taking focus from the user's applications; false
and foreign foreground controls are included. This is not a live WoW replay or
a physical-display test. The separate texture fixture below performs actual
D3D9 Reset calls.

An artifact-only negative control restores the old release-before-cleanup
ordering and must fail the reentrant ownership assertion. Tests also cover
destruction during UI maintenance, delayed old publications after owner reuse,
old-menu destruction after a replacement menu is active, and UI-only teardown
with an independently owned guard. No existing acceptance threshold is relaxed.

Evidence: `out/telredor-window-size-win32-20261104/validation.json`.
The final policy/wiring and existing overlay suites pass on Linux in
`out/pr98-ci-20261104/linux-runs/20261104-222809`. Actual menu rendering on
private SDR/scRGB targets passes in
`out/telredor-stutter-20261104/test-output/20261104-222747`.

## CPU texture retention

Reset now removes DEFAULT and unknown-pool CPU entries while retaining copied
bytes belonging to MANAGED, SYSTEMMEM and SCRATCH textures. The upload hooks
record the existing resource pool, and material copies use the destination's
pool. The normal entry/byte budgets and release/write invalidation remain in
force. A same-content copy to a new owner receives a new lifetime instead of
reusing the previous owner's identity.

Portable tests cover all pools, undecoded uploads, repeated resets, invalid
entries, failed and partial writes, release, recreation and owner reuse.
The command-stream scenarios also exercise pool propagation through the real
ordered upload hooks. The hidden native D3D9 fixture checks a failed reset,
successful retries, unchanged managed GPU/CPU pixels, stable cache identity and
revision, DEFAULT recreation and subsequent write/release invalidation.
Its old-clear negative control fails with `TEXTURE_RESET_MANAGED_MISS`.

The full command-stream suite passes, including the new ordered-upload/reset
scenarios, in `out/pr98-ci-20261104/linux-runs/20261104-215612`.
The Windows-only material follow-up, HDR reset and reset-stack suites pass in
`out/telredor-stutter-20261104/test-output/20261104-215801`; the Windows material
test was naturally skipped in the Linux invocation and was run on Windows.

No test raises cache budgets or changes installer cache versions. `Wow.exe`
remains untouched. GPU timing is not used: an unrelated local game occupied
approximately 98% of the GPU during this investigation.
