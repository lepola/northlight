ps_3_0
dcl_texcoord0 v0
def c5 = 4.00000000e+00, 0.00000000e+00, 0.00000000e+00, 4.00000000e+00
def c6 = 2.12599993e-01, 7.15200007e-01, 7.22000003e-02, 3.57142854e+00
def c7 = -2.57142878e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c8 = 4.00000000e+00, 4.00000000e+00, 4.00000000e+00, 1.25000000e-01
def c9 = 2.50000000e-01, 2.50000000e-01, 7.50000000e-01, 7.50000000e-01
def c10 = 1.50000000e+00, 1.50000000e+00, 1.00000000e+00, 1.00000000e+00
def c11 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c12 = -9.99989986e-01, -5.00000000e-01, 9.99999975e-06, 1.20000001e-02
def c13 = 6.99999975e-04, 3.00000003e-03, 2.00000000e+00, -1.00000000e+00
def c14 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c15 = 9.99999982e-15, 0.00000000e+00, 0.00000000e+00, -1.00000000e+00
def c16 = 5.00000007e-02, 5.00000000e-01, 2.00000000e+00, 2.00000000e+00
def c17 = 5.00000007e-02, 5.00000007e-02, 5.29829178e+01, 3.74136009e+01
def c18 = 3.35552804e-02, 2.91857496e-03, -1.00000000e+00, -1.00000000e+00
def c19 = 9.99999975e-05, -8.00000000e+00, 3.23399991e-01, 1.33900002e-01
def c20 = -2.48799995e-01, 6.00499988e-01, -2.00000000e+00, -3.00000000e+00
def c21 = -8.31499994e-01, -3.44399989e-01, 3.82699996e-01, -9.23900008e-01
def c22 = -4.00000000e+00, 8.31499994e-01, -3.44399989e-01, -5.00000000e+00
def c23 = 2.48799995e-01, 6.00499988e-01, -6.00000000e+00, -7.00000000e+00
def c24 = -3.23399991e-01, 1.33900002e-01, -3.82699996e-01, -9.23900008e-01
def c25 = 9.99999996e-13, -7.99999982e-02, 1.08695650e+00, 1.00000001e-10
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s0
dcl_2d s3
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
rcp r1.x, c0.x
rcp r1.y, c0.y
mad r1.zw, v0.xxxy, r1.xxxy, c9.xxxy
frc r1.zw, r1.xxzw
add r1.zw, -r1.xxzw, c9.xxzw
mad r1.zw, r1.xxzw, c0.xxxy, v0.xxxy
mul r2.xy, c0.xyxx, c10.xyxx
add r2.zw, -r2.xxxy, c10.xxzw
max r1.zw, r1.xxzw, r2.xxxy
min r1.zw, r1.xxzw, r2.xxzw
mov r2.xyzw, c11.xyzw
mov r2.xy, r1.zwzz
texldl r2.xyzw, r2.xyzw, s1
add r0.w, r2.x, -c3.x
mul r0.w, r0.w, c3.y
mov_sat r0.w, r0.w
add r2.x, r0.w, c12.x
cmp r2.x, r2.x, c5.y, c10.z
add r2.x, -r2.x, c10.z
add r2.y, c4.x, c12.y
cmp r2.y, r2.y, c5.y, c10.z
mov r2.z, r2.w
cmp r2.z, -r2.y, r2.z, c5.y
mov r2.w, r2.z
add r2.y, -r2.y, c10.z
if_ne r2.y, -r2.y
    mov r3.xyzw, c11.xyzw
    mov r3.xy, r1.zwzz
    texldl r3.xyzw, r3.xyzw, s3
    add r2.y, c0.z, -r3.x
    cmp r2.y, r2.y, c5.y, c10.z
    mul r2.z, c0.z, c0.w
    add r4.x, c0.w, -c0.z
    mul r4.x, r0.w, r4.x
    add r4.x, c0.w, -r4.x
    max r4.x, r4.x, c12.z
    rcp r4.x, r4.x
    mul r2.z, r2.z, r4.x
    mul r4.x, r3.x, c13.x
    max r4.x, r4.x, c12.w
    add r2.z, r2.z, r4.x
    add r2.z, r2.z, -r3.x
    cmp r2.z, r2.z, c5.y, c10.z
    add r2.z, -r2.z, c10.z
    min r2.y, r2.y, r2.z
    add r2.z, -r3.y, c13.y
    cmp r2.z, r2.z, c5.y, c10.z
    min r2.y, r2.y, r2.z
    mov r2.w, r2.y
else
endif
mov r2.y, r2.w
max r2.x, r2.x, r2.y
mov r3.xyzw, r4.xyzw
cmp r3.xyzw, -r2.x, r3.xyzw, c14.xyzw
mov r4.xyzw, r3.xyzw
add r2.x, -r2.x, c10.z
if_ne r2.x, -r2.x
    mul r2.x, c0.z, c0.w
    add r2.y, c0.w, -c0.z
    mul r0.w, r0.w, r2.y
    add r0.w, c0.w, -r0.w
    max r0.w, r0.w, c12.z
    rcp r0.w, r0.w
    mul r0.w, r2.x, r0.w
    mul r2.z, r1.z, c13.z
    add r2.z, r2.z, c13.w
    mul r2.z, r2.z, r0.w
    rcp r2.w, c1.x
    mul r2.z, r2.z, r2.w
    mul r2.w, r1.w, c13.z
    add r2.w, -r2.w, c10.z
    mul r2.w, r2.w, r0.w
    rcp r3.x, c1.y
    mul r2.w, r2.w, r3.x
    mov r3.x, r2.z
    mov r3.y, r2.w
    mov r3.z, r0.w
    mov r5.xyz, r3.xyzx
    mov r2.z, c0.x
    mov r2.w, c5.y
    mov r6.x, c5.y
    mov r6.y, c0.y
    mov r6.zw, r2.xxzw
    add r6.zw, r1.xxzw, -r6.xxzw
    mov r7.xy, r2.zwzz
    add r7.xy, r1.zwzz, -r7.xyxx
    mov r8.xyzw, c11.xyzw
    mov r8.xy, r7.xyxx
    mov r7.xyzw, r8.xyzw
    texldl r7.xyzw, r7.xyzw, s1
    add r3.w, r7.x, -c3.x
    mul r3.w, r3.w, c3.y
    mov_sat r3.w, r3.w
    mul r3.w, r3.w, r2.y
    add r3.w, c0.w, -r3.w
    max r3.w, r3.w, c12.z
    rcp r3.w, r3.w
    mul r3.w, r2.x, r3.w
    mul r5.w, r6.z, c13.z
    add r5.w, r5.w, c13.w
    mul r5.w, r5.w, r3.w
    rcp r7.x, c1.x
    mul r5.w, r5.w, r7.x
    mul r6.z, r6.w, c13.z
    add r6.z, -r6.z, c10.z
    mul r6.z, r6.z, r3.w
    rcp r6.w, c1.y
    mul r6.z, r6.z, r6.w
    mov r7.x, r5.w
    mov r7.y, r6.z
    mov r7.z, r3.w
    mov r6.zw, r2.xxzw
    add r6.zw, r1.xxzw, r6.xxzw
    add r2.zw, r1.xxzw, r2.xxzw
    mov r8.xyzw, c11.xyzw
    mov r8.xy, r2.zwzz
    texldl r8.xyzw, r8.xyzw, s1
    add r2.z, r8.x, -c3.x
    mul r2.z, r2.z, c3.y
    mov_sat r2.z, r2.z
    mul r2.z, r2.z, r2.y
    add r2.z, c0.w, -r2.z
    max r2.z, r2.z, c12.z
    rcp r2.z, r2.z
    mul r2.z, r2.x, r2.z
    mul r2.w, r6.z, c13.z
    add r2.w, r2.w, c13.w
    mul r2.w, r2.w, r2.z
    rcp r5.w, c1.x
    mul r2.w, r2.w, r5.w
    mul r5.w, r6.w, c13.z
    add r5.w, -r5.w, c10.z
    mul r5.w, r5.w, r2.z
    rcp r6.z, c1.y
    mul r5.w, r5.w, r6.z
    mov r8.x, r2.w
    mov r8.y, r5.w
    mov r8.z, r2.z
    mov r6.zw, r6.xxxy
    add r6.zw, r1.xxzw, -r6.xxzw
    mov r9.xy, r6.xyxx
    add r9.xy, r1.zwzz, -r9.xyxx
    mov r10.xyzw, c11.xyzw
    mov r10.xy, r9.xyxx
    mov r9.xyzw, r10.xyzw
    texldl r9.xyzw, r9.xyzw, s1
    add r2.w, r9.x, -c3.x
    mul r2.w, r2.w, c3.y
    mov_sat r2.w, r2.w
    mul r2.w, r2.w, r2.y
    add r2.w, c0.w, -r2.w
    max r2.w, r2.w, c12.z
    rcp r2.w, r2.w
    mul r2.w, r2.x, r2.w
    mul r5.w, r6.z, c13.z
    add r5.w, r5.w, c13.w
    mul r5.w, r5.w, r2.w
    rcp r7.w, c1.x
    mul r5.w, r5.w, r7.w
    mul r6.z, r6.w, c13.z
    add r6.z, -r6.z, c10.z
    mul r6.z, r6.z, r2.w
    rcp r6.w, c1.y
    mul r6.z, r6.z, r6.w
    mov r9.x, r5.w
    mov r9.y, r6.z
    mov r9.z, r2.w
    mov r6.zw, r6.xxxy
    add r6.zw, r1.xxzw, r6.xxzw
    add r6.xy, r1.zwzz, r6.xyxx
    mov r10.xyzw, c11.xyzw
    mov r10.xy, r6.xyxx
    texldl r10.xyzw, r10.xyzw, s1
    add r5.w, r10.x, -c3.x
    mul r5.w, r5.w, c3.y
    mov_sat r5.w, r5.w
    mul r5.w, r5.w, r2.y
    add r5.w, c0.w, -r5.w
    max r5.w, r5.w, c12.z
    rcp r5.w, r5.w
    mul r5.w, r2.x, r5.w
    mul r6.x, r6.z, c13.z
    add r6.x, r6.x, c13.w
    mul r6.x, r6.x, r5.w
    rcp r6.y, c1.x
    mul r6.x, r6.x, r6.y
    mul r6.y, r6.w, c13.z
    add r6.y, -r6.y, c10.z
    mul r6.y, r6.y, r5.w
    rcp r6.z, c1.y
    mul r6.y, r6.y, r6.z
    mov r6.z, r6.y
    mov r6.w, r5.w
    add r2.z, r2.z, -r0.w
    abs r2.z, r2.z
    add r3.w, r0.w, -r3.w
    abs r3.w, r3.w
    add r2.z, r2.z, -r3.w
    cmp r2.z, r2.z, c5.y, c10.z
    add r8.xyz, r8.xyzx, -r5.xyzx
    add r7.xyz, r5.xyzx, -r7.xyzx
    cmp r7.xyz, -r2.z, r7.xyzx, r8.xyzx
    add r2.z, r5.w, -r0.w
    abs r2.z, r2.z
    add r2.w, r0.w, -r2.w
    abs r2.w, r2.w
    add r2.z, r2.z, -r2.w
    cmp r2.z, r2.z, c5.y, c10.z
    mov r6.xyz, r6.xzwx
    add r6.xyz, r6.xyzx, -r5.xyzx
    mov r8.xyz, r9.xyzx
    add r5.xyz, r5.xyzx, -r8.xyzx
    cmp r5.xyz, -r2.z, r5.xyzx, r6.xyzx
    mul r6.xyz, r7.zxyz, r5.yzxy
    mul r5.xyz, r7.yzxy, r5.zxyz
    add r5.xyz, r5.xyzx, -r6.xyzx
    dp3 r2.z, r5.xyzx, r5.xyzx
    add r2.w, -r2.z, c15.x
    cmp r2.w, r2.w, c5.y, c10.z
    max r2.z, r2.z, c15.x
    rsq r2.z, r2.z
    mov r6.xyz, r2.z
    mul r5.xyz, r5.xyzx, r6.xyzx
    cmp r5.xyz, -r2.w, c15.yzwy, r5.xyzx
    cmp r2.z, -r5.z, c5.y, c10.z
    cmp r5.xyz, -r2.z, r5.xyzx, -r5.xyzx
    max r2.z, c2.y, c16.x
    mul r2.w, r2.z, c16.y
    mul r6.xy, r2.w, c1.xyxx
    max r0.w, r0.w, c0.z
    rcp r6.z, r0.w
    rcp r6.w, r0.w
    mul r6.xy, r6.xyxx, r6.zwzz
    mul r6.zw, c0.xxxy, c16.xxzw
    max r6.xy, r6.xyxx, r6.zwzz
    min r6.xy, r6.xyxx, c17.xyxx
    mul r1.xy, r1.zwzz, r1.xyxx
    dp2add r0.w, r1.xyxx, c18.xyxx, c5.y
    frc r0.w, r0.w
    mul r1.xy, r0.w, c17.zwzz
    frc r1.xy, r1.xyxx
    mul r1.xy, r1.xyxx, c16.zwzz
    add r1.xy, r1.xyxx, c18.zwzz
    dp2add r0.w, r1.xyxx, r1.xyxx, c5.y
    max r0.w, r0.w, c19.x
    rsq r0.w, r0.w
    mov r6.zw, r0.w
    mul r1.xy, r1.xyxx, r6.zwzz
    mul r6.zw, r1.xxxy, r6.xxxy
    mov r7.x, -r1.y
    mov r7.y, r1.x
    mov r1.xy, r7.xyxx
    mul r1.xy, r1.xyxx, r6.xyxx
    mov r0.w, c5.y
    mov r2.w, c5.y
    rep i0.xyzw
        mov r10.x, r2.w
        add r10.x, r10.x, c19.y
        cmp r10.x, r10.x, c5.y, c10.z
        add r10.x, -r10.x, c10.z
        if_ne r10.x, -r10.x
            break
        else
        endif
        mov r10.x, r2.w
        abs r10.y, r10.x
        add r10.y, -r10.y, -r10.y
        cmp r10.y, r10.y, c5.y, c10.z
        add r10.y, -r10.y, c10.z
        cmp r10.yz, -r10.y, c5.xyzx, c19.xzwx
        add r10.w, r10.x, c13.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c20.xxyx
        add r10.w, r10.x, c20.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c21.xxyx
        add r10.w, r10.x, c20.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c21.xzwx
        add r10.w, r10.x, c22.x
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c22.xyzx
        add r10.w, r10.x, c22.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c23.xxyx
        add r10.w, r10.x, c23.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c24.xxyx
        add r10.x, r10.x, c23.w
        abs r10.x, r10.x
        add r10.x, -r10.x, -r10.x
        cmp r10.x, r10.x, c5.y, c10.z
        add r10.x, -r10.x, c10.z
        cmp r10.xy, -r10.x, r10.yzyy, c24.zwzz
        mul r10.xy, r10.x, r6.zwzz
        add r10.xy, r1.zwzz, r10.xyxx
        mov r10.z, r2.w
        abs r10.w, r10.z
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, c5.yzyy, c19.zwzz
        add r10.w, r10.z, c13.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c20.xyxx
        add r10.w, r10.z, c20.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c21.xyxx
        add r10.w, r10.z, c20.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c21.zwzz
        add r10.w, r10.z, c22.x
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c22.yzyy
        add r10.w, r10.z, c22.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c23.xyxx
        add r10.w, r10.z, c23.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c24.xyxx
        add r10.z, r10.z, c23.w
        abs r10.z, r10.z
        add r10.z, -r10.z, -r10.z
        cmp r10.z, r10.z, c5.y, c10.z
        add r10.z, -r10.z, c10.z
        cmp r10.zw, -r10.z, r11.xxxy, c24.xxzw
        mul r10.zw, r10.w, r1.xxxy
        add r10.xy, r10.xyxx, r10.zwzz
        mov r9.xyzw, c11.xyzw
        mov r9.xy, r10.xyxx
        mov r11.xyzw, r9.xyzw
        texldl r11.xyzw, r11.xyzw, s1
        add r10.z, r11.x, -c3.x
        mul r10.z, r10.z, c3.y
        mov_sat r10.z, r10.z
        mul r10.w, r10.z, r2.y
        add r10.w, c0.w, -r10.w
        max r10.w, r10.w, c12.z
        rcp r6.x, r10.w
        mul r10.w, r2.x, r6.x
        mul r11.x, r10.x, c13.z
        add r11.x, r11.x, c13.w
        mul r11.x, r11.x, r10.w
        rcp r7.w, c1.x
        mul r11.x, r11.x, r7.w
        mul r11.y, r10.y, c13.z
        add r11.y, -r11.y, c10.z
        mul r11.y, r11.y, r10.w
        rcp r8.w, c1.y
        mul r11.y, r11.y, r8.w
        mov r8.x, r11.x
        mov r8.y, r11.y
        mov r8.z, r10.w
        mov r11.xyz, r8.xyzx
        mov r12.xyz, r3.xyzx
        add r11.xyz, r11.xyzx, -r12.xyzx
        dp3 r10.w, r11.xyzx, r11.xyzx
        max r11.w, r10.w, c25.x
        rsq r6.y, r11.w
        rcp r3.w, r6.y
        mov r12.xyz, r3.w
        rcp r7.x, r12.x
        rcp r7.y, r12.y
        rcp r7.z, r12.z
        mul r11.xyz, r11.xyzx, r7.xyzx
        dp3 r11.x, r5.xyzx, r11.xyzx
        add r11.x, r11.x, c25.y
        mul r11.x, r11.x, c25.z
        mov_sat r11.x, r11.x
        rcp r5.w, r2.z
        mul r11.y, r3.w, r5.w
        add r11.y, -r11.y, c10.z
        mov_sat r11.y, r11.y
        mul r11.y, r11.y, r11.y
        add r10.z, r10.z, c12.x
        cmp r10.z, r10.z, c5.y, c10.z
        add r10.w, -r10.w, c25.w
        cmp r10.w, r10.w, c5.y, c10.z
        min r10.z, r10.z, r10.w
        cmp r10.w, -r10.x, c5.y, c10.z
        min r10.z, r10.z, r10.w
        cmp r10.w, -r10.y, c5.y, c10.z
        min r10.z, r10.z, r10.w
        add r10.w, r10.x, c13.w
        cmp r10.w, r10.w, c5.y, c10.z
        min r10.z, r10.z, r10.w
        add r10.x, r10.y, c13.w
        cmp r10.x, r10.x, c5.y, c10.z
        min r10.x, r10.z, r10.x
        cmp r10.x, -r10.x, c5.y, c10.z
        mul r10.y, r11.x, r11.y
        mul r10.x, r10.y, r10.x
        mov r10.y, r0.w
        add r10.x, r10.y, r10.x
        mov r0.w, r10.x
        mov r10.x, r2.w
        add r10.x, r10.x, c10.z
        mov r2.w, r10.x
    endrep
    mul r0.w, c1.z, r0.w
    mul r0.w, r0.w, c9.x
    add r0.w, -r0.w, c10.z
    mov_sat r0.w, r0.w
    mov r1.x, c5.y
    mov r1.y, c5.y
    mov r1.z, c5.y
    mov r1.w, r0.w
    mov r4.xyzw, r1.xyzw
else
endif
mov r1.xyzw, r4.xyzw
mov r0.w, r1.w
mov oC0.xyzw, r0.xyzw
