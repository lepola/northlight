# In-game settings

The Northlight addon adds native Video Options categories and reads the settings schema from the renderer. Only keys implemented by that build are listed. The interface uses English labels, exposes technical keys in tooltips, and keeps unsupported effect controls unavailable.

Changes remain a draft until Apply or Okay receives a successful commit. Apply keeps controls editable while that save is pending; later edits belong to the next Apply and survive the background refresh. Writes stay serialized, and Cancel restores the last acknowledged settings once the pending save finishes. Defaults and Inherit edit the draft. The transport has bounded request sizes and timeouts; a timeout after commit submission requires Refresh because the save may already have completed.

One worker preserves unknown INI lines, comments, UTF-8/UTF-16LE encoding and existing line endings. It verifies revisions before writing, serializes transactions with an interprocess mutex, and recovers a journaled multi-file commit. Files changed externally are not silently overwritten.

Supported quality values and celestial profiles apply at a frame boundary. Worker configuration, backend, shadow range, NameMask and SpellLayer creation settings require restart. The renderer carries GI enable/bounce changes in each worker request. Optional AO preview, material and HDR runtime integration belongs to the linked renderer feature PRs; the standalone menu does not add those effects.

Normal packages include the addon under Interface/AddOns/NorthlightSettings. Existing quality and regional profile files are preserved. The CVar bridge is signature gated and retains the installed optimizer's verified detour and the client's protection checks. No server connection or game launch is required by the offline tests.
