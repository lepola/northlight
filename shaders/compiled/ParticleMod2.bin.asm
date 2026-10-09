ps_2_0
dcl v0
dcl t0
def c0 = 2.00000000e+00, 2.00000000e+00, 2.00000000e+00, 0.00000000e+00
def c1 = 2.98999995e-01, 5.87000012e-01, 1.14000000e-01, 0.00000000e+00
dcl_2d s0
texld r0.xyzw, t0.xyxx, s0
mul r0.xyzw, r0.xyzw, v0.xyzw
mov r1.xyzw, r0.xyzw
mul r0.xyz, r0.xyzx, c0.xyzx
mov r1.xyz, r0.xyzx
mov r0.xyzw, r1.xyzw
mov_sat r0.xyzw, r0.xyzw
dp3 r1.x, r0.xyzx, c1.xyzx
mov oC0.xyzw, r0.xyzw
mov oC1.xyzw, r1.x
