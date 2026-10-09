ps_2_0
dcl v0
dcl t0
def c0 = 1.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
mov_sat r1.x, r0.w
max r1.y, r0.y, r0.z
max r1.y, r0.x, r1.y
mov_sat r1.y, r1.y
mul r1.x, r1.x, r1.y
mov r2.x, c0.x
mov r2.y, c0.x
mov r2.z, c0.x
mov r2.w, r1.x
mov oC1.xyzw, r2.xyzw
mov oC0.xyzw, r0.xyzw
