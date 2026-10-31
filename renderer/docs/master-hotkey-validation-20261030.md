# Retained master-effect hotkey

A short Ctrl+Shift+F10 press can begin and end between rendered frames, so polling alone misses the switch. Reuse the existing settings-overlay window hook to retain initial press edges until the render thread consumes them at its existing frame boundary. Ignore repeats, preserve press parity, clear pending input on focus loss/hook teardown, and retain polling if the overlay is disabled or its hook could not be installed. No additional global input hook is installed.

The independent branch passes the actual overlay input/font checks and compiled randomized draw-hook equivalence tests in optimized and ASan/UBSan builds, including the existing deliberately broken frame-latch control. The source audit now excludes namespace-qualified diagnostic flags when counting assignments to Device.enabled; those unrelated flags previously caused a false failure. The independent Windows x86 renderer builds.

The combined build's existing game log confirmed OFF/ON/OFF/ON with short presses. No new game session or FPS measurement was performed for this port. Reproduce with `python tests/test_settings_overlay.py` and `python tests/test_draw_gates.py`.
