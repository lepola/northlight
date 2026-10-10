ps_3_0
dcl_texcoord0 v0
def c68 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, 9.99989986e-01, -5.00000000e-01
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 1.00000000e+00, 9.99999975e-06, 1.20000001e-02, 6.99999975e-04
def c72 = 3.00000003e-03, 2.00000000e+00, -2.00000000e+00, 9.99999996e-13
def c73 = -1.00000000e+00, 1.00000000e+00, 7.50000000e-01, 5.99999987e-02
def c74 = 1.00000005e-03, 7.85398006e-01, 2.73000002e-01, 1.57079601e+00
def c75 = 3.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
dcl_2d s1
dcl_2d s11
mul r0.xy, v0.xyxx, c33.xyxx
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, c33.xxxy, c68.xxxy
max r0.xy, r0.xyxx, c68.zwzz
min r0.xy, r0.xyxx, r0.zwzz
add r0.xy, r0.xyxx, c69.xyxx
mul r0.xy, r0.xyxx, c0.xyxx
mov r1.xyzw, c70.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s1
add r0.z, r1.x, -c1.w
mul r0.z, r0.z, c2.x
mov_sat r0.z, r0.z
min r0.z, r0.z, c69.z
add r0.w, c30.x, c69.w
cmp r0.w, r0.w, c68.z, c71.x
mov r1.x, r1.y
cmp r1.x, -r0.w, r1.x, c68.z
mov r1.y, r1.x
add r0.w, -r0.w, c71.x
if_ne r0.w, -r0.w
    mov r2.xyzw, c70.xyzw
    mov r2.xy, r0.xyxx
    texldl r2.xyzw, r2.xyzw, s11
    add r0.w, c0.z, -r2.x
    cmp r0.w, r0.w, c68.z, c71.x
    mul r1.x, c0.z, c0.w
    add r1.z, c0.w, -c0.z
    mul r1.z, r0.z, r1.z
    add r1.z, c0.w, -r1.z
    max r1.z, r1.z, c71.y
    rcp r1.z, r1.z
    mul r1.x, r1.x, r1.z
    mul r1.z, r2.x, c71.w
    max r1.z, r1.z, c71.z
    add r1.x, r1.x, r1.z
    add r1.x, r1.x, -r2.x
    cmp r1.x, r1.x, c68.z, c71.x
    add r1.x, -r1.x, c71.x
    min r0.w, r0.w, r1.x
    add r1.x, -r2.y, c72.x
    cmp r1.x, r1.x, c68.z, c71.x
    min r0.w, r0.w, r1.x
    cmp r0.w, -r0.w, c68.z, r2.x
    mov r1.y, r0.w
else
endif
mov r0.w, r1.y
cmp r1.x, -r0.w, c68.z, c71.x
mul r1.y, c0.z, c0.w
add r1.z, c0.w, -c0.z
mul r0.z, r0.z, r1.z
add r0.z, c0.w, -r0.z
max r0.z, r0.z, c71.y
rcp r0.z, r0.z
mul r0.z, r1.y, r0.z
cmp r0.z, -r1.x, r0.z, r0.w
mul r1.xy, c0.xyxx, c69.xyxx
add r0.xy, r0.xyxx, -r1.xyxx
mul r0.xy, r0.xyxx, c72.yzyy
add r0.xy, r0.xyxx, c73.xyxx
rcp r1.x, c1.x
rcp r1.y, c1.y
mul r0.xy, r0.xyxx, r1.xyxx
mov r0.w, c1.z
mul r0.xyz, r0.xywx, r0.z
mul r1.xyz, r0.x, c3.xyzx
mul r2.xyz, r0.y, c4.xyzx
add r1.xyz, r1.xyzx, r2.xyzx
mul r0.xyz, r0.z, c5.xyzx
add r0.xyz, r1.xyzx, r0.xyzx
add r0.xyz, r0.xyzx, c6.xyzx
add r0.xyz, r0.xyzx, -c15.xyzx
dp3 r0.w, r0.xyzx, r0.xyzx
rsq r1.x, r0.w
rcp r1.x, r1.x
max r1.y, c21.w, c68.z
min r1.x, r1.x, r1.y
max r0.w, r0.w, c72.w
rsq r0.w, r0.w
mov r1.yzw, r0.w
mul r0.xyz, r0.xyzx, r1.yzwy
add r1.yzw, c15.xxyz, -c36.xxyz
dp3 r0.w, r1.yzwy, r0.xyzx
dp3 r1.y, r1.yzwy, r1.yzwy
mul r1.z, r0.w, r0.w
add r1.y, r1.y, -r1.z
max r1.y, r1.y, c68.z
mul r1.z, c36.w, c36.w
add r1.w, r1.z, -r1.y
max r2.x, r1.w, c68.z
rsq r2.x, r2.x
rcp r2.x, r2.x
mov r2.y, -r2.x
add r2.y, -r0.w, r2.y
max r2.y, r2.y, c58.x
add r2.x, -r0.w, r2.x
min r2.x, r2.x, r1.x
mul r2.z, c36.w, c73.w
max r2.z, r2.z, c73.z
mul r2.zw, r2.z, r2.z
add r1.y, r1.y, r2.z
rsq r1.y, r1.y
add r1.z, r1.z, r2.w
max r1.z, r1.z, c74.x
rcp r1.z, r1.z
cmp r1.w, -r1.w, c68.z, c71.x
add r2.z, r2.y, -r2.x
cmp r2.z, r2.z, c68.z, c71.x
min r1.w, r1.w, r2.z
add r2.z, r2.x, r0.w
mul r2.z, r2.z, r1.y
abs r2.w, r2.z
add r3.x, -r2.w, c71.x
cmp r3.y, r3.x, c68.z, c71.x
add r3.y, -r3.y, c71.x
mul r3.x, r3.x, c74.z
add r3.x, r3.x, c74.y
mul r3.x, r2.w, r3.x
rcp r3.z, r2.w
rcp r2.w, r2.w
mov r2.w, -r2.w
add r2.w, r2.w, c71.x
mul r2.w, r2.w, c74.z
add r2.w, r2.w, c74.y
mul r2.w, r3.z, r2.w
add r2.w, -r2.w, c74.w
cmp r2.w, -r3.y, r2.w, r3.x
cmp r2.z, r2.z, c68.z, c71.x
cmp r2.z, -r2.z, r2.w, -r2.w
add r2.w, r2.y, r0.w
mul r2.w, r2.w, r1.y
abs r3.x, r2.w
add r3.y, -r3.x, c71.x
cmp r3.z, r3.y, c68.z, c71.x
add r3.z, -r3.z, c71.x
mul r3.y, r3.y, c74.z
add r3.y, r3.y, c74.y
mul r3.y, r3.x, r3.y
rcp r3.w, r3.x
rcp r3.x, r3.x
mov r3.x, -r3.x
add r3.x, r3.x, c71.x
mul r3.x, r3.x, c74.z
add r3.x, r3.x, c74.y
mul r3.x, r3.w, r3.x
add r3.x, -r3.x, c74.w
cmp r3.x, -r3.z, r3.x, r3.y
cmp r2.w, r2.w, c68.z, c71.x
cmp r2.w, -r2.w, r3.x, -r3.x
add r2.z, r2.z, -r2.w
mul r1.y, r2.z, r1.y
add r2.z, r2.x, -r2.y
mul r1.z, r2.z, r1.z
add r1.y, r1.y, -r1.z
max r1.y, r1.y, c68.z
cmp r1.y, -r1.w, c68.z, r1.y
max r0.w, -r0.w, r2.y
min r0.w, r0.w, r2.x
add r0.w, r0.w, -c58.x
mul r0.w, r0.w, c58.y
mov_sat r0.w, r0.w
mul r1.z, r0.w, c72.y
add r1.z, -r1.z, c75.x
mul r1.z, r0.w, r1.z
mul r0.w, r0.w, r1.z
mul r1.y, r1.y, c59.x
mul r0.w, r1.y, r0.w
mul r1.yzw, c44.xxyz, r0.w
max r0.w, r1.z, r1.w
max r0.w, r1.y, r0.w
mul r0.w, r0.w, c58.z
mul r2.x, c58.w, r0.w
add r0.w, c58.w, r0.w
rcp r0.w, r0.w
mul r0.w, r2.x, r0.w
add r2.xyz, c15.xyzx, -c37.xyzx
dp3 r2.w, r2.xyzx, r0.xyzx
dp3 r2.x, r2.xyzx, r2.xyzx
mul r2.y, r2.w, r2.w
add r2.x, r2.x, -r2.y
max r2.x, r2.x, c68.z
mul r2.y, c37.w, c37.w
add r2.z, r2.y, -r2.x
max r3.x, r2.z, c68.z
rsq r3.x, r3.x
rcp r3.x, r3.x
mov r3.y, -r3.x
add r3.y, -r2.w, r3.y
max r3.y, r3.y, c58.x
add r3.x, -r2.w, r3.x
min r3.x, r3.x, r1.x
mul r3.z, c37.w, c73.w
max r3.z, r3.z, c73.z
mul r3.zw, r3.z, r3.z
add r2.x, r2.x, r3.z
rsq r2.x, r2.x
add r2.y, r2.y, r3.w
max r2.y, r2.y, c74.x
rcp r2.y, r2.y
cmp r2.z, -r2.z, c68.z, c71.x
add r3.z, r3.y, -r3.x
cmp r3.z, r3.z, c68.z, c71.x
min r2.z, r2.z, r3.z
add r3.z, r3.x, r2.w
mul r3.z, r3.z, r2.x
abs r3.w, r3.z
add r4.x, -r3.w, c71.x
cmp r4.y, r4.x, c68.z, c71.x
add r4.y, -r4.y, c71.x
mul r4.x, r4.x, c74.z
add r4.x, r4.x, c74.y
mul r4.x, r3.w, r4.x
rcp r4.z, r3.w
rcp r3.w, r3.w
mov r3.w, -r3.w
add r3.w, r3.w, c71.x
mul r3.w, r3.w, c74.z
add r3.w, r3.w, c74.y
mul r3.w, r4.z, r3.w
add r3.w, -r3.w, c74.w
cmp r3.w, -r4.y, r3.w, r4.x
cmp r3.z, r3.z, c68.z, c71.x
cmp r3.z, -r3.z, r3.w, -r3.w
add r3.w, r3.y, r2.w
mul r3.w, r3.w, r2.x
abs r4.x, r3.w
add r4.y, -r4.x, c71.x
cmp r4.z, r4.y, c68.z, c71.x
add r4.z, -r4.z, c71.x
mul r4.y, r4.y, c74.z
add r4.y, r4.y, c74.y
mul r4.y, r4.x, r4.y
rcp r4.w, r4.x
rcp r4.x, r4.x
mov r4.x, -r4.x
add r4.x, r4.x, c71.x
mul r4.x, r4.x, c74.z
add r4.x, r4.x, c74.y
mul r4.x, r4.w, r4.x
add r4.x, -r4.x, c74.w
cmp r4.x, -r4.z, r4.x, r4.y
cmp r3.w, r3.w, c68.z, c71.x
cmp r3.w, -r3.w, r4.x, -r4.x
add r3.z, r3.z, -r3.w
mul r2.x, r3.z, r2.x
add r3.z, r3.x, -r3.y
mul r2.y, r3.z, r2.y
add r2.x, r2.x, -r2.y
max r2.x, r2.x, c68.z
cmp r2.x, -r2.z, c68.z, r2.x
max r2.y, -r2.w, r3.y
min r2.y, r2.y, r3.x
add r2.y, r2.y, -c58.x
mul r2.y, r2.y, c58.y
mov_sat r2.y, r2.y
mul r2.z, r2.y, c72.y
add r2.z, -r2.z, c75.x
mul r2.z, r2.y, r2.z
mul r2.y, r2.y, r2.z
mul r2.x, r2.x, c60.x
mul r2.x, r2.x, r2.y
mul r2.xyz, c45.xyzx, r2.x
add r1.yzw, r1.xyzw, r2.xxyz
max r2.w, r2.y, r2.z
max r2.x, r2.x, r2.w
mul r2.x, r2.x, c58.z
mul r2.y, c58.w, r2.x
add r2.x, c58.w, r2.x
rcp r2.x, r2.x
mul r2.x, r2.y, r2.x
add r0.w, r0.w, r2.x
add r2.xyz, c15.xyzx, -c38.xyzx
dp3 r2.w, r2.xyzx, r0.xyzx
dp3 r2.x, r2.xyzx, r2.xyzx
mul r2.y, r2.w, r2.w
add r2.x, r2.x, -r2.y
max r2.x, r2.x, c68.z
mul r2.y, c38.w, c38.w
add r2.z, r2.y, -r2.x
max r3.x, r2.z, c68.z
rsq r3.x, r3.x
rcp r3.x, r3.x
mov r3.y, -r3.x
add r3.y, -r2.w, r3.y
max r3.y, r3.y, c58.x
add r3.x, -r2.w, r3.x
min r3.x, r3.x, r1.x
mul r3.z, c38.w, c73.w
max r3.z, r3.z, c73.z
mul r3.zw, r3.z, r3.z
add r2.x, r2.x, r3.z
rsq r2.x, r2.x
add r2.y, r2.y, r3.w
max r2.y, r2.y, c74.x
rcp r2.y, r2.y
cmp r2.z, -r2.z, c68.z, c71.x
add r3.z, r3.y, -r3.x
cmp r3.z, r3.z, c68.z, c71.x
min r2.z, r2.z, r3.z
add r3.z, r3.x, r2.w
mul r3.z, r3.z, r2.x
abs r3.w, r3.z
add r4.x, -r3.w, c71.x
cmp r4.y, r4.x, c68.z, c71.x
add r4.y, -r4.y, c71.x
mul r4.x, r4.x, c74.z
add r4.x, r4.x, c74.y
mul r4.x, r3.w, r4.x
rcp r4.z, r3.w
rcp r3.w, r3.w
mov r3.w, -r3.w
add r3.w, r3.w, c71.x
mul r3.w, r3.w, c74.z
add r3.w, r3.w, c74.y
mul r3.w, r4.z, r3.w
add r3.w, -r3.w, c74.w
cmp r3.w, -r4.y, r3.w, r4.x
cmp r3.z, r3.z, c68.z, c71.x
cmp r3.z, -r3.z, r3.w, -r3.w
add r3.w, r3.y, r2.w
mul r3.w, r3.w, r2.x
abs r4.x, r3.w
add r4.y, -r4.x, c71.x
cmp r4.z, r4.y, c68.z, c71.x
add r4.z, -r4.z, c71.x
mul r4.y, r4.y, c74.z
add r4.y, r4.y, c74.y
mul r4.y, r4.x, r4.y
rcp r4.w, r4.x
rcp r4.x, r4.x
mov r4.x, -r4.x
add r4.x, r4.x, c71.x
mul r4.x, r4.x, c74.z
add r4.x, r4.x, c74.y
mul r4.x, r4.w, r4.x
add r4.x, -r4.x, c74.w
cmp r4.x, -r4.z, r4.x, r4.y
cmp r3.w, r3.w, c68.z, c71.x
cmp r3.w, -r3.w, r4.x, -r4.x
add r3.z, r3.z, -r3.w
mul r2.x, r3.z, r2.x
add r3.z, r3.x, -r3.y
mul r2.y, r3.z, r2.y
add r2.x, r2.x, -r2.y
max r2.x, r2.x, c68.z
cmp r2.x, -r2.z, c68.z, r2.x
max r2.y, -r2.w, r3.y
min r2.y, r2.y, r3.x
add r2.y, r2.y, -c58.x
mul r2.y, r2.y, c58.y
mov_sat r2.y, r2.y
mul r2.z, r2.y, c72.y
add r2.z, -r2.z, c75.x
mul r2.z, r2.y, r2.z
mul r2.y, r2.y, r2.z
mul r2.x, r2.x, c61.x
mul r2.x, r2.x, r2.y
mul r2.xyz, c46.xyzx, r2.x
add r1.yzw, r1.xyzw, r2.xxyz
max r2.w, r2.y, r2.z
max r2.x, r2.x, r2.w
mul r2.x, r2.x, c58.z
mul r2.y, c58.w, r2.x
add r2.x, c58.w, r2.x
rcp r2.x, r2.x
mul r2.x, r2.y, r2.x
add r0.w, r0.w, r2.x
add r2.xyz, c15.xyzx, -c39.xyzx
dp3 r0.x, r2.xyzx, r0.xyzx
dp3 r0.y, r2.xyzx, r2.xyzx
mul r0.z, r0.x, r0.x
add r0.y, r0.y, -r0.z
max r0.y, r0.y, c68.z
mul r0.z, c39.w, c39.w
add r2.x, r0.z, -r0.y
max r2.y, r2.x, c68.z
rsq r2.y, r2.y
rcp r2.y, r2.y
mov r2.z, -r2.y
add r2.z, -r0.x, r2.z
max r2.z, r2.z, c58.x
add r2.y, -r0.x, r2.y
min r1.x, r2.y, r1.x
mul r2.y, c39.w, c73.w
max r2.y, r2.y, c73.z
mul r2.yw, r2.y, r2.y
add r0.y, r0.y, r2.y
rsq r0.y, r0.y
add r0.z, r0.z, r2.w
max r0.z, r0.z, c74.x
rcp r0.z, r0.z
cmp r2.x, -r2.x, c68.z, c71.x
add r2.y, r2.z, -r1.x
cmp r2.y, r2.y, c68.z, c71.x
min r2.x, r2.x, r2.y
add r2.y, r1.x, r0.x
mul r2.y, r2.y, r0.y
abs r2.w, r2.y
add r3.x, -r2.w, c71.x
cmp r3.y, r3.x, c68.z, c71.x
add r3.y, -r3.y, c71.x
mul r3.x, r3.x, c74.z
add r3.x, r3.x, c74.y
mul r3.x, r2.w, r3.x
rcp r3.z, r2.w
rcp r2.w, r2.w
mov r2.w, -r2.w
add r2.w, r2.w, c71.x
mul r2.w, r2.w, c74.z
add r2.w, r2.w, c74.y
mul r2.w, r3.z, r2.w
add r2.w, -r2.w, c74.w
cmp r2.w, -r3.y, r2.w, r3.x
cmp r2.y, r2.y, c68.z, c71.x
cmp r2.y, -r2.y, r2.w, -r2.w
add r2.w, r2.z, r0.x
mul r2.w, r2.w, r0.y
abs r3.x, r2.w
add r3.y, -r3.x, c71.x
cmp r3.z, r3.y, c68.z, c71.x
add r3.z, -r3.z, c71.x
mul r3.y, r3.y, c74.z
add r3.y, r3.y, c74.y
mul r3.y, r3.x, r3.y
rcp r3.w, r3.x
rcp r3.x, r3.x
mov r3.x, -r3.x
add r3.x, r3.x, c71.x
mul r3.x, r3.x, c74.z
add r3.x, r3.x, c74.y
mul r3.x, r3.w, r3.x
add r3.x, -r3.x, c74.w
cmp r3.x, -r3.z, r3.x, r3.y
cmp r2.w, r2.w, c68.z, c71.x
cmp r2.w, -r2.w, r3.x, -r3.x
add r2.y, r2.y, -r2.w
mul r0.y, r2.y, r0.y
add r2.y, r1.x, -r2.z
mul r0.z, r2.y, r0.z
add r0.y, r0.y, -r0.z
max r0.y, r0.y, c68.z
cmp r0.y, -r2.x, c68.z, r0.y
max r0.x, -r0.x, r2.z
min r0.x, r0.x, r1.x
add r0.x, r0.x, -c58.x
mul r0.x, r0.x, c58.y
mov_sat r0.x, r0.x
mul r0.z, r0.x, c72.y
add r0.z, -r0.z, c75.x
mul r0.z, r0.x, r0.z
mul r0.x, r0.x, r0.z
mul r0.y, r0.y, c62.x
mul r0.x, r0.y, r0.x
mul r0.xyz, c47.xyzx, r0.x
add r1.xyz, r1.yzwy, r0.xyzx
max r1.w, r0.y, r0.z
max r0.x, r0.x, r1.w
mul r0.x, r0.x, c58.z
mul r0.y, c58.w, r0.x
add r0.x, c58.w, r0.x
rcp r0.x, r0.x
mul r0.x, r0.y, r0.x
add r0.x, r0.w, r0.x
mul r0.yzw, r1.xxyz, c58.z
mov r1.xyz, r0.yzwy
mov r1.w, r0.x
mov oC0.xyzw, r1.xyzw
