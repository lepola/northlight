ps_2_0
dcl v0
dcl t0
def c0 = 1.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
mov r1.x, c0.x
mov r1.y, c0.x
mov r1.z, c0.x
mov r1.w, r0.w
mov oC1.xyzw, r1.xyzw
mov oC0.xyzw, r0.xyzw
