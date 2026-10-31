# Tests

About 180 `tests/test_*.py` files. Each is a plain script that exits non-zero on failure. Many build
a native C++ harness (`tests/test_<name>.cpp`, `tests/*.h` or an inline string) against the
production headers in `src/` and `shaders/`. The whole suite takes about 13 minutes: run it for a
release or to verify main, not after every edit.

## Run only what a change affects

```sh
python3 scripts/affected_tests.py                # list the tests covering your changes (git: working tree, index, commits since main, new files)
python3 scripts/affected_tests.py --why          # ... and how each one reaches a changed file
python3 scripts/affected_tests.py src/proxy/spell_layer.h shaders/effects.hlsl   # for named files
python3 scripts/affected_tests.py --run          # run exactly those tests with scripts/run_tests.py
python3 scripts/run_tests.py test_spell_layer    # one test by name
python3 scripts/run_tests.py 'test_replay_*'     # a name pattern
python3 tests/test_spell_layer.py                # one test directly (it prints its checks)
```

Which tests cover which file is derived from the tests themselves, not kept in a table:

- a test covers a file whose name appears in the test's `.py` (`fp.src('quality_settings.h')`,
  `'world_effects.hlsl'`, a `#include "x.h"` in a harness string) or in the native files it builds
  (`test_<name>.cpp`, `tests/*.h`);
- the `#include "..."` chain of what the test compiles is followed, so a change to `b.h` selects a
  test that compiles `a.h` when `a.h` includes `b.h`;
- a test that scans a tree (`fp.sources(...)`, or a `glob`/`rglob`/`iterdir`/`os.walk` over
  `fp.SRC_DIRS`, `src/<group>`, `fp.SHADERS`, ...) is selected by any file under it with a matching
  suffix; a walk written in a way the tool cannot place is missed, so name the directory plainly;
- a changed test selects itself;
- `northlight_paths.py`, `scripts/run_tests.py`, `scripts/check_layout.py` and `tests/support/**`
  are shared: the tool tells you to run the full suite;
- a changed file no test names is reported on stderr: nothing guards it.

The selection errs towards too many tests, never too few. The heuristic is documented at the top of
`scripts/affected_tests.py` and checked by `tests/test_affected_tests.py`.

## The full suite

```sh
python3 scripts/run_tests.py                     # all tests/test_*.py, 4 at a time, nice 15
python3 scripts/run_tests.py --require client    # a missing client FAILs instead of SKIPping
python3 scripts/run_tests.py --record 0.3.158    # reports go to renderer/records/validation-0.3.158
```

Reports go to `out/test-output/<run>/<test>/` (logs in `out/test-output/<run>/logs/`). Nothing is
written into the tree unless you pass `--record`. `validate_*.py` and `verify_*.py` run only when
named; `verify_packages.py` checks the zips `renderer/build_packages.py` wrote.

## Tags

A test declares what it needs on one line within its first 30 lines:

```python
# northlight-test: requires=cxx,client timing
```

| Tag | Meaning |
|---|---|
| `requires=cxx` | host `clang++`/`c++` |
| `requires=client` | a WoW client (see `NORTHLIGHT_CLIENT`) |
| `requires=zig`, `wine`, `stormlib` | toolchain pieces (see the Machine configuration section of the top README.md) |
| `requires=stormlib-src` | the pinned StormLib source tree in `tools/StormLib-master` (`scripts/build_stormlib.py`) |
| `requires=world-cache` | the client's world cache, `<client>/world-cache` (`scripts/install_world_cache.py`) |
| `requires=dll` | a built `renderer/frd9.dll` (build first) |
| `requires=backups`, `archive` | old renderer tarballs / the external archive, for differential tests |
| `requires=base` | `BASE=<pristine older renderer tree>` for a release gate |
| `timing` | CPU-time budgets: the test runs alone after the others |
| `slow` | started first |
| `manual` | needs arguments; runs only when named (`affected_tests.py --run` leaves it out) |
| `known-fail=<reason>` | last on the line; fails at the current version for a known reason (listed in [KNOWN_FAILURES.md](KNOWN_FAILURES.md)): still runs, reported XFAIL (XPASS if it passes, then remove the tag), does not fail the run |

A test whose requirement is missing is reported SKIP with the reason; `--require <name>` turns that
into a FAIL.

The StormLib source is reproducible from `renderer/package-pins.json`: download its
`source_archive_url`, verify `source_archive_sha256`, and extract `source_archive_root` as
`<NORTHLIGHT_TOOLS>/StormLib-master` (default: `tools/StormLib-master`). The tree hash uses
case-sensitive path-component ordering on every host. The build test retains the original
Windows, macOS and Linux binary hashes, ABI checks and empty-cache rebuild comparisons.
Run the complete build test on a POSIX host with Zig 0.15.2; on WSL, use a short native
`NORTHLIGHT_OUT` path for its cold compiler caches. This requires no client settings changes.

## MPQ-backed camera rotation reproduction

Prepare a private fixture from the active client archives and a recorded camera interval:

```sh
python scripts/prepare_rotation_fixture.py --log /path/to/northlight-renderer.log --start-tick 128064984 --end-tick 128068265 --terrain-radius 1277 --output out/rotation-fixture
python tests/validate_mpq_rotation.py --fixture out/rotation-fixture --output out/rotation-comparison --rounds 3
python scripts/run_tests.py test_terrain_pressure_policy.py
```

The ticks above select the investigated 23 October orbit; choose the bounds of the recorded
`WORLD camera` samples in another log. The radius is the client farclip for that run, not a
changed render distance. Output must be new or empty. The generator opens the MPQs read-only,
uses the existing archive-priority resolver and static-world builder, and writes original ADTs,
exact terrain heights/holes, an independent FGS3 corpus, camera samples and hashed provenance.
Neither preparation nor replay starts the game or writes installed settings/world-cache files.
Comparisons reject missing/unsupported inputs and unreadable archives; an unreadable override
must not silently substitute a lower-priority asset. Every generated output is hash-checked.

The Windows replay compiles both the frozen delivered cache/world-cap setter and current code.
It reproduces the logged demand-only trim and separately tests genuine low-address-space
pressure. It covers stable revisions, rewritten vertices, rewritten vertices plus indices, and
a separately labelled synthetic two-layout buffer ring. All emitted triangle positions must
match the current MPQ inputs exactly. Fixed-layout warm demand-only misses, evictions and
rebuilds must disappear; the synthetic ring may create new cache identities. Rewrites still
require exact content validation and reads, which are reported separately. Changed XYZ and
cyclic index mutations must rebuild; deliberately stale revision tokens must fail specifically
at the independent triangle oracle. Genuine-low-memory behavior must remain unchanged.
Alternating runs report CPU capture time and deterministic work counts, without a game-FPS claim.

This is a CPU reproduction using fake D3D buffers and an explicit 110-degree candidate cone.
The original D3D draw stream, game occlusion/LOD and driver waits are unavailable. Recorded
camera motion and actual MPQ geometry are used; this does not certify live visual performance.
Keep the generated game assets and raw logs outside Git. The automatic pressure-policy test
also executes production wiring, admission rules and negative controls without private data.

The same full fixture can exercise worker preparation under real x86 address-space pressure:

```sh
python tests/validate_scene_entry_reclaim.py --baseline-ref de0d60f881ceae554ab319ce84275063c844f6df --fixture out/rotation-fixture --camera-log /path/to/northlight-renderer.log --output out/entry-comparison --rounds 3
python scripts/run_tests.py test_scene_entry_reclaim.py test_scene_entry_reclaim_inputs.py --require windows-exe,zig --jobs 1
```

Use a full fixture containing the selected cameras' static models and tiles, rather than a
terrain-only extraction. The stand holds reserve-only ballast during builds, uses the actual
production admission sampler and keeps the previous BVH pinned. It checks exact scene,
material, shadow-plan, page, ray and probe results; retained immutable allocations stay charged.
Run `--no-pressure` in a separate output folder for the warm control. `--check-inputs-only`
verifies the complete hashed corpus without compiling. Invalid inputs, source drift and reused
nonempty output folders fail. Per-translation-unit compiler dependencies, commands, executable
hashes, reclaim-inclusive timings, setup and process wall time are recorded separately.

Shader experiments have a separate hidden-device differential stand:

```sh
python tests/validate_regional_fog_cache_gpu.py --baseline-ref de0d60f881ceae554ab319ce84275063c844f6df --mpq out/rotation-fixture --fog-metadata /path/to/world-cache/fog --output out/fog-comparison --phase compile
```

Repeat with `--phase fixture`, then `--phase check --backend C:/Windows/SysWOW64/d3d9.dll`
and `--phase benchmark --backend C:/Windows/SysWOW64/d3d9.dll`. Additional `--backend` paths
can select a standalone DXVK DLL without changing the game. The fixture uses production-built
regional fields and MPQ BVH depth, recorded cameras plus explicitly labelled terrain-relative
controls. It requires non-neutral grounded coverage and exact FP16/FP32 output equality.
Synthetic boundary, mask and format controls complement the static MPQ scenes. Projection,
lighting, animation and resampling limits are recorded; this is not a live-game image capture.
Run compiler, GPU and timing jobs serially to avoid contention corrupting comparisons.
Use `--source /path/to/candidate/world_effects.hlsl` with its companion
`auto_bump_normal.hlsl` to replay a saved experiment without editing production shaders.
Bytecode-identical entries are explicitly skipped. Benchmarking a changed entry requires the
complete quality check on the same backend. Resume rejects changed arguments, sources,
executables, shader inputs and fixture inventories; legacy reports require a fresh compile.
The source hash set is a conservative project superset, not per-TU dependency discovery.

## Adding a test

1. Create `tests/test_<name>.py` with `#!/usr/bin/env python3`, a `# northlight-test:` line, a
   docstring saying what it protects, then
   `import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root`
   and `import northlight_paths as fp`. Find sources with `fp.src('name.h')`, native harness flags
   with `fp.test_include_flags()`, report files with `fp.output_dir()`.
2. Run `python3 scripts/run_tests.py --update-discovery` so `tests/discovery.txt` (the list of tests
   the runner must find; a deleted or unlisted test stops the run) includes it.
3. Name the source files the test protects in the test (that is how `affected_tests.py` finds it).
   Basenames of sources, shaders, scripts and tests are unique (`scripts/check_layout.py`).

About 50 `tests/test_*.cpp` files have no Python runner: they are built by hand as their docs
describe (for example [support/gpu_profile/README.md](support/gpu_profile/README.md)).

## What a test may pin

- A text pin (a grep of source) checks an identifier or the order of calls, not prose or
  formatting, and carries a one-line comment saying what it protects, so a failure tells the
  developer what to keep or where to update the pin.
- Tests do not read the repository `README.md`. Player-facing setting documentation is checked only
  in `renderer/windows-package/northlight-quality.ini` and `README.txt` by
  `tests/test_quality_docs.py`.
- A known failure is tagged `known-fail=` and listed in [KNOWN_FAILURES.md](KNOWN_FAILURES.md), not
  left red.

CPU fixtures that execute Windows binaries declare `windows-exe`; offscreen D3D9 tests declare `windows-gpu`. Binary inspection tests declare `lldb` and `objdump`.

The replay-budget fairness fixture requires LLVM libc++ (`libcxx`) to match the renderer runtime; its seeded distributions differ with libstdc++. On Linux install `libc++-dev` and `libc++abi-dev`. Historical Apple Clang byte hashes run on macOS; the portable plan/worker equality checks run on every supported host.

`test_world_local_lights.py` always checks synthetic cache formats and transforms. Pass `--cache /path/to/lights` to additionally validate a cache generated by the branch under test; an installed cache may belong to a different schema.
