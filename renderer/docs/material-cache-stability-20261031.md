# Stable material relief under cache pressure

Honor Hold's central stone is the placed `MonumentRock` M2 at approximately
(-701.40, 2657.58, 90.41), using `statuebase.blp` for its inscription.
The placement and texture were inspected read-only in the active client archives.
Private client data is not included in this change.

## Cause and change

Normal maps, POM and displacement share the generated material-map cache.
A visible set larger than any cache limit repeatedly evicted maps still used
by that same view. The two-build-per-frame allowance then made materials
alternate between relief and the native fallback, even with unchanged inputs.

Track each entry's last use frame separately from its build frame. Capacity
admission cannot evict an entry used this frame or the preceding frame. New
maps wait for a complete unused frame before reclaiming an LRU entry. This
applies to both M2 and world maps and to map bytes, source-reference bytes and
entry-count limits. Unsigned frame age also handles frame-counter wrap.

All memory limits, map resolution, shaders and build allowance are unchanged.
A full, continuously visible set retains its admitted maps; additional materials
use the existing native fallback until space becomes available. Explicit pressure
trims, source revisions, unknown mutable-source expiry, configuration changes
and Reset still invalidate or retire resources as before.

## Validation

- Standalone PR #97 Windows x86 production DLL builds.
- The CPU oracle reproduces 248 relief-presence transitions with the old cache
  and zero with the fix over 96 frames. Separate entry-count and source-byte
  cases reproduce 256 and 246 old transitions, respectively, versus zero.
  O0/O2 and forced hash collisions pass, alongside 12,566 differential cache
  operations, allocation faults, reference ownership, view changes, revisions
  and frame wrap.
- Native D3D9 and DXVK 3 each pass the 96-frame, 32-texture working-set check,
  including the client's Honor Hold inscription texture. The 24 admitted maps
  remain present and equal freshly generated maps byte for byte. Each backend
  also passes all 180 existing fresh/recycled pixel comparisons and lease,
  bound-texture, render-target, resize, failed-draw and Reset checks.
- Mip accounting, material replay-state and the actual material binding/constant
  oracle pass. The binding oracle retains its draw/state equality, resident/UP
  fallbacks, failure handling and reference-restoration checks.

The existing displacement-state test fails at its unsupported-program assertion
(line 546) with either the old or new cache. The upload-lock source audit also
rejects an unchanged vendored ImGui lock at `imgui_impl_dx9.cpp:201`. Neither
failure is introduced by this cache change.

These are cache and offscreen GPU checks using the actual inscription texture,
not a recorded game draw stream. No game session, complete in-game visual
acceptance, unrelated flicker fix or FPS improvement is claimed. Installed
client files, settings and the world cache are unchanged.
