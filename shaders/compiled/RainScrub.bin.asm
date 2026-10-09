ps_2_0
dcl t0
def c0 = -9.99999997e-07, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c1 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 5.01960814e-01
dcl_2d s1
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
texld r1.xyzw, t0.xyxx, s1
add r0.x, r0.x, -r1.x
abs r0.x, r0.x
add r0.x, r0.x, c0.x
texkill r0.x
mov oC0.xyzw, c1.xyzw
