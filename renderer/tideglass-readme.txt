Northlight + Tideglass beta __RELEASE_VERSION__

Close the game. Unpack this folder outside the game directory. Start Install.cmd
on Windows or run bash install.sh on Linux. Use the uninstall launcher from the
same folder to restore the previous renderer. The Linux Wine override is below.

The combined renderer supports the 32-bit 3.3.5a build 12340 executable. The
installer reads its header and installs d3d9.dll, water-core.dll, settings, the
selected backends and northlight-water-archives.txt. It reads the player's own
archives. It leaves realm settings, account data, add-ons and foreign MPQs alone.
A previously installed water DLL or INI from another renderer must be moved
aside before installing. Keep those files to restore that setup.

The optional world cache adds static shadows and GI. Use --no-world-cache for
a renderer-only install. Add --raised-air to generate the Dalaran and Telredor
fog coverage from local geometry. In this beta the art layer is opt-in. Add
--art-layer to build Northlight's lighting MPQs. The default zone skies are used
with art enabled. Add --sky experimental-preserve to try preserving HD skies.
Reinstall after changing locale or adding or removing client archives.

Set Enable=0 in water-host.ini and restart for Northlight with native water.
Set it to 1 and restart to turn Tideglass back on. Appearance settings live in
water.ini. Launch and confirm the water changes after crossing a shoreline.
The game clock and realm selection are kept. Edited water settings survive
reinstall and uninstall. The generated northlight-water-textures directory and
renderer logs can be removed with the game closed.

For a report, include the version, backend, location, time of day, and what you
were doing when it happened. Review water-core.log and northlight-renderer.log
before sharing excerpts. Server acceptance and native Windows play require
separate verification. The standard Northlight reference follows.
