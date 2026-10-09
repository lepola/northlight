ps_2_0
dcl v0
dcl t0
def c0 = 2.00000000e+00, 2.00000000e+00, 2.00000000e+00, 1.00000000e+00
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
mul r1.xyz, r0.xyzx, c0.xyzx
mov r2.xyz, r1.xyzx
mov r2.w, r0.w
mov_sat r0.x, r0.w
max r0.y, r1.y, r1.z
max r0.y, r1.x, r0.y
mov_sat r0.y, r0.y
mul r0.x, r0.x, r0.y
mov r1.x, c0.w
mov r1.y, c0.w
mov r1.z, c0.w
mov r1.w, r0.x
mov oC1.xyzw, r1.xyzw
mov oC0.xyzw, r2.xyzw
