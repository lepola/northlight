ps_3_0
dcl_texcoord0 v0
def c5 = 2.50000000e-01, 2.50000000e-01, 7.50000000e-01, 7.50000000e-01
def c6 = 1.50000000e+00, 1.50000000e+00, 1.00000000e+00, 1.00000000e+00
def c7 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c8 = -9.99989986e-01, -5.00000000e-01, 9.99999975e-06, 1.20000001e-02
def c9 = 6.99999975e-04, 3.00000003e-03, 2.00000000e+00, -1.00000000e+00
def c10 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c11 = 9.99999982e-15, 0.00000000e+00, 0.00000000e+00, -1.00000000e+00
def c12 = 5.00000007e-02, 5.00000000e-01, 2.00000000e+00, 2.00000000e+00
def c13 = 5.00000007e-02, 5.00000007e-02, 5.29829178e+01, 3.74136009e+01
def c14 = 6.71105608e-02, 5.83714992e-03, -1.00000000e+00, -1.00000000e+00
def c15 = 9.99999975e-05, -8.00000000e+00, 3.23399991e-01, 1.33900002e-01
def c16 = -2.48799995e-01, 6.00499988e-01, -2.00000000e+00, -3.00000000e+00
def c17 = -8.31499994e-01, -3.44399989e-01, 3.82699996e-01, -9.23900008e-01
def c18 = -4.00000000e+00, 8.31499994e-01, -3.44399989e-01, -5.00000000e+00
def c19 = 2.48799995e-01, 6.00499988e-01, -6.00000000e+00, -7.00000000e+00
def c20 = -3.23399991e-01, 1.33900002e-01, -3.82699996e-01, -9.23900008e-01
def c21 = 9.99999996e-13, -7.99999982e-02, 1.08695650e+00, 1.00000001e-10
def c22 = 1.25000000e-01, 1.25000000e-01, 1.25000000e-01, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s0
dcl_2d s3
rcp r0.x, c0.x
rcp r0.y, c0.y
mad r0.zw, v0.xxxy, r0.xxxy, c5.xxxy
frc r0.zw, r0.xxzw
add r0.zw, -r0.xxzw, c5.xxzw
mad r0.zw, r0.xxzw, c0.xxxy, v0.xxxy
mul r1.xy, c0.xyxx, c6.xyxx
add r1.zw, -r1.xxxy, c6.xxzw
max r0.zw, r0.xxzw, r1.xxxy
min r0.zw, r0.xxzw, r1.xxzw
mov r1.xyzw, c7.xyzw
mov r1.xy, r0.zwzz
texldl r1.xyzw, r1.xyzw, s1
add r1.x, r1.x, -c3.x
mul r1.x, r1.x, c3.y
mov_sat r1.x, r1.x
add r1.y, r1.x, c8.x
cmp r1.y, r1.y, c7.x, c6.z
add r1.y, -r1.y, c6.z
add r1.z, c4.x, c8.y
cmp r1.z, r1.z, c7.x, c6.z
mov r1.w, r2.x
cmp r1.w, -r1.z, r1.w, c7.x
mov r2.x, r1.w
add r1.z, -r1.z, c6.z
if_ne r1.z, -r1.z
    mov r3.xyzw, c7.xyzw
    mov r3.xy, r0.zwzz
    texldl r3.xyzw, r3.xyzw, s3
    add r1.z, c0.z, -r3.x
    cmp r1.z, r1.z, c7.x, c6.z
    mul r1.w, c0.z, c0.w
    add r2.y, c0.w, -c0.z
    mul r2.y, r1.x, r2.y
    add r2.y, c0.w, -r2.y
    max r2.y, r2.y, c8.z
    rcp r2.y, r2.y
    mul r1.w, r1.w, r2.y
    mul r2.y, r3.x, c9.x
    max r2.y, r2.y, c8.w
    add r1.w, r1.w, r2.y
    add r1.w, r1.w, -r3.x
    cmp r1.w, r1.w, c7.x, c6.z
    add r1.w, -r1.w, c6.z
    min r1.z, r1.z, r1.w
    add r1.w, -r3.y, c9.y
    cmp r1.w, r1.w, c7.x, c6.z
    min r1.z, r1.z, r1.w
    mov r2.x, r1.z
else
endif
mov r1.z, r2.x
max r1.y, r1.y, r1.z
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.y, r2.xyzw, c10.xyzw
mov r3.xyzw, r2.xyzw
add r1.y, -r1.y, c6.z
if_ne r1.y, -r1.y
    mul r1.y, c0.z, c0.w
    add r1.z, c0.w, -c0.z
    mul r1.x, r1.x, r1.z
    add r1.x, c0.w, -r1.x
    max r1.x, r1.x, c8.z
    rcp r1.x, r1.x
    mul r1.x, r1.y, r1.x
    mul r1.w, r0.z, c9.z
    add r1.w, r1.w, c9.w
    mul r1.w, r1.w, r1.x
    rcp r2.x, c1.x
    mul r1.w, r1.w, r2.x
    mul r2.x, r0.w, c9.z
    add r2.x, -r2.x, c6.z
    mul r2.x, r2.x, r1.x
    rcp r2.y, c1.y
    mul r2.x, r2.x, r2.y
    mov r2.y, r1.w
    mov r2.z, r2.x
    mov r2.w, r1.x
    mov r4.xyz, r2.yzwy
    mov r5.x, c0.x
    mov r5.y, c7.x
    mov r5.z, c7.x
    mov r5.w, c0.y
    mov r6.xy, r5.xyxx
    add r6.xy, r0.zwzz, -r6.xyxx
    mov r6.zw, r5.xxxy
    add r6.zw, r0.xxzw, -r6.xxzw
    mov r7.xyzw, c7.xyzw
    mov r7.xy, r6.zwzz
    texldl r7.xyzw, r7.xyzw, s1
    add r1.w, r7.x, -c3.x
    mul r1.w, r1.w, c3.y
    mov_sat r1.w, r1.w
    mul r1.w, r1.w, r1.z
    add r1.w, c0.w, -r1.w
    max r1.w, r1.w, c8.z
    rcp r1.w, r1.w
    mul r1.w, r1.y, r1.w
    mul r2.x, r6.x, c9.z
    add r2.x, r2.x, c9.w
    mul r2.x, r2.x, r1.w
    rcp r4.w, c1.x
    mul r2.x, r2.x, r4.w
    mul r4.w, r6.y, c9.z
    add r4.w, -r4.w, c6.z
    mul r4.w, r4.w, r1.w
    rcp r6.x, c1.y
    mul r4.w, r4.w, r6.x
    mov r6.x, r2.x
    mov r6.y, r4.w
    mov r6.z, r1.w
    mov r7.xy, r5.xyxx
    add r7.xy, r0.zwzz, r7.xyxx
    add r5.xy, r0.zwzz, r5.xyxx
    mov r8.xyzw, c7.xyzw
    mov r8.xy, r5.xyxx
    texldl r8.xyzw, r8.xyzw, s1
    add r2.x, r8.x, -c3.x
    mul r2.x, r2.x, c3.y
    mov_sat r2.x, r2.x
    mul r2.x, r2.x, r1.z
    add r2.x, c0.w, -r2.x
    max r2.x, r2.x, c8.z
    rcp r2.x, r2.x
    mul r2.x, r1.y, r2.x
    mul r4.w, r7.x, c9.z
    add r4.w, r4.w, c9.w
    mul r4.w, r4.w, r2.x
    rcp r5.x, c1.x
    mul r4.w, r4.w, r5.x
    mul r5.x, r7.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r2.x
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r7.x, r4.w
    mov r7.y, r5.x
    mov r7.z, r2.x
    mov r5.xy, r5.zwzz
    add r5.xy, r0.zwzz, -r5.xyxx
    mov r8.xy, r5.zwzz
    add r8.xy, r0.zwzz, -r8.xyxx
    mov r9.xyzw, c7.xyzw
    mov r9.xy, r8.xyxx
    mov r8.xyzw, r9.xyzw
    texldl r8.xyzw, r8.xyzw, s1
    add r4.w, r8.x, -c3.x
    mul r4.w, r4.w, c3.y
    mov_sat r4.w, r4.w
    mul r4.w, r4.w, r1.z
    add r4.w, c0.w, -r4.w
    max r4.w, r4.w, c8.z
    rcp r4.w, r4.w
    mul r4.w, r1.y, r4.w
    mul r6.w, r5.x, c9.z
    add r6.w, r6.w, c9.w
    mul r6.w, r6.w, r4.w
    rcp r7.w, c1.x
    mul r6.w, r6.w, r7.w
    mul r5.x, r5.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r4.w
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r8.x, r6.w
    mov r8.y, r5.x
    mov r8.z, r4.w
    mov r5.xy, r5.zwzz
    add r5.xy, r0.zwzz, r5.xyxx
    add r5.zw, r0.xxzw, r5.xxzw
    mov r9.xyzw, c7.xyzw
    mov r9.xy, r5.zwzz
    texldl r9.xyzw, r9.xyzw, s1
    add r5.z, r9.x, -c3.x
    mul r5.z, r5.z, c3.y
    mov_sat r5.z, r5.z
    mul r5.z, r5.z, r1.z
    add r5.z, c0.w, -r5.z
    max r5.z, r5.z, c8.z
    rcp r5.z, r5.z
    mul r5.z, r1.y, r5.z
    mul r5.w, r5.x, c9.z
    add r5.w, r5.w, c9.w
    mul r5.w, r5.w, r5.z
    rcp r6.w, c1.x
    mul r5.w, r5.w, r6.w
    mul r5.x, r5.y, c9.z
    add r5.x, -r5.x, c6.z
    mul r5.x, r5.x, r5.z
    rcp r5.y, c1.y
    mul r5.x, r5.x, r5.y
    mov r9.x, r5.w
    mov r9.y, r5.x
    mov r9.z, r5.z
    add r2.x, r2.x, -r1.x
    abs r2.x, r2.x
    add r1.w, r1.x, -r1.w
    abs r1.w, r1.w
    add r1.w, r2.x, -r1.w
    cmp r1.w, r1.w, c7.x, c6.z
    mov r5.xyw, r7.xyxz
    add r5.xyw, r5.xyxw, -r4.xyxz
    add r6.xyz, r4.xyzx, -r6.xyzx
    cmp r5.xyw, -r1.w, r6.xyxz, r5.xyxw
    add r1.w, r5.z, -r1.x
    abs r1.w, r1.w
    add r2.x, r1.x, -r4.w
    abs r2.x, r2.x
    add r1.w, r1.w, -r2.x
    cmp r1.w, r1.w, c7.x, c6.z
    mov r6.xyz, r9.xyzx
    add r6.xyz, r6.xyzx, -r4.xyzx
    mov r7.xyz, r8.xyzx
    add r4.xyz, r4.xyzx, -r7.xyzx
    cmp r4.xyz, -r1.w, r4.xyzx, r6.xyzx
    mul r6.xyz, r5.wxyw, r4.yzxy
    mul r4.xyz, r5.ywxy, r4.zxyz
    add r4.xyz, r4.xyzx, -r6.xyzx
    dp3 r1.w, r4.xyzx, r4.xyzx
    add r2.x, -r1.w, c11.x
    cmp r2.x, r2.x, c7.x, c6.z
    max r1.w, r1.w, c11.x
    rsq r1.w, r1.w
    mov r5.xyz, r1.w
    mul r4.xyz, r4.xyzx, r5.xyzx
    cmp r4.xyz, -r2.x, c11.yzwy, r4.xyzx
    cmp r1.w, -r4.z, c7.x, c6.z
    cmp r4.xyz, -r1.w, r4.xyzx, -r4.xyzx
    max r1.w, c2.y, c12.x
    mul r2.x, r1.w, c12.y
    mul r5.xy, r2.x, c1.xyxx
    max r1.x, r1.x, c0.z
    rcp r5.z, r1.x
    rcp r5.w, r1.x
    mul r5.xy, r5.xyxx, r5.zwzz
    mul r5.zw, c0.xxxy, c12.xxzw
    max r5.xy, r5.xyxx, r5.zwzz
    min r5.xy, r5.xyxx, c13.xyxx
    mul r0.xy, r0.zwzz, r0.xyxx
    dp2add r0.x, r0.xyxx, c14.xyxx, c7.x
    frc r0.x, r0.x
    mul r0.xy, r0.x, c13.zwzz
    frc r0.xy, r0.xyxx
    mul r0.xy, r0.xyxx, c12.zwzz
    add r0.xy, r0.xyxx, c14.zwzz
    dp2add r1.x, r0.xyxx, r0.xyxx, c7.x
    max r1.x, r1.x, c15.x
    rsq r1.x, r1.x
    mov r5.zw, r1.x
    mul r0.xy, r0.xyxx, r5.zwzz
    mul r5.zw, r0.xxxy, r5.xxxy
    mov r6.x, -r0.y
    mov r6.y, r0.x
    mov r0.xy, r6.xyxx
    mul r0.xy, r0.xyxx, r5.xyxx
    mov r1.x, c7.x
    mov r6.x, c7.x
    mov r6.y, c7.x
    mov r6.z, c7.x
    mov r2.x, c7.x
    rep i0.xyzw
        mov r11.x, r2.x
        add r11.x, r11.x, c15.y
        cmp r11.x, r11.x, c7.x, c6.z
        add r11.x, -r11.x, c6.z
        if_ne r11.x, -r11.x
            break
        else
        endif
        mov r11.x, r2.x
        abs r11.y, r11.x
        add r11.y, -r11.y, -r11.y
        cmp r11.y, r11.y, c7.x, c6.z
        add r11.y, -r11.y, c6.z
        cmp r11.yz, -r11.y, c7.xxyx, c15.xzwx
        add r11.w, r11.x, c9.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c16.xxyx
        add r11.w, r11.x, c16.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c17.xxyx
        add r11.w, r11.x, c16.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c17.xzwx
        add r11.w, r11.x, c18.x
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c18.xyzx
        add r11.w, r11.x, c18.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c19.xxyx
        add r11.w, r11.x, c19.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r11.yz, -r11.w, r11.xyzx, c20.xxyx
        add r11.x, r11.x, c19.w
        abs r11.x, r11.x
        add r11.x, -r11.x, -r11.x
        cmp r11.x, r11.x, c7.x, c6.z
        add r11.x, -r11.x, c6.z
        cmp r11.xy, -r11.x, r11.yzyy, c20.zwzz
        mul r11.xy, r11.x, r5.zwzz
        add r11.xy, r0.zwzz, r11.xyxx
        mov r11.z, r2.x
        abs r11.w, r11.z
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, c7.xyxx, c15.zwzz
        add r11.w, r11.z, c9.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c16.xyxx
        add r11.w, r11.z, c16.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c17.xyxx
        add r11.w, r11.z, c16.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c17.zwzz
        add r11.w, r11.z, c18.x
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c18.yzyy
        add r11.w, r11.z, c18.w
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c19.xyxx
        add r11.w, r11.z, c19.z
        abs r11.w, r11.w
        add r11.w, -r11.w, -r11.w
        cmp r11.w, r11.w, c7.x, c6.z
        add r11.w, -r11.w, c6.z
        cmp r12.xy, -r11.w, r12.xyxx, c20.xyxx
        add r11.z, r11.z, c19.w
        abs r11.z, r11.z
        add r11.z, -r11.z, -r11.z
        cmp r11.z, r11.z, c7.x, c6.z
        add r11.z, -r11.z, c6.z
        cmp r11.zw, -r11.z, r12.xxxy, c20.xxzw
        mul r11.zw, r11.w, r0.xxxy
        add r11.xy, r11.xyxx, r11.zwzz
        mov r9.xyzw, c7.xyzw
        mov r9.xy, r11.xyxx
        mov r12.xyzw, r9.xyzw
        texldl r12.xyzw, r12.xyzw, s1
        add r11.z, r12.x, -c3.x
        mul r11.z, r11.z, c3.y
        mov_sat r11.z, r11.z
        mul r11.w, r11.z, r1.z
        add r11.w, c0.w, -r11.w
        max r11.w, r11.w, c8.z
        rcp r5.x, r11.w
        mul r11.w, r1.y, r5.x
        mul r12.x, r11.x, c9.z
        add r12.x, r12.x, c9.w
        mul r12.x, r12.x, r11.w
        rcp r5.y, c1.x
        mul r12.x, r12.x, r5.y
        mul r12.y, r11.y, c9.z
        add r12.y, -r12.y, c6.z
        mul r12.y, r12.y, r11.w
        rcp r10.w, c1.y
        mul r12.y, r12.y, r10.w
        mov r7.x, r12.x
        mov r7.y, r12.y
        mov r7.z, r11.w
        mov r12.xyz, r7.xyzx
        mov r13.xyz, r2.yzwy
        add r12.xyz, r12.xyzx, -r13.xyzx
        dp3 r11.w, r12.xyzx, r12.xyzx
        max r12.w, r11.w, c21.x
        rsq r4.w, r12.w
        rcp r7.w, r4.w
        mov r13.xyz, r7.w
        rcp r10.x, r13.x
        rcp r10.y, r13.y
        rcp r10.z, r13.z
        mul r12.xyz, r12.xyzx, r10.xyzx
        dp3 r12.x, r4.xyzx, r12.xyzx
        add r12.x, r12.x, c21.y
        mul r12.x, r12.x, c21.z
        mov_sat r12.x, r12.x
        rcp r6.w, r1.w
        mul r12.y, r7.w, r6.w
        add r12.y, -r12.y, c6.z
        mov_sat r12.y, r12.y
        mul r12.y, r12.y, r12.y
        add r11.z, r11.z, c8.x
        cmp r11.z, r11.z, c7.x, c6.z
        add r11.w, -r11.w, c21.w
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
        mov r11.w, r1.x
        add r11.w, r11.w, r11.z
        mov r1.x, r11.w
        mov r8.xyzw, c7.xyzw
        mov r8.xy, r11.xyxx
        mov r12.xyzw, r8.xyzw
        texldl r12.xyzw, r12.xyzw, s0
        mul r11.xyz, r12.xyzx, r11.z
        mov r12.xyz, r6.xyzx
        add r11.xyz, r12.xyzx, r11.xyzx
        mov r6.xyz, r11.xyzx
        mov r11.x, r2.x
        add r11.x, r11.x, c6.z
        mov r2.x, r11.x
    endrep
    mov r0.xyz, r6.xyzx
    mul r0.xyz, r0.xyzx, c22.xyzx
    mov_sat r0.xyz, r0.xyzx
    mov r0.w, r1.x
    mul r0.w, c1.z, r0.w
    mul r0.w, r0.w, c5.x
    add r0.w, -r0.w, c6.z
    mov_sat r0.w, r0.w
    mov r1.xyz, r0.xyzx
    mov r1.w, r0.w
    mov r0.xyzw, r1.xyzw
    mov r3.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r3.xyzw
