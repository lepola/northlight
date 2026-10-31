# Moving world lamps and small fixture shadows

The Zangarmarsh clips show ZangarLamppost01 near Cenarion Refuge. The minimum
character triangle setting rejected its small M2 sections because fixtures and
characters share skinned shaders. World light positions were also fixed FGL1
coordinates, independent of the fixture's current bone palette.

Small captures now prove whether their vertices use one bone. Rigid sections
remain shadow casters at a minimum of 100; multi-bone sections retain the
triangle threshold. The proof reads only the shader's consumed index lane for
single-influence programs. A separate bounded render-thread cache avoids
sharing the preparation worker's mutable cache. These small candidates need a
bounded geometry snapshot before their rigidity can be distinguished.

The light builder writes optional FGA1 attachment records beside FGL1. Each
record identifies a skin section by its submitted triangle, an unchanged
reference bone by its placed world matrix, and the source's emitting bone.
Current captured matrices update position without changing the source ID,
colour, attenuation or shadow policy. The direct, fog and point-shadow passes
receive the same updated source. Existing point-shadow scheduling already
refreshes all faces when a source moves.

Authored sources use the matching coloured mesh/particle anchor. Inferred glow
cards follow their physical parent: camera-facing rotation and glow scaling
must not move the physical source. Inactive particle children can inherit a
parent that occurs in the skin palette. Explicit animated native light bones
remain authoritative. Unsupported native attachments are excluded rather than
exported as frozen lights.

Attachment updates require the emitting and fixed reference bones in the
submitted palette and a matching placed skin section. Off-screen models retain
their last observed source position until captured again; this does not replay
animation for models the client does not submit. Static GI still uses the
offline source position. This work does not add model identity hooks for moving
server objects without cached placements.

Run the new installer to regenerate lighting caches. A DLL replacement alone
restores small fixture shadows, but older caches have no attachment metadata.
Missing FGA1 files remain compatible with the earlier fixed-light behaviour.
The installer packages the builder and carries FGA1 through continent caches,
optional instance updates and cache variants.

This independent branch checks real active/original Zangar and Darnassus assets, a stationary billboard control, camera and pose invariance, exact instance matching, malformed metadata, pruning and cache lane isolation. Its production capture oracle retains a 99-triangle rigid section at 100, rejects the multi-bone control at 100 and admits it at 99, while checking two live source positions. The complete point-cube coverage, capture-gap and upload-handoff matrix is separate combined #98 evidence. The independent Windows x86 DLL build, native D3D9/DXVK capture oracle, native attachment tests, cache-variant/instance transactions and Windows-installer checks pass. No gameplay replay or FPS result is claimed.
