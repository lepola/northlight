ps_3_0
dcl_texcoord0 v0
def c5 = 2.50000000e-01, 2.50000000e-01, 7.50000000e-01, 7.50000000e-01
def c6 = 1.50000000e+00, 1.50000000e+00, 1.00000000e+00, 1.00000000e+00
def c7 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c8 = -9.99989986e-01, -5.00000000e-01, 9.99999975e-06, 1.20000001e-02
def c9 = 6.99999975e-04, 3.00000003e-03, 2.00000000e+00, -1.00000000e+00
def c10 = 9.99999982e-15, 0.00000000e+00, 0.00000000e+00, -1.00000000e+00
def c11 = 2.50000004e-02, 1.00000001e-01, 2.00000009e-03, -2.50000000e+01
def c12 = 5.00000000e-01, 2.00000003e-01, 5.00000000e+00, -2.00000000e+00
def c13 = 2.00000000e+00, 2.00000000e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s2
dcl_2d s1
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
mov r2.xyzw, c7.xyzw
mov r2.xy, r0.zwzz
texldl r2.xyzw, r2.xyzw, s1
add r2.x, r2.x, -c3.x
mul r2.x, r2.x, c3.y
mov_sat r2.x, r2.x
mov r3.xyzw, c7.xyzw
mov r3.xy, v0.xyxx
texldl r3.xyzw, r3.xyzw, s2
add r2.y, r2.x, c8.x
cmp r2.y, r2.y, c7.x, c6.z
add r2.y, -r2.y, c6.z
add r2.z, c4.x, c8.y
cmp r2.z, r2.z, c7.x, c6.z
mov r2.w, r4.x
cmp r2.w, -r2.z, r2.w, c7.x
mov r4.x, r2.w
add r2.w, -r2.z, c6.z
if_ne r2.w, -r2.w
    mov r5.xyzw, c7.xyzw
    mov r5.xy, r0.zwzz
    texldl r5.xyzw, r5.xyzw, s3
    add r4.y, c0.z, -r5.x
    cmp r4.y, r4.y, c7.x, c6.z
    mul r4.z, c0.z, c0.w
    add r4.w, c0.w, -c0.z
    mul r4.w, r2.x, r4.w
    add r4.w, c0.w, -r4.w
    max r4.w, r4.w, c8.z
    rcp r4.w, r4.w
    mul r4.z, r4.z, r4.w
    mul r4.w, r5.x, c9.x
    max r4.w, r4.w, c8.w
    add r4.z, r4.z, r4.w
    add r4.z, r4.z, -r5.x
    cmp r4.z, r4.z, c7.x, c6.z
    add r4.z, -r4.z, c6.z
    min r4.y, r4.y, r4.z
    add r4.z, -r5.y, c9.y
    cmp r4.z, r4.z, c7.x, c6.z
    min r4.y, r4.y, r4.z
    mov r4.x, r4.y
else
endif
mov r4.y, r4.x
max r2.y, r2.y, r4.y
mov r5.xyzw, r6.xyzw
cmp r5.xyzw, -r2.y, r5.xyzw, r3.xyzw
mov r6.xyzw, r5.xyzw
add r2.y, -r2.y, c6.z
if_ne r2.y, -r2.y
    mul r2.y, c0.z, c0.w
    add r4.y, c0.w, -c0.z
    mul r2.x, r2.x, r4.y
    add r2.x, c0.w, -r2.x
    max r2.x, r2.x, c8.z
    rcp r2.x, r2.x
    mul r2.x, r2.y, r2.x
    mul r4.z, r0.z, c9.z
    add r4.z, r4.z, c9.w
    mul r4.z, r4.z, r2.x
    rcp r4.w, c1.x
    mul r4.z, r4.z, r4.w
    mul r4.w, r0.w, c9.z
    add r4.w, -r4.w, c6.z
    mul r4.w, r4.w, r2.x
    rcp r5.x, c1.y
    mul r4.w, r4.w, r5.x
    mov r5.x, r4.z
    mov r5.y, r4.w
    mov r5.z, r2.x
    mov r7.xyz, r5.xyzx
    mov r4.z, c0.x
    mov r4.w, c7.x
    mov r8.x, c7.x
    mov r8.y, c0.y
    mov r8.zw, r4.xxzw
    add r8.zw, r0.xxzw, -r8.xxzw
    mov r9.xy, r4.zwzz
    add r9.xy, r0.zwzz, -r9.xyxx
    mov r10.xyzw, c7.xyzw
    mov r10.xy, r9.xyxx
    mov r9.xyzw, r10.xyzw
    texldl r9.xyzw, r9.xyzw, s1
    add r5.w, r9.x, -c3.x
    mul r5.w, r5.w, c3.y
    mov_sat r5.w, r5.w
    mul r5.w, r5.w, r4.y
    add r5.w, c0.w, -r5.w
    max r5.w, r5.w, c8.z
    rcp r5.w, r5.w
    mul r5.w, r2.y, r5.w
    mul r7.w, r8.z, c9.z
    add r7.w, r7.w, c9.w
    mul r7.w, r7.w, r5.w
    rcp r9.x, c1.x
    mul r7.w, r7.w, r9.x
    mul r8.z, r8.w, c9.z
    add r8.z, -r8.z, c6.z
    mul r8.z, r8.z, r5.w
    rcp r8.w, c1.y
    mul r8.z, r8.z, r8.w
    mov r9.x, r7.w
    mov r9.y, r8.z
    mov r9.z, r5.w
    mov r8.zw, r4.xxzw
    add r8.zw, r0.xxzw, r8.xxzw
    add r4.zw, r0.xxzw, r4.xxzw
    mov r10.xyzw, c7.xyzw
    mov r10.xy, r4.zwzz
    texldl r10.xyzw, r10.xyzw, s1
    add r4.z, r10.x, -c3.x
    mul r4.z, r4.z, c3.y
    mov_sat r4.z, r4.z
    mul r4.z, r4.z, r4.y
    add r4.z, c0.w, -r4.z
    max r4.z, r4.z, c8.z
    rcp r4.z, r4.z
    mul r4.z, r2.y, r4.z
    mul r4.w, r8.z, c9.z
    add r4.w, r4.w, c9.w
    mul r4.w, r4.w, r4.z
    rcp r7.w, c1.x
    mul r4.w, r4.w, r7.w
    mul r7.w, r8.w, c9.z
    add r7.w, -r7.w, c6.z
    mul r7.w, r7.w, r4.z
    rcp r8.z, c1.y
    mul r7.w, r7.w, r8.z
    mov r10.x, r4.w
    mov r10.y, r7.w
    mov r10.z, r4.z
    mov r8.zw, r8.xxxy
    add r8.zw, r0.xxzw, -r8.xxzw
    mov r11.xy, r8.xyxx
    add r11.xy, r0.zwzz, -r11.xyxx
    mov r12.xyzw, c7.xyzw
    mov r12.xy, r11.xyxx
    mov r11.xyzw, r12.xyzw
    texldl r11.xyzw, r11.xyzw, s1
    add r4.w, r11.x, -c3.x
    mul r4.w, r4.w, c3.y
    mov_sat r4.w, r4.w
    mul r4.w, r4.w, r4.y
    add r4.w, c0.w, -r4.w
    max r4.w, r4.w, c8.z
    rcp r4.w, r4.w
    mul r4.w, r2.y, r4.w
    mul r7.w, r8.z, c9.z
    add r7.w, r7.w, c9.w
    mul r7.w, r7.w, r4.w
    rcp r9.w, c1.x
    mul r7.w, r7.w, r9.w
    mul r8.z, r8.w, c9.z
    add r8.z, -r8.z, c6.z
    mul r8.z, r8.z, r4.w
    rcp r8.w, c1.y
    mul r8.z, r8.z, r8.w
    mov r11.x, r7.w
    mov r11.y, r8.z
    mov r11.z, r4.w
    mov r8.zw, r8.xxxy
    add r8.zw, r0.xxzw, r8.xxzw
    add r0.zw, r0.xxzw, r8.xxxy
    mov r12.xyzw, c7.xyzw
    mov r12.xy, r0.zwzz
    texldl r12.xyzw, r12.xyzw, s1
    add r0.z, r12.x, -c3.x
    mul r0.z, r0.z, c3.y
    mov_sat r0.z, r0.z
    mul r0.z, r0.z, r4.y
    add r0.z, c0.w, -r0.z
    max r0.z, r0.z, c8.z
    rcp r0.z, r0.z
    mul r0.z, r2.y, r0.z
    mul r0.w, r8.z, c9.z
    add r0.w, r0.w, c9.w
    mul r0.w, r0.w, r0.z
    rcp r7.w, c1.x
    mul r0.w, r0.w, r7.w
    mul r7.w, r8.w, c9.z
    add r7.w, -r7.w, c6.z
    mul r7.w, r7.w, r0.z
    rcp r8.x, c1.y
    mul r7.w, r7.w, r8.x
    mov r8.x, r0.w
    mov r8.y, r7.w
    mov r8.z, r0.z
    add r0.w, r4.z, -r2.x
    abs r0.w, r0.w
    add r4.z, r2.x, -r5.w
    abs r4.z, r4.z
    add r0.w, r0.w, -r4.z
    cmp r0.w, r0.w, c7.x, c6.z
    add r10.xyz, r10.xyzx, -r7.xyzx
    add r9.xyz, r7.xyzx, -r9.xyzx
    cmp r9.xyz, -r0.w, r9.xyzx, r10.xyzx
    add r0.z, r0.z, -r2.x
    abs r0.z, r0.z
    add r0.w, r2.x, -r4.w
    abs r0.w, r0.w
    add r0.z, r0.z, -r0.w
    cmp r0.z, r0.z, c7.x, c6.z
    add r8.xyz, r8.xyzx, -r7.xyzx
    mov r10.xyz, r11.xyzx
    add r7.xyz, r7.xyzx, -r10.xyzx
    cmp r7.xyz, -r0.z, r7.xyzx, r8.xyzx
    mul r8.xyz, r9.zxyz, r7.yzxy
    mul r7.xyz, r9.yzxy, r7.zxyz
    add r7.xyz, r7.xyzx, -r8.xyzx
    dp3 r0.z, r7.xyzx, r7.xyzx
    add r0.w, -r0.z, c10.x
    cmp r0.w, r0.w, c7.x, c6.z
    max r0.z, r0.z, c10.x
    rsq r0.z, r0.z
    mov r8.xyz, r0.z
    mul r7.xyz, r7.xyzx, r8.xyzx
    cmp r7.xyz, -r0.w, c10.yzwy, r7.xyzx
    cmp r0.z, -r7.z, c7.x, c6.z
    cmp r7.xyz, -r0.z, r7.xyzx, -r7.xyzx
    mul r0.z, c2.y, c11.y
    mul r0.w, r2.x, c11.z
    min r0.z, r0.z, r0.w
    max r0.z, r0.z, c11.x
    mov r0.w, c7.x
    mov r2.x, c7.x
    mov r4.z, c7.x
    rep i0.xyzw
        mov r18.w, r4.z
        add r18.w, r18.w, c11.w
        cmp r18.w, r18.w, c7.x, c6.z
        add r18.w, -r18.w, c6.z
        if_ne r18.w, -r18.w
            break
        else
        endif
        mov r18.w, r4.z
        add r18.w, r18.w, c12.x
        mul r18.w, r18.w, c12.y
        frc r20.xyzw, r18.w
        add r18.w, r18.w, -r20.x
        mov r19.z, r4.z
        mul r19.w, r18.w, c12.z
        add r19.z, r19.z, -r19.w
        add r19.z, r19.z, c12.w
        add r18.w, r18.w, c12.w
        mov r19.x, r19.z
        mov r19.y, r18.w
        mov r19.zw, r19.xxxy
        mul r20.xy, c0.xyxx, c13.xyxx
        mul r19.zw, r19.xxzw, r20.xxxy
        add r19.zw, v0.xxxy, r19.xxzw
        mad r20.xy, r19.zwzz, r0.xyxx, c5.xyxx
        frc r20.xy, r20.xyxx
        add r20.xy, -r20.xyxx, c5.zwzz
        mad r20.xy, r20.xyxx, c0.xyxx, r19.zwzz
        max r20.xy, r20.xyxx, r1.xyxx
        min r20.xy, r20.xyxx, r1.zwzz
        mov r16.xyzw, c7.xyzw
        mov r16.xy, r20.xyxx
        mov r21.xyzw, r16.xyzw
        texldl r21.xyzw, r21.xyzw, s1
        add r18.w, r21.x, -c3.x
        mul r18.w, r18.w, c3.y
        mov_sat r18.w, r18.w
        mul r20.z, r18.w, r4.y
        add r20.z, c0.w, -r20.z
        max r20.z, r20.z, c8.z
        rcp r18.z, r20.z
        mul r20.w, r2.y, r18.z
        mul r21.x, r20.x, c9.z
        add r21.x, r21.x, c9.w
        mul r21.x, r21.x, r20.w
        rcp r18.y, c1.x
        mul r21.x, r21.x, r18.y
        mul r21.y, r20.y, c9.z
        add r21.y, -r21.y, c6.z
        mul r21.y, r21.y, r20.w
        rcp r17.w, c1.y
        mul r21.y, r21.y, r17.w
        mov r17.x, r21.x
        mov r17.y, r21.y
        mov r17.z, r20.w
        mov r21.xy, r19.xyxx
        abs r21.xy, r21.xyxx
        add r20.w, -r21.x, c6.x
        cmp r20.w, r20.w, c7.x, c6.z
        cmp r20.w, -r20.w, c6.z, c12.x
        add r21.x, -r21.y, c6.x
        cmp r21.x, r21.x, c7.x, c6.z
        cmp r21.x, -r21.x, c6.z, c12.x
        mul r20.w, r20.w, r21.x
        mov r21.xyz, r17.xyzx
        mov r22.xyz, r5.xyzx
        add r21.xyz, r21.xyzx, -r22.xyzx
        dp3 r21.x, r21.xyzx, r7.xyzx
        abs r21.x, r21.x
        rcp r14.w, r0.z
        mul r21.x, -r21.x, r14.w
        mul r21.x, r21.x, c9.z
        exp r14.z, r21.x
        mul r20.w, r20.w, r14.z
        mov r21.xyz, r17.xyzx
        mov r14.x, c0.x
        mov r14.y, c7.x
        mov r22.xy, r14.xyxx
        add r22.xy, r20.xyxx, r22.xyxx
        mov r12.z, c7.x
        mov r12.w, c0.y
        mov r22.zw, r12.xxzw
        add r22.zw, r20.xxxy, r22.xxzw
        mov r13.xyzw, c7.xyzw
        mov r13.xy, r22.xyxx
        mov r23.xyzw, r13.xyzw
        texldl r23.xyzw, r23.xyzw, s1
        add r21.w, r23.x, -c3.x
        mul r21.w, r21.w, c3.y
        mov_sat r21.w, r21.w
        mul r21.w, r21.w, r4.y
        add r21.w, c0.w, -r21.w
        max r21.w, r21.w, c8.z
        rcp r12.y, r21.w
        mul r21.w, r2.y, r12.y
        mul r23.x, r22.x, c9.z
        add r23.x, r23.x, c9.w
        mul r23.x, r23.x, r21.w
        rcp r12.x, c1.x
        mul r23.x, r23.x, r12.x
        mul r22.x, r22.y, c9.z
        add r22.x, -r22.x, c6.z
        mul r22.x, r22.x, r21.w
        rcp r18.x, c1.y
        mul r22.x, r22.x, r18.x
        mov r11.x, r23.x
        mov r11.y, r22.x
        mov r11.z, r21.w
        mov r10.xyzw, c7.xyzw
        mov r10.xy, r22.zwzz
        mov r23.xyzw, r10.xyzw
        texldl r23.xyzw, r23.xyzw, s1
        add r21.w, r23.x, -c3.x
        mul r21.w, r21.w, c3.y
        mov_sat r21.w, r21.w
        mul r21.w, r21.w, r4.y
        add r21.w, c0.w, -r21.w
        max r21.w, r21.w, c8.z
        rcp r8.w, r21.w
        mul r21.w, r2.y, r8.w
        mul r22.x, r22.z, c9.z
        add r22.x, r22.x, c9.w
        mul r22.x, r22.x, r21.w
        rcp r7.w, c1.x
        mul r22.x, r22.x, r7.w
        mul r22.y, r22.w, c9.z
        add r22.y, -r22.y, c6.z
        mul r22.y, r22.y, r21.w
        rcp r5.w, c1.y
        mul r22.y, r22.y, r5.w
        mov r8.x, r22.x
        mov r8.y, r22.y
        mov r8.z, r21.w
        mov r22.xyz, r11.xyzx
        add r22.xyz, r22.xyzx, -r21.xyzx
        mov r23.xyz, r8.xyzx
        add r21.xyz, r23.xyzx, -r21.xyzx
        mul r23.xyz, r22.zxyz, r21.yzxy
        mul r21.xyz, r22.yzxy, r21.zxyz
        add r21.xyz, r21.xyzx, -r23.xyzx
        dp3 r21.w, r21.xyzx, r21.xyzx
        add r22.x, -r21.w, c10.x
        cmp r22.x, r22.x, c7.x, c6.z
        max r21.w, r21.w, c10.x
        rsq r4.w, r21.w
        mov r22.yzw, r4.w
        mul r21.xyz, r21.xyzx, r22.yzwy
        cmp r21.xyz, -r22.x, c10.yzwy, r21.xyzx
        cmp r21.w, -r21.z, c7.x, c6.z
        cmp r21.xyz, -r21.w, r21.xyzx, -r21.xyzx
        dp3 r21.x, r7.xyzx, r21.xyzx
        mov_sat r21.x, r21.x
        mul r21.x, r21.x, r21.x
        mul r20.w, r20.w, r21.x
        add r18.w, r18.w, c8.x
        cmp r18.w, r18.w, c7.x, c6.z
        mov r21.x, r4.x
        cmp r21.x, -r2.z, r21.x, c7.x
        mov r4.x, r21.x
        if_ne r2.w, -r2.w
            mov r9.xyzw, c7.xyzw
            mov r9.xy, r20.xyxx
            mov r21.xyzw, r9.xyzw
            texldl r21.xyzw, r21.xyzw, s3
            add r20.x, c0.z, -r21.x
            cmp r20.x, r20.x, c7.x, c6.z
            rcp r11.w, r20.z
            mul r20.y, r2.y, r11.w
            mul r20.z, r21.x, c9.x
            max r20.z, r20.z, c8.w
            add r20.y, r20.y, r20.z
            add r20.y, r20.y, -r21.x
            cmp r20.y, r20.y, c7.x, c6.z
            add r20.y, -r20.y, c6.z
            min r20.x, r20.x, r20.y
            add r20.y, -r21.y, c9.y
            cmp r20.y, r20.y, c7.x, c6.z
            min r20.x, r20.x, r20.y
            mov r4.x, r20.x
        else
        endif
        mov r20.x, r4.x
        add r20.x, -r20.x, c6.z
        min r18.w, r18.w, r20.x
        cmp r18.w, -r18.w, c7.x, c6.z
        mul r18.w, r20.w, r18.w
        mov r15.xyzw, c7.xyzw
        mov r15.xy, r19.zwzz
        mov r20.xyzw, r15.xyzw
        texldl r20.xyzw, r20.xyzw, s2
        mul r19.z, r20.w, r18.w
        mov r19.w, r0.w
        add r19.z, r19.w, r19.z
        mov r0.w, r19.z
        mov r19.z, r2.x
        add r18.w, r19.z, r18.w
        mov r2.x, r18.w
        mov r18.w, r4.z
        add r18.w, r18.w, c6.z
        mov r4.z, r18.w
    endrep
    mov r0.x, r2.x
    add r0.x, -r0.x, c8.z
    cmp r0.x, r0.x, c7.x, c6.z
    mov r0.y, r0.w
    mov r0.z, r2.x
    rcp r0.z, r0.z
    mul r0.y, r0.y, r0.z
    cmp r0.x, -r0.x, r3.w, r0.y
    mov r1.xyz, r3.xyzx
    mov r1.w, r0.x
    mov r0.xyzw, r1.xyzw
    mov r6.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r6.xyzw
