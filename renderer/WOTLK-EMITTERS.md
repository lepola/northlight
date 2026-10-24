# WotLK world emitters and standalone model patches

The normal Northlight installer supplies reviewed, hash-pinned world-model
emitters through `world-cache/lights`. It does not inject those lights into the
M2 models in patch-Z. Native M2 lights are evaluated by the game, while
Northlight's `LocalDirect` pass adds the cached source to the rendered scene;
enabling both for the same authored source applies its lighting twice. This
is visible around fixtures such as inn chandeliers. Cache source IDs and
parent-WMO duplicate suppression cannot deduplicate the game's separate pass.

The light cache remains the owner of these added static world sources,
including multi-region fixtures and lights without a renderable mesh. Existing
native lights and unknown model revisions are preserved. No spatial heuristic
is used to discard nearby independent lamps.

Reinstalling regenerates both owned patch-Z archives from the original chain,
removing world-model light payloads installed by earlier versions of this PR.
The FGL sources and their IDs are unchanged; this fix needs an art-layer rebuild,
not a manual cache edit. Uninstall and rollback use the existing transaction.
A foreign patch-Z is preserved. Separately installed development emitter
overlays (such as patch-Y) are not removed automatically: remove those overlays
before using Northlight's cached lighting for the same sources.

For development, the wider discovery/audit command remains available:

```sh
python renderer/build_emitter_patch.py --client /path/to/WoW --output out/emitter-patch
```

It writes `patch-y.mpq`, a catalog and an audit report without installing files
or launching the client. This is an alternative native-light experiment, not
an overlay to install alongside Northlight's light cache. Newly discovered
models are not installed automatically: review their pins and regenerate the
light cache before release. The release packager refuses prebuilt cache variants
whose builder/catalog sources are stale.

The generator accepts MD20 version 264, preserves existing native lights and
keeps original geometry, material and animation payloads byte-for-byte. New lights
use an appended static identity bone; the client treats a negative light-bone
index as 65535, so it is not a valid sentinel. Separate luminous regions can use
up to 16 reviewed lights. Unknown revisions fail their hash pin. Unlit/broken
variants and unsupported animated sources are skipped rather than guessed.

The catalog contains 182 locally reviewed model patches, including 35
additional models with 69 sources. Nine additions have multiple separated light
regions. Ordinary native sources still take precedence. Generated models retain
the authored source IDs so parent-WMO duplicate suppression continues to work.

The audit covered 11,261 world M2 files, 1,985 root WMOs and 106 available maps.
This does not imply complete instance support: Northlight also needs compatible
scene geometry for an instance. The current builder must be given the relevant
maps; native model lights and Northlight's cached lighting are different paths.

This generator is original MIT-licensed Northlight code. ComfyAtmosphere was a
conceptual reference; no GPL source or Classic assets were copied. Game model
payloads are not covered by the software's MIT license and are deliberately not
included in the repository. Generate and retain those files locally.

## Local shadow policy

Every reviewed world profile carries `casts_shadows`. Torches, candles, lanterns,
chandeliers, braziers and compact hearth flames opt in regardless of flame colour.
Extended molten surfaces, emissive crystal volumes, distributed sparkles, glowing
runes and smoke-only glows opt out. Missing policy opts out. Unclassified native
equipment observations and short-lived particle lights do not get shadow maps.
The builder preserves source IDs and records the policy in FGL1 flag bit 1;
bit 0 retains its original potentially baked WMO meaning.

`PointShadows=1` and `PointShadowLimit=2` are defaults in all presets. The menu can
change the limit from 1 to 4 between frames. Each selected source owns a complete
256x256 six-face cube and static-face cache. Ranking retains incumbent sources;
a change of rank does not exchange another source's cube. Map/resource changes,
source motion and range changes invalidate the corresponding state.

The local pass removes only its own occluded Northlight direct-light contribution,
with identical colour, selection fade, squared attenuation, smooth normal and sun
dimming. Existing baked/native lighting is not decomposed or guessed. At the
receiver, shadow contrast fades below 15% of incident direct sunlight and reaches
full strength at 50%; night and sun-occluded surfaces use the local brightness
floor instead. Solar visibility comes from the sun shadow buffer, not the camera
location or the clock. Different lamps' light still fills one another's shadows.
