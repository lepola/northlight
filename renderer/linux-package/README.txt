NORTHLIGHT RENDERER __RELEASE_VERSION__ — LINUX (WINE / PROTON)

Northlight renderer adds lighting, shadows, fog, indirect light (GI) and sky
effects to the WoW 3.3.5a client. On Linux the game runs under Wine or Proton
(Lutris, Bottles, Steam or plain Wine), so Northlight installs the same files
as on Windows: d3d9.dll in the game folder, which loads the package's own DXVK
from renderer-backends/. The installer itself (world cache, lighting layer,
renderer files) runs natively on Linux with the package's own Python and
StormLib; you do not need to install anything else. The installer never
starts the game and never changes Wow.exe.

NOT TESTED IN GAME YET
This Linux package has been checked only by automated tests. Nobody has played
with it under Wine or Proton yet. Please report what works and what does not
(the distribution, the launcher and its Wine/Proton version, the GPU and
driver, and northlight-renderer.log from the game folder).

DOWNLOADS
- Northlight-__RELEASE_VERSION__-Linux.zip: the installer and the renderer.
- Nothing else: the installer builds the world cache (world-cache) itself from
  the game's own files, for an unmodified and an HD client alike (10–40 min).

REQUIREMENTS
- 64-bit x86 Linux with glibc 2.17 or newer (any current distribution) and a
  WoW 3.3.5a (12340) client that you run with Wine or Proton.
- Vulkan for 32-bit programs: WoW 3.3.5a is a 32-bit game, so the 32-bit
  (lib32 / i386) Mesa or NVIDIA Vulkan driver must be installed, as for any
  DXVK game. Lutris, Bottles and Steam usually ask for it already.
- The default DXVK 3.1.1 asks for Vulkan 1.3 and extra device features; on an
  older driver or GPU use --backend dxvk2 (DXVK 2.7.1).
- Only ASCII characters in the game folder path, and a short path (the game
  sees it as C:\... inside a Wine prefix or Z:\... elsewhere, and that path may
  have at most about 150 characters).
- About 13 GB of free space on the game's drive and at least 8 GB of memory
  while the installer builds the cache.
- Unzip the package onto a normal Linux file system (for example into
  Downloads in your home folder), not onto an NTFS/exFAT drive: the package's
  Python must be executable.

INSTALLATION
1. Close WoW completely.
2. Unzip Northlight-__RELEASE_VERSION__-Linux.zip OUTSIDE the game folder,
   for example into ~/Downloads.
3. Open a terminal in the unzipped folder and run:
     bash install.sh
   Give the game folder (the folder that holds Wow.exe). You can also drag the
   folder into the terminal window, or pass it directly:
     bash install.sh --client ~/Games/WoW
   With Lutris or Bottles the game folder is usually inside the Wine prefix,
   for example .../drive_c/Program Files/World of Warcraft.
4. The installer first checks the client, the disk space and the package. If
   something is missing, it stops before anything is changed and tells you
   what to do. If a step fails later, it shows the last lines of that step's
   log and the log's path.
5. Then the installer
   - builds the world cache from the game's own files (after an interruption
     it continues where it left off),
   - builds the lighting layer (Data/patch-z.mpq and Data/<locale>/patch-<locale>-z.mpq)
     from the game's own light tables in a few seconds,
   - installs the renderer (d3d9.dll), DXVK and the settings files as one
     transaction that can be undone.
6. Make Wine load the game folder's d3d9.dll (once, see below).
7. Start the game yourself as usual.

You can run the installer again at any time: parts that are already up to date
are skipped. Log: the logs folder of the unzipped package.

MAKE WINE LOAD NORTHLIGHT (ONCE)
Wine must load d3d9.dll from the game folder before its own: the DLL override
d3d9 = native, builtin, that is WINEDLLOVERRIDES="d3d9=n,b".
- Lutris: right-click the game > Configure > Runner options > DLL overrides:
  add the key d3d9 with the value n,b. (Lutris builds WINEDLLOVERRIDES from this
  list, so an environment variable under System options may be overwritten.)
- Bottles: the bottle's settings > DLL Overrides (under Advanced in recent
  versions): d3d9 = Native, then Builtin.
- Steam (WoW added as a non-Steam game; Properties > Compatibility: force a
  Proton version): Properties > Launch options:
    WINEDLLOVERRIDES="d3d9=n,b" %command%
- Plain Wine: start the game with
    WINEDLLOVERRIDES="d3d9=n,b" wine Wow.exe
  or set it once in winecfg > Applications > add Wow.exe > Libraries: d3d9,
  native then builtin.
Your launcher's or Wine prefix's own DXVK does not matter: Northlight loads its
own DXVK from renderer-backends/dxvk/dxvk_d3d9.dll in the game folder, whether
the launcher's DXVK option is on or off. Leave that option as it is, and do not
copy a DXVK d3d9.dll into the game folder (it would replace Northlight's
d3d9.dll). Do not use WineD3D modes (PROTON_USE_WINED3D=1, or DXVK switched off
together with d3d9 set to builtin): then Wine never loads the game folder's
d3d9.dll and the game runs without Northlight.
Check: after the first start the game folder has northlight-renderer.log; its
first lines name Northlight renderer __RELEASE_VERSION__ and backend=dxvk
loaded=1 error=0.

Options (bash install.sh ...):
  --client DIR           the game folder (asked when missing)
  --locale enUS          if the client has several locales and the installer cannot tell which one is used
  --backend dxvk2        DXVK 2.7.1 for drivers DXVK 3 does not support
  --backend native       the prefix's System32 d3d9.dll instead of the package's DXVK; under Wine this works
                         only when that is a real DXVK (Proton, or Lutris/Bottles with DXVK on); untested
  --backend legacy       the d3d9.dll that was already in the game folder
  --no-art-layer         no lighting layer (patch-z)
  --no-world-cache       the renderer only, without the cache (no static world shadows and no GI)
  --yes                  accept the recommended answers without asking
Installation status without changes:  bash install.sh status --client ~/Games/WoW
If the game folder already holds someone else's patch-z.mpq, the installer
leaves it in place, skips the lighting layer and builds the cache itself so
that its content is included (it says so at the end).

UPPER AND LOWER CASE
Linux file names are case-sensitive, Wine's are not. If the game folder already
has a file or folder whose name differs from one Northlight writes only in
case (for example D3D9.dll next to d3d9.dll, or Renderer-Backends), Wine could
load either one, so the installer stops and names it. Rename or remove it and
run the installer again.

HOW THE RENDERER IS INSTALLED
The renderer is installed in the game folder as d3d9.dll. The game loads it
like any d3d9.dll (DXVK, ReShade), and the renderer loads the actual Direct3D 9
implementation from elsewhere.
- If the game folder already has another d3d9.dll (for example your own DXVK
  or ReShade), it is moved to renderer-backends/legacy/legacy_d3d9.dll; the
  uninstaller puts it back.
The backend is chosen in northlight-renderer.ini: Backend=dxvk.
- dxvk:   renderer-backends/dxvk/dxvk_d3d9.dll (the package's DXVK 3.1.1, the default)
- dxvk2:  renderer-backends/dxvk2/dxvk2_d3d9.dll (DXVK 2.7.1; bash install.sh --backend dxvk2)
- native: the prefix's System32 d3d9.dll. Under Wine this works only when the launcher put DXVK
          there (Proton, or Lutris/Bottles with DXVK on); with d3d9=n,b Wine's own builtin d3d9
          cannot be loaded this way. Untested; prefer dxvk or dxvk2.
- legacy: renderer-backends/legacy/legacy_d3d9.dll (the game folder's earlier d3d9.dll)
If the game closes at start with DXVK 3.1.1, look at Wow_d3d9.log in the game
folder ("Failed to initialize DXVK", "Device does not support required
feature") and install with --backend dxvk2. Northlight does not switch backends
by itself. A reinstall without --backend keeps an installed dxvk2 or native
choice; --backend dxvk switches back to DXVK 3.
WTF/Config.wtf, login details and addon settings are not part of the package.

QUALITY SETTINGS (northlight-quality.ini)
northlight-quality.ini in the game folder chooses the quality: Preset=Quality (the
default, full quality), Balanced or Performance. The file is read when the game
starts; restart WoW after a change. The installer adds the file if it does not
exist yet; an update never changes your lines or values, it only appends the
settings your file does not mention yet, commented out. The individual
settings are described in the file itself.

UNINSTALL
Close the game and run:
  bash uninstall.sh --client ~/Games/WoW
Every change the installer made is undone from the newest to the oldest,
including the game folder's earlier d3d9.dll and the patch-z files. The cache
(world-cache) is removed only if the installer made it. The restore copies
stay in the game folder's renderer-backups folder. Files changed later are not
overwritten; a conflict stops the restore before any change. Remove the
d3d9=n,b override afterwards if nothing else needs it.

SOURCES AND LICENSES
LICENSES folder: Python (PSF) and the libraries linked into it
(LICENSES/python-third-party), StormLib (MIT) and the zlib, bzip2,
LibTomCrypt/LibTomMath and LZMA SDK that come with it, DXVK 3.1.1 and 2.7.1 (zlib license).
Python: python-build-standalone, https://github.com/astral-sh/python-build-standalone
DXVK 3.1.1: https://github.com/doitsujin/dxvk/releases/tag/v3.1.1
DXVK 2.7.1: https://github.com/doitsujin/dxvk/releases/tag/v2.7.1
BUILD-INFO.json lists the versions and checksums of every part of the package.
