ps_2_0
dcl v0
dcl t0
def c0 = 2.00000000e+00, 2.00000000e+00, 2.00000000e+00, 0.00000000e+00
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
mov r1.xyzw, r0.xyzw
mul r0.xyz, r0.xyzx, c0.xyzx
mov r1.xyz, r0.xyzx
mov r0.xyzw, r1.xyzw
mov_sat r0.xyzw, r0.xyzw
max r1.x, r0.y, r0.z
max r1.x, r0.x, r1.x
mov_sat r1.x, r1.x
mov oC0.xyzw, r0.xyzw
mov oC1.xyzw, r1.x
