NORTHLIGHT RENDERER __RELEASE_VERSION__ — macOS (WoWSilicon)

Northlight renderer adds lighting, shadows, fog, indirect light (GI) and sky
effects to the WoW 3.3.5a client. The package works with both an HD client and
an unmodified (stock) 3.3.5a client. You do not need to install anything else:
the installer's own Python and StormLib are in the package (the Xcode tools and
the system Python are not used). The installer never starts the game or
WoWSilicon and never changes wow.exe.

DOWNLOADS
- Northlight-__RELEASE_VERSION__-macOS.zip: the installer and the renderer.
- Nothing else: the installer builds the world cache (world-cache) itself from
  the game's own files, for an unmodified and an HD client alike (10–40 min).

REQUIREMENTS
- An Apple Silicon Mac and WoWSilicon, with this client patched with the DXVK
  backend (WoWSilicon: Patch, with DXVK as the graphics backend, not MTLD3D).
- Only ASCII characters in the game folder path (no ä, ö, å or other special characters).
- About 13 GB of free disk space and at least 8 GB of memory while the
  installer builds the cache.

INSTALLATION
1. Close WoW and WoWSilicon completely.
2. Extract Northlight-__RELEASE_VERSION__-macOS.zip OUTSIDE the game folder
   (for example into Downloads; Safari usually extracts it itself).
3. Open Terminal. Type  bash  and a space, drag "Install Northlight.command" from
   the extracted folder into the Terminal window and press Enter.
   (Double-clicking the file works only if macOS does not block it; the bash
   command always works. The installer removes the download mark only from its
   own folder, so that its Python and StormLib are allowed to start.)
4. Choose the game folder by its number, or drag the game folder into the
   window and press Enter. The game folder can also be given directly:
     bash "<dragged Install Northlight.command>" --client "/path/to/game/folder"
5. The installer first checks the client, WoWSilicon's DXVK setting, the disk
   space and the package. If something is missing, it stops before anything is
   changed and tells you what to do.
6. Then the installer
   - builds the world cache from the game's own files (after an interruption
     it continues where it left off),
   - builds the dungeon and raid groups you chose (optional, none by default;
     see "Dungeon and raid world cache" below),
   - builds the lighting layer (Data/patch-z.mpq and Data/<locale>/patch-<locale>-z.mpq)
     from the game's own light tables in a few seconds,
   - installs the renderer as mods/d3d9.dll, adds it to WoWSilicon's
     dlls.txt file and writes northlight-renderer.ini (Backend=legacy,
     BackendPath=renderer-backends/dxvk/dxvk_d3d9.dll = WoWSilicon's own DXVK).
     The game folder's d3d9.dll (WoWSilicon's DXVK) and wow.exe stay unchanged.
   - sets extShadowQuality=0 in WTF/Config.wtf to prevent native detailed
     shadows overlapping Northlight. The original config is backed up.
7. Start the game from WoWSilicon as usual.

You can run the installer again at any time: parts that are already up to date
are skipped. Log: the logs folder of the extracted package.

Options (add them after the command):
  --locale enUS          if the client has several locales and the installer cannot tell which one is used
  --no-art-layer         no lighting layer (patch-z)
  --no-world-cache       the renderer only, without the cache (no static world shadows and no GI)
  --instances 1,2,6      the dungeon and raid groups to have, without the question: numbers 1-6, names, all or none
  --yes                  accept the recommended answers without asking

Dungeon and raid world cache (optional): the continents get static world
shadows and GI from the world cache. Dungeons and raids do not, unless you
add them. The installer asks while it checks the client, before anything is
built, and builds the chosen groups after the continents:
  1  Classic dungeons  ~0.6 GB  ~1.7 min
  2  Classic raids     ~0.3 GB  ~0.9 min
  3  TBC dungeons      ~0.6 GB  ~1.8 min
  4  TBC raids         ~0.4 GB  ~1.3 min
  5  Wrath dungeons    ~1.3 GB  ~2.3 min
  6  Wrath raids       ~0.7 GB  ~1.9 min
Type the numbers ("1 2 6"), all or none; Enter keeps what the game folder has
now (none on a first install). The sizes are disk space on top of the
continents; the times are build time of one group on one CPU core. Groups
that were built earlier are kept when you install again or update the
continents; leaving a group out of the answer removes it and the files only it
used. --instances answers the question for scripted installs; --yes keeps the
current choice.

If the game folder already holds someone else's patch-z.mpq, the installer
leaves it in place, skips the lighting layer and builds the cache itself so
that its content is included (it says so at the end).
If WoWSilicon's "Patch" is run again later, run the installer again.
Installation status without changes:  bash "<dragged Install Northlight.command>" status --client "/path/to/game/folder"

QUALITY SETTINGS
northlight-quality.ini in the game folder chooses the quality: Preset=Quality (the
default), Balanced or Performance. The file is read when the game starts. The
installer adds it if it does not exist yet. An update enables newly introduced
Quality effect bundles once when Quality is selected. Other overrides and later
edits to migrated effects stay intact. Balanced and Performance are unchanged.
Missing ordinary settings are appended commented out.

UNINSTALL
Close the game and WoWSilicon. In Terminal, type  bash  and a space, drag
"Uninstall Northlight.command" into the window and press Enter. Every change the
installer made is undone from the newest to the oldest (mods/d3d9.dll, the
dlls.txt line, the settings files, patch-z). The cache (world-cache) is removed
only if the installer made it. The renderer-backups folder in the game folder
keeps only the files that were there before Northlight first changed them;
every update removes the copies of older Northlight versions, and uninstall
puts those originals back. Files changed later are not overwritten.

SOURCES AND LICENSES
LICENSES folder: Python (PSF; python-build-standalone), StormLib (MIT) and the
zlib, bzip2, LibTomCrypt/LibTomMath and LZMA SDK that come with it.
LICENSES/python-third-party: the libraries built into Python (OpenSSL, expat,
libffi, mpdecimal, bzip2, xz, SQLite, libuuid, HACL*, mimalloc).
BUILD-INFO.json lists the versions and checksums of every part of the package.
