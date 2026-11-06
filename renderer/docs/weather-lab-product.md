# Weather Lab: scoped product and visual direction

Audience: Northlight developers tuning weather without starting WoW. Preserve the
existing Win32/ImGui tool, embedded Roboto and native controls (**Operate** mode).

## Camera effects are on the lens

The user's explicit correction takes precedence over the previous storm direction:
rain beads, snow clumps, frost and sand coating live on the camera glass. They are
not world geometry, depth fog or camera-facing planes placed in the scene.

- Sand uses persistent irregular grains, a few softer dusty marks and sparse thin
  deposits. Keep the scene readable; do not cover it with a uniform colored veil.
- Positions and frost texture coordinates stay attached to the lens during yaw and
  pitch changes. Do not reproject, rotate, scale or radially expand their layout.
- Project world wind onto the lens to drive gradual particle forces. The component
  into the glass affects new impacts; it does not move existing marks in 3D.
- Optional camera-turn reaction applies bounded inertial velocity impulses to the
  particles. Disabling it removes the impulse while retaining wind direction.
- Preserve continuous snow atlas blending, eased merge position/radius and the
  0.35-second absorbed-flake fade. Preserve water mass and directional rivulets.
- Frost grows through density and detail, with a clear center and no texture zoom.

References studied: [Battlefield 1](https://www.ea.com/en-gb/games/battlefield/news/battlefield-1-sinai-desert),
[its gameplay frame](https://www.tweaktown.com/articles/7912/battlefield-multiplayer-gameplay-thoughts/index.html),
[Assassin's Creed Origins at Letopolis](https://www.neoseeker.com/assassins-creed-origins/walkthrough/The_Scarab%27s_Lies),
and the Skyrim sources in [asset provenance](../../shaders/assets/README.md).
They inform grain shape, varied density and material. Their world weather does not
override the user's lens-only requirement. No game or mod assets are copied.

## Interaction and scope

- `WeatherWindDirection`: world +X at 0 degrees, +Y at 90, range 0..359, default 45.
- `WeatherWindStrength`: 0..300 percent, default 100.
- `WeatherCameraMotion`: optional lens inertia, 0/1, default 1.
- Drag the preview to yaw/pitch; provide numeric controls, automatic rotation and
  reset. The synthetic background and its depth rotate together as a reference
  behind the fixed glass effects.
- The existing supplemental scene rain/snow pass remains separate and unchanged.
  Lens effects ignore scene depth. Native client particles are not reproduced.
- Captured background is fixed. Rotating the preview suspends its mismatched depth
  for the supplemental scene pass until Reset view; it does not clip lens marks.
- Presets include wind and response settings. Replay begins with fresh lens masses,
  dust and turn history. Original WCP2 imports remain readable.

The lab is not an in-game verification of the Zangarmarsh occlusion report.
