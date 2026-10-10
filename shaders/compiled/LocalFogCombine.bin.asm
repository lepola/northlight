ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = 9.99999997e-07, 1.00000000e+00, 6.00000024e-01, 0.00000000e+00
dcl_2d s9
mov r0.xyzw, c68.xyzw
mov r0.xy, v0.xyxx
texldl r0.xyzw, r0.xyzw, s9
max r1.x, r0.y, r0.z
max r1.x, r0.x, r1.x
add r1.x, c58.w, r1.x
rcp r1.x, r1.x
mul r1.x, c58.w, r1.x
mul r1.xyz, r0.xyzx, r1.x
max r1.w, r1.y, r1.z
max r1.w, r1.x, r1.w
max r1.w, r1.w, c69.x
rcp r1.w, r1.w
mul r0.x, r0.w, r1.w
max r0.x, r0.x, c69.y
log r0.x, r0.x
mul r0.x, r0.x, c69.z
exp r0.x, r0.x
mov r0.xyz, r0.x
mul r0.xyz, r1.xyzx, r0.xyzx
mov r0.w, c68.x
mov oC0.xyzw, r0.xyzw
