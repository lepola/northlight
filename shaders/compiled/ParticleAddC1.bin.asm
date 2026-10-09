ps_2_0
dcl v0
dcl t0
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
max r1.x, r0.y, r0.z
max r1.x, r0.x, r1.x
mov_sat r1.x, r1.x
mov oC0.xyzw, r0.xyzw
mov oC1.xyzw, r1.x
