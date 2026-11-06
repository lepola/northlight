---
name: Northlight Weather Lab
description: Visual and UX conventions for the native Windows weather lab
colors:
  window: "rgba(7.5%, 8.2%, 9.8%, 1)"
  text: "rgba(93%, 94%, 96%, 1)"
  text-muted: "rgba(65%, 69%, 74%, 1)"
  frame: "rgba(16%, 29%, 48%, 0.54)"
  accent: "rgba(26%, 59%, 98%, 1)"
  slider-grab: "rgba(24%, 52%, 88%, 1)"
  button: "rgba(26%, 59%, 98%, 0.40)"
  button-active: "rgba(6%, 53%, 98%, 1)"
  separator: "rgba(43%, 43%, 50%, 0.50)"
typography:
  body:
    fontFamily: "Roboto Medium (embedded)"
    fontSize: "17px"
    fontWeight: 500
rounded:
  window: "0px"
  frame: "4px"
spacing:
  window: "16px"
  item-x: "10px"
  item-y: "9px"
  frame-x: "7px"
  frame-y: "5px"
components:
  button:
    backgroundColor: "{colors.button}"
    textColor: "{colors.text}"
    rounded: "{rounded.frame}"
    padding: "5px 7px"
  button-hover:
    backgroundColor: "{colors.accent}"
  button-active:
    backgroundColor: "{colors.button-active}"
---

# Design System: Northlight Weather Lab

## Overview

An engineering workbench in Operate mode: adjust settings and inspect the neighboring
preview immediately. Native Win32 + ImGui + D3D9. The visual reference is the existing
`src/overlay/settings_overlay.cpp::baseStyle`: dark surfaces, compact controls, blue
accent and embedded Roboto. The lab uses its own `StyleColorsDark` setup rather than
calling `baseStyle`; the values above describe the lab itself.

Water drops, snow masses, frost and sand occupy 2D glass coordinates on the lens.
Scene geometry does not clip them. The separate existing scene rain/snow pass keeps
its own behavior; wind controls affect only the lens effects.

Sources: [implementation](../../scripts/weather_lab.cpp),
[capture preparation](../../scripts/weather_lab.py), [guide](weather-lab.md),
[product direction](weather-lab-product.md), [materials](../../shaders/assets/README.md).

UI checks used `desktop.png`, `compact.png`, `alpha-zero-desktop.bmp` and
`alpha-zero-compact.bmp` in `.local/out/weather-lab/`. The latter pair uses a native
D3D9 diagnostic 37x23 fixture, not a game frame. Effect captures include `smoke-3.png`
(sand), `smoke-13/14.png` (heavy/light frost), `smoke-2/7.png` (snow sizes) and
`smoke-16.bmp` (saved rain). These GPU fixtures do not reproduce the reported game
view. Source review and selected `snow-merge-detail.png`, `rain-drift-detail.png` and
`frost-density-detail.png` contact sheets in `.local/out/lens-gpu/` cover motion;
GIF playback and in-game operation remain unchecked. Current sand/wind/camera
captures `smoke-3/20/25/26/28.png` cover lens sand, background rotation, turn response
and depth toggles. Manual preview dragging remains unconfirmed.

## Colors

A neutral near-black surface and light text leave the scene visually dominant.
Blue marks interactive controls, checkboxes, slider grabs and keyboard focus.
Frontmatter values are source colors, not averaged screenshot pixels; standard ImGui
control alpha participates in composition. Preview scene alpha is always 255 while
scene RGB and the separate mask's coverage alpha are retained.

## Typography

Embedded Roboto Medium serves labels, values, buttons, status and explanations.
Order, spacing and `SeparatorText` establish hierarchy without a separate display
font. Long explanations and world coefficients wrap to their column width.

## Layout

- Independently scrollable controls on the left; preview and explanations on the
  right. Both columns remain in the checked compact window size.
- At client widths of 850 px or more, controls occupy 340 px; below that, they use
  `max(245 px, 42% of width)`. Preview receives the remaining width.
- Order: weather mode, intensity, `Save preset` beside `Defaults`, save result,
  common switches, `Wind and camera`, scene particles, lens effects, collapsed
  `World fog (readout only)`. Saving needs no initial downward scroll; buttons
  belong to scrolling content rather than a fixed toolbar.
- Preview preserves aspect ratio: 960:640 by default, captured W:H for replay.
  Simulation limits, status, readouts and `Save preview BMP` sit below it.
  Smaller windows shrink the image and wrap text; additional capture readouts
  remain accessible by scrolling the right panel.

## Elevation, depth and shapes

Flat working panels use spacing, thin separators, field backgrounds and scrollbars
for grouping. Depth inside the preview belongs to the test scene. Window, child
panels and preview are rectangular; fields and buttons have slight rounding.
These are the lab's actual dimensions rather than all radii from the game overlay.

## Components and behavior

- **Fields and switches.** Sliders and combos fill the control width. Numeric labels
  sit above values; percentages, intensity and heat show units or ranges.
  Rain/Snow/Sand expose their corresponding lens controls.
- **Input.** Mouse changes values and states; the wheel scrolls. Standard ImGui
  keyboard navigation supports Tab/Shift+Tab, arrows and Enter/Space. Losing focus
  clears held input.
- **Wind and view.** `Wind heading (world degrees)` and `Wind strength (%)` define
  world wind projected into glass forces. Into-glass pressure affects new arrivals;
  existing deposits retain 2D positions and gradually move under forces.
  `React to camera turns` adds bounded velocity impulses without globally shifting
  the coating. Disabling it preserves wind; large cuts rebase history. `Lens wind`
  shows right/down/into glass. There is no radial perspective. Preview drag changes
  yaw/pitch; collapsed `Camera controls` adds sliders, auto rotation and `Reset view`.
  Synthetic scene and depth rotate together behind glass; lens effects do not reproject.
- **Feedback.** Changes apply immediately. `Pause` stops time, merging and visual
  transitions while retaining manual size adjustment. `Restart simulation` clears
  state. `Save preset` writes local `weather-lab.ini` and reports success/failure
  beside the action. Rain/Snow readouts show camera particles, merges since restart
  and frost percentage. All three quality presets have save/load smoke coverage.
- **Preview scope.** Shared production shaders and CPU lens simulation draw
  supplemental precipitation and lens effects. Default background/depth are
  synthetic; custom-background depth is also synthetic. Native particles, maps,
  world lighting and world fog are not rerendered; light/fog coefficients are readouts.
- **Capture replay.** `--capture` starts paused with original dimensions, raw depth
  and projection; lens masses start empty. `Captured depth occlusion` compares
  clipping. `Respect captured name mask` uses resolved coverage; MSAA edges are
  approximate. Readouts show depth min/max, viewport range, near/far, incomplete
  inputs and HDR-to-SDR conversion when relevant. A captured scene remains static:
  changing view suspends scene-pass depth occlusion until `Reset view`, with a
  visible explanation. Lens effects always ignore scene depth. Wind settings are
  retained; lens masses, sand and camera inertia are initialized anew.
- **Capture acquisition.** Client `Diagnostics` -> `Capture weather` writes JSON
  and FGR once to `northlight-diagnostics/session-.../`. Ordinary frames avoid those
  GPU readbacks; subsequent replay requires no running game.
- **Shelter.** `Camera under shelter` hides lens effects and retains exterior
  precipitation; `Fully indoors (no sky)` disables both passes. These lab inputs
  do not prove correct shelter discovery throughout the game world.
- **Rain.** `RainRivulets` / `Merge drops into rivulets` enables head collisions and
  trail water collection while conserving mass. Long curved trails can form.
  Rain and snow have sideways gusts; snow also sways. Trail rendering and collection
  use the same direction. With rivulets disabled, rain drops remain independent.
- **Snow.** Supplemental scene and camera snow share an original 16-floc atlas with
  ragged crystalline contours, rotation, stretch and varied opacity. Embedded L8
  512x512 data includes mipmaps; native client particles are unchanged. Camera
  variants blend continuously; visible centers, radii and snow fractions ease during
  merging. The absorbed particle moves toward its neighbor and fades over 0.35 seconds
  from its original opacity without adding mass.
- **Frost.** `SnowFrost` and `SnowFrostStrength` operate independently of `Snow on
  camera`. Original L8 1024x576 mipmapped material provides irregular asymmetric
  deposits, translucent facets and mild local refraction. Snow mass and intensity
  drive accumulation; heat accelerates melting. The pattern stays in glass
  coordinates while density and revealed details change without material scaling.
  The central gameplay area remains clear.
- **Sand.** `SandLens`, `SandLensStrength` and `SandLensSize` control up to 128
  persistent 2D grains, occasional soft marks and sparse thin film. Varied adhesion
  and smooth forces produce sticking and sliding; particle age controls appearance
  and fading. Sand remains on the lens as the background turns, without 3D layers,
  scene-depth clipping or a uniform colored veil. These keys, rivulets and frost
  are available in quality settings, the game menu and the lab. Native sand and storm
  fog remain the client's responsibility. BF1/Origins are grain/material references;
  their world-fog model is not reproduced here.

## Do's and Don'ts

- Keep synthetic/captured and readout-only labels beside the preview. Static replay
  does not reproduce the entire game world.
- Preserve the sequence of adjustment, immediate feedback and local saving, with
  visible save results.
- Keep this local workbench within the existing visual language.
- Do not treat synthetic/depth fixtures as proof that the Zangarmarsh mushroom
  report is resolved. The external camera view above the mushroom requires a game capture.
