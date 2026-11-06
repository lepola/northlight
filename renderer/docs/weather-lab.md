# Weather Lab without the game

Double-click `scripts/Weather Lab.cmd`, or run this from the repository root:

```powershell
python scripts/weather_lab.py
```

Requires Windows and a configured Zig toolchain (`northlight.local.ini`). The lab
builds locally without loading the game, MPQ archives, server or game proxy. It uses
system D3D9 by default. To try DXVK, pass `--backend <path to an x86 d3d9 DLL>`.
The executable and outputs are stored in `<out>/weather-lab/`.

Camera water, snow, frost and sand sit on the lens in glass coordinates. The separate,
existing supplemental scene rain/snow pass remains available. The new wind controls
affect neither that pass nor the client's native particles.

## What the lab can test

- Clear / Rain / Snow / Sand and intensity from 0 to 1.
- Smooth transitions through the production weather tracker, pause and restart.
  Pause also stops merges and fades; size can still be adjusted manually.
- `Camera under shelter` hides lens effects while retaining exterior precipitation;
  `Fully indoors (no sky)` disables both passes.
- Two or three supplemental precipitation layers (Performance / Quality).
- Scene precipitation clipped by synthetic ground, column and background depth.
  Lens deposits remain visible over geometry and ignore this toggle.
- Camera drop size, strength and optional rivulet coalescence. Wet trails collect
  nearby drops along their rendered direction. Rain and snow drift sideways under
  gusts; snow also sways. The readout includes a merge counter.
- Loose crystalline flocs in supplemental scene snow and on the camera: 16 irregular
  forms with varied rotation, stretch and opacity. Camera variants blend continuously.
  Merge centers and radii ease toward their targets; the absorbed flake moves toward
  its neighbor and fades over 0.35 seconds while preserving its initial opacity.
- Asymmetric textured frost with translucent facets and mild local refraction.
  Snow mass and storm intensity reveal density and detail in a fixed pattern, without
  texture zoom. The center stays clear.
- Persistent irregular sand grains, occasional soft marks and a sparse thin film on
  glass, with gradual force-driven sliding.
- World wind projected into lens forces and arrival rates, plus optional camera-turn inertia.
- Snow melting from `Fire heat` (0 to 1). This is a simulation input, not a test of
  in-game fire discovery or obstacle checks.
- Saving settings to `weather-lab.ini` and an image to `preview.bmp`.

The lab shares `surface_renderer.h`, `weather_lens.h`, `weather_state.h`,
`weather_effects.h` and compiled `surface_effects.hlsl` with the game. Its synthetic
background is labeled. It previews Northlight's supplemental precipitation and lens
effects; it does not reproduce native client particles, sky, lighting, world fog or
maps. Fog and light adjustment coefficients appear as numerical readouts.

Snow and frost use [original embedded materials](../../shaders/assets/README.md):
a shared scene/camera 4x4 snow atlas (L8, 512x512) and frost texture (L8, 1024x576),
both with mipmaps. `scripts/generate_weather_art.py` embeds them in
`src/generated/weather_texture_data.h`; runtime does not load the PNG files.
A deterministic 128x128 L8 noise texture with mipmaps supplies grain detail and the
sparse fixed film. These materials belong to Northlight's supplemental effects;
they do not replace the client's native particles.

A custom background can be supplied:

```powershell
python scripts/weather_lab.py --background screenshot.png
```

Image conversion requires Pillow. Screenshots have no real depth: occlusion starts
disabled and remains synthetic if enabled.

## Wind and camera turns

`Wind and camera` sets world heading (0 degrees along +X, 90 along +Y) and wind
strength in percent. Its projection gradually changes particle velocities on the
glass; the into-glass component affects new impacts. Existing deposits retain 2D
positions without radial expansion or global reprojection. These controls do not
change the separate supplemental scene rain/snow pass or native client particles.

Drag the preview with the left mouse button to change yaw/pitch. The collapsed
`Camera controls` contains numeric sliders, `Auto rotate camera`, speed and
`Reset view`. The synthetic ray scene and depth rotate together as a reference behind
the lens. `React to camera turns` adds bounded particle velocity impulses; disabling
it preserves wind forces. Large cuts rebase camera history. Deposits and frost remain
in glass coordinates without global panning, scaling or 3D perspective.
The `Lens wind` readout shows right/down/into glass and turn-response status.

## Replaying a captured game frame

In the updated renderer, open `Ctrl+Shift+F11` -> `Diagnostics` -> `Capture weather`
at the problematic view. The one-shot capture writes files to the client's
`northlight-diagnostics/session-.../`. Ordinary frames do not perform these GPU
readbacks. The lab does not install the updated DLL into the client.

Subsequent testing does not require the game:

```powershell
python scripts/weather_lab.py --capture "<session-folder>/capture-1-weather.json"
```

Keep the matching `weather-depth.fgr`, `weather-scene.fgr` and, when name protection
was active, `weather-name-mask.fgr` beside the JSON with the same prefix.
JSON records exact projection, near/far and depth-range constants, weather inputs,
three wind/camera settings and input availability. Import creates WCP3 with a
276-byte header containing 19 settings and four weather-state values alongside the
context. The native reader also accepts WCP2. A capture taken without an active
weather pass is explicitly `unavailable`.

Replay starts paused with the original projection and raw depth. `Captured depth
occlusion` compares scene precipitation with and without clipping; `Respect captured
name mask` controls name protection independently. Readouts show actual depth minima,
maxima and viewport range. The captured scene remains static when the camera turns;
occlusion for the separate scene pass is temporarily suspended with an explanation.
`Reset view` restores the original projection. Scene depth never clips lens deposits,
including sand.

Water/snow masses, lens sand and camera-inertia history start anew. The mask stores
resolved permitted coverage, so MSAA edges are approximate. HDR backgrounds are
converted to SDR with a visible label. Incomplete captures are also labeled. Replay
reproduces weather-pass inputs, not the entire game world.

MSAA export permits packaged DXVK 2/3. This diagnostic restriction does not establish
that the current materials have been tested on those backends. With MSAA on system
D3D9 or unverified backends, export is `unavailable` with reason
`multisample-capture-not-verified`; disable MSAA to capture there. Ordinary weather
rendering is unaffected by this diagnostic restriction.

## Existing and added settings

Before the lab, `Atmosphere & weather` already exposed `Weather`, `WeatherMemory`,
`RainFog` (0-2), `RainLens`, `SnowLens` and their `Strength` settings (0-100, default
60). `FogClouds` and `FogCloudDensity` separately control moving fog clouds.
Precipitation and camera-drop sizes were constants.

The shared settings schema and in-game menu now also support:

| `[Quality]` key | Purpose | Range / default |
| --- | --- | --- |
| `RainDropSize` | Supplemental Northlight rain particle radius | 25-300% / 100% |
| `SnowFlakeSize` | Supplemental Northlight snowflake radius | 25-300% / 100% |
| `RainLensSize` | Camera water size and merge radius | 25-300% / 100% |
| `SnowLensSize` | Camera snow size and merge radius | 25-300% / 100% |
| `RainRivulets` | Drop coalescence and water collection along trails | 0-1 / 1 |
| `SnowFrost` | Frost independent of individual camera flakes | 0-1 / 1 |
| `SnowFrostStrength` | Frost strength, increasing with snow load | 0-100% / 70% |
| `SandLens` | Lens sand and sparse storm deposits | 0-1 / 1 |
| `SandLensStrength` | Grain and thin-film visibility | 0-100% / 60% |
| `SandLensSize` | Sand grain size | 25-300% / 100% |
| `WeatherWindDirection` | World heading: +X = 0 degrees, +Y = 90 | 0-359 / 45 |
| `WeatherWindStrength` | Wind force on lens particles | 0-300% / 100% |
| `WeatherCameraMotion` | Additional camera-turn inertia | 0-1 / 1 |

100% is the effect's baseline size. These settings do not change WoW's native
particles. Their textures are generated separately by `build_weather_textures.py`
when building the art layer; changing those textures requires rebuilding the MPQ.

`Save preset` writes only local `weather-lab.ini`. To transfer values, copy the desired
keys from its `[Quality]` section into the existing `[Quality]` section of the game's
`northlight-quality.ini`, keeping other settings. `[Preview]` belongs only to the lab.
New keys take effect in the game after building and installing the updated renderer;
the lab does not install it.

## Sandstorms and frost

`weather_memory.h` recognizes native sand, and the tracker retains its intensity.
`SandLens` supports up to 128 persistent 2D grains on glass with varied adhesion, age
and softness. Wind gradually changes their sliding; camera response supplies velocity
impulses. Occasional soft marks and sparse fixed film preserve background readability.
Scene depth does not clip them, and camera orientation alone does not rotate or
reproject sand positions. Shelter disables the camera effect. The lab does not
reproduce native particles, storm fog or the client's volumetric engine.
BF1/Origins provide grain and material references rather than a world-fog model.

Frost accumulates over several seconds and melts with `Fire heat`. Unpause captured
replay to let snow masses and frost accumulate again. `SnowLens=0` hides individual
camera flakes while retaining independent `SnowFrost`. Up to 32 fading merge
silhouettes exist only for visual continuity: they add neither physical snow mass nor
frost load.

## Verification and source changes

```powershell
python scripts/weather_lab.py --smoke
python tests/test_weather_lens.py
python tests/test_weather_lab_capture.py
```

`--smoke` runs on the real GPU and saves `smoke-*.bmp`. Its 29 cases cover weather
modes, shelter, indoors, disabling, sizes, heat, rivulets, sand, frost intensity and
clear center, and camera response on/off for rain, snow and sand. Sand cases 3 and 28
must match when scene depth is toggled. Indoors, disabled coatings and sheltered sand
leave the source frame unchanged.

Current materials were checked on native D3D9; this does not confirm a current DXVK
run or in-game behavior. UI and visual fixtures are listed in the
[design document](weather-lab-design.md). The native `tests/lens_effects_gpu.cpp`
fixture checks frozen sand at different camera orientations, atlas-boundary
continuity and fixed frost landmarks as density changes. Automated comparisons and
UI captures do not establish manual preview-drag behavior; that remains unconfirmed.

Temporary `snow-merge.gif`, `rain-drift.gif`, `frost-density.gif` and corresponding
`*-detail.png` files live in `.local/out/lens-gpu/`. Source review and selected contact
sheets cover merging, shape transitions, sideways drift and density without scaling.
GIF playback and an in-game run have not been checked.

### Mushroom occlusion regression

Production no longer applies the global camera-shelter gate to exterior precipitation.
Shelter now limits lens effects; a fully indoor view without sky still disables both
passes. `tests/test_weather_exterior.py` exercises the actual `surfaceWeather()`
dispatch; `tests/validate_weather_occlusion_gpu.py` runs the production shader against
a mushroom silhouette with cap, stem, open sky and varied depth ranges.

Earlier occlusion checks passed on native D3D9, DXVK 3 and DXVK 2; that scope is
separate from the current material replacement. The reported "rain only on the cap"
view has not been reproduced exactly. Screen depth clips particles against foreground
geometry but does not provide a roof test above every individual particle.

After changing HLSL:

```powershell
python scripts/weather_lab.py --recompile-shaders
```

This rebuilds shared production bytecode with the configured offline compiler and
then builds the lab. The Microsoft compiler DLL must already be configured.
