# Weather materials

Original transparent PNGs generated with the built-in image generator on 2026-11-01:

- `camera-frost.png`: irregular frost on camera glass, clear central play area.
- `snow-flocs.png`: 4×4 atlas of sixteen distinct irregular snow aggregates.

The complete generation prompts are in [prompts.json](prompts.json) and embedded
in each PNG as `impeccable:prompt`. They describe production materials, not mockups.
`scripts/generate_weather_art.py` packs luminance × alpha and complete L8 mip chains
into `src/generated/weather_texture_data.h`. Both the renderer and Weather Lab use
the embedded data; runtime PNG loading is unnecessary.

The same generator adds an original deterministic 128×128 L8 noise texture with
complete mip levels, using `random.Random(0x51a7)`. This is generated data, not an
image-generator output or a third-party texture. The snow and frost PNGs are unchanged.

Visual references studied:

- [R.A.S.S. camera weather effects](https://www.nexusmods.com/skyrimspecialedition/mods/22780?tab=images): layered particles and dust clouds.
- [M-ENB frost screenshot](https://www.nexusmods.com/skyrim/images/560099): irregular flakes and restrained corner frost.
- [Solstheim Ash Storms with Glowing Embers](https://www.nexusmods.com/skyrimspecialedition/mods/89384?tab=images): billowing storm sheets; embers were not carried into ordinary sand weather.

These references inform grain shape, material and varied coverage. No Skyrim or mod
textures, shaders or other game assets were copied into the project. Sand uses the
generated noise for grain detail and sparse thin deposits fixed to the camera glass.
Persistent 2D grains are simulated separately; scene geometry and depth do not clip
them. BF1 and Origins are material references only: their world fog model is not
part of the lens effect. The current sand references and their sources are recorded in
[the scoped direction](../../renderer/docs/weather-lab-product.md).
