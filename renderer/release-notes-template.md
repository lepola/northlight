## Northlight <v>

<!-- Template for the GitHub release notes: replace <v>, write the changes, paste the SHA-256 lines
     from SHA256SUMS.txt. The Download and Install sections below are the same for every release. -->

### Changes since <previous version>
- ...

### Download
- `Northlight-<v>-macOS.zip` — macOS with WoWSilicon (Apple Silicon)
- `Northlight-<v>-Windows.zip` — Windows 10/11 (bundles DXVK 3.1.1, with DXVK 2.7.1 as an alternative)
- `Northlight-<v>-Linux.zip` — Linux (x86_64, glibc 2.17+) for a game run with Wine or Proton (Lutris,
  Bottles, Steam); the same renderer and DXVK builds as Windows. **The Linux package has not been tested in
  game yet**: it passes the automated package checks, but nobody has played with it under Wine or Proton.
  Reports are welcome.

Nothing else to install: each package bundles its own Python and StormLib.

### Install
1. Close the game.
2. Unzip the package **outside** the game folder.
3. macOS: open Terminal, type `bash ` and drag `Install Northlight.command` into the window, press Enter.
   Windows: run `Install.cmd`.
   Linux: open a terminal in the unzipped folder and run `bash install.sh`.
4. Point it at your game folder (the one with `Wow.exe`). The installer builds the world cache and the lighting
   art layer from your own client (about 10–40 minutes, at least 8 GB of RAM) and never modifies `Wow.exe`.
5. Linux only, once: make Wine load the game folder's `d3d9.dll` with `WINEDLLOVERRIDES="d3d9=n,b"`.
   Lutris: Configure > Runner options > DLL overrides, `d3d9` = `n,b`. Bottles: the bottle's DLL Overrides,
   `d3d9` = Native, then Builtin. Steam/Proton: launch options `WINEDLLOVERRIDES="d3d9=n,b" %command%`.
   Northlight brings its own DXVK, so leave your launcher's DXVK setting as it is. The package's README.txt
   has the details.
6. Start the game yourself.

Updating from an earlier version: run the new installer the same way; the world cache is kept if it is up to date.
An existing `northlight-quality.ini` keeps every line and value; settings it does not have yet are appended
with their comments, commented out.
Uninstall with `Uninstall Northlight.command` / `Uninstall.cmd` / `bash uninstall.sh`; it restores every change.

### SHA-256
```
<sha256>  Northlight-<v>-macOS.zip
<sha256>  Northlight-<v>-Windows.zip
<sha256>  Northlight-<v>-Linux.zip
```
