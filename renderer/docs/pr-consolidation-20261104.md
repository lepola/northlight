# Consolidation of the open renderer PRs — 2026-11-04

Retain [PR #98](https://github.com/lepola/northlight/pull/98) as the single review
entry point for all 17 PRs below. All were authored by `aphelion105` and targeted
`lepola/northlight:main`. Five topic PRs (#92, #94, #96, #97 and #111) had the
same head; #112 was already an ancestor of the complete integration.

## History preservation

The 10 remaining topic heads contained 45 original commit objects that were
not ancestors of the integrated branch. Their changes had already been ported.
The [preceding integration audit](publication-audit-20261104.md) and its feature
evidence were rechecked against the current source: 43 feature/test commits and
two upstream-reconciliation merges. The restored #113 transition is verified at
`155ae2f7926b2243dbcac3b850f5e499def4789d`, replacing that audit's initial gap.

History merge `ace81c1c46a72dac2b57b03744a2bbb96b11af5d` has the previous integration and all 10
topic heads as parents. It retains exactly tree `0dbc6a4f601007e2a6470a92ab341ac4a7224ced`
from `4f7f490dec13237a970d5f31be447913ea8232ca`. Every original PR head and all its
ancestors are now reachable from #98. Original SHA values, author metadata and
commit messages remain unchanged. There is no squash, rebase or branch deletion.

The merge deliberately retains the tested integrated implementations, including
the latest lamp fixes, rather than reapplying older standalone API variants.
Only this audit document and its [machine-readable manifest](pr-consolidation-20261104.json)
are added afterward. No renderer, shader, test or installer input changes.

## PR map

The head column records each PR before consolidation. Closed topic descriptions
link to #98; their discussions and source branches remain accessible.

| PR | Topic | Original head | Added original commits |
| --- | --- | --- | --- |
| [#76](https://github.com/lepola/northlight/pull/76) | Preserve native vegetation alpha and add bounded grass shadows | [`6cd7b1cd81b6`](https://github.com/aphelion105/northlight/commit/6cd7b1cd81b6dffbc14e2ef01021a667d195b9b0) | 6 |
| [#86](https://github.com/lepola/northlight/pull/86) | Avoid NVIDIA present-wait stalls and retain the DXVK 3 colour workaround | [`1f270b99897c`](https://github.com/aphelion105/northlight/commit/1f270b99897cb7d517a19cc6fde3f98554a5c855) | 2 |
| [#92](https://github.com/lepola/northlight/pull/92) | Review complete renderer integration: local-source shadows | [`25db9c6fa641`](https://github.com/aphelion105/northlight/commit/25db9c6fa641f0b51f7af8b8427b7fd3bdbe97f3) | 0 |
| [#94](https://github.com/lepola/northlight/pull/94) | Review complete renderer integration: HDR presentation | [`25db9c6fa641`](https://github.com/aphelion105/northlight/commit/25db9c6fa641f0b51f7af8b8427b7fd3bdbe97f3) | 0 |
| [#96](https://github.com/lepola/northlight/pull/96) | Review complete renderer integration: diagnostics | [`25db9c6fa641`](https://github.com/aphelion105/northlight/commit/25db9c6fa641f0b51f7af8b8427b7fd3bdbe97f3) | 0 |
| [#97](https://github.com/lepola/northlight/pull/97) | Review complete renderer integration: materials and terrain | [`25db9c6fa641`](https://github.com/aphelion105/northlight/commit/25db9c6fa641f0b51f7af8b8427b7fd3bdbe97f3) | 0 |
| [#98](https://github.com/lepola/northlight/pull/98) | Integrate the complete renderer with HDR, persistent lights, shadows and weather | [`4f7f490dec13`](https://github.com/aphelion105/northlight/commit/4f7f490dec13237a970d5f31be447913ea8232ca) | 0 |
| [#109](https://github.com/lepola/northlight/pull/109) | Preserve regional fog during memory deferral and skip empty rendering | [`f266891348b2`](https://github.com/aphelion105/northlight/commit/f266891348b2f3d3bc583c038c3e97568232c30d) | 7 |
| [#110](https://github.com/lepola/northlight/pull/110) | Preserve native sun colors and occlude glare behind horizon geometry | [`1ce599530b63`](https://github.com/aphelion105/northlight/commit/1ce599530b634a4380cb8f314a7b3831d6342ae1) | 3 |
| [#111](https://github.com/lepola/northlight/pull/111) | Review complete renderer integration: EVENT completion | [`25db9c6fa641`](https://github.com/aphelion105/northlight/commit/25db9c6fa641f0b51f7af8b8427b7fd3bdbe97f3) | 0 |
| [#112](https://github.com/lepola/northlight/pull/112) | Reduce thin actor AO halos while preserving contact and environment occlusion | [`92e8d4b482c4`](https://github.com/aphelion105/northlight/commit/92e8d4b482c4a38a4caa4de18668e0297093fe1e) | 0 |
| [#113](https://github.com/lepola/northlight/pull/113) | Crossfade directional shadow cache updates without retaining actor poses | [`369f73590d94`](https://github.com/aphelion105/northlight/commit/369f73590d9430845719c16b8051f48ca653cece) | 3 |
| [#114](https://github.com/lepola/northlight/pull/114) | Expand dynamic emitter coverage, glue light animation and local transmission shadows | [`233bf23981db`](https://github.com/aphelion105/northlight/commit/233bf23981db522d803705e95df1c4b86e967e0d) | 6 |
| [#116](https://github.com/lepola/northlight/pull/116) | Make tooling optional, reuse valid caches, and recognize generated art layers | [`f039e2589da6`](https://github.com/aphelion105/northlight/commit/f039e2589da651c49069b7cd717fe57a2465e8e0) | 5 |
| [#117](https://github.com/lepola/northlight/pull/117) | Bound scene geometry allocations and recover fragmented worker builds | [`20098e6c1598`](https://github.com/aphelion105/northlight/commit/20098e6c1598a998511376e148d79c92c8c277b2) | 5 |
| [#118](https://github.com/lepola/northlight/pull/118) | Add ASSAO quality variants and preserve AO silhouette controls | [`d85f58de8d7e`](https://github.com/aphelion105/northlight/commit/d85f58de8d7eea64f5de67abdc4d2d5e55f1a56d) | 7 |
| [#120](https://github.com/lepola/northlight/pull/120) | Support Unicode game paths in the installer and renderer | [`781097f927be`](https://github.com/aphelion105/northlight/commit/781097f927be98859c9436e03cdf1f35147e33b9) | 1 |

## Verification

- All 17 original PR heads are ancestors of the retained branch.
- The original integration commit set is retained in full; its history expands
  by exactly the 45 original commits plus the merge commit.
- The history merge's entire Git tree is identical to the pre-consolidation tree.
- Renderer source identity remains `1fbbbf318ad5cfa8f3e970d0ba19d737b9cac8e1831bd8729331c5f20cda750c`.
- The verified [Windows installer .3](complete-installer-20261104.3.md) remains
  current; consolidation changes Git history and review metadata only.
- Upstream `main` is not merged or modified by this consolidation.
