ps_3_0
dcl_texcoord0 v0
def c5 = 2.50000000e-01, 2.50000000e-01, 7.50000000e-01, 7.50000000e-01
def c6 = 1.50000000e+00, 1.50000000e+00, 1.00000000e+00, 1.00000000e+00
def c7 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c8 = -9.99989986e-01, -5.00000000e-01, 9.99999975e-06, 1.20000001e-02
def c9 = 6.99999975e-04, 3.00000003e-03, 2.00000000e+00, -1.00000000e+00
def c10 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c11 = -9.99999982e-15, 9.99999982e-15, 5.00000007e-02, 5.00000000e-01
def c12 = 0.00000000e+00, 0.00000000e+00, -1.00000000e+00, 9.99999975e-05
def c13 = 2.00000000e+00, 2.00000000e+00, 5.00000007e-02, 5.00000007e-02
def c14 = 5.29829178e+01, 3.74136009e+01, 6.71105608e-02, 5.83714992e-03
def c15 = -1.00000000e+00, -1.00000000e+00, -8.00000000e+00, -2.00000000e+00
def c16 = 3.23399991e-01, 1.33900002e-01, -2.48799995e-01, 6.00499988e-01
def c17 = -8.31499994e-01, -3.44399989e-01, -3.00000000e+00, -4.00000000e+00
def c18 = 3.82699996e-01, -9.23900008e-01, 8.31499994e-01, -3.44399989e-01
def c19 = -5.00000000e+00, 2.48799995e-01, 6.00499988e-01, -6.00000000e+00
def c20 = -3.23399991e-01, 1.33900002e-01, -7.00000000e+00, 9.99999996e-13
def c21 = -3.82699996e-01, -9.23900008e-01, -7.99999982e-02, 1.08695650e+00
def c22 = 1.00000001e-10, 1.25000000e-01, 1.25000000e-01, 1.25000000e-01
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s0
dcl_2d s3
rcp r0.x, c0.x
rcp r0.y, c0.y
mad r0.xy, v0.xyxx, r0.xyxx, c5.xyxx
frc r0.xy, r0.xyxx
add r0.xy, -r0.xyxx, c5.zwzz
mad r0.xy, r0.xyxx, c0.xyxx, v0.xyxx
mul r0.zw, c0.xxxy, c6.xxxy
add r1.xy, -r0.zwzz, c6.zwzz
max r0.xy, r0.xyxx, r0.zwzz
min r0.xy, r0.xyxx, r1.xyxx
mov r1.xyzw, c7.xyzw
mov r1.xy, r0.xyxx
texldl r1.xyzw, r1.xyzw, s1
add r0.z, r1.x, -c3.x
mul r0.z, r0.z, c3.y
mov_sat r0.z, r0.z
add r0.w, r0.z, c8.x
cmp r0.w, r0.w, c7.x, c6.z
add r0.w, -r0.w, c6.z
add r1.x, c4.x, c8.y
cmp r1.x, r1.x, c7.x, c6.z
mov r1.y, r1.z
cmp r1.y, -r1.x, r1.y, c7.x
mov r1.z, r1.y
add r1.x, -r1.x, c6.z
if_ne r1.x, -r1.x
    mov r2.xyzw, c7.xyzw
    mov r2.xy, r0.xyxx
    texldl r2.xyzw, r2.xyzw, s3
    add r1.x, c0.z, -r2.x
    cmp r1.x, r1.x, c7.x, c6.z
    mul r1.y, c0.z, c0.w
    add r1.w, c0.w, -c0.z
    mul r1.w, r0.z, r1.w
    add r1.w, c0.w, -r1.w
    max r1.w, r1.w, c8.z
    rcp r1.w, r1.w
    mul r1.y, r1.y, r1.w
    mul r1.w, r2.x, c9.x
    max r1.w, r1.w, c8.w
    add r1.y, r1.y, r1.w
    add r1.y, r1.y, -r2.x
    cmp r1.y, r1.y, c7.x, c6.z
    add r1.y, -r1.y, c6.z
    min r1.x, r1.x, r1.y
    add r1.y, -r2.y, c9.y
    cmp r1.y, r1.y, c7.x, c6.z
    min r1.x, r1.x, r1.y
    mov r1.z, r1.x
else
endif
mov r1.x, r1.z
max r0.w, r0.w, r1.x
mov r1.xyzw, r2.xyzw
cmp r1.xyzw, -r0.w, r1.xyzw, c10.xyzw
mov r2.xyzw, r1.xyzw
add r0.w, -r0.w, c6.z
if_ne r0.w, -r0.w
    mul r0.w, c0.z, c0.w
    add r1.x, c0.w, -c0.z
    mul r0.z, r0.z, r1.x
    add r0.z, c0.w, -r0.z
    max r0.z, r0.z, c8.z
    rcp r0.z, r0.z
    mul r0.z, r0.w, r0.z
    mul r1.y, r0.x, c9.z
    add r1.y, r1.y, c9.w
    mul r1.y, r1.y, r0.z
    rcp r1.z, c1.x
    mul r1.y, r1.y, r1.z
    mul r1.z, r0.y, c9.z
    add r1.z, -r1.z, c6.z
    mul r1.z, r1.z, r0.z
    rcp r1.w, c1.y
    mul r1.z, r1.z, r1.w
    mov r3.x, r1.y
    mov r3.y, r1.z
    mov r3.z, r0.z
    mov r1.yzw, r3.xxyz
    mov r4.x, c0.x
    mov r4.y, c7.x
    mov r4.z, c7.x
    mov r4.w, c0.y
    mov r5.xy, r4.xyxx
    add r5.xy, r0.xyxx, -r5.xyxx
    mov r5.zw, r4.xxxy
    add r5.zw, r0.xxxy, -r5.xxzw
    mov r6.xyzw, c7.xyzw
    mov r6.xy, r5.zwzz
    texldl r6.xyzw, r6.xyzw, s1
    add r3.w, r6.x, -c3.x
    mul r3.w, r3.w, c3.y
    mov_sat r3.w, r3.w
    mul r3.w, r3.w, r1.x
    add r3.w, c0.w, -r3.w
    max r3.w, r3.w, c8.z
    rcp r3.w, r3.w
    mul r3.w, r0.w, r3.w
    mul r5.z, r5.x, c9.z
    add r5.z, r5.z, c9.w
    mul r5.z, r5.z, r3.w
    rcp r5.w, c1.x
    mul r5.z, r5.z, r5.w
    mul r5.x, r5.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r3.w
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r5.y, r5.z
    mov r5.z, r5.x
    mov r5.w, r3.w
    mov r6.xy, r4.xyxx
    add r6.xy, r0.xyxx, r6.xyxx
    add r4.xy, r0.xyxx, r4.xyxx
    mov r7.xyzw, c7.xyzw
    mov r7.xy, r4.xyxx
    texldl r7.xyzw, r7.xyzw, s1
    add r4.x, r7.x, -c3.x
    mul r4.x, r4.x, c3.y
    mov_sat r4.x, r4.x
    mul r4.x, r4.x, r1.x
    add r4.x, c0.w, -r4.x
    max r4.x, r4.x, c8.z
    rcp r4.x, r4.x
    mul r4.x, r0.w, r4.x
    mul r4.y, r6.x, c9.z
    add r4.y, r4.y, c9.w
    mul r4.y, r4.y, r4.x
    rcp r5.x, c1.x
    mul r4.y, r4.y, r5.x
    mul r5.x, r6.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r4.x
    rcp r6.x, c1.y
    mul r5.x, r5.x, r6.x
    mov r6.x, r4.y
    mov r6.y, r5.x
    mov r6.z, r4.x
    mov r7.xy, r4.zwzz
    add r7.xy, r0.xyxx, -r7.xyxx
    mov r7.zw, r4.xxzw
    add r7.zw, r0.xxxy, -r7.xxzw
    mov r8.xyzw, c7.xyzw
    mov r8.xy, r7.zwzz
    texldl r8.xyzw, r8.xyzw, s1
    add r4.y, r8.x, -c3.x
    mul r4.y, r4.y, c3.y
    mov_sat r4.y, r4.y
    mul r4.y, r4.y, r1.x
    add r4.y, c0.w, -r4.y
    max r4.y, r4.y, c8.z
    rcp r4.y, r4.y
    mul r4.y, r0.w, r4.y
    mul r5.x, r7.x, c9.z
    add r5.x, r5.x, c9.w
    mul r5.x, r5.x, r4.y
    rcp r6.w, c1.x
    mul r5.x, r5.x, r6.w
    mul r6.w, r7.y, c9.z
    add r6.w, -r6.w, c6.z
    mul r6.w, r6.w, r4.y
    rcp r7.x, c1.y
    mul r6.w, r6.w, r7.x
    mov r7.x, r5.x
    mov r7.y, r6.w
    mov r7.z, r4.y
    mov r8.xy, r4.zwzz
    add r8.xy, r0.xyxx, r8.xyxx
    add r4.zw, r0.xxxy, r4.xxzw
    mov r9.xyzw, c7.xyzw
    mov r9.xy, r4.zwzz
    texldl r9.xyzw, r9.xyzw, s1
    add r4.z, r9.x, -c3.x
    mul r4.z, r4.z, c3.y
    mov_sat r4.z, r4.z
    mul r4.z, r4.z, r1.x
    add r4.z, c0.w, -r4.z
    max r4.z, r4.z, c8.z
    rcp r4.z, r4.z
    mul r4.z, r0.w, r4.z
    mul r4.w, r8.x, c9.z
    add r4.w, r4.w, c9.w
    mul r4.w, r4.w, r4.z
    rcp r5.x, c1.x
    mul r4.w, r4.w, r5.x
    mul r5.x, r8.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r4.z
    rcp r6.w, c1.y
    mul r5.x, r5.x, r6.w
    mov r8.x, r4.w
    mov r8.y, r5.x
    mov r8.z, r4.z
    add r4.x, r4.x, -r0.z
    abs r4.x, r4.x
    add r3.w, r0.z, -r3.w
    abs r3.w, r3.w
    add r3.w, r4.x, -r3.w
    cmp r3.w, r3.w, c7.x, c6.z
    add r6.xyz, r6.xyzx, -r1.yzwy
    mov r5.xyz, r5.yzwy
    add r5.xyz, r1.yzwy, -r5.xyzx
    cmp r5.xyz, -r3.w, r5.xyzx, r6.xyzx
    add r3.w, r4.z, -r0.z
    abs r3.w, r3.w
    add r4.x, r0.z, -r4.y
    abs r4.x, r4.x
    add r3.w, r3.w, -r4.x
    cmp r3.w, r3.w, c7.x, c6.z
    mov r4.xyz, r8.xyzx
    add r4.xyz, r4.xyzx, -r1.yzwy
    mov r6.xyz, r7.xyzx
    add r1.yzw, r1.xyzw, -r6.xxyz
    cmp r1.yzw, -r3.w, r1.xyzw, r4.xxyz
    dp3 r3.w, r1.yzwy, r1.yzwy
    add r3.w, r3.w, c11.x
    cmp r3.w, r3.w, c7.x, c6.z
    mov r4.xyz, r8.xyzx
    mov r6.xyz, r7.xyzx
    add r4.xyz, r4.xyzx, -r6.xyzx
    cmp r1.yzw, -r3.w, r1.xyzw, r4.xxyz
    mul r4.xyz, r5.zxyz, r1.zwyz
    mul r1.yzw, r5.xyzx, r1.xwyz
    add r1.yzw, r1.xyzw, -r4.xxyz
    dp3 r3.w, r1.yzwy, r1.yzwy
    add r4.x, -r3.w, c11.y
    cmp r4.x, r4.x, c7.x, c6.z
    max r3.w, r3.w, c11.y
    rsq r3.w, r3.w
    mov r4.yzw, r3.w
    mul r1.yzw, r1.xyzw, r4.xyzw
    cmp r1.yzw, -r4.x, c12.xxyz, r1.xyzw
    cmp r3.w, -r1.w, c7.x, c6.z
    cmp r1.yzw, -r3.w, r1.xyzw, -r1.xyzw
    max r3.w, c2.y, c11.z
    mul r4.x, r3.w, c11.w
    mul r4.xy, r4.x, c1.xyxx
    max r0.z, r0.z, c0.z
    rcp r4.z, r0.z
    rcp r4.w, r0.z
    mul r4.xy, r4.xyxx, r4.zwzz
    mul r4.zw, c0.xxxy, c13.xxxy
    max r4.xy, r4.xyxx, r4.zwzz
    min r4.xy, r4.xyxx, c13.zwzz
    rcp r4.z, c0.x
    rcp r4.w, c0.y
    mul r4.zw, r0.xxxy, r4.xxzw
    dp2add r0.z, r4.zwzz, c14.zwzz, c7.x
    frc r0.z, r0.z
    mul r4.zw, r0.z, c14.xxxy
    frc r4.zw, r4.xxzw
    mul r4.zw, r4.xxzw, c13.xxxy
    add r4.zw, r4.xxzw, c15.xxxy
    dp2add r0.z, r4.zwzz, r4.zwzz, c7.x
    max r0.z, r0.z, c12.w
    rsq r0.z, r0.z
    mov r5.xy, r0.z
    mul r4.zw, r4.xxzw, r5.xxxy
    mul r5.xy, r4.zwzz, r4.xyxx
    mov r5.z, -r4.w
    mov r5.w, r4.z
    mov r4.zw, r5.xxzw
    mul r4.xy, r4.zwzz, r4.xyxx
    mov r0.z, c7.x
    mov r6.x, c7.x
    mov r6.y, c7.x
    mov r6.z, c7.x
    mov r4.z, c7.x
    rep i0.xyzw
        mov r11.x, r4.z
        add r11.x, r11.x, c15.z
        cmp r11.x, r11.x, c7.x, c6.z
        add r11.x, -r11.x, c6.z
        if_ne r11.x, -r11.x
            break
        else
        endif
        mov r11.x, r4.z
        abs r11.y, r11.x
        add r11.y, -r11.y, -r11.y
        cmp r11.y, r11.y, c7.x, c6.z
        add r11.y, -r11.y, c6.z
        cmp r11.yz, -r11.y, c7.xxyx, c16.xxyx
        add r11.w, r11.x, c9.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c16.xzwx
        add r11.w, r11.x, c15.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c17.xxyx
        add r11.w, r11.x, c17.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c18.xxyx
        add r11.w, r11.x, c17.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c18.xzwx
        add r11.w, r11.x, c19.x
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c19.xyzx
        add r11.w, r11.x, c19.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c20.xxyx
        add r11.x, r11.x, c20.z
        abs r11.x, r11.x
        add r11.x, -r11.x, -r11.x
        cmp r11.x, r11.x, c7.x, c6.z
        add r11.x, -r11.x, c6.z
        cmp r11.xy, -r11.x, r11.yzyy, c21.xyxx
        mul r11.xy, r11.x, r5.xyxx
        add r11.xy, r0.xyxx, r11.xyxx
        mov r11.z, r4.z
        abs r11.w, r11.z
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, c7.xyxx, c16.xyxx
        add r11.w, r11.z, c9.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c16.zwzz
        add r11.w, r11.z, c15.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c17.xyxx
        add r11.w, r11.z, c17.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c18.xyxx
        add r11.w, r11.z, c17.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c18.zwzz
        add r11.w, r11.z, c19.x
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c19.yzyy
        add r11.w, r11.z, c19.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c20.xyxx
        add r11.z, r11.z, c20.z
        abs r11.z, r11.z
        add r11.z, -r11.z, -r11.z
        cmp r11.z, r11.z, c7.x, c6.z
        add r11.z, -r11.z, c6.z
        cmp r11.zw, -r11.z, r12.xxxy, c21.xxxy
        mul r11.zw, r11.w, r4.xxxy
        add r11.xy, r11.xyxx, r11.zwzz
        mov r7.xyzw, c7.xyzw
        mov r7.xy, r11.xyxx
        mov r12.xyzw, r7.xyzw
        texldl r12.xyzw, r12.xyzw, s1
        add r11.z, r12.x, -c3.x
        mul r11.z, r11.z, c3.y
        mov_sat r11.z, r11.z
        mul r11.w, r11.z, r1.x
        add r11.w, c0.w, -r11.w
        max r11.w, r11.w, c8.z
        rcp r5.w, r11.w
        mul r11.w, r0.w, r5.w
        mul r12.x, r11.x, c9.z
        add r12.x, r12.x, c9.w
        mul r12.x, r12.x, r11.w
        rcp r6.w, c1.x
        mul r12.x, r12.x, r6.w
        mul r12.y, r11.y, c9.z
        add r12.y, -r12.y, c6.z
        mul r12.y, r12.y, r11.w
        rcp r10.w, c1.y
        mul r12.y, r12.y, r10.w
        mov r10.x, r12.x
        mov r10.y, r12.y
        mov r10.z, r11.w
        mov r12.xyz, r10.xyzx
        mov r13.xyz, r3.xyzx
        add r12.xyz, r12.xyzx, -r13.xyzx
        dp3 r11.w, r12.xyzx, r12.xyzx
        max r12.w, r11.w, c20.w
        rsq r4.w, r12.w
        rcp r8.w, r4.w
        mov r13.xyz, r8.w
        rcp r8.x, r13.x
        rcp r8.y, r13.y
        rcp r8.z, r13.z
        mul r12.xyz, r12.xyzx, r8.xyzx
        dp3 r12.x, r1.yzwy, r12.xyzx
        add r12.x, r12.x, c21.z
        mul r12.x, r12.x, c21.w
        mov_sat r12.x, r12.x
        rcp r5.z, r3.w
        mul r12.y, r8.w, r5.z
        add r12.y, -r12.y, c6.z
        mov_sat r12.y, r12.y
        mul r12.y, r12.y, r12.y
        add r11.z, r11.z, c8.x
        cmp r11.z, r11.z, c7.x, c6.z
        add r11.w, -r11.w, c22.x
        cmp r11.w, r11.w, c7.x, c6.z
        min r11.z, r11.z, r11.w
        cmp r11.w, -r11.x, c7.x, c6.z
        min r11.z, r11.z, r11.w
        cmp r11.w, -r11.y, c7.x, c6.z
        min r11.z, r11.z, r11.w
        add r11.w, r11.x, c9.w
        cmp r11.w, r11.w, c7.x, c6.z
        min r11.z, r11.z, r11.w
        add r11.w, r11.y, c9.w
        cmp r11.w, r11.w, c7.x, c6.z
        min r11.z, r11.z, r11.w
        cmp r11.z, -r11.z, c7.x, c6.z
        mul r11.w, r12.x, r12.y
        mul r11.z, r11.w, r11.z
        mov r11.w, r0.z
        add r11.w, r11.w, r11.z
        mov r0.z, r11.w
        mov r9.xyzw, c7.xyzw
        mov r9.xy, r11.xyxx
        mov r12.xyzw, r9.xyzw
        texldl r12.xyzw, r12.xyzw, s0
        mul r11.xyz, r12.xyzx, r11.z
        mov r12.xyz, r6.xyzx
        add r11.xyz, r12.xyzx, r11.xyzx
        mov r6.xyz, r11.xyzx
        mov r11.x, r4.z
        add r11.x, r11.x, c6.z
        mov r4.z, r11.x
    endrep
    mov r0.xyw, r6.xyxz
    mul r0.xyw, r0.xyxw, c22.yzxw
    mov_sat r0.xyw, r0.xyxw
    mul r0.z, c1.z, r0.z
    mul r0.z, r0.z, c5.x
    add r0.z, -r0.z, c6.z
    mov_sat r0.z, r0.z
    mov r1.xyz, r0.xywx
    mov r1.w, r0.z
    mov r0.xyzw, r1.xyzw
    mov r2.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r2.xyzw
