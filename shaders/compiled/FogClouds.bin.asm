ps_3_0
dcl_texcoord0 v0
def c68 = -5.00000000e-01, 0.00000000e+00, 1.00000000e+00, 9.99989986e-01
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c70 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c71 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c72 = 9.99999975e-06, 1.20000001e-02, 6.99999975e-04, 3.00000003e-03
def c73 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c74 = 9.99999996e-13, 2.50000004e-02, -4.10000000e+01, 9.99999997e-07
def c75 = 7.81250000e-03, 7.81250000e-03, 6.40000000e+01, 6.40000000e+01
def c76 = -5.00000000e-01, -5.00000000e-01, 1.56250000e-02, 1.56250000e-02
def c77 = 1.50000000e+00, 5.00000000e-01, 5.00000000e-01, 1.50000000e+00
def c78 = 1.50000000e+00, 1.50000000e+00, 6.30000000e+01, 3.00000000e+00
def c79 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c80 = -2.50000000e+00, 4.00000006e-01, 6.00000000e+00, 1.00000005e-03
def c81 = 6.49999976e-01, 3.49999994e-01, -1.25000000e+00, 2.66666681e-01
def c82 = -3.00000012e-01, 1.44269502e+00, 8.33333358e-02, 4.00000000e+00
def c83 = 6.66666687e-01, 6.66666687e-01, 2.00000000e+00, 2.00000000e+00
def c84 = 3.00000000e+00, 3.00000000e+00, -4.00000000e+00, 3.29999998e-02
def c85 = 2.19999999e-01, 9.99999975e-05, 0.00000000e+00, 0.00000000e+00
def c86 = 3.49999994e-01, 3.49999994e-01, 3.49999994e-01, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_volume s14
dcl_2d s1
dcl_2d s13
dcl_2d s3
dcl_2d s11
add r0.x, c21.x, c68.x
cmp r0.x, r0.x, c68.y, c68.z
mov r1.xyzw, r2.xyzw
cmp r1.xyzw, -r0.x, r1.xyzw, c69.xyzw
mov r2.xyzw, r1.xyzw
add r0.x, -r0.x, c68.z
if_ne r0.x, -r0.x
    mul r0.xy, v0.xyxx, c33.xyxx
    frc r1.xyzw, r0.xyxx
    add r0.xy, r0.xyxx, -r1.xyzw
    add r0.zw, c33.xxxy, c70.xxxy
    max r0.xy, r0.xyxx, c69.xyxx
    min r0.xy, r0.xyxx, r0.zwzz
    add r0.xy, r0.xyxx, c70.zwzz
    mul r0.xy, r0.xyxx, c0.xyxx
    mov r1.xyzw, c71.xyzw
    mov r1.xy, r0.xyxx
    texldl r1.xyzw, r1.xyzw, s1
    add r0.z, r1.x, -c1.w
    mul r0.z, r0.z, c2.x
    mov_sat r0.z, r0.z
    min r0.z, r0.z, c68.w
    add r0.w, c30.x, c68.x
    cmp r0.w, r0.w, c68.y, c68.z
    mov r1.x, r1.y
    cmp r1.x, -r0.w, r1.x, c68.y
    mov r1.y, r1.x
    add r0.w, -r0.w, c68.z
    if_ne r0.w, -r0.w
        mov r3.xyzw, c71.xyzw
        mov r3.xy, r0.xyxx
        texldl r3.xyzw, r3.xyzw, s11
        add r0.w, c0.z, -r3.x
        cmp r0.w, r0.w, c68.y, c68.z
        mul r1.x, c0.z, c0.w
        add r1.z, c0.w, -c0.z
        mul r1.z, r0.z, r1.z
        add r1.z, c0.w, -r1.z
        max r1.z, r1.z, c72.x
        rcp r1.z, r1.z
        mul r1.x, r1.x, r1.z
        mul r1.z, r3.x, c72.z
        max r1.z, r1.z, c72.y
        add r1.x, r1.x, r1.z
        add r1.x, r1.x, -r3.x
        cmp r1.x, r1.x, c68.y, c68.z
        add r1.x, -r1.x, c68.z
        min r0.w, r0.w, r1.x
        add r1.x, -r3.y, c72.w
        cmp r1.x, r1.x, c68.y, c68.z
        min r0.w, r0.w, r1.x
        cmp r0.w, -r0.w, c68.y, r3.x
        mov r1.y, r0.w
    else
    endif
    mov r0.w, r1.y
    cmp r1.x, -r0.w, c68.y, c68.z
    mul r1.y, c0.z, c0.w
    add r1.z, c0.w, -c0.z
    mul r0.z, r0.z, r1.z
    add r0.z, c0.w, -r0.z
    max r0.z, r0.z, c72.x
    rcp r0.z, r0.z
    mul r0.z, r1.y, r0.z
    cmp r0.z, -r1.x, r0.z, r0.w
    mul r1.xy, c0.xyxx, c70.zwzz
    add r0.xy, r0.xyxx, -r1.xyxx
    mul r0.xy, r0.xyxx, c73.xyxx
    add r0.xy, r0.xyxx, c73.zwzz
    rcp r1.x, c1.x
    rcp r1.y, c1.y
    mul r0.xy, r0.xyxx, r1.xyxx
    mov r0.w, c1.z
    mul r0.xyz, r0.xywx, r0.z
    mul r1.xyz, r0.x, c3.xyzx
    mul r3.xyz, r0.y, c4.xyzx
    add r1.xyz, r1.xyzx, r3.xyzx
    mul r0.xyz, r0.z, c5.xyzx
    add r0.xyz, r1.xyzx, r0.xyzx
    dp3 r0.w, r0.xyzx, r0.xyzx
    rsq r1.x, r0.w
    rcp r1.x, r1.x
    min r1.x, r1.x, c21.w
    max r0.w, r0.w, c74.x
    rsq r0.w, r0.w
    mov r1.yzw, r0.w
    mul r0.xyz, r0.xyzx, r1.yzwy
    mov r0.w, c68.z
    mov r1.y, c68.z
    mov r1.z, c68.y
    mov r1.w, c68.y
    abs r3.xyz, r0.xyzx
    add r3.w, r3.x, -r3.y
    cmp r3.w, r3.w, c68.y, c68.z
    add r3.w, -r3.w, c68.z
    mov r4.x, r0.x
    mov r4.y, c15.x
    mov r4.z, r0.y
    mov r4.w, c15.y
    cmp r4.xy, -r3.w, r4.zwzz, r4.xyxx
    abs r3.w, r4.x
    add r3.x, r3.w, -r3.z
    cmp r3.x, r3.x, c68.y, c68.z
    add r3.x, -r3.x, c68.z
    mov r3.y, r0.z
    mov r3.z, c15.z
    cmp r3.xy, -r3.x, r3.yzyy, r4.xyxx
    mul r3.z, c21.w, c74.y
    abs r3.w, r3.x
    rcp r3.w, r3.w
    mul r3.w, r3.z, r3.w
    cmp r4.x, r3.x, c68.y, c68.z
    cmp r4.x, -r4.x, c68.z, c70.x
    mul r3.x, r3.y, r4.x
    rcp r3.y, r3.z
    mul r3.x, r3.x, r3.y
    frc r3.x, r3.x
    mov r3.y, c68.y
    rep i0.xyzw
        mov r19.z, r3.y
        add r19.z, r19.z, c74.z
        cmp r19.z, r19.z, c68.y, c68.z
        add r19.z, -r19.z, c68.z
        if_ne r19.z, -r19.z
            break
        else
        endif
        mov r19.z, r3.y
        add r19.z, r19.z, -r3.x
        mul r19.z, r19.z, r3.w
        max r19.z, r19.z, c68.y
        add r19.w, r19.z, -r1.x
        cmp r19.w, r19.w, c68.y, c68.z
        add r19.w, -r19.w, c68.z
        if_ne r19.w, -r19.w
            break
        else
        endif
        mov r19.w, r3.y
        add r19.w, r19.w, c68.z
        add r19.w, r19.w, -r3.x
        mul r19.w, r19.w, r3.w
        min r19.w, r19.w, r1.x
        add r21.x, r19.w, -r19.z
        add r19.z, r19.z, r19.w
        mul r19.z, r19.z, c70.z
        mul r21.yzw, r0.xxyz, r19.z
        add r22.xyz, c15.xyzx, r21.yzwy
        add r23.xy, r22.xyxx, -c31.xyxx
        mul r23.xy, r23.xyxx, c31.z
        add r23.xy, r23.xyxx, c75.xyxx
        mul r23.xy, r23.xyxx, c75.zwzz
        add r23.xy, r23.xyxx, c76.xyxx
        frc r24.xyzw, r23.xyxx
        add r23.zw, r23.xxxy, -r24.xxxy
        add r24.xy, r23.xyxx, -r23.zwzz
        add r24.zw, r23.xxzw, c70.xxzw
        mul r24.zw, r24.xxzw, c76.xxzw
        mov r6.xyzw, c71.xyzw
        mov r6.xy, r24.zwzz
        mov r25.xyzw, r6.xyzw
        texldl r25.xyzw, r25.xyzw, s13
        add r24.zw, r23.xxzw, c77.xxxy
        mul r24.zw, r24.xxzw, c76.xxzw
        mov r7.xyzw, c71.xyzw
        mov r7.xy, r24.zwzz
        mov r26.xyzw, r7.xyzw
        texldl r26.xyzw, r26.xyzw, s13
        add r24.zw, r23.xxzw, c77.xxzw
        mul r24.zw, r24.xxzw, c76.xxzw
        mov r16.xyzw, c71.xyzw
        mov r16.xy, r24.zwzz
        mov r27.xyzw, r16.xyzw
        texldl r27.xyzw, r27.xyzw, s13
        add r23.zw, r23.xxzw, c78.xxxy
        mul r23.zw, r23.xxzw, c76.xxzw
        mov r10.xyzw, c71.xyzw
        mov r10.xy, r23.zwzz
        mov r28.xyzw, r10.xyzw
        texldl r28.xyzw, r28.xyzw, s13
        add r19.w, -r24.x, c68.z
        add r22.w, -r24.y, c68.z
        mul r23.z, r19.w, r22.w
        mul r22.w, r24.x, r22.w
        mul r19.w, r19.w, r24.y
        mul r23.w, r24.x, r24.y
        mov r12.x, r23.z
        mov r12.y, r22.w
        mov r12.z, r19.w
        mov r12.w, r23.w
        mov r24.xyzw, r12.xyzw
        cmp r19.w, -r25.w, c68.y, c68.z
        cmp r22.w, -r26.w, c68.y, c68.z
        cmp r23.z, -r27.w, c68.y, c68.z
        cmp r23.w, -r28.w, c68.y, c68.z
        mov r13.x, r19.w
        mov r13.y, r22.w
        mov r13.z, r23.z
        mov r13.w, r23.w
        mov r29.xyzw, r13.xyzw
        mul r24.xyzw, r24.xyzw, r29.xyzw
        dp4 r19.w, r24.xyzw, c79.xyzw
        max r22.w, r19.w, c74.w
        rcp r8.w, r22.w
        min r22.w, r23.x, r23.y
        add r23.z, -r23.x, c78.z
        add r23.x, -r23.y, c78.z
        min r23.x, r23.z, r23.x
        min r22.w, r22.w, r23.x
        mul r22.w, r22.w, c70.z
        mov_sat r22.w, r22.w
        mul r19.w, r19.w, r22.w
        mul r19.w, r19.w, r22.w
        mul r22.w, r22.w, c73.x
        add r22.w, -r22.w, c78.w
        mul r19.w, r19.w, r22.w
        mov r15.x, r25.x
        mov r15.y, r26.x
        mov r15.z, r27.x
        mov r15.w, r28.x
        mov r23.xyzw, r15.xyzw
        dp4 r22.w, r24.xyzw, r23.xyzw
        mul r22.w, r22.w, r8.w
        mov r23.xyzw, r12.xyzw
        mov r20.x, r25.y
        mov r20.y, r26.y
        mov r20.z, r27.y
        mov r20.w, r28.y
        mov r29.xyzw, r20.xyzw
        dp4 r23.x, r23.xyzw, r29.xyzw
        mov r29.xyzw, r12.xyzw
        mov r9.x, r25.z
        mov r9.y, r26.z
        mov r9.z, r27.z
        mov r9.w, r28.z
        mov r30.xyzw, r9.xyzw
        dp4 r23.y, r29.xyzw, r30.xyzw
        mov r14.x, r25.w
        mov r14.y, r26.w
        mov r14.z, r27.w
        mov r14.w, r28.w
        mov r25.xyzw, r14.xyzw
        dp4 r23.z, r24.xyzw, r25.xyzw
        mul r23.z, r23.z, r8.w
        add r22.w, r22.z, -r22.w
        cmp r23.w, r22.w, c68.y, c68.z
        add r23.w, -r23.w, c68.z
        cmp r19.w, -r23.w, c68.y, r19.w
        add r19.z, r19.z, -c58.x
        mul r19.z, r19.z, c58.y
        mov_sat r19.z, r19.z
        mul r23.w, r19.z, c73.x
        add r23.w, -r23.w, c78.w
        mul r23.w, r19.z, r23.w
        mul r19.z, r19.z, r23.w
        add r23.w, r23.z, c80.x
        mul r23.w, r23.w, c80.y
        mov_sat r23.w, r23.w
        add r23.w, -r23.w, c68.z
        mul r23.w, c31.w, r23.w
        add r24.x, -r23.z, c80.z
        mul r23.w, r23.w, r24.x
        add r23.w, r23.z, r23.w
        max r23.w, r23.w, c80.w
        rcp r8.z, r23.w
        mul r23.w, r22.w, r8.z
        add r23.w, -r23.w, c68.z
        mov_sat r23.w, r23.w
        mul r23.y, r23.y, c31.w
        add r23.x, r23.x, r23.y
        max r23.x, r23.x, c68.y
        mul r23.x, r23.x, r23.w
        mul r23.x, r23.x, r23.w
        add r23.x, r23.x, c59.w
        max r23.x, r23.x, c68.y
        mul r23.x, r23.x, r19.w
        mul r23.x, r23.x, r19.z
        mul r24.xyz, r21.yzwy, c63.y
        add r24.xyz, r24.xyzx, c60.yzwy
        mov r11.xyzw, c71.xyzw
        mov r11.xyz, r24.xyzx
        mov r24.xyzw, r11.xyzw
        texldl r24.xyzw, r24.xyzw, s14
        mul r21.yzw, r21.xyzw, c63.z
        add r21.yzw, r21.xyzw, c61.xyzw
        mov r18.xyzw, c71.xyzw
        mov r18.xyz, r21.yzwy
        mov r25.xyzw, r18.xyzw
        texldl r25.xyzw, r25.xyzw, s14
        mul r21.y, r25.x, c81.y
        mad r21.y, c81.x, r24.x, r21.y
        add r21.y, r21.y, -c62.y
        mul r21.y, r21.y, c63.w
        mov_sat r21.y, r21.y
        max r21.z, c62.z, c80.w
        rcp r17.w, r21.z
        mul r21.z, r22.w, r17.w
        add r21.z, -r21.z, c68.z
        mov_sat r21.z, r21.z
        add r21.w, r23.z, c81.z
        mul r21.w, r21.w, c81.w
        mov_sat r21.w, r21.w
        mul r21.w, r21.w, c82.x
        add r21.w, r21.w, c68.z
        mul r21.y, r21.y, r21.z
        mul r21.y, r21.y, r21.z
        mul r21.y, r21.y, r21.w
        mul r21.y, r21.y, c62.w
        mul r19.w, r21.y, r19.w
        mul r19.z, r19.w, r19.z
        cmp r19.w, -r19.z, c68.y, c68.z
        if_ne r19.w, -r19.w
            mul r19.z, -r19.z, r21.x
            mul r19.z, r19.z, c82.y
            exp r17.x, r19.z
            mov r19.z, -r17.x
            add r19.z, r19.z, c68.z
            mul r19.w, r22.w, c82.z
            mov_sat r19.w, r19.w
            mul r21.y, r19.w, c73.x
            add r21.y, -r21.y, c78.w
            mul r21.y, r19.w, r21.y
            mul r19.w, r19.w, r21.y
            mov r21.y, r1.y
            mul r21.y, r21.y, r19.z
            mov r21.z, r0.w
            mul r21.z, r21.z, r21.y
            mul r23.yzw, r22.x, c11.xxyz
            mul r24.xyz, r22.y, c12.xyzx
            add r23.yzw, r23.xyzw, r24.xxyz
            mul r22.xyz, r22.z, c13.xyzx
            add r22.xyz, r23.yzwy, r22.xyzx
            add r22.xyz, r22.xyzx, c14.xyzx
            mul r21.w, r22.x, c70.z
            add r21.w, r21.w, c70.z
            mul r22.w, r22.y, c70.z
            add r22.w, -r22.w, c70.z
            mov r8.x, r21.w
            mov r8.y, r22.w
            mov r23.yz, r8.xxyx
            mul r21.w, c2.w, c70.z
            add r23.yz, r23.xyzx, r21.w
            cmp r24.xy, r23.yzyy, c69.xyxx, c79.xyxx
            max r21.w, r24.x, r24.y
            add r24.xy, -r23.yzyy, c79.xyxx
            cmp r24.xy, r24.xyxx, c69.xyxx, c79.xyxx
            max r22.w, r24.x, r24.y
            max r21.w, r21.w, r22.w
            cmp r22.w, r22.z, c68.y, c68.z
            max r21.w, r21.w, r22.w
            add r22.w, -r22.z, c68.z
            cmp r22.w, r22.w, c68.y, c68.z
            max r21.w, r21.w, r22.w
            mov r22.w, r19.x
            cmp r22.w, -r21.w, r22.w, c68.z
            mov r19.x, r22.w
            add r21.w, -r21.w, c68.z
            if_ne r21.w, -r21.w
                add r24.xy, r22.xyxx, -c14.xyxx
                mov r17.y, c2.z
                mov r17.z, -c2.z
                mov r24.zw, r17.xxyz
                mul r24.xy, r24.xyxx, r24.zwzz
                mul r24.xy, r24.xyxx, c83.xyxx
                frc r24.xy, r24.xyxx
                mul r24.zw, r24.xxxy, r24.xxxy
                mul r25.xy, r24.xyxx, c83.zwzz
                add r25.xy, -r25.xyxx, c84.xyxx
                mul r24.zw, r24.xxzw, r25.xxxy
                mul r21.w, c24.x, c82.w
                add r21.w, r22.z, -r21.w
                mov r19.y, c68.y
                mov r5.w, c68.y
                rep i0.xyzw
                    mov r22.x, r5.w
                    add r22.x, r22.x, c84.z
                    cmp r22.x, r22.x, c68.y, c68.z
                    add r22.x, -r22.x, c68.z
                    if_ne r22.x, -r22.x
                        break
                    else
                    endif
                    mov r22.x, r5.w
                    mul r22.x, r22.x, c70.z
                    frc r25.xyzw, r22.x
                    add r22.x, r22.x, -r25.xyzw
                    mov r22.y, r5.w
                    mul r22.z, r22.x, c73.x
                    add r22.y, r22.y, -r22.z
                    mov r5.y, r22.y
                    mov r5.z, r22.x
                    mov r22.xy, r5.yzyy
                    add r22.xy, r22.xyxx, -r24.xyxx
                    mul r22.z, c2.z, c73.x
                    rcp r5.x, r22.z
                    mul r22.z, r5.x, c77.x
                    mul r22.xy, r22.xyxx, r22.z
                    add r22.xy, r23.yzyy, r22.xyxx
                    mov r4.xyzw, c71.xyzw
                    mov r4.xy, r22.xyxx
                    mov r22.xyzw, r4.xyzw
                    texldl r22.xyzw, r22.xyzw, s3
                    add r25.xy, -r24.zwzz, c79.xyxx
                    mov r25.zw, r5.xxyz
                    add r26.xy, r24.zwzz, -r25.xyxx
                    mul r25.zw, r25.xxzw, r26.xxxy
                    add r25.xy, r25.xyxx, r25.zwzz
                    add r22.x, r22.x, -r21.w
                    cmp r22.x, r22.x, c68.y, c68.z
                    add r22.x, -r22.x, c68.z
                    cmp r22.x, -r22.x, c68.y, c68.z
                    mul r22.x, r22.x, r25.x
                    mul r22.x, r22.x, r25.y
                    mov r22.y, r19.y
                    add r22.x, r22.y, r22.x
                    mov r19.y, r22.x
                    mov r22.x, r5.w
                    add r22.x, r22.x, c68.z
                    mov r5.w, r22.x
                endrep
                mov r21.w, r19.y
                mov r19.x, r21.w
            else
            endif
            mov r21.w, r19.x
            mul r21.z, r21.z, r21.w
            mul r19.w, r21.z, r19.w
            mov r21.z, r1.z
            add r19.w, r21.z, r19.w
            mov r1.z, r19.w
            mov r19.w, r1.w
            add r19.w, r19.w, r21.y
            mov r1.w, r19.w
            add r19.z, -r19.z, c68.z
            mov r19.w, r1.y
            mul r19.z, r19.w, r19.z
            mov r1.y, r19.z
        else
        endif
        mul r19.z, -r23.x, r21.x
        mul r19.z, r19.z, c82.y
        exp r3.z, r19.z
        mov r19.z, r0.w
        mul r19.z, r19.z, r3.z
        mov r0.w, r19.z
        mov r19.z, r3.y
        add r19.z, r19.z, c68.z
        mov r3.y, r19.z
    endrep
    max r3.xyz, c17.xyzx, c69.xyzx
    dp3 r0.x, r0.xyzx, c16.xyzx
    mad r0.x, c84.w, r0.x, c85.x
    mul r0.xyz, r3.xyzx, r0.x
    mov_sat r3.xyz, c22.xyzx
    mul r0.xyz, r0.xyzx, r3.xyzx
    max r0.w, c21.y, c68.y
    mov r1.x, r1.z
    mul r0.w, r0.w, r1.x
    mul r0.xyz, r0.xyzx, r0.w
    max r0.w, r0.y, r0.z
    max r0.w, r0.x, r0.w
    max r1.x, c21.z, c85.y
    add r0.w, r1.x, r0.w
    rcp r0.w, r0.w
    mul r0.w, r1.x, r0.w
    mul r0.xyz, r0.xyzx, r0.w
    max r3.xyz, c18.xyzx, c69.xyzx
    mul r3.xyz, r3.xyzx, c86.xyzx
    mul r4.xyz, c26.xyzx, c25.w
    max r3.xyz, r3.xyzx, r4.xyzx
    mov r0.w, r1.w
    mul r1.xzw, r3.xxyz, r0.w
    add r0.xyz, r1.xzwx, r0.xyzx
    mov r0.w, r1.y
    mov r1.xyz, r0.xyzx
    mov r1.w, r0.w
    mov r0.xyzw, r1.xyzw
    mov r2.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r2.xyzw
