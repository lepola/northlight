ps_3_0
dcl_texcoord0 v0
def c68 = -1.00000000e+00, -1.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = 5.00000000e-01, 5.00000000e-01, 9.99989986e-01, -5.00000000e-01
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 1.00000000e+00, 9.99999975e-06, 1.20000001e-02, 6.99999975e-04
def c72 = 3.00000003e-03, 2.00000000e+00, -2.00000000e+00, 9.99999996e-13
def c73 = -1.00000000e+00, 1.00000000e+00, 2.50000004e-02, -4.10000000e+01
def c74 = 7.81250000e-03, 7.81250000e-03, 6.40000000e+01, 6.40000000e+01
def c75 = -5.00000000e-01, -5.00000000e-01, 1.56250000e-02, 1.56250000e-02
def c76 = 1.50000000e+00, 5.00000000e-01, 5.00000000e-01, 1.50000000e+00
def c77 = 1.50000000e+00, 1.50000000e+00, 9.99999997e-07, 6.30000000e+01
def c78 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c79 = 3.00000000e+00, -2.50000000e+00, 4.00000006e-01, 6.00000000e+00
def c80 = 1.00000005e-03, 2.66666681e-01, -3.33333343e-01, -3.00000012e-01
def c81 = 1.44269502e+00, 8.33333358e-02, 6.66666687e-01, 6.66666687e-01
def c82 = 2.00000000e+00, 2.00000000e+00, 3.00000000e+00, 3.00000000e+00
def c83 = 4.00000000e+00, -4.00000000e+00, 3.29999998e-02, 2.19999999e-01
def c84 = 9.99999975e-05, 3.49999994e-01, 3.49999994e-01, 3.49999994e-01
defi i0 = 255, 0, 0, 0
dcl_volume s14
dcl_2d s1
dcl_2d s13
dcl_2d s3
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
dp3 r0.w, r0.xyzx, r0.xyzx
rsq r1.x, r0.w
rcp r1.x, r1.x
min r1.x, r1.x, c21.w
max r0.w, r0.w, c72.w
rsq r0.w, r0.w
mov r1.yzw, r0.w
mul r0.xyz, r0.xyzx, r1.yzwy
mov r0.w, c68.z
mov r1.y, c71.x
mov r1.z, c68.z
mov r1.w, c68.z
abs r2.xyz, r0.xyzx
add r2.w, r2.x, -r2.y
cmp r2.w, r2.w, c68.z, c71.x
add r2.w, -r2.w, c71.x
mov r3.x, r0.x
mov r3.y, c15.x
mov r3.z, r0.y
mov r3.w, c15.y
cmp r3.xy, -r2.w, r3.zwzz, r3.xyxx
abs r2.w, r3.x
add r2.x, r2.w, -r2.z
cmp r2.x, r2.x, c68.z, c71.x
add r2.x, -r2.x, c71.x
mov r2.y, r0.z
mov r2.z, c15.z
cmp r2.xy, -r2.x, r2.yzyy, r3.xyxx
mul r2.z, c21.w, c73.z
abs r2.w, r2.x
rcp r2.w, r2.w
mul r2.w, r2.z, r2.w
cmp r3.x, r2.x, c68.z, c71.x
cmp r3.x, -r3.x, c71.x, c68.x
mul r2.x, r2.y, r3.x
rcp r2.y, r2.z
mul r2.x, r2.x, r2.y
frc r2.x, r2.x
mov r2.y, c68.z
rep i0.xyzw
    mov r11.w, r2.y
    add r11.w, r11.w, c73.w
    cmp r11.w, r11.w, c68.z, c71.x
    add r11.w, -r11.w, c71.x
    if_ne r11.w, -r11.w
        break
    else
    endif
    mov r11.w, r2.y
    add r11.w, r11.w, -r2.x
    mul r11.w, r11.w, r2.w
    max r11.w, r11.w, c68.z
    add r20.x, r11.w, -r1.x
    cmp r20.x, r20.x, c68.z, c71.x
    add r20.x, -r20.x, c71.x
    if_ne r20.x, -r20.x
        break
    else
    endif
    mov r20.x, r2.y
    add r20.x, r20.x, c71.x
    add r20.x, r20.x, -r2.x
    mul r20.x, r20.x, r2.w
    min r20.x, r20.x, r1.x
    add r20.y, r20.x, -r11.w
    add r11.w, r11.w, r20.x
    mul r11.w, r11.w, c69.x
    mul r20.xzw, r0.xxyz, r11.w
    add r21.xyz, c15.xyzx, r20.xzwx
    add r22.xy, r21.xyxx, -c31.xyxx
    mul r22.xy, r22.xyxx, c31.z
    add r22.xy, r22.xyxx, c74.xyxx
    mul r22.xy, r22.xyxx, c74.zwzz
    add r22.xy, r22.xyxx, c75.xyxx
    frc r23.xyzw, r22.xyxx
    add r22.zw, r22.xxxy, -r23.xxxy
    add r23.xy, r22.xyxx, -r22.zwzz
    add r23.zw, r22.xxzw, c69.xxxy
    mul r23.zw, r23.xxzw, c75.xxzw
    mov r18.xyzw, c70.xyzw
    mov r18.xy, r23.zwzz
    mov r24.xyzw, r18.xyzw
    texldl r24.xyzw, r24.xyzw, s13
    add r23.zw, r22.xxzw, c76.xxxy
    mul r23.zw, r23.xxzw, c75.xxzw
    mov r19.xyzw, c70.xyzw
    mov r19.xy, r23.zwzz
    mov r25.xyzw, r19.xyzw
    texldl r25.xyzw, r25.xyzw, s13
    add r23.zw, r22.xxzw, c76.xxzw
    mul r23.zw, r23.xxzw, c75.xxzw
    mov r17.xyzw, c70.xyzw
    mov r17.xy, r23.zwzz
    mov r26.xyzw, r17.xyzw
    texldl r26.xyzw, r26.xyzw, s13
    add r22.zw, r22.xxzw, c77.xxxy
    mul r22.zw, r22.xxzw, c75.xxzw
    mov r5.xyzw, c70.xyzw
    mov r5.xy, r22.zwzz
    mov r27.xyzw, r5.xyzw
    texldl r27.xyzw, r27.xyzw, s13
    add r21.w, -r23.x, c71.x
    add r22.z, -r23.y, c71.x
    mul r22.w, r21.w, r22.z
    mul r22.z, r23.x, r22.z
    mul r21.w, r21.w, r23.y
    mul r23.x, r23.x, r23.y
    mov r3.x, r22.w
    mov r3.y, r22.z
    mov r3.z, r21.w
    mov r3.w, r23.x
    mov r23.xyzw, r3.xyzw
    cmp r21.w, -r24.w, c68.z, c71.x
    cmp r22.z, -r25.w, c68.z, c71.x
    cmp r22.w, -r26.w, c68.z, c71.x
    cmp r28.x, -r27.w, c68.z, c71.x
    mov r16.x, r21.w
    mov r16.y, r22.z
    mov r16.z, r22.w
    mov r16.w, r28.x
    mov r28.xyzw, r16.xyzw
    mul r23.xyzw, r23.xyzw, r28.xyzw
    dp4 r21.w, r23.xyzw, c78.xyzw
    max r22.z, r21.w, c77.z
    rcp r11.z, r22.z
    min r22.z, r22.x, r22.y
    add r22.w, -r22.x, c77.w
    add r22.x, -r22.y, c77.w
    min r22.x, r22.w, r22.x
    min r22.x, r22.z, r22.x
    mul r22.x, r22.x, c69.x
    mov_sat r22.x, r22.x
    mul r21.w, r21.w, r22.x
    mul r21.w, r21.w, r22.x
    mul r22.x, r22.x, c72.y
    add r22.x, -r22.x, c79.x
    mul r21.w, r21.w, r22.x
    mov r15.x, r24.x
    mov r15.y, r25.x
    mov r15.z, r26.x
    mov r15.w, r27.x
    mov r22.xyzw, r15.xyzw
    dp4 r22.x, r23.xyzw, r22.xyzw
    mul r22.x, r22.x, r11.z
    mov r28.xyzw, r3.xyzw
    mov r14.x, r24.y
    mov r14.y, r25.y
    mov r14.z, r26.y
    mov r14.w, r27.y
    mov r29.xyzw, r14.xyzw
    dp4 r22.y, r28.xyzw, r29.xyzw
    mov r28.xyzw, r3.xyzw
    mov r13.x, r24.z
    mov r13.y, r25.z
    mov r13.z, r26.z
    mov r13.w, r27.z
    mov r29.xyzw, r13.xyzw
    dp4 r22.z, r28.xyzw, r29.xyzw
    mov r12.x, r24.w
    mov r12.y, r25.w
    mov r12.z, r26.w
    mov r12.w, r27.w
    mov r24.xyzw, r12.xyzw
    dp4 r22.w, r23.xyzw, r24.xyzw
    mul r22.w, r22.w, r11.z
    add r22.x, r21.z, -r22.x
    cmp r23.x, r22.x, c68.z, c71.x
    add r23.x, -r23.x, c71.x
    cmp r21.w, -r23.x, c68.z, r21.w
    add r11.w, r11.w, -c58.x
    mul r11.w, r11.w, c58.y
    mov_sat r11.w, r11.w
    mul r23.x, r11.w, c72.y
    add r23.x, -r23.x, c79.x
    mul r23.x, r11.w, r23.x
    mul r11.w, r11.w, r23.x
    add r23.x, r22.w, c79.y
    mul r23.x, r23.x, c79.z
    mov_sat r23.x, r23.x
    add r23.x, -r23.x, c71.x
    mul r23.x, c31.w, r23.x
    add r23.y, -r22.w, c79.w
    mul r23.x, r23.x, r23.y
    add r23.x, r22.w, r23.x
    max r23.x, r23.x, c80.x
    rcp r11.y, r23.x
    mul r23.x, r22.x, r11.y
    add r23.x, -r23.x, c71.x
    mov_sat r23.x, r23.x
    mul r22.z, r22.z, c31.w
    add r22.y, r22.y, r22.z
    max r22.y, r22.y, c68.z
    mul r22.y, r22.y, r23.x
    mul r22.y, r22.y, r23.x
    add r22.y, r22.y, c59.w
    max r22.y, r22.y, c68.z
    mul r22.y, r22.y, r21.w
    mul r22.y, r22.y, r11.w
    mov r11.x, c68.z
    add r22.z, c62.x, -r22.x
    min r22.z, r21.w, r22.z
    cmp r22.z, -r22.z, c68.z, c71.x
    if_ne r22.z, -r22.z
        mul r23.xyz, r20.xzwx, c63.y
        add r23.xyz, r23.xyzx, c60.yzwy
        mov r10.xyzw, c70.xyzw
        mov r10.xyz, r23.xyzx
        mov r23.xyzw, r10.xyzw
        texldl r23.xyzw, r23.xyzw, s14
        add r22.z, c61.x, -r23.x
        cmp r22.z, r22.z, c68.z, c71.x
        if_ne r22.z, -r22.z
            mul r20.xzw, r20.xxzw, c63.z
            add r20.xzw, r20.xxzw, c61.yxzw
            mov r9.xyzw, c70.xyzw
            mov r9.xyz, r20.xzwx
            mov r24.xyzw, r9.xyzw
            texldl r24.xyzw, r24.xyzw, s14
            mad r20.x, r24.x, c63.x, c62.y
            mad r20.x, r23.x, c63.w, r20.x
            mov_sat r20.x, r20.x
            mad r20.z, r23.x, c62.z, c59.y
            max r20.z, r20.z, c80.x
            rcp r8.w, r20.z
            mul r20.z, r22.x, r8.w
            add r20.z, -r20.z, c71.x
            mov_sat r20.z, r20.z
            mad r20.w, r22.w, c80.y, c80.z
            mov_sat r20.w, r20.w
            mad r20.w, r20.w, c80.w, c71.x
            mul r20.x, r20.x, r20.z
            mul r20.x, r20.x, r20.z
            mul r20.x, r20.x, r20.w
            mul r20.x, r20.x, c62.w
            mul r20.x, r20.x, r21.w
            mul r11.w, r20.x, r11.w
            mov r11.x, r11.w
        else
        endif
    else
    endif
    mov r11.w, r11.x
    cmp r11.w, -r11.w, c68.z, c71.x
    if_ne r11.w, -r11.w
        mov r11.w, r11.x
        mul r11.w, -r11.w, r20.y
        mul r11.w, r11.w, c81.x
        exp r8.z, r11.w
        mov r11.w, -r8.z
        add r11.w, r11.w, c71.x
        mul r20.x, r22.x, c81.y
        mov_sat r20.x, r20.x
        mul r20.z, r20.x, c72.y
        add r20.z, -r20.z, c79.x
        mul r20.z, r20.x, r20.z
        mul r20.x, r20.x, r20.z
        mov r20.z, r1.y
        mul r20.z, r20.z, r11.w
        mov r20.w, r0.w
        mul r20.w, -r20.w, c81.x
        exp r7.w, r20.w
        mul r20.w, r7.w, r20.z
        mul r22.xzw, r21.x, c11.xxyz
        mul r23.xyz, r21.y, c12.xyzx
        add r22.xzw, r22.xxzw, r23.xxyz
        mul r21.xyz, r21.z, c13.xyzx
        add r21.xyz, r22.xzwx, r21.xyzx
        add r21.xyz, r21.xyzx, c14.xyzx
        mul r21.w, r21.x, c69.x
        add r21.w, r21.w, c69.x
        mul r22.x, r21.y, c69.x
        add r22.x, -r22.x, c69.x
        mov r8.x, r21.w
        mov r8.y, r22.x
        mov r22.xz, r8.xxyx
        mul r21.w, c2.w, c69.x
        add r22.xz, r22.xxzx, r21.w
        cmp r23.xy, r22.xzxx, c68.zwzz, c78.xyxx
        max r21.w, r23.x, r23.y
        add r23.xy, -r22.xzxx, c78.xyxx
        cmp r23.xy, r23.xyxx, c68.zwzz, c78.xyxx
        max r22.w, r23.x, r23.y
        max r21.w, r21.w, r22.w
        cmp r22.w, r21.z, c68.z, c71.x
        max r21.w, r21.w, r22.w
        add r22.w, -r21.z, c71.x
        cmp r22.w, r22.w, c68.z, c71.x
        max r21.w, r21.w, r22.w
        mov r22.w, r7.z
        cmp r22.w, -r21.w, r22.w, c71.x
        mov r7.z, r22.w
        add r21.w, -r21.w, c71.x
        if_ne r21.w, -r21.w
            add r23.xy, r21.xyxx, -c14.xyxx
            mov r7.x, c2.z
            mov r7.y, -c2.z
            mov r23.zw, r7.xxxy
            mul r23.xy, r23.xyxx, r23.zwzz
            mul r23.xy, r23.xyxx, c81.zwzz
            frc r23.xy, r23.xyxx
            mul r23.zw, r23.xxxy, r23.xxxy
            mul r24.xy, r23.xyxx, c82.xyxx
            add r24.xy, -r24.xyxx, c82.zwzz
            mul r23.zw, r23.xxzw, r24.xxxy
            mul r21.w, c24.x, c83.x
            add r21.x, r21.z, -r21.w
            mov r6.w, c68.z
            mov r6.z, c68.z
            rep i0.xyzw
                mov r21.y, r6.z
                add r21.y, r21.y, c83.y
                cmp r21.y, r21.y, c68.z, c71.x
                add r21.y, -r21.y, c71.x
                if_ne r21.y, -r21.y
                    break
                else
                endif
                mov r21.y, r6.z
                mul r21.y, r21.y, c69.x
                frc r24.xyzw, r21.y
                add r21.y, r21.y, -r24.x
                mov r21.z, r6.z
                mul r21.w, r21.y, c72.y
                add r21.z, r21.z, -r21.w
                mov r6.x, r21.z
                mov r6.y, r21.y
                mov r21.yz, r6.xxyx
                add r21.yz, r21.xyzx, -r23.xxyx
                mul r21.w, c2.z, c72.y
                rcp r2.z, r21.w
                mul r21.w, r2.z, c76.x
                mul r21.yz, r21.xyzx, r21.w
                add r21.yz, r22.xxzx, r21.xyzx
                mov r4.xyzw, c70.xyzw
                mov r4.xy, r21.yzyy
                mov r24.xyzw, r4.xyzw
                texldl r24.xyzw, r24.xyzw, s3
                add r21.yz, -r23.xzwx, c78.xxyx
                mov r25.xy, r6.xyxx
                add r25.zw, r23.xxzw, -r21.xxyz
                mul r25.xy, r25.xyxx, r25.zwzz
                add r21.yz, r21.xyzx, r25.xxyx
                add r21.w, r24.x, -r21.x
                cmp r21.w, r21.w, c68.z, c71.x
                add r21.w, -r21.w, c71.x
                cmp r21.w, -r21.w, c68.z, c71.x
                mul r21.w, r21.w, r21.y
                mul r21.y, r21.w, r21.z
                mov r21.z, r6.w
                add r21.y, r21.z, r21.y
                mov r6.w, r21.y
                mov r21.y, r6.z
                add r21.y, r21.y, c71.x
                mov r6.z, r21.y
            endrep
            mov r21.x, r6.w
            mov r7.z, r21.x
        else
        endif
        mov r21.x, r7.z
        mul r20.w, r20.w, r21.x
        mul r20.x, r20.w, r20.x
        mov r20.w, r1.z
        add r20.x, r20.w, r20.x
        mov r1.z, r20.x
        mov r20.x, r1.w
        add r20.x, r20.x, r20.z
        mov r1.w, r20.x
        add r11.w, -r11.w, c71.x
        mov r20.x, r1.y
        mul r11.w, r20.x, r11.w
        mov r1.y, r11.w
    else
    endif
    mul r11.w, r22.y, r20.y
    mov r20.x, r0.w
    add r11.w, r20.x, r11.w
    mov r0.w, r11.w
    mov r11.w, r2.y
    add r11.w, r11.w, c71.x
    mov r2.y, r11.w
endrep
max r2.xyz, c17.xyzx, c70.xyzx
dp3 r0.x, r0.xyzx, c16.xyzx
mad r0.x, c83.z, r0.x, c83.w
mul r0.xyz, r2.xyzx, r0.x
max r0.w, c21.y, c68.z
mov r1.x, r1.z
mul r0.w, r0.w, r1.x
mul r0.xyz, r0.xyzx, r0.w
max r0.w, r0.y, r0.z
max r0.w, r0.x, r0.w
max r1.x, c21.z, c84.x
add r0.w, r1.x, r0.w
rcp r0.w, r0.w
mul r0.w, r1.x, r0.w
mul r0.xyz, r0.xyzx, r0.w
max r2.xyz, c18.xyzx, c70.xyzx
mul r2.xyz, r2.xyzx, c84.yzwy
mul r3.xyz, c26.xyzx, c25.w
max r2.xyz, r2.xyzx, r3.xyzx
mov r0.w, r1.w
mul r1.xzw, r2.xxyz, r0.w
add r0.xyz, r1.xzwx, r0.xyzx
mov r0.w, r1.y
mov r1.xyz, r0.xyzx
mov r1.w, r0.w
mov oC0.xyzw, r1.xyzw
