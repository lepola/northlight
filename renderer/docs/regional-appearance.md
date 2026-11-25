# Regional appearance

How Northlight gives each region of the world its own look: a tint on the game's fog, stronger or softer shadow
contrast, and a colour grade over the finished picture. What a player sees and can change is in the
[settings menu guide](settings-menu.md#regional-appearance); this page is for someone changing the code or the
tools.

## What a look is

A look is one INI file in `appearance-presets/` in the game folder. It uses the region sections of
`celestial-profiles.ini` (`[default]`, `[Azeroth:default]`, `[Azeroth:12]`, a subzone under its parent) but accepts
only the appearance keys, never sun or moon settings (`src/settings/appearance_presets.h`):

| Key | What it does |
| --- | --- |
| `day_`, `dusk_`, `night_` + `fog_tint`, `fog_tint_strength` | Colours the game's existing fog at that time of day. The tint keeps the fog's brightness. |
| `day_`, `dusk_`, `night_` + `fog_brightness` | Darkens or lifts that fog: 1 leaves it alone, 0 to 2. |
| `fog_tint`, `fog_tint_strength` | One value for all hours, as looks had before fog was per time. Read as the three per-time keys of its section; a per-time key in the same section wins. |
| `shadow_contrast` | Deepens or lifts shadows in the lit world. |
| `tone_curve_strength` | How much of the grade below is applied. Zero leaves the picture untouched. |
| `<time>_<channel>low_input`, `low_bend`, `shadow_level`, `midtone_level`, `highlight_level` | One tone curve per time and channel. Channels are brightness (no prefix), `red_`, `green_` and `blue_`. |
| `<time>_vibrance` | Lifts muted colours more than saturated ones. |
| `<time>_<range>_hue_shift`, `_saturation`, `_lightness` | Adjusts one colour range. |

`<time>` is `day`, `dusk` or `night`. `<range>` is `red`, `orange`, `yellow`, `green`, `cyan`, `blue` or `magenta`,
centred on 0°, 30°, 60°, 120°, 180°, 240° and 300°. A colour between two centres is shared between them in
proportion, so the ranges always add up to the whole wheel (`hueWeights` in `src/sky/world_tone_curves.h`).

`Default` is the packaged neutral look and its name is reserved. `Vibrant` and `Atmospheric` are the optional bundled
looks (`client-config/appearance-presets/`). Atmospheric is generated in the Tideglass checkout from written intents
(`tools/regional-look-intents.json` there) and has no table pack: its tables are baked as the player arrives. A player's own looks sit beside them under any other name; the menu
saves, replaces and deletes those and leaves the packaged ones alone (`packaged`, and the preset jobs in
`src/settings/settings_service.h`).

A region without a value takes its parent's: subzone, zone, map default, then `[default]`. The menu shows this as
Inherit, Off (an explicit zero) or Tuned. The selected look is laid under the player's own regional settings in
`celestial-profiles.ini`, so a personal edit of one region wins over the look and the rest still follows it
(`compose`).

## Time of day

A grade has three anchors: Daylight, Dusk and Night. The renderer blends between two of them at a time from the
game's own clock, never the computer's (`clockWeights` in `world_tone_curves.h`):

| Game clock | Grade |
| --- | --- |
| 21:15 to 04:30 | Night |
| 04:30 to 06:00 | Night to Dusk, as dawn breaks |
| 06:00 to 08:00 | Dusk to Daylight |
| 08:00 to 18:00 | Daylight |
| 18:00 to 19:30 | Daylight to Dusk |
| 19:30 to 20:30 | Dusk, through the sunset |
| 20:30 to 21:15 | Dusk to Night |

Dusk looks are authored on 19:30 frames. The Dusk look also serves the morning, because the light tables give dawn
and sunset nearly the same tint in most zones.

A Dusk value that a look does not set is taken 84% of the way from its Daylight value to its Night value, which is
what a look with only two anchors showed at 19:30 (`resolveLegacyDusk` in `src/sky/celestial_profiles.h`).

These hours are the ones the installed light tables turn at (`build_lighting.py`: dawn from 04:30, sunset colour
until 20:35, night at 21:15). They are not `nightFactor`'s 06:00 to 08:00 and 18:00 to 20:00, which the grade used
at first: that put the Night grade on a scene still lit by the sunset and took the Dusk look away an hour before
the light changed. Zones differ a little (Tirisfal keeps its night light until 06:00), so one clock is a
compromise; a separate Dawn anchor would be a fourth set of values and of reference frames.

## From settings to pixels

1. **The region.** Each frame the renderer resolves the camera's area to a region's settings and blends neighbouring
   regions across authored chunk centres, so walking over a border changes the look gradually
   (`src/sky/celestial_profiles.h`).
2. **Bake keys.** The grade at each anchor is rounded to a fixed precision and frozen into a key (`makeBakeKey`). A
   grade that changes nothing, or has strength zero, becomes the empty key and costs nothing.
3. **Tables.** A background worker turns each key into a 32×32×32 table of 16-bit colours, about 256 KiB
   (`src/sky/grade_lut_worker.h`). It first looks in the bundled pack, then bakes. A newer request replaces a queued
   one, so a fast flight across regions bakes where the player is now. Twelve tables stay in memory.
4. **The frame.** The final world composite samples the table after fog and water and before the interface. It
   blends the two anchors the clock sits between, and cross-fades over 180 ms when the region's keys change
   (`prepareGrade` in `src/world/world_renderer.h`). Until a table is ready the picture stays ungraded or keeps the
   previous table; nothing waits on the worker.
5. **Without volume textures.** A device that cannot create the table texture falls back to the tone curves alone,
   from a small one-dimensional table. Vibrance and the colour ranges are then left out.

## The bundled table pack

Baking the Vibrant look's tables on first entry to each region would show a brief ungraded moment, so the installer
ships them ready-made in `appearance-luts/Vibrant.nllutpack` (`renderer/build_appearance_lut_pack.py` writes it,
`src/sky/grade_lut_pack.h` reads it).

- The file is a 104-byte header, an index and the tables. Each index record holds the key's hash, the full key, the
  table's offset and length, and its SHA-256, which is checked before a table is used.
- The header carries the bake version (`BakeMathSchemaVersion`). Change the grade's arithmetic and this number
  together: a pack from another version is refused as a whole and the renderer bakes instead. Version 2 introduced
  the seven colour ranges.
- A key is found only by exact match, so a player who edits a region simply gets a baked table for it.
- Rebuild the pack whenever `Vibrant.ini` or the bake arithmetic changes; packaging takes the pack as an input.

## Editing looks outside the game

The Tideglass checkout has a browser tool, `tools/regional_grade_lab.html`, which applies the same arithmetic to
saved screenshots (`tools/regional_grade_lab_core.js`, checked by `regional_grade_lab_core_test.js`). It reads and
writes the same INI files. Its arithmetic is a second copy of `world_tone_curves.h`: change both together and
compare them on a grade that uses every control. Screenshots for it must be taken with the Default look so that
nothing is graded twice.

## Checks

- `tests/test_world_tone_curves.cpp`: the curve, range and key arithmetic, and the bake version.
- `tests/test_grade_lut_pack.cpp` and `tests/test_appearance_lut_pack.py`: the pack format, and the shipped Vibrant
  pack against the renderer's own baker.
- `tests/test_world_grade_lut_worker.cpp`: the background worker.
- `tests/test_appearance_presets.cpp`: look files and how a look combines with personal settings.
