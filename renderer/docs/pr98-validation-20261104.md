# PR #98: post-consolidation validation, November 4, 2026

PR: https://github.com/lepola/northlight/pull/98. This follow-up fixes failures exposed by running the combined suite. It does not change the history-preserving consolidation method.

## GitHub and history

- Checked head before this follow-up: `1b49fad90afa1c568676842f0784db6a6500c369`; upstream base: `e19ffee601acbad2b991917109bcb6d8b2f7d5ca`.
- GitHub reports `MERGEABLE`. Its simulated merge `6cccbb2c18b33c565bb4aea459187edd742bd809` has the same tree as that head; upstream main is already an ancestor.
- Both repositories have zero GitHub Actions workflows. The PR has zero check runs and zero commit statuses. There is no remote green CI result to claim. `BLOCKED` comes from `REVIEW_REQUIRED`: the main ruleset requires one approval, not a missing required status check.
- All 17 original PR heads, all 45 restored original commits and their adaptation commits remain reachable. All source paths and 123/123 recorded implementation snippets still exist after the fixes.
- The history-only merge `ace81c1c46a72dac2b57b03744a2bbb96b11af5d` retains the pre-consolidation tree `0dbc6a4f601007e2a6470a92ab341ac4a7224ced`. These follow-up fixes are ordinary descendant changes; no original commits were rewritten.

The [consolidation manifest](pr-consolidation-20261104.json) records the original heads and per-commit adaptations. Local detailed recheck: `out/pr98-ci-20261104/validation-summary.json`.

## Runtime failures and fixes

| Failure | Root correction | Regression evidence |
| --- | --- | --- |
| Three-frame snapshot pool exceeded its existing 1 MiB assertion after native-light/attachment payload growth | Bound capacity by bytes: 13 current snapshots (999,960 bytes), still enough for all 12 in-flight triggers plus one spare | Pool exhaustion/reuse at frame settings 0, 1, 2, 3 and 9; original command-stream 1 MiB assertions retained |
| Direct depth-surface dispatch bypassed `Device::SetDepthStencilSurface`, including deferred material capture before depth changes | Remove only this setter from the generated direct-dispatch allowlist; retain asynchronous replay through Device | Bind/unbind checks assert hook order, replay thread and no extra synchronization. Restoring the old generated branch in an isolated negative control aborts at the new hook assertion |
| Pumped EVENT completion was not published to the completion cache, allowing redundant background polls | Publish terminal results only for the matching generation/epoch and a valid result payload | Original exact poll-count/deadline assertions; terminal-generation assertion; fallback and negative controls; ASan/UBSan/TSan |
| Near-shadow work could consume more than its assigned share while some unfinished packets were excluded from normal rotation | Apply the half-share to the actual operation/vertex budget, restore the phase limits, and retain every unfinished packet in rotation | Unmodified `test_replay_bounds_job.py`: six sun/moon clock-step stress cases, original coverage/latency/operation limits, optimized build, ASan/UBSan/TSan and real-clock microbenchmark |

The shadow stress fixture previously left 101 packets uncovered after 300 frames where at most four heavy packets are allowed. The final implementation passes those original limits. Global operation and time budgets were not increased. The depth change adds no new GPU pass or synchronization. No whole-game FPS claim is made from offline timings.

## Test fixture repairs

The combined implementation had outgrown several frozen interfaces and textual wiring checks. Repairs retain their original behavior, precision, allocation, ownership and performance assertions:

- Extend worker/mirror/D3D stubs for attachment records, cancellation, normal capture and the existing sampler enum. Add cancellation and normal-capture route checks.
- Extract embedded C++ harnesses at `int main(`, accommodating the existing argument-taking main functions. Generate mirror forwarding code into a temporary file and compare exact bytes instead of rewriting a live header during parallel tests.
- Adapt the frozen scene assembler only to the existing FGS lamp ownership format. Its geometry, cropping, ordering and allocation algorithms remain frozen; 16 independent valid/invalid tag cases check both decoders.
- Keep both complete 20-placement client fixture hashes. The current client uses the same pinned sign model at placement `0x116f60000027a` instead of `0x116f600000272`; all matrices and model identities remain checked.
- Pin Composite to the already-reviewed transparent-coverage change `f24adeeb343e0cf39f60fa64a2e055001c88939f`, rather than the obsolete pre-fix binary. Rebuild the Northlight-owned integrated performance baseline using the existing generator, keeping its independent optimization reversions and the `1e-5` paired-output tolerance.
- Update frozen light-selection policy inputs and shader slot costs from the current independent manifest; retain historical ranking algorithms and randomized equivalence checks. Add a check that both cost inputs match that manifest.
- Update exact wiring/site counts for integrated features, versioned-cache tests and the current startup identity. No wildcard acceptance, timing increase, disabled assertion, new skip or expected-failure annotation was introduced.
- Remove pre-existing CRLF/trailing-whitespace errors in 16 files without changing their non-whitespace content. `git diff --check origin/main` passes.

## Local validation

Across the standard 518 discovered scripts, the full platform runs plus focused reruns finish with **489 distinct PASS, 0 FAIL, 29 SKIP, 0 XFAIL**. This is cumulative coverage, not a claim that one final invocation ran every script on one operating system.

| Run | Result | Local results under `out/pr98-ci-20261104/` |
| --- | --- | --- |
| Full Windows suite | 302 PASS, 216 platform/manual/prerequisite SKIP | `final-windows/test-output/20261104-164019/results.tsv` |
| Linux host-C++ sweep, followed by repaired-case reruns | All 187 non-manual C++ scripts PASS cumulatively | `linux-runs/20261104-163933/results.tsv` and later reports |
| Affected Windows contracts and offscreen GPU fixtures | 33 PASS | `final-affected/test-output/20261104-170125/results.tsv` |
| Final EVENT express and shadow bounds scheduler | 2 PASS, including sanitizer variants | `linux-runs/20261104-171457/results.tsv` |
| Final shadow wiring, snapshot, device mirror, snapshot replay and water-mask contracts | 6 PASS | `linux-runs/20261104-171613/results.tsv` |
| Final complete command stream and idle attribution | 2 PASS | `linux-runs/20261104-172044/results.tsv` |
| Final Windows pipeline, EVENT reuse/budget, query latency, shadow crowd and DLL identity | 6 PASS | `final-core/test-output/20261104-172510/results.tsv` |

The 29 remaining skips are 27 pre-existing manual scripts and two unavailable prerequisites: `test_contact_normal_endpoints.py` needs an external historical renderer archive; `test_dll_memcmp.py` needs LLDB/objdump. They are not counted as passes. No new skip annotations were added. Automated city-lamp, smoke, equipment-energy, material, AO and production-shadow GPU checks pass in the Windows runs.

Representative commands, with paths configured through `northlight.local.ini` or environment variables:

```powershell
python scripts/check_layout.py
python scripts/run_tests.py --jobs 4
python scripts/run_tests.py test_pipeline_bindings_cpu test_event_terminal_reuse test_event_budget test_query_poll_latency test_shadow_crowd test_build_source_identity --jobs 4
python scripts/build_renderer.py
git diff --check origin/main
```

On Linux, select the 187 discovered non-manual scripts requiring `cxx`, then run them through `scripts/run_tests.py --require cxx --jobs 4`; timing-tagged fixtures run serially. Use Zig 0.15.2, LLVM libc++, StormLib and the read-only client assets required by their tags. Windows native/GPU checks and production compilation ran outside the restricted execution sandbox as required by the project instructions.

## Build and scope

- Production DLL rebuilt with `scripts/build_renderer.py`, including the compiler preflight and all shader freshness gates. Its embedded source identity matches the final sources.
- Source identity: `3ebb2f1923c57c45c8eeed06da3e5092912e9fd3d2c1e98156c3cef997b1cdb0`.
- DLL SHA-256: `65d97544ca83cc92415409c8101c99846632c7d4375c591e1781f94129e73f07` (9,918,464 bytes).
- `Wow.exe` SHA-256 remains `99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`. The installed game DLL was not replaced.
- This turn validates and publishes source changes. Installer `0.3.217-test.20261104.3` predates these follow-up fixes; no new installer was produced here.
