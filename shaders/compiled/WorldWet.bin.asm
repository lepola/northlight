ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = -9.99989986e-01, 1.00000000e+00, -5.00000000e-01, 9.99999975e-06
def c71 = 1.20000001e-02, 6.99999975e-04, 3.00000003e-03, -5.50000012e-01
def c72 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c73 = 2.85714293e+00, 5.00000000e-01, 5.00000000e-01, 5.00000000e-01
def c74 = 4.00000000e+00, 6.00000000e+00, -2.40000000e+01, 2.08333340e-02
def c75 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 5.00000000e+00
def c76 = 9.59999979e-01, 3.99999991e-02, 1.50000006e-01, 0.00000000e+00
def c77 = 3.49999994e-01, 3.49999994e-01, 3.49999994e-01, 0.00000000e+00
dcl_2d s1
dcl_2d s14
dcl_2d s10
dcl_2d s7
dcl_2d s11
mov r0.xyzw, c68.xyzw
mov r0.xy, v0.xyxx
texldl r0.xyzw, r0.xyzw, s14
mul r1.xy, v0.xyxx, c33.xyxx
frc r2.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r2.xyzw
add r1.zw, c33.xxxy, c69.xxxy
max r1.xy, r1.xyxx, c68.xyxx
min r1.xy, r1.xyxx, r1.zwzz
add r1.xy, r1.xyxx, c69.zwzz
mul r1.xy, r1.xyxx, c0.xyxx
mov r2.xyzw, c68.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s1
add r1.z, r2.x, -c1.w
mul r1.z, r1.z, c2.x
mov_sat r1.z, r1.z
add r1.w, r1.z, c70.x
cmp r1.w, r1.w, c68.x, c70.y
add r1.w, -r1.w, c70.y
add r2.x, c30.x, c70.z
cmp r2.x, r2.x, c68.x, c70.y
mov r2.y, r2.z
cmp r2.y, -r2.x, r2.y, c68.x
mov r2.z, r2.y
add r2.x, -r2.x, c70.y
if_ne r2.x, -r2.x
    mov r3.xyzw, c68.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s11
    add r2.x, c0.z, -r3.x
    cmp r2.x, r2.x, c68.x, c70.y
    mul r2.y, c0.z, c0.w
    add r2.w, c0.w, -c0.z
    mul r2.w, r1.z, r2.w
    add r2.w, c0.w, -r2.w
    max r2.w, r2.w, c70.w
    rcp r2.w, r2.w
    mul r2.y, r2.y, r2.w
    mul r2.w, r3.x, c71.y
    max r2.w, r2.w, c71.x
    add r2.y, r2.y, r2.w
    add r2.y, r2.y, -r3.x
    cmp r2.y, r2.y, c68.x, c70.y
    add r2.y, -r2.y, c70.y
    min r2.x, r2.x, r2.y
    add r2.y, -r3.y, c71.z
    cmp r2.y, r2.y, c68.x, c70.y
    min r2.x, r2.x, r2.y
    cmp r2.x, -r2.x, c68.x, r3.x
    mov r2.z, r2.x
else
endif
mov r2.x, r2.z
cmp r2.x, -r2.x, c68.x, c70.y
max r1.w, r1.w, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.w, r2.xyzw, c68.xyzw
mov r3.xyzw, r2.xyzw
add r1.w, -r1.w, c70.y
if_ne r1.w, -r1.w
    mul r1.w, c0.z, c0.w
    add r2.x, c0.w, -c0.z
    mul r1.z, r1.z, r2.x
    add r1.z, c0.w, -r1.z
    max r1.z, r1.z, c70.w
    rcp r1.z, r1.z
    mul r1.z, r1.w, r1.z
    mul r2.xy, c0.xyxx, c69.zwzz
    add r1.xy, r1.xyxx, -r2.xyxx
    mul r1.xy, r1.xyxx, c72.xyxx
    add r1.xy, r1.xyxx, c72.zwzz
    rcp r2.x, c1.x
    rcp r2.y, c1.y
    mul r1.xy, r1.xyxx, r2.xyxx
    mov r1.w, c1.z
    mul r1.xyz, r1.xywx, r1.z
    mul r2.xyz, r1.x, c3.xyzx
    mul r4.xyz, r1.y, c4.xyzx
    add r2.xyz, r2.xyzx, r4.xyzx
    mul r1.xyz, r1.z, c5.xyzx
    add r1.xyz, r2.xyzx, r1.xyzx
    add r1.xyz, r1.xyzx, c6.xyzx
    add r1.w, r0.z, c71.w
    mul r1.w, r1.w, c73.x
    mov_sat r1.w, r1.w
    mul r2.x, c19.w, c69.z
    mov r2.y, c68.x
    mov r2.z, c68.x
    mov r2.w, r2.x
    mov r2.xyz, r2.yzwy
    add r2.xyz, r1.xyzx, r2.xyzx
    rcp r4.x, c19.w
    rcp r4.y, c19.w
    rcp r4.z, c19.w
    mul r2.xyz, r2.xyzx, r4.xyzx
    add r2.xyz, r2.xyzx, c73.yzwy
    frc r4.xyzw, r2.xyzx
    add r2.xyz, r2.xyzx, -r4.xyzw
    rcp r4.x, c20.x
    rcp r4.y, c20.x
    rcp r4.z, c20.x
    mul r4.xyz, r2.xyzx, r4.xyzx
    frc r5.xyzw, r4.xyzx
    add r4.xyz, r4.xyzx, -r5.xyzw
    mul r4.xyz, c20.x, r4.xyzx
    add r4.xyz, r2.xyzx, -r4.xyzx
    mul r2.w, r4.z, c20.x
    add r2.w, r4.x, r2.w
    mov r5.x, r2.w
    mov r5.y, r4.y
    add r5.xy, r5.xyxx, c69.zwzz
    mul r2.w, c20.x, c20.x
    mov r5.z, r2.w
    mov r5.w, c20.x
    rcp r6.x, r5.z
    rcp r6.y, r5.w
    mul r5.xy, r5.xyxx, r6.xyxx
    mov r6.xyzw, c68.xyzw
    mov r6.xy, r5.xyxx
    texldl r6.xyzw, r6.xyzw, s10
    mul r2.w, c20.x, c74.x
    add r2.w, r4.y, r2.w
    add r2.w, r2.w, c69.z
    mul r4.x, c20.x, c74.y
    rcp r4.x, r4.x
    mul r2.w, r2.w, r4.x
    mov r4.x, r5.x
    mov r4.y, r2.w
    mov r4.z, c68.x
    mov r4.w, c68.x
    mov r5.xyzw, c68.xyzw
    mov r5.xy, r4.xyxx
    mov r4.xyzw, r5.xyzw
    texldl r4.xyzw, r4.xyzw, s7
    add r2.w, c20.w, c70.z
    cmp r2.w, r2.w, c68.x, c70.y
    add r2.w, -r2.w, c70.y
    add r2.xyz, r6.xyzx, -r2.xyzx
    abs r2.xyz, r2.xyzx
    add r2.xyz, -r2.xyzx, -r2.xyzx
    cmp r2.xyz, r2.xyzx, c68.xyzx, c75.xyzx
    add r2.xyz, -r2.xyzx, c75.xyzx
    min r5.x, r2.x, r2.y
    min r2.x, r5.x, r2.z
    min r2.x, r2.w, r2.x
    cmp r2.y, r6.w, c68.x, c70.y
    add r2.y, -r2.y, c70.y
    min r2.x, r2.x, r2.y
    add r2.y, r4.x, c74.z
    mul r2.y, r2.y, c74.w
    mov_sat r2.y, r2.y
    mul r2.y, r2.y, r4.z
    cmp r2.x, -r2.x, c68.x, r2.y
    mul r1.w, c59.y, r1.w
    mul r1.w, r1.w, r2.x
    mul r1.w, r1.w, r0.w
    add r1.xyz, c15.xyzx, -r1.xyzx
    dp3 r2.x, r1.xyzx, r1.xyzx
    rsq r2.x, r2.x
    mov r2.xyz, r2.x
    mul r1.xyz, r2.xyzx, r1.xyzx
    dp3 r1.x, r0.xyzx, r1.xyzx
    mov_sat r1.x, r1.x
    add r1.x, -r1.x, c70.y
    log r1.x, r1.x
    mul r1.x, r1.x, c75.w
    exp r1.x, r1.x
    mul r1.x, r1.x, c76.x
    add r1.x, r1.x, c76.y
    dp3 r0.x, r0.xyzx, c28.xyzx
    mov_sat r0.x, r0.x
    mul r0.xyz, c29.xyzx, r0.x
    add r0.xyz, c18.xyzx, r0.xyzx
    mul r0.w, r1.x, c76.z
    mul r1.xyz, r0.w, c18.xyzx
    mul r0.xyz, r0.xyzx, c77.xyzx
    add r0.xyz, r1.xyzx, -r0.xyzx
    mul r0.xyz, r1.w, r0.xyzx
    mov r0.w, c68.x
    mov r3.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r3.xyzw
