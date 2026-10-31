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

## Directional cache transitions (issue 107)

The `test_shadow_transition*.py` tests cover the production controller, projection
math, optional GPU ownership/failures, unchanged baseline bytecode and consumer
wiring. `test_gpu_profile.py` verifies that four history copies fit in one sampled
frame without truncating the remaining effects.

On Windows, `test_shadow_transition_gpu.py --benchmark --out out/shadow-transition`
uses a hidden native D3D9 device and authored data; it needs no client or game
launch. `--backend` selects an explicit 32-bit D3D9 implementation.
`--compile-only` validates/builds without GPU execution. The pinned Microsoft
compiler is required; `--compatible-compiler` explicitly permits the second
verified distribution. `--reuse-binaries` validates provenance before replaying
the same build on another backend.

The GPU oracle checks thin canopy gaps, moving casters, receiver slopes,
independent cascades, actual day/dusk/moon projection frames, depth clipping,
near-threshold comparisons, fog transmittance and exact static-history crops.
JSON evidence distinguishes warm paired timings, first-use costs and the
calculated 60-second update schedule. These are offline measurements, not an
in-game FPS or fresh-driver-cache guarantee.
