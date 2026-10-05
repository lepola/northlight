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
def c15 = -9.99999982e-15, 9.99999982e-15, 5.00000007e-02, 5.00000000e-01
def c16 = 0.00000000e+00, 0.00000000e+00, -1.00000000e+00, 9.99999975e-05
def c17 = 2.00000000e+00, 2.00000000e+00, 5.00000007e-02, 5.00000007e-02
def c18 = 5.29829178e+01, 3.74136009e+01, 6.71105608e-02, 5.83714992e-03
def c19 = -1.00000000e+00, -1.00000000e+00, -8.00000000e+00, -2.00000000e+00
def c20 = 3.23399991e-01, 1.33900002e-01, -2.48799995e-01, 6.00499988e-01
def c21 = -8.31499994e-01, -3.44399989e-01, -3.00000000e+00, -4.00000000e+00
def c22 = 3.82699996e-01, -9.23900008e-01, 8.31499994e-01, -3.44399989e-01
def c23 = -5.00000000e+00, 2.48799995e-01, 6.00499988e-01, -6.00000000e+00
def c24 = -3.23399991e-01, 1.33900002e-01, -7.00000000e+00, 9.99999996e-13
def c25 = -3.82699996e-01, -9.23900008e-01, -7.99999982e-02, 1.08695650e+00
def c26 = 1.00000001e-10, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
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
mad r1.xy, v0.xyxx, r1.xyxx, c9.xyxx
frc r1.xy, r1.xyxx
add r1.xy, -r1.xyxx, c9.zwzz
mad r1.xy, r1.xyxx, c0.xyxx, v0.xyxx
mul r1.zw, c0.xxxy, c10.xxxy
add r2.xy, -r1.zwzz, c10.zwzz
max r1.xy, r1.xyxx, r1.zwzz
min r1.xy, r1.xyxx, r2.xyxx
mov r2.xyzw, c11.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s1
add r0.w, r2.x, -c3.x
mul r0.w, r0.w, c3.y
mov_sat r0.w, r0.w
add r1.z, r0.w, c12.x
cmp r1.z, r1.z, c5.y, c10.z
add r1.z, -r1.z, c10.z
add r1.w, c4.x, c12.y
cmp r1.w, r1.w, c5.y, c10.z
mov r2.x, r2.y
cmp r2.x, -r1.w, r2.x, c5.y
mov r2.y, r2.x
add r1.w, -r1.w, c10.z
if_ne r1.w, -r1.w
    mov r3.xyzw, c11.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s3
    add r1.w, c0.z, -r3.x
    cmp r1.w, r1.w, c5.y, c10.z
    mul r2.x, c0.z, c0.w
    add r2.z, c0.w, -c0.z
    mul r2.z, r0.w, r2.z
    add r2.z, c0.w, -r2.z
    max r2.z, r2.z, c12.z
    rcp r2.z, r2.z
    mul r2.x, r2.x, r2.z
    mul r2.z, r3.x, c13.x
    max r2.z, r2.z, c12.w
    add r2.x, r2.x, r2.z
    add r2.x, r2.x, -r3.x
    cmp r2.x, r2.x, c5.y, c10.z
    add r2.x, -r2.x, c10.z
    min r1.w, r1.w, r2.x
    add r2.x, -r3.y, c13.y
    cmp r2.x, r2.x, c5.y, c10.z
    min r1.w, r1.w, r2.x
    mov r2.y, r1.w
else
endif
mov r1.w, r2.y
max r1.z, r1.z, r1.w
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.z, r2.xyzw, c14.xyzw
mov r3.xyzw, r2.xyzw
add r1.z, -r1.z, c10.z
if_ne r1.z, -r1.z
    mul r1.z, c0.z, c0.w
    add r1.w, c0.w, -c0.z
    mul r0.w, r0.w, r1.w
    add r0.w, c0.w, -r0.w
    max r0.w, r0.w, c12.z
    rcp r0.w, r0.w
    mul r0.w, r1.z, r0.w
    mul r2.x, r1.x, c13.z
    add r2.x, r2.x, c13.w
    mul r2.x, r2.x, r0.w
    rcp r2.y, c1.x
    mul r2.x, r2.x, r2.y
    mul r2.y, r1.y, c13.z
    add r2.y, -r2.y, c10.z
    mul r2.y, r2.y, r0.w
    rcp r2.z, c1.y
    mul r2.y, r2.y, r2.z
    mov r2.z, r2.y
    mov r2.w, r0.w
    mov r4.xyz, r2.xzwx
    mov r5.x, c0.x
    mov r5.y, c5.y
    mov r5.z, c5.y
    mov r5.w, c0.y
    mov r6.xy, r5.xyxx
    add r6.xy, r1.xyxx, -r6.xyxx
    mov r6.zw, r5.xxxy
    add r6.zw, r1.xxxy, -r6.xxzw
    mov r7.xyzw, c11.xyzw
    mov r7.xy, r6.zwzz
    texldl r7.xyzw, r7.xyzw, s1
    add r2.y, r7.x, -c3.x
    mul r2.y, r2.y, c3.y
    mov_sat r2.y, r2.y
    mul r2.y, r2.y, r1.w
    add r2.y, c0.w, -r2.y
    max r2.y, r2.y, c12.z
    rcp r2.y, r2.y
    mul r2.y, r1.z, r2.y
    mul r4.w, r6.x, c13.z
    add r4.w, r4.w, c13.w
    mul r4.w, r4.w, r2.y
    rcp r6.z, c1.x
    mul r4.w, r4.w, r6.z
    mul r6.x, r6.y, c13.z
    add r6.x, -r6.x, c10.z
    mul r6.x, r6.x, r2.y
    rcp r6.y, c1.y
    mul r6.x, r6.x, r6.y
    mov r6.y, r4.w
    mov r6.z, r6.x
    mov r6.w, r2.y
    mov r7.xy, r5.xyxx
    add r7.xy, r1.xyxx, r7.xyxx
    add r5.xy, r1.xyxx, r5.xyxx
    mov r8.xyzw, c11.xyzw
    mov r8.xy, r5.xyxx
    texldl r8.xyzw, r8.xyzw, s1
    add r4.w, r8.x, -c3.x
    mul r4.w, r4.w, c3.y
    mov_sat r4.w, r4.w
    mul r4.w, r4.w, r1.w
    add r4.w, c0.w, -r4.w
    max r4.w, r4.w, c12.z
    rcp r4.w, r4.w
    mul r4.w, r1.z, r4.w
    mul r5.x, r7.x, c13.z
    add r5.x, r5.x, c13.w
    mul r5.x, r5.x, r4.w
    rcp r5.y, c1.x
    mul r5.x, r5.x, r5.y
    mul r5.y, r7.y, c13.z
    add r5.y, -r5.y, c10.z
    mul r5.y, r5.y, r4.w
    rcp r6.x, c1.y
    mul r5.y, r5.y, r6.x
    mov r7.x, r5.x
    mov r7.y, r5.y
    mov r7.z, r4.w
    mov r5.xy, r5.zwzz
    add r5.xy, r1.xyxx, -r5.xyxx
    mov r8.xy, r5.zwzz
    add r8.xy, r1.xyxx, -r8.xyxx
    mov r9.xyzw, c11.xyzw
    mov r9.xy, r8.xyxx
    mov r8.xyzw, r9.xyzw
    texldl r8.xyzw, r8.xyzw, s1
    add r6.x, r8.x, -c3.x
    mul r6.x, r6.x, c3.y
    mov_sat r6.x, r6.x
    mul r6.x, r6.x, r1.w
    add r6.x, c0.w, -r6.x
    max r6.x, r6.x, c12.z
    rcp r6.x, r6.x
    mul r6.x, r1.z, r6.x
    mul r7.w, r5.x, c13.z
    add r7.w, r7.w, c13.w
    mul r7.w, r7.w, r6.x
    rcp r8.x, c1.x
    mul r7.w, r7.w, r8.x
    mul r5.x, r5.y, c13.z
    add r5.x, -r5.x, c10.z
    mul r5.x, r5.x, r6.x
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r8.x, r7.w
    mov r8.y, r5.x
    mov r8.z, r6.x
    mov r5.xy, r5.zwzz
    add r5.xy, r1.xyxx, r5.xyxx
    add r5.zw, r1.xxxy, r5.xxzw
    mov r9.xyzw, c11.xyzw
    mov r9.xy, r5.zwzz
    texldl r9.xyzw, r9.xyzw, s1
    add r5.z, r9.x, -c3.x
    mul r5.z, r5.z, c3.y
    mov_sat r5.z, r5.z
    mul r5.z, r5.z, r1.w
    add r5.z, c0.w, -r5.z
    max r5.z, r5.z, c12.z
    rcp r5.z, r5.z
    mul r5.z, r1.z, r5.z
    mul r5.w, r5.x, c13.z
    add r5.w, r5.w, c13.w
    mul r5.w, r5.w, r5.z
    rcp r7.w, c1.x
    mul r5.w, r5.w, r7.w
    mul r5.x, r5.y, c13.z
    add r5.x, -r5.x, c10.z
    mul r5.x, r5.x, r5.z
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r9.x, r5.w
    mov r9.y, r5.x
    mov r9.z, r5.z
    add r4.w, r4.w, -r0.w
    abs r4.w, r4.w
    add r2.y, r0.w, -r2.y
    abs r2.y, r2.y
    add r2.y, r4.w, -r2.y
    cmp r2.y, r2.y, c5.y, c10.z
    mov r5.xyw, r7.xyxz
    add r5.xyw, r5.xyxw, -r4.xyxz
    add r6.yzw, r4.xxyz, -r6.xyzw
    cmp r5.xyw, -r2.y, r6.yzxw, r5.xyxw
    add r2.y, r5.z, -r0.w
    abs r2.y, r2.y
    add r4.w, r0.w, -r6.x
    abs r4.w, r4.w
    add r2.y, r2.y, -r4.w
    cmp r2.y, r2.y, c5.y, c10.z
    mov r6.xyz, r9.xyzx
    add r6.xyz, r6.xyzx, -r4.xyzx
    mov r7.xyz, r8.xyzx
    add r4.xyz, r4.xyzx, -r7.xyzx
    cmp r4.xyz, -r2.y, r4.xyzx, r6.xyzx
    dp3 r2.y, r4.xyzx, r4.xyzx
    add r2.y, r2.y, c15.x
    cmp r2.y, r2.y, c5.y, c10.z
    mov r6.xyz, r9.xyzx
    mov r7.xyz, r8.xyzx
    add r6.xyz, r6.xyzx, -r7.xyzx
    cmp r4.xyz, -r2.y, r4.xyzx, r6.xyzx
    mul r6.xyz, r5.wxyw, r4.yzxy
    mul r4.xyz, r5.ywxy, r4.zxyz
    add r4.xyz, r4.xyzx, -r6.xyzx
    dp3 r2.y, r4.xyzx, r4.xyzx
    add r4.w, -r2.y, c15.y
    cmp r4.w, r4.w, c5.y, c10.z
    max r2.y, r2.y, c15.y
    rsq r2.y, r2.y
    mov r5.xyz, r2.y
    mul r4.xyz, r4.xyzx, r5.xyzx
    cmp r4.xyz, -r4.w, c16.xyzx, r4.xyzx
    cmp r2.y, -r4.z, c5.y, c10.z
    cmp r4.xyz, -r2.y, r4.xyzx, -r4.xyzx
    max r2.y, c2.y, c15.z
    mul r4.w, r2.y, c15.w
    mul r5.xy, r4.w, c1.xyxx
    max r0.w, r0.w, c0.z
    rcp r5.z, r0.w
    rcp r5.w, r0.w
    mul r5.xy, r5.xyxx, r5.zwzz
    mul r5.zw, c0.xxxy, c17.xxxy
    max r5.xy, r5.xyxx, r5.zwzz
    min r5.xy, r5.xyxx, c17.zwzz
    rcp r5.z, c0.x
    rcp r5.w, c0.y
    mul r5.zw, r1.xxxy, r5.xxzw
    dp2add r0.w, r5.zwzz, c18.zwzz, c5.y
    frc r0.w, r0.w
    mul r5.zw, r0.w, c18.xxxy
    frc r5.zw, r5.xxzw
    mul r5.zw, r5.xxzw, c17.xxxy
    add r5.zw, r5.xxzw, c19.xxxy
    dp2add r0.w, r5.zwzz, r5.zwzz, c5.y
    max r0.w, r0.w, c16.w
    rsq r0.w, r0.w
    mov r6.xy, r0.w
    mul r5.zw, r5.xxzw, r6.xxxy
    mul r6.xy, r5.zwzz, r5.xyxx
    mov r6.z, -r5.w
    mov r6.w, r5.z
    mov r5.zw, r6.xxzw
    mul r5.xy, r5.zwzz, r5.xyxx
    mov r0.w, c5.y
    mov r4.w, c5.y
    rep i0.xyzw
        mov r10.x, r4.w
        add r10.x, r10.x, c19.z
        cmp r10.x, r10.x, c5.y, c10.z
        add r10.x, -r10.x, c10.z
        if_ne r10.x, -r10.x
            break
        else
        endif
        mov r10.x, r4.w
        abs r10.y, r10.x
        add r10.y, -r10.y, -r10.y
        cmp r10.y, r10.y, c5.y, c10.z
        add r10.y, -r10.y, c10.z
        cmp r10.yz, -r10.y, c5.xyzx, c20.xxyx
        add r10.w, r10.x, c13.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c20.xzwx
        add r10.w, r10.x, c19.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c21.xxyx
        add r10.w, r10.x, c21.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c22.xxyx
        add r10.w, r10.x, c21.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c22.xzwx
        add r10.w, r10.x, c23.x
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c23.xyzx
        add r10.w, r10.x, c23.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r10.yz, -r10.w, r10.xyzx, c24.xxyx
        add r10.x, r10.x, c24.z
        abs r10.x, r10.x
        add r10.x, -r10.x, -r10.x
        cmp r10.x, r10.x, c5.y, c10.z
        add r10.x, -r10.x, c10.z
        cmp r10.xy, -r10.x, r10.yzyy, c25.xyxx
        mul r10.xy, r10.x, r6.xyxx
        add r10.xy, r1.xyxx, r10.xyxx
        mov r10.z, r4.w
        abs r10.w, r10.z
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, c5.yzyy, c20.xyxx
        add r10.w, r10.z, c13.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c20.zwzz
        add r10.w, r10.z, c19.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c21.xyxx
        add r10.w, r10.z, c21.z
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c22.xyxx
        add r10.w, r10.z, c21.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c22.zwzz
        add r10.w, r10.z, c23.x
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c23.yzyy
        add r10.w, r10.z, c23.w
        abs r10.w, r10.w
        add r10.w, -r10.w, -r10.w
        cmp r10.w, r10.w, c5.y, c10.z
        add r10.w, -r10.w, c10.z
        cmp r11.xy, -r10.w, r11.xyxx, c24.xyxx
        add r10.z, r10.z, c24.z
        abs r10.z, r10.z
        add r10.z, -r10.z, -r10.z
        cmp r10.z, r10.z, c5.y, c10.z
        add r10.z, -r10.z, c10.z
        cmp r10.zw, -r10.z, r11.xxxy, c25.xxxy
        mul r10.zw, r10.w, r5.xxxy
        add r10.xy, r10.xyxx, r10.zwzz
        mov r8.xyzw, c11.xyzw
        mov r8.xy, r10.xyxx
        mov r11.xyzw, r8.xyzw
        texldl r11.xyzw, r11.xyzw, s1
        add r10.z, r11.x, -c3.x
        mul r10.z, r10.z, c3.y
        mov_sat r10.z, r10.z
        mul r10.w, r10.z, r1.w
        add r10.w, c0.w, -r10.w
        max r10.w, r10.w, c12.z
        rcp r5.z, r10.w
        mul r10.w, r1.z, r5.z
        mul r11.x, r10.x, c13.z
        add r11.x, r11.x, c13.w
        mul r11.x, r11.x, r10.w
        rcp r5.w, c1.x
        mul r11.x, r11.x, r5.w
        mul r11.y, r10.y, c13.z
        add r11.y, -r11.y, c10.z
        mul r11.y, r11.y, r10.w
        rcp r6.w, c1.y
        mul r11.y, r11.y, r6.w
        mov r7.x, r11.x
        mov r7.y, r11.y
        mov r7.z, r10.w
        mov r11.xyz, r7.xyzx
        mov r12.xyz, r2.xzwx
        add r11.xyz, r11.xyzx, -r12.xyzx
        dp3 r10.w, r11.xyzx, r11.xyzx
        max r11.w, r10.w, c24.w
        rsq r7.w, r11.w
        rcp r9.w, r7.w
        mov r12.xyz, r9.w
        rcp r9.x, r12.x
        rcp r9.y, r12.y
        rcp r9.z, r12.z
        mul r11.xyz, r11.xyzx, r9.xyzx
        dp3 r11.x, r4.xyzx, r11.xyzx
        add r11.x, r11.x, c25.z
        mul r11.x, r11.x, c25.w
        mov_sat r11.x, r11.x
        rcp r6.z, r2.y
        mul r11.y, r9.w, r6.z
        add r11.y, -r11.y, c10.z
        mov_sat r11.y, r11.y
        mul r11.y, r11.y, r11.y
        add r10.z, r10.z, c12.x
        cmp r10.z, r10.z, c5.y, c10.z
        add r10.w, -r10.w, c26.x
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
        mov r10.x, r4.w
        add r10.x, r10.x, c10.z
        mov r4.w, r10.x
    endrep
    mul r0.w, c1.z, r0.w
    mul r0.w, r0.w, c9.x
    add r0.w, -r0.w, c10.z
    mov_sat r0.w, r0.w
    mov r1.x, c5.y
    mov r1.y, c5.y
    mov r1.z, c5.y
    mov r1.w, r0.w
    mov r3.xyzw, r1.xyzw
else
endif
mov r1.xyzw, r3.xyzw
mov r0.w, r1.w
mov oC0.xyzw, r0.xyzw
