# In-game settings

Press **Ctrl+Shift+F11** in game to open or close the settings window. It takes the mouse only while the cursor is over it, and the keyboard only while one of its text fields is active, so the game keeps your input everywhere else. Closing the window keeps unsaved edits and their live preview; a small line in the lower left corner says so until you save or revert.

The pages on the left are grouped by what you come to do. **Overview** holds the three choices that set the overall look and cost (the quality preset, the Water preset and the whole-world appearance) and the main effect switches. **Northlight effects** and **Tideglass** tune single effects. **By region** shapes how one place looks: pick a region once and **Regional appearance**, **Colour grading** and **Sun & moon** all edit it. **System** has the backend, performance budgets and diagnostics. Each row starts with a mark: a filled blue dot for a value set in your files, a hollow dot for one that follows its preset or parent region, and an amber dot for an edit you have not saved. Amber beside a page name means that page holds unsaved edits. An **R** marks a setting that takes effect after restarting WoW. Hover over a row for its file, key and current, saved and running values; right-click a row to return it to its inherited value. **Save** and **Revert** are at the lower right, **Defaults** for the current page at the lower left.

## Water

Open **Water** to tune waves, foam, reflections, underwater visibility, slime and magma when the Tideglass core is installed. **Save** applies the water settings in game. **Revert** discards edits and **Defaults** restores the packaged values for this page when saved. **Use Tideglass** takes effect after restarting WoW.

Choose the **Water preset** on **Overview** or on the **Water** page, then **Save**. This controls Tideglass independently of Northlight's overall quality preset.

| Water preset | Reflections | Resolution | Distance | Fine highlights | Nearby reflected scenery | Depth blur |
| --- | --- | --- | --- | --- | --- | --- |
| Ultra | Scenery and sky | 100% | Far | On | On | On |
| Quality (default) | Scenery and sky | 100% | Standard | Off | Off | On |
| Balanced | Scenery and sky | 75% | Standard | Off | Off | On |
| Performance | Sky only | Inactive | Inactive | Off | Inactive | Off |

**Depth blur** softens the bottom seen through deeper water. Performance selects zero. Waves still refract the scene. You can tune the strength separately. Existing installations keep their saved strength on opening the menu. A sky-only setup using the previous Performance settings and normal blur appears as **Custom** until you select a preset.

**Nearby reflected scenery** adds dock props, falls and supported pool attachments. It prepares extra models and draws them when visible. Existing settings keep their previous scenery.

**Fine water and slime highlights** adds detail to nearby ripples catching the sun or moon. You can select it independently, including with **Sky only**. Existing settings keep their previous highlights until you enable it or select Ultra.

Reflection resolution controls the pass that maps mirrored scenery onto water and slime. Waves, foam and the game keep their usual resolution. Try **Short** distance if travel past dense reflected scenery stutters. It limits the models and buildings prepared for the mirror and brings the fade closer. **Far** prepares a larger area and fades reflections farther from the camera. The menu shows **Custom** when the active choices differ from the presets. Switching reflections to **Sky only** keeps the distance and resolution for when you restore scenery. Your wave, foam and colour settings carry across presets. **Sky only** also skips the mirrored scenery seen from below the surface.

## Regional appearance

On **Regional appearance**, choose a **Region** and adjust **Fog tint strength**, the fog colour, or **Shadow contrast**. Each strength has three choices. **Inherit** follows the parent region, **Off** pins zero locally, and **Tuned** sets a local strength. Choosing Inherit for one strength leaves the other settings in place. Atmosphere previews live. Save keeps it after restart.

**Whole-world appearance** selects one look for the world, independently of the graphics and Water presets. **Default** keeps Northlight's existing appearance and your regional edits. **Vibrant** is optional. Search **Region** by its game name or INI key to adjust one location. Water has its own region selector for slime.

**Save as preset** stores the whole-world look as you see it now under a name: the selected look, your regional edits and anything not yet saved, with its colours, fog, contrast, curves, vibrance and selective colour adjustments. It appears in the **Whole-world appearance** list at once. Saving under the name of one of your own presets replaces it, after the dialog says so. **Delete preset** removes the preset chosen in the list; if it is the look in use, the world returns to Default. The looks that come with Northlight can be neither replaced nor deleted from the menu. Both actions write at once and **Revert** does not undo them.

To share a preset, copy its `.ini` from the game's `appearance-presets` folder into the same folder of the other installation. Open the settings there, select it under **Whole-world appearance**, then click **Save** to keep it after restart.

Fog tint colours the existing fog at its current density. Shadow contrast deepens dark scene detail. Fog tint strength ranges from 0 to 2. A value of 1.70 requests twice the tint of 0.85. Saturated swatches stop gaining strength when a fog channel reaches zero. Both strengths start at zero. The original composite shader runs when all regional appearance effects are inactive. Tuning uses the existing composite pass. Tirisfal and Eversong have cool and warm starting colours respectively.

Each region can have its own daylight, dusk and night 3D lookup tables. Each table combines brightness, RGB curves, vibrance and selective colour. Northlight blends between the looks as the clock advances. A region can inherit all or part of its parent’s settings.

The installer includes ready-to-use tables for **Vibrant**. A personal preset can reuse any matching table. If you change a region’s dusk colours, Northlight prepares that dusk look in the background while play continues. The named `.ini` contains everything you need to share your edit.

On **Colour grading**, **Tone curve strength** controls the regional grade after fog and water, before the HUD. While it is zero the grade is off and the page says so. Open the **Daylight grade**, **Dusk grade** or **Night grade** tab, then pick **Brightness**, **Red**, **Green** or **Blue** beside the graph and edit its curve. Higher points brighten that part of the selected channel. Brightness scales the three channels together. Red, Green and Blue act afterwards on their own channel. A straight diagonal gives the original response. Dusk is the look at 19:30. It holds through the sunset until 20:30 and returns around sunrise at 06:00. A missing Dusk value keeps the previous Daylight-to-Night result; **Inherit selected point** removes a local value and restores its inherited result.

**Vibrance** enriches muted colours with a separate slider for daylight, dusk and night. Start at zero and raise it while watching foliage and nearby lamps. Tone curve strength controls each period's full grade, including vibrance and selective colour. Right-click the slider to inherit the parent region's value. The saved keys are `day_vibrance`, `dusk_vibrance` and `night_vibrance`, each from 0 to 1.

Under each grade's curve, **Colour ranges** adjusts red, orange, yellow, green, cyan, blue or magenta: choose a range, then its three sliders. A range is strongest at its own hue and fades out toward its two neighbours, so cyan can change without touching green or blue foliage and sky. Each range has a hue shift in degrees, an additive saturation adjustment and an additive lightness adjustment. Zero leaves that range unchanged. **Inherit range** restores all three values from the parent; right-click one control to inherit just that value. Selective colour and Vibrance need filterable 16-bit colour volume support. On devices without it, those adjustments are saved but not rendered; Northlight still applies the regional curves.

Drag one of the four points and look at the world behind the overlay. A pale bar shows how far each point may travel, and double-clicking a point returns it to its inherited place. The first point moves horizontally from 4% to 20% input for fine control in dark areas. The other points sit at 25%, 50% and 75%. **Point values** also offers sliders. **Inherit selected point** restores that region’s parent value for just that point. **Hold to compare** temporarily removes the selected region’s grade while keeping its fog settings. Release to restore the preview. **Save** keeps it after restart. Northlight blends the three grades by the game clock: Night until 04:30, Dusk at the 06:00 sunrise, Daylight from 08:00 to 18:00, Dusk from 19:30 to 20:30 and Night from 21:15.

The **Region** selector under **Water** also controls slime pigment and movement. Choose a mode and **Save** to apply it. An existing Undercity strength carries over. Outdoor slime starts at its usual appearance.

For manual edits, `celestial-profiles.ini` accepts `[Azeroth:85]` for Tirisfal, `[Azeroth:85/159]` for Brill, and `[Azeroth:1497@inside]` for Undercity. Portable looks live in the game folder under `appearance-presets/<name>.ini`; they use the same regional section syntax and only appearance values. `[default]` is the world base, `[Map:default]` is an optional map base, and `[Map:zone]` sets a regional value. The installer updates its bundled looks when their installed files are unchanged and keeps player-edited copies. Share a named `.ini` file to share its visual values; renderer, water, sun and moon settings are not included.

`fog_tint` takes three colour channels from 0 to 1. `fog_tint_strength` accepts 0 to 2. `shadow_contrast` and `tone_curve_strength` accept 0 to 1. Curve and vibrance keys start with `day_`, `dusk_` or `night_`. For a curve colour channel insert `red_`, `green_` or `blue_`, as in `day_red_midtone_level`. The outputs are `shadow_level` (0.13 to 0.37), `midtone_level` (0.38 to 0.62) and `highlight_level` (0.63 to 0.87). `low_input` positions the first point (0.04 to 0.20). `low_bend` at 0.50 follows the line from black to the shadow point. Lower values deepen those tones and higher values lift them. Selective colour keys use a time prefix, a band (`red`, `orange`, `yellow`, `green`, `cyan`, `blue` or `magenta`), and `hue_shift`, `saturation` or `lightness`; for example, `dusk_red_hue_shift=10`. Hue shift accepts -30 to 30 degrees, saturation -1 to 1, and lightness -0.25 to 0.25. New terrain cache tiles let outdoor settings blend across subzone boundaries. With older tiles, the live subzone can select a local look when its parent agrees with the terrain. On dungeon maps, a configured room takes precedence over its parent zone, then the map supplies the fallback. Tideglass reads numeric area IDs in `water.ini` under `[RegionalSlime]`.

## Saving and restarting

Edits are a draft. A live ("F") setting previews immediately and writes nothing; Save commits, Revert discards the draft and returns the renderer to the saved configuration. Settings marked "R" (restart required) are saved but only read at start-up: the command stream, worker and GI solver settings, NameMask, SpellLayer, EquipmentLights, backend and regional shadow range. Unavailable controls are greyed out.

## Technical reference

The renderer DLL draws the window over the finished frame and handles its hotkey. The menu reads and saves renderer settings. Regional appearance uses the existing terrain cache and live area information described above. How a regional look becomes a graded frame (look files, the three times of day, the table pack) is in [regional appearance](regional-appearance.md). `[Renderer] SettingsOverlay=0` in `northlight-renderer.ini` turns the window off completely (read at device creation). `[Renderer] SettingsOverlayKey=Ctrl+Shift+F11` changes the toggle: F1 to F12 with any of Ctrl, Shift and Alt (matched exactly; Ctrl+Shift+F7 to F10 and F12 stay the renderer's own keys). An unusable value keeps Ctrl+Shift+F11 and says so once in `northlight-renderer.log`. Everything the menu shows comes from the same schema the renderer loads.

The window's look (colours, spacing, scale and font) lives in `src/overlay/overlay_theme.h`, and the pieces its pages are assembled from (cards, page headers, state marks) in `src/overlay/overlay_widgets.h`. To see a change without starting the game, run `python3 scripts/dev_tuning/settings_menu_preview.py`: it builds the real window against the settings service and writes a picture of every page to `out/settings-menu-preview`. Steps such as `"click=Water" shot=water` drive it by the labels on screen. The preview shows layout, wording and how controls respond; blending over a real scene and input still need a look in game.

Saving is transactional. A worker thread preserves unknown INI lines, comments, UTF-8/UTF-16LE encoding and line endings, verifies the file revision before writing, serializes writers with an interprocess lock, and journals the multi-file commit so a crash is rolled back at the next start (before any INI is read). A read-only or externally edited file is reported by name and never overwritten. Saved and previewed live values reach the renderer at a frame boundary.

The in-process API is `NorthlightSettings::Service` (`src/settings/settings_service.h`): `requestSnapshot`/`snapshot`, `preview`/`clearPreview` (latest draft wins), `commit`, `status`. All calls are non-blocking; the worker does every file access.
