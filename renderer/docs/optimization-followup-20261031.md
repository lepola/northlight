# Overlay state ownership follow-up (2026-10-31)

The renderer already holds a full SavedState around native menu rendering. Let this caller opt out of the menu backend's duplicate state block while preserving explicit world/view/projection restoration. Other callers retain the default standalone save/restore path. This avoids an extra state-block creation/capture/apply without weakening failure handling.

The independent actual-menu oracle compares all six SDR page cases on native D3D9 and DXVK 3, including sentinel state, transforms and refused duplicate allocation. The independent x86 DLL builds. This branch remains SDR-capable on its own; #94 supplies experimental HDR transfer when integrated, and #98 checks the combined version. No game run or duration/FPS benchmark is used.
