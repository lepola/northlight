ps_3_0
dcl_texcoord0 v0
def c5 = 0.00000000e+00, 9.99999975e-06, 2.00000000e+00, -1.00000000e+00
def c6 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c7 = 1.00000000e+00, -9.99989986e-01, -5.00000000e-01, 1.20000001e-02
def c8 = 6.99999975e-04, 3.00000003e-03, 9.99999982e-15, -5.00000000e+00
def c9 = 0.00000000e+00, 0.00000000e+00, -1.00000000e+00, -2.00000000e+00
def c10 = 5.00000000e-01, 5.00000000e-01, 9.99999978e-03, 9.99999978e-03
def c11 = 1.00000000e+00, 1.00000000e+00, 2.50000000e-01, 2.50000000e-01
def c12 = -1.00000000e+00, 0.00000000e+00, 1.00000000e+00, 0.00000000e+00
def c13 = -3.00000000e+00, -4.00000000e+00, 2.50000004e-02, 1.00000001e-01
def c14 = 2.00000009e-03, 1.50000000e+00, 4.00000000e+00, 4.00000000e+00
def c15 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c16 = 2.12599993e-01, 7.15200007e-01, 7.22000003e-02, -7.20000029e-01
def c17 = 3.57142854e+00, 4.00000000e+00, 4.00000000e+00, 4.00000000e+00
def c18 = 1.25000000e-01, 1.25000000e-01, 1.25000000e-01, 0.00000000e+00
def c19 = 1.00000001e-01, 1.00000001e-01, 1.00000001e-01, 0.00000000e+00
def c20 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s2
dcl_2d s1
dcl_2d s0
dcl_2d s3
mov r0.x, c5.x
mov r1.xyzw, c6.xyzw
mov r1.xy, v0.xyxx
texldl r1.xyzw, r1.xyzw, s0
mov r2.xyzw, c6.xyzw
mov r2.xy, v0.xyxx
texldl r2.xyzw, r2.xyzw, s1
add r0.y, r2.x, -c3.x
mul r0.y, r0.y, c3.y
mov_sat r0.y, r0.y
mul r0.z, c0.z, c0.w
add r0.w, c0.w, -c0.z
mul r2.x, r0.y, r0.w
add r2.x, c0.w, -r2.x
max r2.x, r2.x, c5.y
rcp r2.y, r2.x
mul r2.y, r0.z, r2.y
mul r2.z, v0.x, c5.z
add r2.z, r2.z, c5.w
mul r2.z, r2.z, r2.y
rcp r2.w, c1.x
mul r2.z, r2.z, r2.w
mul r2.w, v0.y, c5.z
add r2.w, -r2.w, c7.x
mul r2.w, r2.w, r2.y
rcp r3.x, c1.y
mul r2.w, r2.w, r3.x
mov r3.x, r2.z
mov r3.y, r2.w
mov r3.z, r2.y
mov r4.x, c5.x
mov r4.y, c5.x
mov r4.z, c5.x
mov r4.w, c7.x
add r0.y, r0.y, c7.y
cmp r0.y, r0.y, c5.x, c7.x
add r2.z, c4.x, c7.z
cmp r2.z, r2.z, c5.x, c7.x
mov r2.w, r3.w
cmp r2.w, -r2.z, r2.w, c5.x
mov r3.w, r2.w
add r2.z, -r2.z, c7.x
if_ne r2.z, -r2.z
    mov r5.xyzw, c6.xyzw
    mov r5.xy, v0.xyxx
    texldl r5.xyzw, r5.xyzw, s3
    add r2.z, c0.z, -r5.x
    cmp r2.z, r2.z, c5.x, c7.x
    rcp r2.x, r2.x
    mul r2.x, r0.z, r2.x
    mul r2.w, r5.x, c8.x
    max r2.w, r2.w, c7.w
    add r2.x, r2.x, r2.w
    add r2.x, r2.x, -r5.x
    cmp r2.x, r2.x, c5.x, c7.x
    add r2.x, -r2.x, c7.x
    min r2.x, r2.z, r2.x
    add r2.z, -r5.y, c8.y
    cmp r2.z, r2.z, c5.x, c7.x
    min r2.x, r2.x, r2.z
    mov r3.w, r2.x
else
endif
mov r2.x, r3.w
add r2.x, -r2.x, c7.x
min r2.x, r0.y, r2.x
if_ne r2.x, -r2.x
    mov r2.xzw, r3.xxyz
    mov r5.x, c0.x
    mov r5.y, c5.x
    mov r5.zw, r5.xxxy
    add r5.zw, v0.xxxy, r5.xxzw
    mov r6.x, c5.x
    mov r6.y, c0.y
    mov r6.zw, r6.xxxy
    add r6.zw, v0.xxxy, r6.xxzw
    mov r7.xyzw, c6.xyzw
    mov r7.xy, r5.zwzz
    texldl r7.xyzw, r7.xyzw, s1
    add r3.w, r7.x, -c3.x
    mul r3.w, r3.w, c3.y
    mov_sat r3.w, r3.w
    mul r3.w, r3.w, r0.w
    add r3.w, c0.w, -r3.w
    max r3.w, r3.w, c5.y
    rcp r3.w, r3.w
    mul r3.w, r0.z, r3.w
    mul r7.x, r5.z, c5.z
    add r7.x, r7.x, c5.w
    mul r7.x, r7.x, r3.w
    rcp r7.y, c1.x
    mul r7.x, r7.x, r7.y
    mul r5.z, r5.w, c5.z
    add r5.z, -r5.z, c7.x
    mul r5.z, r5.z, r3.w
    rcp r5.w, c1.y
    mul r5.z, r5.z, r5.w
    mov r7.y, r5.z
    mov r7.z, r3.w
    mov r8.xyzw, c6.xyzw
    mov r8.xy, r6.zwzz
    texldl r8.xyzw, r8.xyzw, s1
    add r3.w, r8.x, -c3.x
    mul r3.w, r3.w, c3.y
    mov_sat r3.w, r3.w
    mul r3.w, r3.w, r0.w
    add r3.w, c0.w, -r3.w
    max r3.w, r3.w, c5.y
    rcp r3.w, r3.w
    mul r3.w, r0.z, r3.w
    mul r5.z, r6.z, c5.z
    add r5.z, r5.z, c5.w
    mul r5.z, r5.z, r3.w
    rcp r5.w, c1.x
    mul r5.z, r5.z, r5.w
    mul r5.w, r6.w, c5.z
    add r5.w, -r5.w, c7.x
    mul r5.w, r5.w, r3.w
    rcp r6.z, c1.y
    mul r5.w, r5.w, r6.z
    mov r8.x, r5.z
    mov r8.y, r5.w
    mov r8.z, r3.w
    mov r9.xyz, r7.xyzx
    add r9.xyz, r9.xyzx, -r2.xzwx
    mov r10.xyz, r8.xyzx
    add r2.xzw, r10.xxyz, -r2.xxzw
    mul r10.xyz, r9.zxyz, r2.zwxz
    mul r2.xzw, r9.yxzx, r2.wxxz
    add r2.xzw, r2.xxzw, -r10.xxyz
    dp3 r3.w, r2.xzwx, r2.xzwx
    add r5.z, -r3.w, c8.z
    cmp r5.z, r5.z, c5.x, c7.x
    max r3.w, r3.w, c8.z
    rsq r3.w, r3.w
    mov r9.xyz, r3.w
    mul r2.xzw, r2.xxzw, r9.xxyz
    cmp r2.xzw, -r5.z, c9.xxyz, r2.xxzw
    cmp r3.w, -r2.w, c5.x, c7.x
    cmp r2.xzw, -r3.w, r2.xxzw, -r2.xxzw
    mov r4.xyzw, c6.xyzw
    mov r3.w, c5.x
    rcp r5.z, c0.x
    rcp r5.w, c0.y
    mul r5.zw, r5.xxzw, c10.xxxy
    add r5.zw, r5.xxzw, c10.xxzw
    frc r9.xyzw, r5.zwzz
    add r5.zw, r5.xxzw, -r9.xxxy
    max r5.zw, r5.xxzw, c11.xxxy
    rcp r6.z, r5.z
    rcp r6.w, r5.w
    mul r5.zw, v0.xxxy, r5.xxzw
    frc r9.xyzw, r5.zwzz
    add r5.zw, r5.xxzw, -r9.xxxy
    add r5.zw, r5.xxzw, c10.xxxy
    mul r9.xy, c0.xyxx, c11.zwzz
    mad r5.zw, r5.xxzw, r6.xxzw, r9.xxxy
    mov r7.w, c5.x
    rep i0.xyzw
        mov r16.z, r7.w
        add r16.z, r16.z, c8.w
        cmp r16.z, r16.z, c5.x, c7.x
        add r16.z, -r16.z, c7.x
        if_ne r16.z, -r16.z
            break
        else
        endif
        mov r16.z, r7.w
        abs r16.w, r16.z
        add r16.w, -r16.w, -r16.w
        cmp r16.w, r16.w, c5.x, c7.x
        add r16.w, -r16.w, c7.x
        cmp r17.xy, -r16.w, c6.xyxx, c6.xyxx
        add r16.w, r16.z, c5.w
        abs r16.w, r16.w
        add r16.w, -r16.w, -r16.w
        cmp r16.w, r16.w, c5.x, c7.x
        add r16.w, -r16.w, c7.x
        cmp r17.xy, -r16.w, r17.xyxx, c12.xyxx
        add r16.w, r16.z, c9.w
        abs r16.w, r16.w
        add r16.w, -r16.w, -r16.w
        cmp r16.w, r16.w, c5.x, c7.x
        add r16.w, -r16.w, c7.x
        cmp r17.xy, -r16.w, r17.xyxx, c12.zwzz
        add r16.w, r16.z, c13.x
        abs r16.w, r16.w
        add r16.w, -r16.w, -r16.w
        cmp r16.w, r16.w, c5.x, c7.x
        add r16.w, -r16.w, c7.x
        cmp r17.xy, -r16.w, r17.xyxx, c9.yzyy
        add r16.z, r16.z, c13.y
        abs r16.z, r16.z
        add r16.z, -r16.z, -r16.z
        cmp r16.z, r16.z, c5.x, c7.x
        add r16.z, -r16.z, c7.x
        cmp r16.zw, -r16.z, r17.xxxy, c12.xxyz
        mul r16.zw, r16.xxzw, r6.xxzw
        add r16.zw, r5.xxzw, r16.xxzw
        mov r13.xyzw, c6.xyzw
        mov r13.xy, r16.zwzz
        mov r17.xyzw, r13.xyzw
        texldl r17.xyzw, r17.xyzw, s1
        add r17.x, r17.x, -c3.x
        mul r17.x, r17.x, c3.y
        mov_sat r17.x, r17.x
        mul r17.y, r17.x, r0.w
        add r17.y, c0.w, -r17.y
        max r17.y, r17.y, c5.y
        rcp r12.z, r17.y
        mul r17.y, r0.z, r12.z
        mul r17.z, r16.z, c5.z
        add r17.z, r17.z, c5.w
        mul r17.z, r17.z, r17.y
        rcp r8.w, c1.x
        mul r17.z, r17.z, r8.w
        mul r17.w, r16.w, c5.z
        add r17.w, -r17.w, c7.x
        mul r17.w, r17.w, r17.y
        rcp r12.x, c1.y
        mul r17.w, r17.w, r12.x
        mov r14.x, r17.z
        mov r14.y, r17.w
        mov r14.z, r17.y
        mov r17.yzw, r14.xxyz
        mov r5.x, c0.x
        mov r5.y, c5.x
        mov r18.xy, r5.xyxx
        add r18.xy, r16.zwzz, r18.xyxx
        mov r6.x, c5.x
        mov r6.y, c0.y
        mov r18.zw, r6.xxxy
        add r18.zw, r16.xxzw, r18.xxzw
        mov r15.xyzw, c6.xyzw
        mov r15.xy, r18.xyxx
        mov r19.xyzw, r15.xyzw
        texldl r19.xyzw, r19.xyzw, s1
        add r19.x, r19.x, -c3.x
        mul r19.x, r19.x, c3.y
        mov_sat r19.x, r19.x
        mul r19.x, r19.x, r0.w
        add r19.x, c0.w, -r19.x
        max r19.x, r19.x, c5.y
        rcp r12.w, r19.x
        mul r19.x, r0.z, r12.w
        mul r19.y, r18.x, c5.z
        add r19.y, r19.y, c5.w
        mul r19.y, r19.y, r19.x
        rcp r16.y, c1.x
        mul r19.y, r19.y, r16.y
        mul r18.x, r18.y, c5.z
        add r18.x, -r18.x, c7.x
        mul r18.x, r18.x, r19.x
        rcp r9.x, c1.y
        mul r18.x, r18.x, r9.x
        mov r7.x, r19.y
        mov r7.y, r18.x
        mov r7.z, r19.x
        mov r10.xyzw, c6.xyzw
        mov r10.xy, r18.zwzz
        mov r19.xyzw, r10.xyzw
        texldl r19.xyzw, r19.xyzw, s1
        add r18.x, r19.x, -c3.x
        mul r18.x, r18.x, c3.y
        mov_sat r18.x, r18.x
        mul r18.x, r18.x, r0.w
        add r18.x, c0.w, -r18.x
        max r18.x, r18.x, c5.y
        rcp r14.w, r18.x
        mul r18.x, r0.z, r14.w
        mul r18.y, r18.z, c5.z
        add r18.y, r18.y, c5.w
        mul r18.y, r18.y, r18.x
        rcp r9.y, c1.x
        mul r18.y, r18.y, r9.y
        mul r18.z, r18.w, c5.z
        add r18.z, -r18.z, c7.x
        mul r18.z, r18.z, r18.x
        rcp r9.z, c1.y
        mul r18.z, r18.z, r9.z
        mov r8.x, r18.y
        mov r8.y, r18.z
        mov r8.z, r18.x
        mov r18.xyz, r7.xyzx
        add r18.xyz, r18.xyzx, -r17.yzwy
        mov r19.xyz, r8.xyzx
        add r17.yzw, r19.xxyz, -r17.xyzw
        mul r19.xyz, r18.zxyz, r17.zwyz
        mul r17.yzw, r18.xyzx, r17.xwyz
        add r17.yzw, r17.xyzw, -r19.xxyz
        dp3 r18.x, r17.yzwy, r17.yzwy
        add r18.y, -r18.x, c8.z
        cmp r18.y, r18.y, c5.x, c7.x
        max r18.x, r18.x, c8.z
        rsq r9.w, r18.x
        mov r18.xzw, r9.w
        mul r17.yzw, r17.xyzw, r18.xxzw
        cmp r17.yzw, -r18.y, c9.xxyz, r17.xyzw
        cmp r18.x, -r17.w, c5.x, c7.x
        cmp r17.yzw, -r18.x, r17.xyzw, -r17.xyzw
        mov r18.xyz, r14.xyzx
        mov r19.xyz, r3.xyzx
        add r18.xyz, r18.xyzx, -r19.xyzx
        dp3 r18.x, r18.xyzx, r2.xzwx
        abs r18.x, r18.x
        mul r18.y, c2.y, c13.w
        mul r18.z, r2.y, c14.x
        min r18.y, r18.y, r18.z
        max r18.y, r18.y, c13.z
        dp3 r17.y, r2.xzwx, r17.yzwy
        mov_sat r17.y, r17.y
        rcp r12.y, r18.y
        mul r17.z, -r18.x, r12.y
        mul r17.z, r17.z, c5.z
        exp r16.x, r17.z
        mul r17.y, r17.y, r17.y
        mul r17.y, r16.x, r17.y
        mov r17.z, r7.w
        abs r17.z, r17.z
        add r17.z, -r17.z, -r17.z
        cmp r17.z, r17.z, c5.x, c7.x
        add r17.z, -r17.z, c7.x
        cmp r17.z, -r17.z, c7.x, c5.z
        mul r17.y, r17.y, r17.z
        add r17.x, r17.x, c7.y
        cmp r17.x, r17.x, c5.x, c7.x
        add r17.x, r17.x, -r0.y
        abs r17.x, r17.x
        add r17.x, -r17.x, -r17.x
        cmp r17.x, r17.x, c5.x, c7.x
        add r17.x, -r17.x, c7.x
        cmp r17.x, -r17.x, c5.x, c7.x
        mul r17.x, r17.y, r17.x
        mov r11.xyzw, c6.xyzw
        mov r11.xy, r16.zwzz
        mov r18.xyzw, r11.xyzw
        texldl r18.xyzw, r18.xyzw, s2
        mul r18.xyzw, r18.xyzw, r17.x
        mov r19.xyzw, r4.xyzw
        add r18.xyzw, r19.xyzw, r18.xyzw
        mov r4.xyzw, r18.xyzw
        mov r16.z, r3.w
        add r16.z, r16.z, r17.x
        mov r3.w, r16.z
        mov r16.z, r7.w
        add r16.z, r16.z, c7.x
        mov r7.w, r16.z
    endrep
    mov r0.y, r3.w
    add r0.y, -r0.y, c5.y
    cmp r0.y, r0.y, c5.x, c7.x
    mov r5.xyzw, r4.xyzw
    mov r0.z, r3.w
    max r0.z, r0.z, c5.y
    rcp r3.x, r0.z
    rcp r3.y, r0.z
    rcp r3.z, r0.z
    rcp r3.w, r0.z
    mul r3.xyzw, r5.xyzw, r3.xyzw
    cmp r3.xyzw, -r0.y, c15.xyzw, r3.xyzw
    mov r4.xyzw, r3.xyzw
else
endif
add r0.y, -c2.z, c14.y
cmp r0.y, r0.y, c5.x, c7.x
if_ne r0.y, -r0.y
    add r0.y, r2.y, c7.x
    log r0.y, r0.y
    add r0.z, c0.w, c7.x
    log r0.z, r0.z
    rcp r0.z, r0.z
    mul r0.y, r0.y, r0.z
    mov_sat r0.y, r0.y
    mov r2.xyz, r0.y
    mov r2.w, r1.w
    mov r0.x, c7.x
else
endif
mov r0.y, r0.x
add r0.y, -r0.y, c7.x
if_ne r0.y, -r0.y
    add r0.y, -c2.z, c10.x
    cmp r0.y, r0.y, c5.x, c7.x
    mov r3.xyzw, r4.xyzw
    mov r5.xyzw, r6.xyzw
    cmp r0.z, -r0.y, r5.x, r3.w
    mov r6.x, r0.z
    mov r5.xyzw, r6.xyzw
    cmp r0.z, -r0.y, r5.y, r3.w
    mov r6.y, r0.z
    mov r5.xyzw, r6.xyzw
    cmp r0.z, -r0.y, r5.z, r3.w
    mov r6.z, r0.z
    mov r3.xyzw, r6.xyzw
    cmp r0.z, -r0.y, r3.w, r1.w
    mov r6.w, r0.z
    mov r3.xyzw, r6.xyzw
    mov r5.xyzw, r2.xyzw
    cmp r3.xyzw, -r0.y, r5.xyzw, r3.xyzw
    mov r2.xyzw, r3.xyzw
    cmp r0.x, -r0.y, r0.x, c7.x
    add r0.x, -r0.x, c7.x
    if_ne r0.x, -r0.x
        mul r0.xy, c0.xyxx, c14.zwzz
        dp3 r0.z, r1.xyzx, c16.xyzx
        add r0.z, r0.z, c16.w
        mul r0.z, r0.z, c17.x
        mov_sat r0.z, r0.z
        mul r3.xyz, r1.xyzx, r0.z
        mul r3.xyz, r3.xyzx, c17.yzwy
        mov r0.z, r0.x
        mov r0.w, c5.x
        add r0.zw, v0.xxxy, r0.xxzw
        mov r5.xyzw, c6.xyzw
        mov r5.xy, r0.zwzz
        texldl r5.xyzw, r5.xyzw, s0
        dp3 r0.z, r5.xyzx, c16.xyzx
        add r0.z, r0.z, c16.w
        mul r0.z, r0.z, c17.x
        mov_sat r0.z, r0.z
        mul r5.xyz, r5.xyzx, r0.z
        add r3.xyz, r3.xyzx, r5.xyzx
        mov r0.z, -r0.x
        mov r0.w, c5.x
        add r0.zw, v0.xxxy, r0.xxzw
        mov r5.xyzw, c6.xyzw
        mov r5.xy, r0.zwzz
        texldl r5.xyzw, r5.xyzw, s0
        dp3 r0.z, r5.xyzx, c16.xyzx
        add r0.z, r0.z, c16.w
        mul r0.z, r0.z, c17.x
        mov_sat r0.z, r0.z
        mul r5.xyz, r5.xyzx, r0.z
        add r3.xyz, r3.xyzx, r5.xyzx
        mov r0.z, c5.x
        mov r0.w, r0.y
        add r0.zw, v0.xxxy, r0.xxzw
        mov r5.xyzw, c6.xyzw
        mov r5.xy, r0.zwzz
        texldl r5.xyzw, r5.xyzw, s0
        dp3 r0.z, r5.xyzx, c16.xyzx
        add r0.z, r0.z, c16.w
        mul r0.z, r0.z, c17.x
        mov_sat r0.z, r0.z
        mul r5.xyz, r5.xyzx, r0.z
        add r3.xyz, r3.xyzx, r5.xyzx
        mov r0.z, c5.x
        mov r0.w, -r0.y
        mov r0.xy, r0.zwzz
        add r0.xy, v0.xyxx, r0.xyxx
        mov r5.xyzw, c6.xyzw
        mov r5.xy, r0.xyxx
        mov r0.xyzw, r5.xyzw
        texldl r0.xyzw, r0.xyzw, s0
        dp3 r3.w, r0.xyzx, c16.xyzx
        add r3.w, r3.w, c16.w
        mul r3.w, r3.w, c17.x
        mov_sat r3.w, r3.w
        mul r0.xyz, r0.xyzx, r3.w
        add r0.xyz, r3.xyzx, r0.xyzx
        mul r0.xyz, r0.xyzx, c18.xyzx
        mov r3.xyzw, r4.xyzw
        mul r3.xyz, r1.xyzx, r3.w
        mul r4.xyz, r4.xyzx, c1.w
        add r5.xyz, r1.xyzx, c19.xyzx
        mov_sat r5.xyz, r5.xyzx
        mul r4.xyz, r4.xyzx, r5.xyzx
        add r3.xyz, r3.xyzx, r4.xyzx
        mul r0.xyz, r0.xyzx, c2.x
        mov_sat r4.xyz, r3.xyzx
        add r4.xyz, -r4.xyzx, c20.xyzx
        mul r0.xyz, r0.xyzx, r4.xyzx
        add r0.xyz, r3.xyzx, r0.xyzx
        mov_sat r0.xyz, r0.xyzx
        mov r0.w, r1.w
        mov r2.xyzw, r0.xyzw
    else
    endif
else
endif
mov oC0.xyzw, r2.xyzw
