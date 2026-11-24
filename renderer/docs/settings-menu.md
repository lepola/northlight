# In-game settings

Press **Ctrl+Shift+F11** in game to open or close the settings window. It takes the mouse only while the cursor is over it, and the keyboard only while one of its text fields is active, so the game keeps your input everywhere else. Closing the window keeps unsaved edits and their live preview; a small line in the lower left corner says so until you save or revert.

## Water

Open **Water** to tune waves, foam, reflections, underwater visibility, slime and magma when the Tideglass core is installed. **Save** applies the water settings in game. **Revert** discards edits and **Defaults** restores the packaged values for this page when saved. **Use Tideglass** takes effect after restarting WoW.

Under **Water → Performance**, choose the **Water preset**, then **Save**. This controls Tideglass independently of Northlight's overall quality preset.

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

In **Atmosphere & weather**, choose a **Region** and adjust **Fog tint strength**, the fog colour, or **Shadow contrast**. Each strength has three choices. **Inherit** follows the parent region, **Off** pins zero locally, and **Tuned** sets a local strength. Choosing Inherit for one strength leaves the other settings in place. Atmosphere previews live. Save keeps it after restart.

Fog tint colours the existing fog at its current density. Shadow contrast deepens dark scene detail. Both strengths start at zero. The original composite shader runs when both effects are inactive. Tuning uses the existing composite pass. Tirisfal and Eversong have cool and warm starting colours respectively.

The **Region** selector under **Water** also controls slime pigment and movement. Choose a mode and **Save** to apply it. An existing Undercity strength carries over. Outdoor slime starts at its usual appearance.

For manual edits, `celestial-profiles.ini` accepts `[Azeroth:85]` for Tirisfal, `[Azeroth:85/159]` for Brill, and `[Azeroth:1497@inside]` for Undercity. The appearance keys are `fog_tint` (three colour channels), `fog_tint_strength` and `shadow_contrast`, all from 0 to 1. New terrain cache tiles let outdoor settings blend across subzone boundaries. Older tiles can use the current client area, and missing area information falls back to the parent. Tideglass reads numeric area IDs in `water.ini` under `[RegionalSlime]`.

## Saving and restarting

Edits are a draft. A live ("F") setting previews immediately and writes nothing; Save commits, Revert discards the draft and returns the renderer to the saved configuration. Settings marked "R" (restart required) are saved but only read at start-up: the command stream, worker and GI solver settings, NameMask, SpellLayer, EquipmentLights, backend and regional shadow range. Unavailable controls are greyed out.

## Technical reference

The window is drawn by the renderer DLL over the finished frame; the hotkey never reaches the game. Nothing in this feature calls, reads or patches the game client, and there is no addon or CVar. `[Renderer] SettingsOverlay=0` in `northlight-renderer.ini` turns the window off completely (read at device creation). `[Renderer] SettingsOverlayKey=Ctrl+Shift+F11` changes the toggle: F1 to F12 with any of Ctrl, Shift and Alt (matched exactly; Ctrl+Shift+F7 to F10 and F12 stay the renderer's own keys). An unusable value keeps Ctrl+Shift+F11 and says so once in `northlight-renderer.log`. Everything the menu shows comes from the same schema the renderer loads.

Saving is transactional. A worker thread preserves unknown INI lines, comments, UTF-8/UTF-16LE encoding and line endings, verifies the file revision before writing, serializes writers with an interprocess lock, and journals the multi-file commit so a crash is rolled back at the next start (before any INI is read). A read-only or externally edited file is reported by name and never overwritten. Saved and previewed live values reach the renderer at a frame boundary.

The in-process API is `NorthlightSettings::Service` (`src/settings/settings_service.h`): `requestSnapshot`/`snapshot`, `preview`/`clearPreview` (latest draft wins), `commit`, `status`. All calls are non-blocking; the worker does every file access.
