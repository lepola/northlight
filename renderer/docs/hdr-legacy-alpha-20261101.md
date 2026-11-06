# Bound legacy alpha before HDR sky replay

Native sky shaders render into an unsigned normalized framebuffer. The HDR
replay redirects their output into a temporary register before applying the
linear transfer and blending into FP16. Previously, that redirection preserved
unbounded alpha even though RGB was bounded by the transfer. An opacity above
one makes inverse-source-alpha negative; negative opacity can also subtract
source color. This can produce black and colored artifacts in layered skies.

Apply the existing shader destination saturation modifier when redirecting
each native color output. This preserves the original source range before
unpremultiplication, transfer and blending, without adding an instruction.
The SM2 compiler assumes COLOR0 is already bounded and can remove a clamp
written only in the transfer shader, so the bound belongs at the redirected
output. Owned HDR radiance uses separate passes and retains its headroom.
Compiled shaders and rendering settings are unchanged.

The numerical GPU regression covers SM2/SM3, opaque/straight/premultiplied
draws, 80/160-nit transfer scales and seven alpha values from -2 to 2.
Both DXVK 2.7.1 and 3.1.1 reproduce 68 channel failures before the fix, with
RGB reaching -3.69922. The corrected 84-case matrix has zero failures and
maximum absolute error 0.000547 on each backend. The Frame runner now records
and copies the bytecode patch header along with its other tested inputs.

The complete offline HDR acceptance passes from the PR checkout: Windows x86
release build, CPU UI/sky boundaries, nine composition scenarios, 2,869 Frame
cases and 43 sun cases per backend, plus the production proxy in HDR, SDR and
MSAA8/triple-buffer modes. The tested DLL SHA-256 is
`fe3717893f0de856b7ab02776a1941e0d8b97003c1a1c9c07f7de1f36f1dada2`.

The supplied screenshot shows Icecrown. The four locally available F12
captures instead show the orc starting area and Dalaran; they do not provide
an exact Icecrown replay. This is a controlled reproduction of the identified
blend defect, not an in-game before/after confirmation or an FPS measurement.

Reproduce with `python tests/verify_hdr_pipeline.py --output <isolated-output>`.
GPU verification requires the game to be closed. Raw client captures remain
private and are not committed.
