NORTHLIGHT RENDERER __RELEASE_VERSION__ — WINDOWS

Northlight renderer adds lighting, shadows, fog, indirect light (GI) and sky
effects to the WoW 3.3.5a client. The package works with both an HD client and
an unmodified (stock) 3.3.5a client. You do not need to install anything else:
the installer's own Python, StormLib and DXVK are in the package. The installer
never starts the game and never changes wow.exe.

DOWNLOADS
- Northlight-__RELEASE_VERSION__-Windows.zip: the installer and the renderer (about 20 MB).
- Nothing else: the installer builds the world cache (world-cache) itself from
  the game's own files, for an unmodified and an HD client alike (10–40 min).

REQUIREMENTS
- 64-bit Windows 10 or 11 and a WoW 3.3.5a (12340) client.
- A graphics driver with Vulkan for x86 programs. The default DXVK 3.1.1
  still asks for Vulkan 1.3 but also needs extra device features (for example
  maintenance6, 8/16-bit storage and scalarBlockLayout), so an old driver or
  GPU (some AMD Polaris/Vega and Intel Gen9 graphics) may not be supported.
  On such a driver the first start ends (see below) and the following starts
  use DXVK 2.7.1 (the dxvk2 backend) by themselves.
- AMD RX 5000/6000 (RDNA 1/2): DXVK's own release notes say DXVK 3 performs
  badly on them on Windows. Install with Install.cmd --backend dxvk2.
- Only ASCII characters in the game folder path (no ä, ö, å or other special
  characters), a path of at most about 150 characters, and the game must not
  be in Program Files.
- About 13 GB of free space on the game's drive and at least 8 GB of memory
  while the installer builds the cache.

INSTALLATION
1. Close WoW completely.
2. Extract Northlight-__RELEASE_VERSION__-Windows.zip OUTSIDE the game folder,
   for example into Downloads.
3. Open Install.cmd in the extracted folder and give the game folder path (the
   folder that holds wow.exe), for example C:\Games\WoW. Or, in a command prompt:
     Install.cmd --client "C:\Games\WoW"
4. The installer first checks the client, the disk space and the package. If
   something is missing, it stops before anything is changed and tells you
   what to do.
5. Then the installer
   - builds the world cache from the game's own files (after an interruption
     it continues where it left off),
   - builds the lighting layer (Data\patch-z.mpq and Data\<locale>\patch-<locale>-z.mpq)
     from the game's own light tables in a few seconds,
   - installs the renderer (d3d9.dll), DXVK and the settings files as one
     transaction that can be undone.
6. Start wow.exe yourself.

You can run the installer again at any time: parts that are already up to date
are skipped. Log: the logs folder of the extracted package.

Options (Install.cmd ...):
  --locale enUS          if the client has several locales and the installer cannot tell which one is used
  --backend dxvk2        DXVK 2.7.1 for AMD RX 5000/6000 or drivers DXVK 3 does not support
  --backend native       Windows' own Direct3D 9 instead of DXVK
  --backend legacy       the d3d9.dll that was already in the game folder (for example your own DXVK or ReShade)
  --no-art-layer         no lighting layer (patch-z)
  --no-world-cache       the renderer only, without the cache (no static world shadows and no GI)
  --yes                  accept the recommended answers without asking
Installation status without changes:  Install.cmd status --client "C:\Games\WoW"
If the game folder already holds someone else's patch-z.mpq, the installer
leaves it in place, skips the lighting layer and builds the cache itself so
that its content is included (it says so at the end).
An old full renderer package (0.3.98–0.3.144), which installed the world-cache
with the package, is undone first; the installer asks about this.

HOW THE RENDERER IS INSTALLED
The renderer is installed in the game folder as d3d9.dll. WoW loads it itself,
the same way as any d3d9.dll (such as DXVK or ReShade), and the renderer loads
the actual Direct3D 9 implementation from elsewhere.
- If the game folder already has another d3d9.dll (for example your own DXVK or
  ReShade), it is moved to renderer-backends\legacy\legacy_d3d9.dll. Only the
  renderer may have the module name d3d9.dll. Uninstall.cmd puts it back in
  the game folder. The ReShade/ENB settings files (ReShade.ini,
  reshade-shaders, enbseries.ini) stay in the game folder.

The backend is chosen in northlight-renderer.ini: Backend=dxvk.
- dxvk:   renderer-backends\dxvk\dxvk_d3d9.dll (the package's DXVK 3.1.1, the default)
- dxvk2:  renderer-backends\dxvk2\dxvk2_d3d9.dll (DXVK 2.7.1; for AMD RX 5000/6000 and
          for drivers DXVK 3 does not support: Install.cmd --backend dxvk2 or
          Backend=dxvk2 in northlight-renderer.ini)
- native: Windows' own System32\d3d9.dll
- legacy: renderer-backends\legacy\legacy_d3d9.dll (the game folder's earlier d3d9.dll)
On a driver DXVK 3.1.1 does not support, DXVK usually throws an error while
the game starts, and the game closes (DXVK's own log, Wow_d3d9.log in the
game folder, says "Failed to initialize DXVK" or "Device does not support
required feature"). The renderer marks that start in
renderer-backends\dxvk\northlight-dxvk3-init.pending. The renderer does not
switch backends within a start: when the DXVK 3 start fails or finds no
adapter, the next start uses DXVK 2.7.1 (the log says BACKEND DXVK 3 start
failed reason=...; the next start uses dxvk2, and later starts say BACKEND
FALLBACK dxvk -> dxvk2). So the first start can fail and the next one works.
The marker is tied to the DXVK 3 build it was written for: a package with a
new DXVK 3 build tries DXVK 3 again by itself. To try DXVK 3 again on the same
build after a driver update, delete the .pending file or run
Install.cmd --backend dxvk (the file is removed once that install succeeded).
To avoid the failed first start, install with --backend dxvk2. If DXVK 2.7.1
fails too (the driver has no Vulkan 1.3), use --backend native.
Known limitation: if the game is closed or crashes during the short DXVK 3
start (for example an overlay or Vulkan layer crashes), the next starts also
use DXVK 2.7.1 until the .pending file is deleted or Install.cmd --backend dxvk
is run.
A reinstall without --backend keeps an installed dxvk2 or native choice and a
pending marker; --backend dxvk switches back to DXVK 3 and clears the marker.
Some antivirus products flag 32-bit DXVK builds as a false positive. If one
removes a DXVK file of the package, the installer still installs the backend you
use and says which one is not available; an older copy of that file in the game
folder is removed, so nothing unverified is loaded. A DXVK file that is present
but damaged or modified is always refused: download and unzip the package again.
If the file of the backend you install is missing, the installer stops and
names the options: allow the file in the antivirus product and unzip again, or
install with --backend dxvk2 (for the dxvk file) / --backend dxvk (for the
dxvk2 file) / --backend native.
BackendPath= can also point to another D3D9 implementation (a path relative to
the game folder or a full path); the file name must not be d3d9.dll (name the
copy, for example, my_d3d9.dll). The renderer never loads itself: if the chosen
path points to the renderer itself or to another renderer version, it uses
Windows' d3d9.dll and writes the reason to the log (BACKEND SELF-LOAD REFUSED).
Windows system files are not changed.
Wine/CrossOver/Lutris: the game folder's d3d9.dll loads only if d3d9 is set to
native (WINEDLLOVERRIDES=d3d9=n,b), as DXVK also requires.
WTF/Config.wtf, login details and addon settings are not part of the package.

QUALITY SETTINGS (northlight-quality.ini)
northlight-quality.ini in the game folder chooses the quality: Preset=Quality (the
default, full quality), Balanced or Performance. On a weaker processor, Balanced
or Performance raises the FPS by lightening mainly shadows and lamps.
The file is read when the game starts; restart WoW after a change.
The installer adds the file only if it does not exist yet: an update never
replaces your own changes. northlight-renderer.log shows the values in use (QUALITY).
Individual settings (Quality / Balanced / Performance):
  ActorShadowBudgetMiB  0 / 16 / 8     character shadows, nearest first (0 = no limit)
  ActorShadowRadius     40 / 35 / 20   characters more than N yards from your own character cast no shadow (0..200; 0 = no limit; 1..3 = your own character, mount, weapons and whatever is right next to you)
  ActorShadows          1 / 1 / 1      shadows of characters and moving objects (0 = static shadows only: terrain, buildings, and the trees and objects placed on the map; characters, creatures, mounts and pets lose their shadow, your own character too, and so do objects the server places, such as doors, elevators, ships, zeppelins, mailboxes and event decorations; swaying trees, windmills and flags keep a shadow frozen in their rest pose; the game's own round shadows return under characters; saves about 4–5 ms per frame in crowds and about 1 ms in quiet areas; with GIDynamicProbes=1 characters are still copied about every 200 ms for indirect light, GIDynamicProbes=0 removes that too; 0 also turns off the ShadowFateDiagnostics and DiagReplayProbe settings; no preset changes this); with 1, shop signs and other small still objects the server places keep their shadow when the camera turns away
  MinSkinnedTriangles   100/150/180    small animated parts cast no shadow
  FarShadowInterval     4 / 5 / 6      distant shadows (beyond ~48 m) are drawn every Nth frame (1..16)
  NearShadowInterval    1 / 2 / 2      moving parts of the near shadows (characters) every Nth frame (1..16)
  LocalLightLimit       32 / 24 / 16   lamps lighting at the same time (8..64; above 32 = more distant lamps too)
  PointShadows          0 / 0 / 0      lamp shadows (1 = on): only lights inside buildings cast faint shadows, at dusk and night; street lamps, lanterns and torches never do; off by default
  PointShadowRefreshMs  0 / 33 / 33    lamp shadow update interval
  PointShadowFacesPerFrame 6 / 6 / 6   lamp shadow directions updated per frame (1..6; 6 = all at once; lower = smaller spikes, a brief seam at the edge)
  ShadowDirectionSteps  2048/2048/2048 sun direction quantization (fewer jumps)
  StaticCacheSlices     1 / 1 / 1      a partial redraw of the cached shadow is spread over N frames (1..4; 1 = in one frame; a new shadow can appear N-1 frames late)
  CaptureBudgetMiB      32 / 32 / 32   capture limit for animated geometry (your own character and mount may use an extra 4 MiB once it is reached)
  GI                    1 / 1 / 1      indirect light (0 = off, the background computation too)
  GIRays                64 / 48 / 32   rays per light probe
  GIBounces             3 / 3 / 2      light bounces
  GIProbeMoveStep       8 / 8 / 16     camera movement before the probes are updated
  GIDynamicProbes       1 / 0 / 0      characters' effect on indirect light
  GIThreads             1 / 1 / 1      computation threads (raise only if you have enough cores)
  GIFastBVH             1 / 1 / 1      new fast ray tracing (0 = the 0.3.137 version)
  GIStrength            60 / 60 / 60   indirect light strength in percent (0..100; 60 = standard, 100 = maximum)
  GIDistance            76 / 52 / 52   reach of the indirect light from the camera in yards (36..84, rounded down to 36/44/52/60/68/76/84; above 52 the background computation is about 1.3x (60), 1.65x (68), 2x (76) and 2.5x (84), a new area fills more slowly and slightly more memory is used; small FPS impact, because the larger light texture is updated while moving)
  GIProbeAhead          0 / 0 / 0      moves the indirect light area forward from the camera towards your own character, in yards (0..48; 0 = around the camera; the camera is about 15–27 yards behind the character; values near the camera distance, about 15–25 (20 recommended), bring light further ahead of the character and reduce background computation when the camera turns; large values (40+) increase it; turning the camera can move the area)
  HorizonHaze           50 / 50 / 50   horizon haze: distant terrain and the lowest band of the sky in the game's own fog colour, in percent (0..100; 0 = off, the image as in 0.3.153; the fog toggle Ctrl+Shift+F7 also turns off the haze; small FPS impact)
  HorizonHazeStart      75 / 75 / 75   where the haze starts on the terrain, in percent of the game's own fog end distance (50..95; nearer terrain does not change; raise to 85–95 if the haze reaches too close)
  HorizonHazeBand       6 / 6 / 6      how many degrees above the horizon the haze rises in the sky (2..15)
  HorizonHazeTerrain    1 / 1 / 1      1 = haze on distant terrain and in the sky, 0 = only the sky band
  ShadowFateDiagnostics 0 / 0 / 0      shadow diagnostics in the log (1 = on; the image does not change)
  Diagnostics           0 / 0 / 0      periodic statistics and timings in the log (0 = only startup, settings and errors; the image does not change)
  RenderProfile         0 / 0 / 0      measurement: render thread timings in the log (needs Diagnostics=1; the image does not change)
  DiagReplayProbe       0 / 0 / 0      measurement: the near-shadow character draws a second time, hidden, in 10 s periods, and the timing in the log (needs RenderProfile=1; the image does not change)
  FrameDrawGates        1 / 1 / 1      per-frame draw checks (0 = check every draw as before 0.3.187, for comparisons; the image does not change)
  ShadowPivotCorrection 1 / 1 / 1      near shadow detail follows camera zoom and collisions (0 = the distance of the sharp shadow area is only estimated while orbiting, as before 0.3.190, for comparisons)
  CommandStream         1 / 1 / 1      the game records its graphics calls and a second thread runs the renderer (0 = everything on the game's thread as before 0.3.192, for comparisons; read at game start)
When both NearShadowInterval and FarShadowInterval are at least 2, frames that
draw neither shadow also skip copying the character geometry (about 1–1.5 ms
of CPU per skipped frame). Light and normal frames alternate: the average FPS
rises, the slowest frames do not get faster.
Near shadows lag by at most N frames (2: about 33 ms at 60 FPS); the shadow of a
fast-moving character can appear to step at a low FPS. The benefit is largest
when FarShadowInterval is a multiple of NearShadowInterval (for example 2 and 4).
With shadows off (Ctrl+Shift+F9), the copying is done only for indirect light.
The old shadow-experiment.ini applies only with Preset=Quality.
Every preset turns on the distance selection of character shadows (ActorShadowRadius,
and ActorShadowBudgetMiB in Balanced/Performance; about 0.4 ms); the benefit comes
from drawing clearly fewer characters into shadows.
The file can be saved as UTF-8, ANSI or Notepad's Unicode.

WINDOWS TEST
Before a test or a problem report, set Diagnostics=1 in northlight-quality.ini
(the default is 0); the log then contains the periodic statistics and timings.
The LOCK METER line (every 2 s) shows how much buffer data the game and the
renderer lock per frame, which DXVK 3 counts towards its upload throttling:
discardKiB/frame and stagingKiB/frame (avg, max), over10MiB = frames above
10 MiB, the share of each source (replayVB, replayIB, liveIB, arena, instances,
game), ring = the renderer's upload ring wraps (fenceReuse = no DISCARD needed),
readback = the renderer's read-only locks of game buffers (dynamic, defaultStatic,
other) with the lock flags used (flag=0x1010 on DXVK 3, 0x10 otherwise).
MEMMAP lines (always on, a few per device lifecycle point): when the game recreates
the D3D9 device (for example after an MSAA change) the log gets an address-space
snapshot at destroy-begin, destroy-end, create and at frame 300 of each device
(device 1's is "baseline-frame300"). MEMMAP summary = committed/reserved MiB per
type (image/mapped/private), free and largest free block; top = the 12 largest
allocations (module= names an image); diff-new / diff-gone / diff-changed = what
appeared, vanished or changed by 1 MiB or more since the previous snapshot;
process, heaps, threads give the working set, heap sizes and thread/module counts;
northlight = Northlight's own cheap tallies. Compare destroy-begin with destroy-end
to see which allocations of the old device survive.
1. Check the start of the new run's northlight-renderer.log file:
   Northlight renderer <version>; d3d9.dll proxy ... backend=dxvk ... loaded=1 error=0
   The backend path must point to renderer-backends\dxvk\dxvk_d3d9.dll.
   The BACKEND selected=... runtime=v3.1.1 line (v2.7.1 with dxvk2) gives the loaded DXVK version and
   the HOST line the path of the wow.exe used.
   The Backend capabilities line is expected to show INTZ=1 RESZ=1 floatRT=1 SM3=1.
   If the log has BACKEND FALLBACK dxvk -> dxvk2, DXVK 2.7.1 is in use because
   DXVK 3 found no supported adapter or an earlier start ended while it started.
   The first launches compile shaders (cached under %LOCALAPPDATA%), so they
   stutter more. To locate a GPU hang, start the game with the environment
   variable DXVK_DEBUG=hang set and send the log.
2. Test the Stormwind crowd and Tanaris/Gadgetzan. Check the shadows of
   characters and trees, GI, fog, the sun being covered, and camera rotation.
3. Let the shaders warm up for one round. Stop at the same view:
   20 seconds with the mod on, Ctrl+Shift+F10, 20 seconds off, the same key,
   20 seconds on. Keep the camera still and the game in the foreground.
4. Also test Alt+Tab and returning to the game. Note any flicker, missing
   shadows, crash or other difference from the Mac test.
5. Close the game and save northlight-renderer.log before the next start.
   Also take a new wow_d3d9.log if one appears; check its timestamp so that
   you do not send an old log that came with the client. Report the GPU,
   the driver version, the resolution and the estimated number of players.

UNINSTALL
Close the game and open Uninstall.cmd. Give the same game folder. Every change
the installer made is undone from the newest to the oldest, including the game
folder's earlier d3d9.dll and the patch-z files. The cache (world-cache) is
removed only if the installer made it. The restore copies stay in the game
folder's renderer-backups folder. Files changed later are not overwritten;
a conflict stops the restore before any change.
  Uninstall.cmd --client "C:\Games\WoW"

SOURCES AND LICENSES
LICENSES folder: Python (PSF), StormLib (MIT) and the zlib, bzip2,
LibTomCrypt/LibTomMath and LZMA SDK that come with it, DXVK 3.1.1 and 2.7.1 (zlib license).
DXVK 3.1.1: https://github.com/doitsujin/dxvk/releases/tag/v3.1.1
DXVK 2.7.1: https://github.com/doitsujin/dxvk/releases/tag/v2.7.1
BUILD-INFO.json lists the versions and checksums of every part of the package.
