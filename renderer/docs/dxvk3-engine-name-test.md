# Experimental DXVK 3 engine-name colour workaround

`[Renderer] DxvkEngineNameWorkaround=1` in `northlight-renderer.ini` enables an
experimental native-Windows workaround for NVIDIA presentation/colour issues.
The default is 0. Restart WoW after changing it. This key is independent of
`ExperimentalHDR`; test SDR and HDR separately.

The configuration example is `dxvk3-engine-name.ini.example`. Merge its keys
into the existing `[Renderer]` section; keep other renderer/HDR settings.
To ship this workaround, build Northlight's `d3d9.dll` with these source changes
and expose this opt-in key. Continue shipping the original DXVK DLL. No DXVK
source patch or prepatched backend is required.

The selected, recognised DXVK 3 backend's `GetProcAddress` import is redirected
after loading and before its first Direct3D factory call. The redirect intercepts
Vulkan instance creation and changes `VkApplicationInfo.pEngineName` from `DXVK`
to `Northlight`. All remaining application/instance parameters are forwarded.
The original DXVK DLL on disk and the system Vulkan loader are unchanged. No
driver profile, registry setting, administrator access, or external utility is
required. DXVK 2, native D3D9, legacy backends, and Wine skip the workaround.
The callback's owning Northlight module is pinned until process exit before its
address is installed. WoW unloads and reloads its D3D9 probe; leaving a callback
into an unloaded DLL caused the first test build's startup crash.

When eligible, Northlight appends `dxvk.allowFse = False` to its process-local
DXVK configuration, after existing inline assignments. Keep WoW windowed or
borderless and also set `dxvk.allowFse = False` in `dxvk.conf` for the off run.
The per-backend import redirection adds no per-frame work.

Expected log entries in `northlight-renderer.log`:

```
DXVK3 engine-name requested=1 eligible=1 wine=0 backend=dxvk version=v3.1.1
DXVK3 engine-name FSE forced=False process-config-applied=1
DXVK3 engine-name import status=active-module-pinned
DXVK3 engine-name instance original=DXVK replacement=Northlight result=0
```

Compare 0 and 1 with otherwise identical configuration, restarting each time.
Check scene/UI colours, Alt+Tab out and back, Windows Magnifier while the game
is focused, and return from minimize. Readback tests cannot establish final
monitor colour accuracy or Magnifier usability; those need an interactive test.
Changing the engine identity may bypass other NVIDIA DXVK-specific behaviour,
so this is a diagnostic build, not an unconditional production default.

References:
- https://github.com/doitsujin/dxvk/blob/v3.1.1/src/vulkan/vulkan_loader.cpp
- https://github.com/doitsujin/dxvk/blob/v3.1.1/src/dxvk/dxvk_instance.cpp
- https://forums.developer.nvidia.com/t/windowed-vulkan-hdr-is-presented-through-the-sdr-path-when-vkapplicationinfo-penginename-is-exactly-dxvk/382517
