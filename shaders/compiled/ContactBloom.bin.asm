ps_3_0
dcl_texcoord0 v0
def c5 = 4.00000000e+00, 0.00000000e+00, 0.00000000e+00, 4.00000000e+00
def c6 = 2.12599993e-01, 7.15200007e-01, 7.22000003e-02, 3.57142854e+00
def c7 = -2.57142878e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c8 = 4.00000000e+00, 4.00000000e+00, 4.00000000e+00, 1.25000000e-01
def c9 = 1.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
dcl_2d s0
mul r0.xyzw, c0.xyxy, c5.xyzw
texld r1.xyzw, v0.xyxx, s0
dp3 r2.x, r1.xyzx, c6.xyzx
mad r2.x, r2.x, c6.w, c7.x
mov_sat r2.x, r2.x
mad r1.xyz, r1.xyzx, r2.x, c7.yzwy
mul r1.xyz, r1.xyzx, c8.xyzx
add r2.xy, v0.xyxx, r0.xyxx
texld r2.xyzw, r2.xyxx, s0
dp3 r1.w, r2.xyzx, c6.xyzx
mad r1.w, r1.w, c6.w, c7.x
mov_sat r1.w, r1.w
mad r1.xyz, r2.xyzx, r1.w, r1.xyzx
add r2.xy, v0.xyxx, -r0.xyxx
texld r2.xyzw, r2.xyxx, s0
dp3 r1.w, r2.xyzx, c6.xyzx
mad r1.w, r1.w, c6.w, c7.x
mov_sat r1.w, r1.w
mad r1.xyz, r2.xyzx, r1.w, r1.xyzx
add r2.xy, v0.xyxx, r0.zwzz
texld r2.xyzw, r2.xyxx, s0
dp3 r1.w, r2.xyzx, c6.xyzx
mad r1.w, r1.w, c6.w, c7.x
mov_sat r1.w, r1.w
mad r1.xyz, r2.xyzx, r1.w, r1.xyzx
add r0.xy, v0.xyxx, -r0.zwzz
texld r0.xyzw, r0.xyxx, s0
dp3 r1.w, r0.xyzx, c6.xyzx
mad r1.w, r1.w, c6.w, c7.x
mov_sat r1.w, r1.w
mad r0.xyz, r0.xyzx, r1.w, r1.xyzx
mul r0.w, c2.x, c8.w
mul r0.xyz, r0.xyzx, r0.w
mov r0.w, c9.x
mov oC0.xyzw, r0.xyzw
