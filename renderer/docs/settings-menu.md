# In-game settings

Press **Ctrl+Shift+F11** in game to open or close the settings window. It takes the mouse only while the cursor is over it, and the keyboard only while one of its text fields is active, so the game keeps your input everywhere else. Closing the window keeps unsaved edits and their live preview; a small line in the lower left corner says so until you save or revert.

## Water

Open **Water** to tune waves, foam, reflections, underwater visibility, slime and magma when the Tideglass core is installed. **Save** applies the water settings in game. **Revert** discards edits and **Defaults** restores the packaged values for this page when saved. **Use Tideglass** takes effect after restarting WoW.

Under **Water → Performance**, choose a water preset, then **Save**.

| Preset | Reflections | Resolution | Distance |
| --- | --- | --- | --- |
| Quality (default) | Scenery and sky | 100% | Standard |
| Balanced | Scenery and sky | 75% | Standard |
| Performance | Sky only | Inactive | Inactive |

Reflection resolution controls the pass that maps mirrored scenery onto water and slime. Waves, foam and the game keep their usual resolution. Try **Short** distance if travel past dense reflected scenery stutters. It limits the models and buildings prepared for the mirror and brings the fade closer. The menu shows **Custom** when your choices differ from all three presets. Your wave, foam and colour settings carry across presets. **Sky only** also skips the mirrored scenery seen from below the surface.

## Saving and restarting

Edits are a draft. A live ("F") setting previews immediately and writes nothing; Save commits, Revert discards the draft and returns the renderer to the saved configuration. Settings marked "R" (restart required) are saved but only read at start-up: the command stream, worker and GI solver settings, NameMask, SpellLayer, EquipmentLights, backend and regional shadow range. Unavailable controls are greyed out.

## Technical reference

The window is drawn by the renderer DLL over the finished frame; the hotkey never reaches the game. Nothing in this feature calls, reads or patches the game client, and there is no addon or CVar. `[Renderer] SettingsOverlay=0` in `northlight-renderer.ini` turns the window off completely (read at device creation). `[Renderer] SettingsOverlayKey=Ctrl+Shift+F11` changes the toggle: F1 to F12 with any of Ctrl, Shift and Alt (matched exactly; Ctrl+Shift+F7 to F10 and F12 stay the renderer's own keys). An unusable value keeps Ctrl+Shift+F11 and says so once in `northlight-renderer.log`. Everything the menu shows comes from the same schema the renderer loads.

Saving is transactional. A worker thread preserves unknown INI lines, comments, UTF-8/UTF-16LE encoding and line endings, verifies the file revision before writing, serializes writers with an interprocess lock, and journals the multi-file commit so a crash is rolled back at the next start (before any INI is read). A read-only or externally edited file is reported by name and never overwritten. Saved and previewed live values reach the renderer at a frame boundary.

The in-process API is `NorthlightSettings::Service` (`src/settings/settings_service.h`): `requestSnapshot`/`snapshot`, `preview`/`clearPreview` (latest draft wins), `commit`, `status`. All calls are non-blocking; the worker does every file access.
