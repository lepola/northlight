# Review readiness and integration map

All feature branches target main and build without checking out another feature branch. Readiness means the implementation and reproducible correctness evidence are available for review; it is not a claim of universal hardware, game visual acceptance or FPS parity. No new performance measurements are requested while the client cache is being generated.

| PR | Review unit | Important boundary |
| --- | --- | --- |
| #76 | Native vegetation alpha/order and bounded grass casting | Native capture and all shadow-alpha modes pass D3D9/DXVK; game appearance remains visual acceptance. |
| #86 | DXVK presentation compatibility | Retains the existing backend-specific colour workaround. |
| #92 | Bounded local-source shadows and conservative clipping | Four shadowed lights by default; high limits need hardware cost review. |
| #94 | Opt-in HDR world/UI presentation | Physical display behaviour and other hardware are not inferred from offscreen tests. |
| #96 | Fresh diagnostics and reliable effect toggles | Diagnostic evidence does not certify a game frame by itself. |
| #97 | Material preparation, relief and shared geometry | Material quality needs scene review. |
| #109 | Regional fog independent of geometry admission; exact fog/cloud preparation | Screen atmosphere does not promise full geometry publication on every driver. |
| #110 | Native sun/glare colour and composition | Preserves the native source contribution. |
| #111 | Bounded message-pumped EVENT completion | Pending work remains owned until completion or the existing safe recovery path. |
| #112 | Finite-thickness actor AO | Separate visual-quality alternative with documented extra cost; not included in this candidate or inferred from #118. |
| #113 | Directional shadow transition | Independently tested cache/actor separation. |
| #114 | Emitter coverage, native animation and local transmission | Carries its #92 foundation snapshot; unsupported sources and native-slot ownership remain explicit. |
| #116 | Optional tooling imports and isolated fixtures | Client-dependent prerequisites remain explicit. |
| #117 | Bounded CPU scene storage and exact GI packing | Worker-entry 395/434 MiB tests do not establish complete cold-GPU recovery. |
| #118 | GTAO silhouette handling and exact footprint preparation | Distinct from the actor-thickness alternative in #112. |

## Why retain #98

The integration candidate contains additional scene/GPU recovery and shared-resource ownership changes beyond the independent storage foundation. Closing it would hide the source and reproduction path for the tested installer. It remains a combined integration review, with overlapping feature history, rather than an additional independent feature merge. Review the small units first; reconcile accepted changes and re-run integration checks before merging the combined candidate. Do not infer inclusion of every latest independent change from this table. In particular #112 remains separate.

The existing combined cold/warm GPU matrix at 395/434 MiB, Reset/probe reuse, screen fog/cloud MSAA and HDR results are prior baseline evidence recorded in the adjacent notes. They are not newly repeated against a cache that is being generated, and independent PRs do not inherit them.

## Current functional update

Grass casting had been blocked by the generic blended-model gate and by a cutout-only material gate. The fix admits only verified stock DetailDoodad pairs and normal straight-alpha blending, keeps all snapshot limits, and uses fade-preserving UV coverage for blended masks. Both the independent and combined production-renderer oracles pass 384 directional/local mask checks per backend on native D3D9 and DXVK, all legacy alpha modes, alpha-test controls, and unknown/additive/disabled rejection. No game or live cache is used. The combined receiving-order source guard now checks the actual surfaceLighting-gated helper and both lit/unlit call sites.

The independent storage tests retain allocator/publication checks and 2,285,440 exact differential slots. Correctness runs no longer execute embedded probe/grass microbenchmarks by default. See grass-shadow-capture-20261030.md and scene-storage-review-20261030.md for scope and reproduction.
