# Equipment and particle lights

The normal Northlight installer packages and runs the equipment and conservative
spell generators as part of its art-layer transaction. Their model-only entries
are merged into both patch-Z archives. No separate MPQ installation, source
checkout or post-install command is required. Reinstalling reads the original
archive chain without Northlight's own patch-Z; uninstall restores the original
files through the existing installer transaction. Foreign patch-Z archives are
preserved, so their owners must resolve any art-layer conflict.

Quality enables `EquipmentLights=1` and `CommandStream=1` by default, including
live particle lighting for accepted equipment, mount and spell draws.
Quality upgrades enable the new equipment/particle bundle and its CommandStream
dependency once, including old explicit zeros. Later player edits are preserved.
Balanced and Performance keep equipment lights
off. No Spell.dbc or other gameplay table is modified.

When a supported server creature cache exists, installation also generates the
proven mount M2 additions. A fresh client without that cache still gets live
mount-particle lighting; the installer never guesses server creature mappings.
An absent or malformed WDB does not prevent installation. The following commands
are development/audit entry points, not player installation requirements.

`python renderer/build_equipment_emitters.py --client CLIENT --output STAGING`
reads the user's own 3.3.5a MPQs and creates `equipment-lights-staged.mpq`
plus a report with source archives, model hashes, bone anchors, particle/texture
provenance and exclusions. It never installs anything. StormLib uses the normal
`NORTHLIGHT_STORMLIB` / `northlight.local.ini` path configuration.

Only native v264 models under `Item/ObjectComponents` and spell models referenced
by `ItemVisualEffects.dbc` qualify. A compact, continuously enabled additive
particle must supply a literal colour track and a decodable literal texture.
The new light stays on that particle's original animated bone. Existing lights,
geometry, animations, materials and particle data remain intact.

The 12340 [native material mapping](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Model2/M2Model.cpp#L7394)
maps particle blend 3 to NoAlphaAdd and 4 to Add; blend 2 is ordinary alpha.
The [native light update](https://github.com/SatyPardus/wowdecomp/blob/b1c71b8bd108823bca952f16c6f83a7ad33b9648/Engine/Source/Model2/M2Model.cpp#L2513)
transforms each light through its bone and scene view inverse. These are version
3.3.5a/build 12340 references, not a later client's particle enum.

Particle flag 0x10 is supported: the 12340 particle renderer applies the
emitter transform at draw time, so the light follows the same original bone.
For world-space particle trails the light approximates the current emitter
anchor rather than illuminating every old particle separately.

Particle-derived equipment lights retain the calibrated texture/lifetime
radiance. Particle count (emission rate times lifetime) is not another intensity
multiplier: it made the Utgarde dagger's three sources 2.8, 9 and 50 times too
strong. The original rate track still controls intensity relative to its peak,
and the enabled track still turns the source off. This matches the live particle
path's policy of measuring emission without summing every sprite. These values
are generated offline; correcting them adds no per-frame work. Existing
installations need regenerated equipment entries in both Northlight patch-Z
archives; replacing the renderer DLL alone cannot fix their stored intensity.

Every generated native record is read back with the existing track parser and
every MPQ entry is reopened and byte-compared. Generated model binaries belong
to the user's client and must not be committed or redistributed as source.

For a deliberate standalone development test, use a verified free patch slot.
`EquipmentLights` defaults to 1 in Quality and 0 in Balanced/Performance; an
explicit `[Quality] EquipmentLights=0/1` overrides that preset. The native engine
must expose the light in a proven Model VS2/3 draw; Northlight then reuses the
same bounded local-light and fog passes. Dynamic sources share `LocalLightLimit`,
cap at eight, and fade out after disappearing from native draws.

This is an average colour/opacity and compact artistic falloff, not individual
particle simulation. Shader-only glows, ribbons, replacement colours, burst,
multitexture and nonconstant emitter state are excluded. An equipment model with
no qualifying source receives no guessed white light. Native VS1 and sources
not selected into native point-light constants remain unsupported.

When more than two anchors qualify, keep the two strongest proven sources and
report omitted sources. This preserves the model instead of excluding it after
support for more particle flags is added. Capture deduplicates semantic light
banks across a frame and reserves half its reads/banks for skinned draws. This
does not identify a source as belonging to the player; nearby creatures and
animated scenery may share that draw type.

## Mounts

`python renderer/build_mount_emitters.py --client CLIENT --output STAGING`
reads mounted-aura creature entries from Spell.dbc, resolves them through this
server's `Cache/WDB/LOCALE/creaturecache.wdb`, then reads CreatureDisplayInfo and
CreatureModelData. A creature entry is not a display ID. Uncached entries are
reported rather than guessed. `--creature-cache PATH` selects another cache.

Only actual additive particle colours and textures qualify. The two strongest
anchors keep their original bones, emission-rate and enabled-state tracks.
Models with existing lights are preserved. A shared model with display-specific
particle replacement colours is excluded when no universal light colour exists.
Shared NPC uses of a mount model also receive its native light. Existing source
bytes, particles, geometry, DBC and WDB remain unchanged; the MPQ contains M2 only.

## Spells

`python renderer/build_spell_emitters.py --client CLIENT --output STAGING
--exclude-report EQUIPMENT_REPORT` stages a separate, conservative spell patch.
The default accepts only exclusively referenced missile models with luminous,
finite particle tails and no ribbons. Read-only DBC references prove the model's
owners; nothing edits or packs Spell.dbc. Original animated rate and enabled
tracks drive the new light. A 1 ms global colour refresh keeps the 12340 native
light updater committing changing intensity.

The current client yields only one such safe holiday missile. General cast,
impact, aura and area models are deliberately excluded: the native engine can
stop their particles without stopping M2 lights. `--audit-broad` is for reports
only and its output is marked unsuitable for installation. Adding live spell
illumination broadly uses the live draw path below; these M2 patches do not
provide that capability.

## Live particle lighting

With `CommandStream=1`, `EquipmentLights=1` also samples accepted native Particle, ParticleUnlit and
single-texture additive Model draws. The shader's proven position, COLOR0 and
UV instructions supply the current particle position and colour. Texture RGB
and alpha come from successful CPU uploads, including native BC formats and
sRGB sampler state. No GPU texture or buffer readback is started. Unsupported
shaders, missing uploads, multitexture effects, UI draws and ordinary alpha
surfaces stay unlit. The client-specific `northlight-emission.bin` catalog now
admits qualified luminous alpha materials as well. It is generated from the
winning MPQ chain and installed/restored in the same art-layer transaction.
Simple unlit fixed-function texture-times-diffuse ribbons use their actual
world/view transforms, vertex colours and uploaded texture, with the same
current-frame lifetime rule.

Sampling is bounded to 32 additive draws and 16 triangles per draw. Nearby
samples form compact lights, sharing the eight-source cap with native item and
mount lights. Overlapping native sources are suppressed. Unlike native lamps,
particle lights have no TTL or selection fade: only this frame's accepted
submissions contribute, including draws deferred by SpellLayer. This preserves
the effect's authored opacity and end of life without touching Spell.dbc.

Cold Blood 14177 is the reference case: its cast and aura use the native
right-hand attachment and blue particle textures. The read-only fixture and
native shader/texture tests cover this asset chain. Runtime diagnostics report
`PARTICLELIGHT` accepted draws, cache misses and selected source RGB/position;
`PTEX` reports the bounded replay texture cache. A new dynamic buffer may miss
its first draw until a subsequent successful upload; stale bytes never supply a
light. In-game appearance and frame cost still require a client comparison.

All these sources share `EquipmentLights`, `LocalLightLimit` and the eight-source
Northlight admission cap. Those limits do not cap the game's own per-model
native light updates. Generated archives need an in-game performance comparison.

## Sky lightning

The catalog identifies lightning and meteor-flash layers in every installed sky
model. The current archive chain contains 17 such models / 254 material batches;
27 batches use a second texture mask. Capture reads the native shader's current RGBA and
animated UVs, so no independent random timer can drift from the visible flash.
The sky viewport is admitted separately, before world depth/camera recovery.
View-space sources are converted only when the current world camera is known.
Both textures retain their own proven UV output, sampler addressing/filtering,
RGB and alpha. A black or unavailable mask cannot leave an unmasked lamp.
Texture alpha is interpreted by the native combiner; bright zero-alpha texels
remain valid for Opaque combiners. Palette textures use an exact canonical RGBA
identity after native CPU expansion. These rules apply throughout the resolved
archive chain, without a location allowlist.

Skybox scale is not physical distance. The explicit transport model intersects
the measured direction with a cloud layer 350 yards above the viewer (a maximum
1750-yard distance near the horizon). Source range is 2000 yards. World bolts
use actual transformed geometry, with a 32-yard extent gate that prevents small
spells sharing the same atlas from receiving environmental-bolt energy.
This is relative artistic calibration, not measured lightning photometry.

The transient inverse-square term has a finite core and a smooth zero at the
range boundary. Brightness follows the native flash alpha; no selection fade or
release tail is applied. Up to two of the eight dynamic slots are reserved for
visible discharges. At most 64 candidate probes and four luminous discharge
draws are sampled per frame; zero-opacity scheduled layers cannot consume the
four slots. The ordinary lamp shaders remain byte-identical. Only batches with
a discharge use the separate 447-slot `LocalFlash` shader; they never enter the
ordinary sixteen-lamp variant. Lightning does not currently add a new shadow
map, cloud bloom pass or thunder simulation.
