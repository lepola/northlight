# Offline city-lamp audit — 2026-11-04

Follow-up: [lamp-shadow fixes and reproducible regressions](lamp-shadow-fix-20261104.md)
address the Dalaran/orc findings below and the subsequently reported Stormwind
daylight defect. This document preserves the earlier audit and its failures.

The tested street lamp from Stormwind illuminates a floor and casts independent
shadows from a weighted character and a separate obstacle. The tested dwarf and
draenei lamps also pass. This is not confirmation that every city lamp works:
the Dalaran native-light path and two orc floor fixtures have the gaps below.

The harness was compiled against the source snapshot of the installed 0.3.217
DLL (SHA-256 `3cb2fbd927dae47e3944e2b974c5e3957d92c1985c73c1d270bc3a383e2f5437`).
Its relevant point-light, actor-shadow and native-source code/shader bytes match
the current integration. The final run used the installed DXVK backend
(`265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa`).

## GPU checks

The existing 16 lamp cases pass unchanged. Three additional fixtures pass the
production soft-shadow resolve and lighting consumer, with no hard-shadow
fallback. Each uses a real stock HumanMale skin section: 620 triangles and
46 palette bones, with original positions and skin weights. The three controlled
states cover placement, movement and one-bone articulation, not gameplay animation.

| Fixture | Character-shadow pixels in three states | Separate obstacle-shadow pixels |
| --- | --- | --- |
| `stormwindstreetlamp01.m2` | 3026 / 3732 / 3706 | 4499 |
| `dwarvenhanginglanternsmall01.m2` | 1611 / 4128 / 2309 | 3341 |
| `dr_lamppost_01.m2` | 3022 / 3773 / 3701 | 5066 |

Actor-off and actor-removal controls restore the exact lamp-plus-obstacle result.
Obstacle removal restores the lamp-only cube. Existing static shadow texels are
preserved. Direct illumination varies across the receiver; the combined direct
and shadow-correction output stays finite and nonnegative. The renderer supplies
the floor lighting; the presentation meshes use flat colours without textures.
The hard-shadow cube/correction controls also passed on native D3D9 and DXVK.

The GPU runner now accepts `--source-root`, repeatable `--backend` and
`--actor-case` fixture directories, `--actor-only` and `--actor-soft`.
Actor fixtures contain `tile.fg3`, `models/` and `sources.fgl` produced from the
user's client; proprietary models and caches are not checked into Git.
The default 16-case regression matrix remains unchanged. Actor auditing is an
explicit additional mode; failing city fixtures are not hidden by weaker checks.

The existing WMO ownership suite also passes at O0/O2: 27 actual lamp matches,
own-enclosure transmission, and continued occlusion by walls, metal, tables and
other glass. This is CPU geometry evidence, not a complete-city GPU replay.

## Confirmed gaps and limits

- `dalaran_lamppost01.m2` and `dalaran_lampwall01.m2`: the no-draw native scene
  reader emits positive lights with a 12-yard range, but conversion leaves
  `CastShadow` unset. Point-shadow eligibility and selection are both zero at
  O0/O2. Their animated sources are omitted from the static light cache.
- `orcbrazierstreetlamp.m2` and `orchanginglantern.m2`: the floor setup fails
  independent obstacle-shadow coverage because of strong own-mesh occlusion.
  The hanging model also produces no character-shadow pixels there. CPU rays
  corroborate the blocked floor directions; this does not prove all receiver
  orientations or in-game placements are broken.
- The tested canal, Ironforge and Undercity lantern model files have no standalone
  native/authored light in these fixtures. Their surrounding WMO or nearby lights
  were not reconstructed, so no city-wide darkness claim follows.

This audit changes tests only, not production lighting. It does not validate
live capture/admission, a complete city frame, HDR composition, MSAA or gameplay
FPS. The game DLL, both installed Northlight archives and `Wow.exe` were unchanged.
Local logs, manifests and the actual rendered preview are recorded in
`out/city-lamps-20261104/validation.json`; the final 19-case GPU run is under
`.local/diagnostics/city-lamp-character-20261104/soft-regression/`.
