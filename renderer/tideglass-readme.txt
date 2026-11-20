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

A standard install builds Northlight's lighting, current zone skies and weather,
plus outdoor fog coverage in Dalaran and Telredor. It repairs the last mip of
four known incomplete HD dock, sign and plant textures where needed. All inputs
come from your client. Your original archives stay in place. If patch-Z belongs
to another mod, the installer uses a free slot and reports any locale conflict.

The world cache adds static shadows and global illumination. --no-world-cache
skips that build. Without cached fog tables the installer can still generate
the scenery repairs and raised fog coverage. --no-art-layer and --no-raised-air
skip the respective generation steps. HD users can try --sky experimental-preserve.
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
