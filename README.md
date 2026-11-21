# Northlight + Tideglass

Northlight adds lighting, shadows, skies and fog. Tideglass adds waves, underwater effects and reflections, from a Nagrand lake to the green canals of Undercity. This beta pairs them for **World of Warcraft 3.3.5a, build 12340**.

[![Sunbeams cut through palms on the coast and cast a band of gold across the waves](renderer/docs/screenshots/golden-coast.jpg)](renderer/docs/screenshots/golden-coast.jpg)

## Get the beta

**Beta 9** is the first public combo build. Download the ZIP for your platform.

[Download for Windows](https://github.com/lepola/northlight/releases/download/v0.3.216-tideglass.9/Northlight-0.3.216-tideglass.9-Windows.zip) · [Download for Linux](https://github.com/lepola/northlight/releases/download/v0.3.216-tideglass.9/Northlight-0.3.216-tideglass.9-Linux.zip) · [Release notes and checksums](https://github.com/lepola/northlight/releases/tag/v0.3.216-tideglass.9)

This beta has no macOS package. On a Mac with WoWSilicon, use [Northlight 0.3.216](https://github.com/lepola/northlight/releases/tag/v0.3.216), which has no Tideglass water.

## Install

1. Close WoW. Extract the whole ZIP somewhere outside your game folder, such as Downloads. Keep the extracted folder for uninstall.
2. **Windows 10 or 11 (64-bit)** — double-click `Install.cmd`. If Windows SmartScreen appears, choose **More info**, then **Run anyway**. **Linux (x86-64, Wine or Proton)** — open a terminal in the extracted folder and run `bash install.sh`.
3. When asked for the game folder, paste the path to the folder containing **Wow.exe**, or drag the executable into the prompt and press Enter. Press **Enter** for the default sky and dungeon choices. The first install builds outdoor lighting from your game files. Allow about **20–40 minutes**, **13 GB free** on the game drive and **8 GB RAM**. If interrupted, run the installer again to resume.
4. **Linux only** — set your launcher to load the game's copy of `d3d9.dll` before starting WoW. Use the instructions for your launcher below.
5. Launch WoW as usual. Visit a shore with trees or buildings across the water. Turn along the bank to see their reflections, then dip the camera underwater.

<details>
<summary>Linux setup for Steam, Lutris, Bottles or plain Wine</summary>

- **Steam/Proton** — in the game's **Properties → Launch Options**, enter `WINEDLLOVERRIDES="d3d9=n,b" %command%`.
- **Lutris** — in **Configure → Runner options → DLL overrides**, add `d3d9` with value `n,b`.
- **Bottles** — in the bottle's **DLL Overrides**, set `d3d9` to **Native, then Builtin**.
- **Plain Wine** — from the game folder run `WINEDLLOVERRIDES="d3d9=n,b" wine Wow.exe`.

</details>

## What the defaults give you

Reflected scenery, shaped ocean waves, underwater effects, slime and magma run alongside Northlight's zone skies, weather, lighting and fog. The installer includes the raised-city fog fixes for Dalaran and Telredor and repairs four known HD dock and plant textures when it finds the affected versions. Dungeon and raid lighting can be added later by running the installer again.

| Marsh water | Slime |
| --- | --- |
| [![Blue ripples catch the light beneath giant mushrooms](renderer/docs/screenshots/marsh-water.jpg)](renderer/docs/screenshots/marsh-water.jpg) | [![Dark green slime reflects the silhouettes of two towering figures](renderer/docs/screenshots/slime.jpg)](renderer/docs/screenshots/slime.jpg) |

| Magma | Fel magma |
| --- | --- |
| [![Orange magma winds around dark rock beneath a glowing cavern wall](renderer/docs/screenshots/magma.jpg)](renderer/docs/screenshots/magma.jpg) | [![Bright green fel magma pools around jagged black spires in shafts of sunlight](renderer/docs/screenshots/fel-magma.jpg)](renderer/docs/screenshots/fel-magma.jpg) |

Your game supplies the artwork, so stock and HD clients can look different. The installer preserves existing game archives, realm settings and add-ons. Install and rollback have been checked on stock and modified client copies. An earlier combo build also ran in game on native Windows with updated graphics drivers. Native Windows rollback and compatibility with individual realms, including Warmane, still need confirmation.

## Change settings or roll back

- **Adjust lighting and water in game** — press **Ctrl+Shift+F11**. On **Water**, choose **Quality**, **Balanced** or **Performance**, adjust waves or foam, then choose **Save**. [Settings guide](renderer/docs/settings-menu.md#water)
- **Keep Northlight and use the game's water** — on **Water**, clear **Use Tideglass**, choose **Save** and restart WoW.
- **Remove the combo** — close WoW and run `Uninstall.cmd` (Windows) or `bash uninstall.sh` (Linux) from the extracted package. Select the same game folder. This removes Northlight and Tideglass together, puts back the game files as they were before Northlight was first installed and keeps edited water settings. To keep Northlight without the beta, install [Northlight 0.3.216](https://github.com/lepola/northlight/releases/tag/v0.3.216) afterwards.
- **The game closes at startup** — try the older bundled graphics backend. On Windows, right-click an empty spot in the extracted folder, choose **Open in Terminal** (on Windows 10, hold Shift and choose **Open PowerShell window here**) and run `.\Install.cmd --backend dxvk2`. On Linux, run `bash install.sh --backend dxvk2` in the extracted folder.
- **Report a problem** — [open an issue](https://github.com/lepola/northlight/issues/new). Include the package version, zone, time of day and what you did just before it happened. For flicker or a hitch, say whether it happens while turning, moving or holding still. A screenshot or short clip helps.

For build instructions, renderer settings and test commands, see the [contributor and renderer reference](renderer/docs/northlight-reference.md). [License](LICENSE)
