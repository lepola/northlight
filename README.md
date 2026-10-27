# Northlight renderer

<img width="50%" height="1058" alt="Näyttökuva 2026-10-10 kello 22 38 31" src="https://github.com/user-attachments/assets/c1996381-8dc2-481c-97c9-e3f1316a08bf" /><img width="50%" height="768" alt="image" src="https://github.com/user-attachments/assets/20cc99fb-216c-4430-901b-35b5b5df30d9" />
<img width="50%" height="768" alt="image" src="https://github.com/user-attachments/assets/e1181ddd-953d-4fd0-a703-eb97e5969ed2" /><img width="50%" height="1696" alt="image" src="https://github.com/user-attachments/assets/7b2c2595-f32b-42b0-bf67-95f7ee9ad570" />



A Direct3D 9 extension for the World of Warcraft 3.3.5a client: a proxy `d3d9.dll`
(built as `frd9.dll`) that adds lighting, shadows, fog, GI and sky effects. The repository also
holds the offline asset builders (MPQ patches, world cache) and their validation.

The repository holds no game files: every tool that needs game data reads it from your own
3.3.5a client. Licensed under the [MIT License](LICENSE).

The changelog, release notes and validation records are not part of the public source.

Download latest release: https://github.com/lepola/northlight/releases

## What Northlight adds

Every effect below is on by default and is drawn on top of the game's own frame.

- **Sun and moon.** Its own sun and moon discs on an orbit that follows the game's clock; the game's
  sun and moon billboards are hidden. The sun glows in the game's own sun colour for the zone and
  time, and a veil softens trees, towers and ridges in front of it; fog and horizon haze take the
  same hue toward the sun.
- **Shadows.** Sun and moon shadows in two cascades (about 48 and 192 yards around the player), with
  a cached static layer for terrain, buildings, trees and props, and distant terrain shadows out to
  928 yards (up to 4096 in the zones listed in `shadow-range-profiles.ini`). Characters, creatures, mounts,
  doors, ships and other moving objects cast shadows too; near the player they get their own finer shadow
  map and a soft filter that is sharp at the feet and softer further from the caster, so character and mount
  shadows are crisp and their edges no longer shimmer while the model animates (`ActorShadowDetail`, 0.3.209). The game's baked terrain shadows are
  replaced, its round blob shadows under characters are kept, fainter (`BlobShadowStrength`), and where the sun is blocked the
  game's painted sunlight is removed smoothly, without facet steps, and the thin light line it left on the ground along wall feet and under characters in shadow is mostly gone: the lighting upsample now follows the scene colour there (0.3.208).
- **Global illumination.** Sky light and bounced light from probes ray traced against the world
  geometry on a background thread, about 76 yards around the camera (52 in the Balanced and Performance
  presets); characters add their own bounce and occlusion (Quality preset).
- **Ambient occlusion.** Screen-space AO with contact shading and a light bloom.
- **Fog and air.** Volumetric sun and moon light with light shafts through the shadows; soft haze on
  the far landscape and the lowest sky in the game's own fog colour; regional ground fog in forests,
  wetlands and basins, denser at night, derived from the map. The sun light comes from the game's sky
  block; the game darkens that light towards the sun (its sun glare) after copying its colours, and a read that lands
  between the copy and the darkening keeps the last darkened colours, so the sun light and its glow in the fog no
  longer flash brighter for a frame in Duskwood (0.3.207). The darkening is predicted from the game's own glare
  value, so matching reads skip the hold; the hold stays for underwater, spell flashes and frames where the client skips the darkening.
  In maps without a world cache (battlegrounds, arenas, and the dungeons and raids that were not added to it), the geometry builder checks the cache once
  per entry and skips the repeated build attempts (0.3.210). These maps get no Northlight shadows, so the game's own
  shadows and its round blob shadows under characters are drawn as the game draws them there. Dungeons and raids are an optional part of the cache:
  the installer asks which of six groups (Classic, TBC and Wrath dungeons and raids; about 0.3-1.3 GB and 1-2
  build minutes each) to build, `--instances` answers for it, and changing the choice never rebuilds the continents.
- **Lamps.** Up to 48 nearby lamps (Quality; `LocalLightLimit`, 8..64), lanterns, braziers and fires light the ground and walls around
  them and glow in the fog. Lamps in view take the slots first, and lamps close together on screen are drawn
  together (0.3.217), so the same `LocalLightLimit` lights more of what you see for less GPU time. In direct sun lamps dim. Lamp shadows are off by default (`PointShadows=1`
  turns them on): only lights inside buildings cast faint shadows, at dusk and night; street lamps,
  lanterns and torches never do. Overlapping lamp glows are capped once over all lamps (0.3.205), so the
  glow no longer flickers when walking or riding past rows of lamps.
- **Lighting art layer.** An MPQ patch (`patch-z`) built from your client's own `Light*.dbc`: retuned
  outdoor clear-weather light and fog colours, warmer Mulgore, denser Stormwind day fog. Storm weather
  gets darker light and fog bands. Sky models
  that paint their own sun or moon into the clear-weather sky lose it.
- **Weather.** A heavy-rain look: procedural rain and snow textures in the art layer, darker storm light
  and fog bands, and Northlight's own haze, fog, light shafts, sun and moon, shadows, GI and lamps follow
  detected rain or snow (F10 turns it off). With `WeatherMemory=1` (default) detection reads the game's own weather state
  from memory (type including sand, intensity; plain reads, nothing is hooked or patched), so the art layer is not required for
  detection; the effects still follow the weather only on frames where the sky is drawn (indoors they fade out). It falls back to
  the textures the art layer ships when that read fails, and `WeatherMemory=0` uses only those and reads nothing from the weather state (they need the Northlight art layer; `Weather`, `RainFog`). Because the game's weather state does not depend on the `weatherDensity` setting, the rain look also comes on when the game's own rain particles are turned off; `Weather=0` (or F10) turns Northlight's weather look off. The game's rain streaks are drawn alpha-blended so they stay crisp instead of taking the background's colour, and the game's mist puffs, which darkened night and storm rain into black balls, are left out while it rains; snow and sand storms keep them.
- **Fog clouds.** Low fog banks that drift with the wind, thicker and faster in rain, few and faint in clear
  weather and only in forest and grass zones then (its own half-resolution pass; `FogClouds`, `FogCloudDensity`). Ctrl+Shift+F7 (fog) and F10 turn
  them off with the rest; the Performance preset has them off. The fog is also smoothed over frames so lamp glows
  and shafts do not shimmer while moving.
- **Water.** The game's water is drawn unchanged; a liquid mask keeps the relighting and AO off the
  surface, and fog is measured to the water surface.
- **Settings.** `northlight-quality.ini` has three presets (Quality, the default, Balanced and
  Performance) and about 30 keys for shadows, GI, lamps and haze; `celestial-profiles.ini` sets the
  sun and moon look per zone.
- **Hotkeys** (with Ctrl+Shift): F7 fog and haze, F8 GI, F9 shadows, F10 all effects, F11 the settings window, F12 debug views
  (shadows, GI, fog volume). On a Mac, first remove macOS's own Control+F7 shortcut. The settings window (Ctrl+Shift+F11) is
  drawn by the renderer over the game; it takes the mouse only over the window and the keyboard only while one of its text
  fields is active. `[Renderer] SettingsOverlay=0` in `northlight-renderer.ini` turns it off; `[Renderer] SettingsOverlayKey`
  (default `Ctrl+Shift+F11`, F1-F12 with Ctrl/Shift/Alt) changes its key.
- **Night brightness.** `NightBrightness` (0..100, default 50; 0 = Northlight's full night darkness) gives back the night ambient light the art layer takes away: 100 restores the original game's night ambient (the art layer scales it by .82 with a cool tint). It is added like light, so moon shadows, lamps and colours keep their contrast, and it assumes the Northlight art layer is installed. It ramps in at dusk and out at dawn; daytime, the sky, water and places without a sky of their own (dungeons, raids) are unchanged.
- **Font mods.** Lexara, TweakWoW2 (HD Font on) and the AwesomeWotLK MSDF fork replace the game's UI shaders,
  which Northlight uses to find where the world ends and the UI begins. Northlight then learns the mod's UI draws
  instead: effects start about 2 seconds after entering the world, and the log shows
  `EFFECT boundary fallback active: UI shaders replaced by another module`. From then on `NameMask` also recognises
  floating names drawn with the mod's shaders (Lexara, TweakWoW2), so they keep their colour too.
- **Platforms and install.** macOS with WoWSilicon (preloaded as `mods/d3d9.dll`), Linux with Wine or Proton
  (the Windows payload; see [Install on Linux](#install-on-linux-wine-or-proton)) and Windows (a
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
| `renderer/` | `windows-package/`, `mac-package/`, `linux-package/`, the player installer `northlight_install.py`, the pipeline and packaging tools (`world_*_builder.py`, `extract_*.py`, `migrate_mac_proxy.py`, `build_packages.py`, `package-pins.json`, ...) and the built `frd9.dll` |
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
  - Wine and Microsoft's redistributable `d3dcompiler_47.dll` 10.0.17134.12 (x64), for the shader
    compilers only (`scripts/shaders/compile_*_shaders.py`): put the DLL in
    `tools/d3dcompiler_47-10.0.17134.12-x64/` (it is not in git; `shader_compiler.py` pins its SHA-256);
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

After editing a shader, recompile it (Microsoft's compiler under Wine, natively on Windows; nothing
touches the game):

```sh
python3 scripts/shaders/compile_world_shaders.py   # or compile_shaders, compile_local_light_shaders, ...
```

A compiler writes `shaders/compiled/<Entry>.bin` and `.bin.asm`, `src/generated/*_compiled_shaders.h`
and the `shaders/*-shader-build.json` manifest. Every shader uses the one toolchain in
`scripts/shaders/shader_compiler.py` (IEEE strict, O3; Wine's builtin compiler is refused).
`build_environment.py` keeps the Wine prefix outside the client folder.

## Test

```sh
python3 scripts/affected_tests.py --run          # only the tests your change affects
python3 scripts/run_tests.py test_replay_*       # by name pattern
python3 scripts/run_tests.py                     # the whole suite (about 13 minutes): release and main verification
```

Reports go to `out/test-output/<run>/<test>/`. Nothing is written into the tree unless you pass
`--record <version>` (`renderer/records/` is ignored by git, and no test reads from it). The test
tags (`requires=`, `slow`, `timing`, `manual`, `known-fail=`), the skip rules, `tests/discovery.txt`
and the rules for writing a test are in [tests/README.md](tests/README.md). With no client,
toolchain or archive you can still build the DLL (given zig) and run most tests, which are pure
Python or native C++.

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
| `NORTHLIGHT_TOOLS` | `tools/` | third-party downloads (DXVK, StormLib, zig, d3dcompiler_47) |
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

## Install on Linux (Wine or Proton)

The Linux package (`Northlight-<version>-Linux.zip`) is for players who run the game under Wine or
Proton (Lutris, Bottles, Steam or plain Wine). **It has not been tested in game yet**; reports are welcome.
The game gets the Windows payload (the game-folder `d3d9.dll` and the bundled DXVK 3.1.1 and 2.7.1 in
`renderer-backends/`), while the installer, the world cache and the art layer run natively on x86_64 Linux
(glibc 2.17 or newer) with the package's own Python (python-build-standalone) and StormLib (`libstorm.so`).

With the game closed, unzip the package outside the game folder and run, in that folder:

```sh
bash install.sh --client ~/Games/WoW          # the folder that holds Wow.exe; asked when left out
bash install.sh --client ~/Games/WoW --backend dxvk2   # DXVK 2.7.1 for drivers DXVK 3 does not support
bash uninstall.sh --client ~/Games/WoW
```

Then make Wine load the game-folder `d3d9.dll` once, with the override `d3d9=n,b`
(`WINEDLLOVERRIDES="d3d9=n,b"`):

- Lutris: Configure > Runner options > DLL overrides: `d3d9` = `n,b` (Lutris builds `WINEDLLOVERRIDES`
  from this list).
- Bottles: the bottle's settings > DLL Overrides (under Advanced in recent versions): `d3d9` = Native, then Builtin.
- Steam/Proton (a non-Steam game): Launch options `WINEDLLOVERRIDES="d3d9=n,b" %command%`.
- Plain Wine: `WINEDLLOVERRIDES="d3d9=n,b" wine Wow.exe`, or winecfg > Applications > `Wow.exe` > Libraries.

Northlight loads its own DXVK from `renderer-backends/`, so the DXVK option of Lutris, Bottles, Proton or
the prefix does not matter. Leave it as it is, and do not copy a DXVK `d3d9.dll` into the game folder.

On native Windows with bundled DXVK 3.1.1 and an NVIDIA adapter, Northlight disables Vulkan present-wait features to avoid a driver wait that can stall the client when its window is obscured. This can affect frame pacing. Set `[Renderer] DxvkPresentWaitWorkaround=0` in `northlight-renderer.ini` to restore the driver's present-wait path.
WineD3D modes (`PROTON_USE_WINED3D=1`) bypass Northlight. 32-bit (lib32/i386) Vulkan drivers are needed,
as for any DXVK game. Because Linux names are case-sensitive and Wine's are not, the installer stops
when the game folder already has a case variant of a name it writes (for example `D3D9.dll`). The player
instructions are in `renderer/linux-package/README.txt`.
