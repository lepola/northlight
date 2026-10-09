# Northlight renderer

A Direct3D 9 extension for the World of Warcraft 3.3.5a client: a proxy `d3d9.dll`
(built as `frd9.dll`) that adds lighting, shadows, fog, GI and sky effects. The repository also
holds the offline asset builders (MPQ patches, world cache) and their validation.

The repository holds no game files: every tool that needs game data reads it from your own
3.3.5a client. Licensed under the [MIT License](LICENSE).

The changelog, release notes and validation records are not part of the public source.

## What Northlight adds

Every effect below is on by default and is drawn on top of the game's own frame.

- **Sun and moon.** Its own sun and moon discs on an orbit that follows the game's clock; the game's
  sun and moon billboards are hidden. The sun glows in the game's own sun colour for the zone and
  time, and a veil softens trees, towers and ridges in front of it; fog and horizon haze take the
  same hue toward the sun.
- **Shadows.** Sun and moon shadows in two cascades (about 48 and 192 yards around the player), with
  a cached static layer for terrain, buildings, trees and props, and distant terrain shadows out to
  928 yards (up to 4096 in the zones listed in `shadow-range-profiles.ini`). Characters, creatures, mounts,
  doors, ships and other moving objects cast shadows too. The game's baked terrain shadows are
  replaced, its round blob shadows under characters are kept, fainter (`BlobShadowStrength`), and where the sun is blocked the
  game's painted sunlight is removed smoothly, without facet steps.
- **Global illumination.** Sky light and bounced light from probes ray traced against the world
  geometry on a background thread, about 76 yards around the camera (52 in the Balanced and Performance
  presets); characters add their own bounce and occlusion (Quality preset).
- **Ambient occlusion.** Screen-space AO with contact shading and a light bloom.
- **Fog and air.** Volumetric sun and moon light with light shafts through the shadows; soft haze on
  the far landscape and the lowest sky in the game's own fog colour; regional ground fog in forests,
  wetlands and basins, denser at night, derived from the map.
- **Lamps.** Up to 32 nearby lamps, lanterns, braziers and fires light the ground and walls around
  them and glow in the fog. In direct sun lamps dim. Lamp shadows are off by default (`PointShadows=1`
  turns them on): only lights inside buildings cast faint shadows, at dusk and night; street lamps,
  lanterns and torches never do.
- **Lighting art layer.** An MPQ patch (`patch-z`) built from your client's own `Light*.dbc`: retuned
  outdoor clear-weather light and fog colours, warmer Mulgore, denser Stormwind day fog. Storm weather
  gets darker light and fog bands. Sky models
  that paint their own sun or moon into the clear-weather sky lose it.
- **Weather.** A heavy-rain look: procedural rain and snow textures in the art layer, darker storm light
  and fog bands, and Northlight's own haze, fog, light shafts, sun and moon, shadows, GI and lamps follow
  detected rain or snow (F10 turns it off). Detection reads only the
  textures the art layer ships, no game memory, so it needs the Northlight art layer (`Weather`, `RainFog`). The game's rain streaks are drawn alpha-blended so they stay crisp instead of taking the background's colour, and the game's mist puffs, which darkened night and storm rain into black balls, are left out while it rains; snow and sand storms keep them.
- **Fog clouds.** Low fog banks that drift with the wind, thicker and faster in rain, few and faint in clear
  weather and only in forest and grass zones then (its own half-resolution pass; `FogClouds`, `FogCloudDensity`). Ctrl+Shift+F7 (fog) and F10 turn
  them off with the rest; the Performance preset has them off. The fog is also smoothed over frames so lamp glows
  and shafts do not shimmer while moving.
- **Water.** The game's water is drawn unchanged; a liquid mask keeps the relighting and AO off the
  surface, and fog is measured to the water surface.
- **Settings.** `northlight-quality.ini` has three presets (Quality, the default, Balanced and
  Performance) and about 30 keys for shadows, GI, lamps and haze; `celestial-profiles.ini` sets the
  sun and moon look per zone.
- **Hotkeys** (with Ctrl+Shift): F7 fog and haze, F8 GI, F9 shadows, F10 all effects, F12 debug views
  (shadows, GI, fog volume). On a Mac, first remove macOS's own Control+F7 shortcut.
- **Cost.** Northlight costs frame time, mostly on the game's main CPU thread. Character shadows are
  the largest part in crowds (about 4-5 ms per frame): `ActorShadows=0` in `northlight-quality.ini`
  keeps only the static shadows, and the Balanced and Performance presets trade small details for speed.
  On the graphics card, `GpuBudgetMs` (4 / 3 / 2 ms, 0 = off) keeps Northlight's own measured GPU time near
  the budget: while it is over, the fog clouds take fewer steps in two stages, then the
  fog takes fewer steps and at most 16 lamps light at once; under it the picture is the full one.
  With the command stream (`CommandStream=1`) the game records its graphics calls and a second thread draws them;
  `StreamFramesAhead` (1..3, default 2) sets how many frames the game may run ahead of that thread (1 = as in 0.3.199).
  `ReplayJobs=1` (every preset) runs part of the renderer thread's per-frame CPU work (lamp choice, fog
  clouds, the terrain and character culling of the sun and moon shadows) on 1-4 helper threads with the
  exact same picture; `0` keeps all of it on the renderer thread.
  `StreamFramesAhead` (1..3, default 2) sets how many frames the game may run ahead of that thread (1 = as in 0.3.199);
  with `StreamFrameSkip=1` (default) a thread two whole frames behind skips drawing the older frame (state and uploads still
  apply; frames that render to textures, use occlusion queries or copy the picture are always drawn), so the game does not wait for it.
  `IDirect3DDevice9::TestCooperativeLevel` is answered on the game thread (D3D_OK) while the device is fine, so a client that calls it
  hundreds of times a frame no longer waits for the draw thread on each call (0.3.204); a lost or not yet reset device is reported by the
  real call, up to `StreamFramesAhead` frames after the loss. The `CSTREAM` log line shows the locally answered calls per frame as
  `coop=` (next to `answered=`); `synced=` counts only the calls that waited, which now includes `TestCooperativeLevel` calls made
  while the device is lost. With `Diagnostics=1` the log also names the module that calls it once (`CSTREAM TestCooperativeLevel caller=`)
  and the `CSTREAM` line gains `split[per frame]:` with the game thread's time in buffer/texture locks (`lockMs`, `lockMB` queued), in recording generated calls (`recordMs`, sampled 1 call in 16), in snapshot capture (`snapMs`) and in the Present bookkeeping (`presentMs`), all without waits (0.3.204).
  The shadow allowance for large dynamic buffers holds two of them (up to 24 MiB each, 36 MiB in total) and is released under memory pressure. Dynamic buffers in the default pool, large or not (the CPU-skinned vertex buffers), are handed to the draw thread without a copy: an unlock of 4 KiB or more with DISCARD or NOOVERWRITE only records a reference into the buffer's game-side memory, and a DISCARD while the draw thread still reads the old contents switches to another copy of the buffer (a ring of `StreamFramesAhead`+2 slices per buffer, more (up to 12) for a buffer that DISCARDs several times a frame, counted in the large allowance or the regular shadow cap, at most twice that cap, none added under memory pressure, unused ones freed after 60 frames); with Diagnostics on a separate `CSTREAM zerocopy[per frame]` log line next to the `CSTREAM` line shows the zero-copy unlocks, renames, rename and write waits, the ring and retired memory and why a DISCARD/NOOVERWRITE unlock of a dynamic buffer did not go zero-copy.
  `ContactAO=1` (default, every preset) keeps the screen-space contact shading in creases and corners; `0` skips it and its denoise pass (about 0.5 ms of GPU time) and leaves the bloom as it is.
- **Font mods.** Lexara, TweakWoW2 (HD Font on) and the AwesomeWotLK MSDF fork replace the game's UI shaders,
  which Northlight uses to find where the world ends and the UI begins. Northlight then learns the mod's UI draws
  instead: effects start about 2 seconds after entering the world, and the log shows
  `EFFECT boundary fallback active: UI shaders replaced by another module`.
- **Platforms and install.** macOS with WoWSilicon (preloaded as `mods/d3d9.dll`) and Windows (a
  game-folder `d3d9.dll` on the bundled DXVK 3.1.1, with DXVK 2.7.1 as the `dxvk2` alternative backend for AMD RX 5000/6000 and
  older drivers (`Install.cmd --backend dxvk2`; on a driver DXVK 3 does not support, install with `--backend dxvk2`, Northlight never switches by itself; a reinstall without `--backend` keeps `dxvk2` or `native`), the system D3D9, or an existing `d3d9.dll`). The
  installer never writes `wow.exe`. It builds the world cache (terrain, models, lamps and fog
  regions) and the lighting art layer from your own client on your machine, about 10-40 minutes and
  at least 8 GB of RAM; nothing from the game is shipped. The packages bundle their own Python and StormLib, and uninstall restores every change.

## Layout

| Path | Contents |
|---|---|
| `src/<group>/` | DLL sources, grouped: `proxy` (the D3D9 proxy and device mirror), `core` (settings, memory, logging, profiling), `world`, `replay`, `shadows`, `gi`, `lights`, `sky`, `water`, `gamedata` (tables derived from the game's MPQs), `generated` (headers written by the generators and shader compilers; do not edit) |
| `shaders/` | `*.hlsl` and their `*-shader-build.json` manifests; `shaders/compiled/` holds the compiled `<Entry>.bin` and `.bin.asm` |
| `scripts/` | `build_renderer.py`, `build_environment.py`, `generate_*.py`, `run_tests.py`, `check_layout.py`, `pe_normalized_hash.py`; `scripts/shaders/` holds the shader compilers (`compile_*_shaders.py`, `compile_shaders.cpp`, `disassemble_*.cpp`) |
| `renderer/` | `windows-package/`, `mac-package/`, the player installer `northlight_install.py`, the pipeline and packaging tools (`world_*_builder.py`, `extract_*.py`, `migrate_mac_proxy.py`, `build_packages.py`, `package-pins.json`, ...) and the built `frd9.dll` |
| `tests/` | test runners (`test_*.py`), native tests (`test_*.cpp/.h`), release validators (`validate_*.py`, `verify_*.py`) |
| `tests/support/`, `tests/fixtures/` | d3d9.h/windows.h shims for native builds; the archived baselines (old sources of this project, digests) that tests compare against. Real client data (shaders, doodad placements) is read from your client by `tests/client_fixtures.py`, never stored |
| `northlight_paths.py` | the only place that knows where the client, toolchain and outputs are |
| `client-config/` | tracked copies of the client-root profile `.ini` files |
| `*.py` (top level) | the art layer (MPQ patch) builder `build_art_layer.py` and its steps, `mpq.py`, `client_archives.py`, `renderer_status.py` (install/uninstall on macOS) |
| `out/` | build and test output (ignored) |

`tools/` and `backups/` are local and untracked.

## Requirements

- macOS on Apple Silicon is the supported development host. Linux may work with zig, clang and
  Wine on `PATH`, but it is untested. Windows is a target only: the DLL runs there, but the
  development scripts do not.
- Python 3.9 or newer; only the standard library is used.
- [zig 0.15.2](https://ziglang.org/download/) to build the DLL (it cross-compiles to 32-bit Windows).
- clang++ and c++ (Xcode command line tools) for the native tests.
- Optional:
  - a WoW 3.3.5a client (game data, world cache);
  - Wine, for the shader compilers only (`scripts/shaders/compile_*_shaders.py`);
  - StormLib, for reading MPQs;
  - the external archive and `backups/`, for differential tests against old versions.

## Build

```sh
python3 scripts/build_renderer.py           # -> renderer/frd9.dll
python3 scripts/pe_normalized_hash.py renderer/frd9.dll
```

The build first runs the layout check (nothing back at a pre-move path, unique basenames). It
regenerates the forwarding headers in `src/generated/`. It checks every `shaders/*.hlsl` against its
`*-shader-build.json` and refuses to build if a shader changed without a recompile. The raw
DLL bytes depend on the build directory, because the PDB paths feed the build ID. Compare builds
with `pe_normalized_hash.py`, which zeroes only those fields.

After editing a shader, recompile it (Wine, compiler only; nothing touches the game):

```sh
python3 scripts/shaders/compile_world_shaders.py   # or compile_shaders, compile_water_shaders, ...
```

A compiler writes `shaders/compiled/<Entry>.bin` and `.bin.asm`, `src/generated/*_compiled_shaders.h`
and the `shaders/*-shader-build.json` manifest. `build_environment.py` keeps the Wine prefix
outside the client folder.

## Test

```sh
python3 scripts/run_tests.py                     # all tests/test_*.py, 4 at a time, nice 15
python3 scripts/run_tests.py test_replay_*       # by pattern; validate_*/verify_* only when named
python3 scripts/run_tests.py --require client    # a missing client FAILs instead of SKIPping
python3 scripts/run_tests.py --record 0.3.158    # write reports to renderer/records/validation-0.3.158
```

Reports go to `out/test-output/<run>/<test>/`. Nothing is written into the tree unless you
pass `--record` (`renderer/records/` is ignored by git, and no test reads from it). A test
declares what it needs on one line under the shebang:

```python
# northlight-test: requires=cxx,client timing
```

| Tag | Meaning |
|---|---|
| `cxx` | host `clang++`/`c++` |
| `client` | a WoW client (see `NORTHLIGHT_CLIENT`) |
| `zig`, `wine`, `stormlib` | toolchain pieces (see below) |
| `stormlib-src` | the pinned StormLib source tree in `tools/StormLib-master` (`scripts/build_stormlib.py`) |
| `world-cache` | the client's world cache, `<client>/world-cache` (`scripts/install_world_cache.py`) |
| `dll` | a built `renderer/frd9.dll` (build first) |
| `backups`, `archive` | old renderer tarballs / the external archive, for differential tests |
| `base` | `BASE=<pristine older renderer tree>` for a release gate |
| `timing` | CPU-time budgets: the test runs alone after the others |
| `slow` | started first |
| `manual` | needs arguments; runs only when named |
| `known-fail=<reason>` | last on the line; fails at the current version for a known reason ([tests/KNOWN_FAILURES.md](tests/KNOWN_FAILURES.md)): still runs, reported XFAIL (XPASS if it passes), does not fail the run |

A test whose requirement is missing is reported as SKIP with the reason. `tests/discovery.txt`
lists the tests the runner must find. After adding a test, run with `--update-discovery`.

With no client, toolchain or archive you can still build the DLL (given zig) and run most
tests, which are pure Python or native C++. Tests that run on real client data read it from your
client through `tests/client_fixtures.py`: the four-bone skin and the one-influence shader programs
from the stock archives (`client,stormlib`), and real doodad placements from the world cache
(`world-cache`).

About 50 `tests/test_*.cpp` files have no Python runner: no runner executes them, and they are
built by hand as their docs describe (for example
[tests/support/gpu_profile/README.md](tests/support/gpu_profile/README.md)). The release tool
`tests/verify_packages.py` checks the zips `renderer/build_packages.py` wrote (run it by name).

## Machine configuration

Nothing in the code names a machine path. Each setting comes from, in order, an environment
variable, then `northlight.local.ini` (copy [northlight.local.ini.example](northlight.local.ini.example);
it is ignored by git), then a default. `python3 northlight_paths.py` prints what resolves.

| Variable | Default | Used for |
|---|---|---|
| `NORTHLIGHT_CLIENT` | the repository's parent folder, if it holds `Wow.exe` and `Data/` | client data for tests, validators and pipeline scripts, and the client `renderer_status.py` installs into |
| `NORTHLIGHT_ZIG` | `tools/zig-*/zig`, then `zig` on `PATH` (must be 0.15.2) | DLL build, generators, shader tools |
| `NORTHLIGHT_WINE` | WoWSilicon's bundled Wine, then `wine` on `PATH` | shader compilers only |
| `NORTHLIGHT_STORMLIB` | `tools/storm-build/storm.framework/storm` | `mpq.py` |
| `NORTHLIGHT_TOOLS` | `tools/` | third-party downloads (DXVK, StormLib, zig) |
| `NORTHLIGHT_ARCHIVE` | none | old rollbacks and runtime diagnostics (`<archive>/renderer/`) |
| `NORTHLIGHT_BACKUPS` | `backups/` | old renderer tarballs |
| `NORTHLIGHT_OUT` | `out/` | build and test output |
| `NORTHLIGHT_TEST_OUTPUT_DIR` | set by `run_tests.py` | where a test writes its report |
| `NORTHLIGHT_SHADER_CORPUS` | none | optional captured shader corpus (extra sub-cases) |
| `NORTHLIGHT_DOWNLOADS` | none (then `tools/`) | pinned runtime downloads for `renderer/build_packages.py` (`renderer/package-pins.json`) |
| `NORTHLIGHT_LIVE_CLIENT`, `NORTHLIGHT_STOCK_CLIENT`, `NORTHLIGHT_LIGHTS_STAGE`, `BASE` | none | a few specific tests; see their docstrings (`NORTHLIGHT_STOCK_CLIENT`: a stock 3.3.5a client for the identity and variant tests) |

## Conventions

- **Basenames are the include namespace.** C++ includes use bare names (`#include "world_gi.h"`),
  each `src/<group>/` is one `-I` (`northlight_paths.include_flags()`), and Python finds sources the
  same way, with `northlight_paths.src('world_gi.h')` (sources and shaders) or `tracked()` (also
  scripts and tests). Directories only group files: moving a file between groups needs no edit.
  Every basename must be unique; `scripts/check_layout.py` enforces this.
- A test starts with the tag line and these two lines, then uses `fp.src(...)`,
  `fp.test_include_flags()`, `fp.output_dir()` and so on:
  ```python
  import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
  import northlight_paths as fp
  ```
- Tests never write next to the sources. They write to `fp.output_dir()`.
- `.gitattributes` has `* -text`: hash gates compare raw bytes, so keep LF line endings.
- The pre-commit hook rejects files over 5 MB and game or binary payloads, and it runs
  `check_layout.py`. Enable it in each clone with `git config core.hooksPath githooks`.
- `git clean -x` also deletes the ignored local folders (`tools/`, `backups/`, `out/`).

## Install on macOS (WoWSilicon)

With the game closed:

```sh
python3 <repository>/renderer_status.py status
python3 <repository>/renderer_status.py on      # or: off
python3 <repository>/renderer_status.py on --client <client folder>
```

The client is `--client PATH`, else `NORTHLIGHT_CLIENT`, else `northlight.local.ini` `[paths] client`,
else the repository's parent folder if it holds `Wow.exe` and `Data/`; with none of these the tool
stops with an error that names them.
`on` installs `renderer/frd9.dll` as `mods/d3d9.dll`, and `off` restores the recorded
transaction. Add `--dry-run` to preview. The Windows package template is in
`renderer/windows-package/`.
