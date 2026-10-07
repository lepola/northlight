ps_3_0
dcl_texcoord0 v0
def c68 = -5.00000000e-01, 0.00000000e+00, 1.00000000e+00, 9.99989986e-01
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 1.00000000e+00
def c70 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c71 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c72 = 9.99999975e-06, 1.20000001e-02, 6.99999975e-04, 3.00000003e-03
def c73 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c74 = 9.99999996e-13, 2.77500004e-01, 1.72249997e+00, 1.70000005e+00
def c75 = 9.99999978e-03, -1.50000000e+00, 2.99999993e-02, 3.29999998e-02
def c76 = 5.00000000e-01, 5.00000000e-01, 0.00000000e+00, 0.00000000e+00
def c77 = 2.19999999e-01, 2.08333340e-02, -4.90000000e+01, 9.99999997e-07
def c78 = 7.81250000e-03, 7.81250000e-03, 6.40000000e+01, 6.40000000e+01
def c79 = -5.00000000e-01, -5.00000000e-01, 1.56250000e-02, 1.56250000e-02
def c80 = 1.50000000e+00, 5.00000000e-01, 5.00000000e-01, 1.50000000e+00
def c81 = 1.50000000e+00, 1.50000000e+00, 6.30000000e+01, 3.00000000e+00
def c82 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c83 = -2.50000000e+00, 4.00000006e-01, -1.25000000e+00, 8.00000012e-01
def c84 = 6.00000000e+00, 1.00000005e-03, 1.60000002e+00, 1.44269502e+00
def c85 = 8.33333358e-02, 6.66666687e-01, 6.66666687e-01, 4.00000000e+00
def c86 = 2.00000000e+00, 2.00000000e+00, 3.00000000e+00, 3.00000000e+00
def c87 = -4.00000000e+00, 9.99999975e-05, 3.49999994e-01, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s13
dcl_2d s3
dcl_2d s15
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
    mov r1.y, c68.y
    mov r1.z, c68.y
    mov r1.w, c68.y
    dp3 r3.x, r0.xyzx, c16.xyzx
    mul r3.y, r3.x, c74.w
    add r3.y, -r3.y, c74.z
    max r3.y, r3.y, c75.x
    log r3.y, r3.y
    mul r3.y, r3.y, c75.y
    exp r3.y, r3.y
    mul r3.y, r3.y, c74.y
    texldl r4.xyzw, c76.xyzw, s15
    mov_sat r3.z, r4.x
    mul r3.z, r3.z, c75.z
    mad r3.x, c75.w, r3.x, c77.x
    mad r3.x, r3.z, r3.y, r3.x
    max r3.yzw, c17.xxyz, c69.xxyz
    mul r3.xyz, r3.yzwy, r3.x
    mov_sat r4.xyz, c22.xyzx
    mul r3.xyz, r3.xyzx, r4.xyzx
    max r3.w, c21.y, c68.y
    mul r3.xyz, r3.xyzx, r3.w
    abs r4.xyz, r0.xyzx
    add r3.w, r4.x, -r4.y
    cmp r3.w, r3.w, c68.y, c68.z
    add r3.w, -r3.w, c68.z
    mov r5.x, r0.x
    mov r5.y, c15.x
    mov r5.z, r0.y
    mov r5.w, c15.y
    cmp r5.xy, -r3.w, r5.zwzz, r5.xyxx
    abs r3.w, r5.x
    add r3.w, r3.w, -r4.z
    cmp r3.w, r3.w, c68.y, c68.z
    add r3.w, -r3.w, c68.z
    mov r4.x, r0.z
    mov r4.y, c15.z
    cmp r4.xy, -r3.w, r4.xyxx, r5.xyxx
    mul r3.w, c21.w, c77.y
    abs r4.z, r4.x
    rcp r4.z, r4.z
    mul r4.z, r3.w, r4.z
    cmp r4.w, r4.x, c68.y, c68.z
    cmp r4.w, -r4.w, c68.z, c70.x
    mul r4.x, r4.y, r4.w
    rcp r3.w, r3.w
    mul r3.w, r4.x, r3.w
    frc r3.w, r3.w
    mov r4.x, c68.y
    rep i0.xyzw
        mov r19.x, r4.x
        add r19.x, r19.x, c77.z
        cmp r19.x, r19.x, c68.y, c68.z
        add r19.x, -r19.x, c68.z
        if_ne r19.x, -r19.x
            break
        else
        endif
        mov r19.x, r4.x
        add r19.x, r19.x, -r3.w
        mul r19.x, r19.x, r4.z
        max r19.x, r19.x, c68.y
        add r19.y, r19.x, -r1.x
        cmp r19.y, r19.y, c68.y, c68.z
        add r19.y, -r19.y, c68.z
        if_ne r19.y, -r19.y
            break
        else
        endif
        mov r19.y, r4.x
        add r19.y, r19.y, c68.z
        add r19.y, r19.y, -r3.w
        mul r19.y, r19.y, r4.z
        min r19.y, r19.y, r1.x
        add r19.z, r19.y, -r19.x
        add r19.xy, r19.x, r19.y
        mul r19.w, r19.x, c70.z
        mul r20.xyz, r0.xyzx, r19.w
        add r20.xyz, c15.xyzx, r20.xyzx
        add r21.xy, r20.xyxx, -c31.xyxx
        mul r21.xy, r21.xyxx, c31.z
        add r21.xy, r21.xyxx, c78.xyxx
        mul r21.xy, r21.xyxx, c78.zwzz
        add r21.xy, r21.xyxx, c79.xyxx
        frc r22.xyzw, r21.xyxx
        add r21.zw, r21.xxxy, -r22.xxxy
        add r22.xy, r21.xyxx, -r21.zwzz
        add r22.zw, r21.xxzw, c70.xxzw
        mul r22.zw, r22.xxzw, c79.xxzw
        mov r5.xyzw, c71.xyzw
        mov r5.xy, r22.zwzz
        mov r23.xyzw, r5.xyzw
        texldl r23.xyzw, r23.xyzw, s13
        add r22.zw, r21.xxzw, c80.xxxy
        mul r22.zw, r22.xxzw, c79.xxzw
        mov r18.xyzw, c71.xyzw
        mov r18.xy, r22.zwzz
        mov r24.xyzw, r18.xyzw
        texldl r24.xyzw, r24.xyzw, s13
        add r22.zw, r21.xxzw, c80.xxzw
        mul r22.zw, r22.xxzw, c79.xxzw
        mov r17.xyzw, c71.xyzw
        mov r17.xy, r22.zwzz
        mov r25.xyzw, r17.xyzw
        texldl r25.xyzw, r25.xyzw, s13
        add r21.zw, r21.xxzw, c81.xxxy
        mul r21.zw, r21.xxzw, c79.xxzw
        mov r16.xyzw, c71.xyzw
        mov r16.xy, r21.zwzz
        mov r26.xyzw, r16.xyzw
        texldl r26.xyzw, r26.xyzw, s13
        add r19.w, -r22.x, c68.z
        add r20.w, -r22.y, c68.z
        mul r21.z, r19.w, r20.w
        mul r20.w, r22.x, r20.w
        mul r19.w, r19.w, r22.y
        mul r21.w, r22.x, r22.y
        mov r15.x, r21.z
        mov r15.y, r20.w
        mov r15.z, r19.w
        mov r15.w, r21.w
        mov r22.xyzw, r15.xyzw
        cmp r19.w, -r23.w, c68.y, c68.z
        cmp r20.w, -r24.w, c68.y, c68.z
        cmp r21.z, -r25.w, c68.y, c68.z
        cmp r21.w, -r26.w, c68.y, c68.z
        mov r14.x, r19.w
        mov r14.y, r20.w
        mov r14.z, r21.z
        mov r14.w, r21.w
        mov r27.xyzw, r14.xyzw
        mul r22.xyzw, r22.xyzw, r27.xyzw
        dp4 r19.w, r22.xyzw, c82.xyzw
        max r20.w, r19.w, c77.w
        rcp r9.z, r20.w
        min r20.w, r21.x, r21.y
        add r21.z, -r21.x, c81.z
        add r21.x, -r21.y, c81.z
        min r21.x, r21.z, r21.x
        min r20.w, r20.w, r21.x
        mul r20.w, r20.w, c70.z
        mov_sat r20.w, r20.w
        mul r19.w, r19.w, r20.w
        mul r19.w, r19.w, r20.w
        mul r20.w, r20.w, c73.x
        add r20.w, -r20.w, c81.w
        mul r19.w, r19.w, r20.w
        mov r13.x, r23.x
        mov r13.y, r24.x
        mov r13.z, r25.x
        mov r13.w, r26.x
        mov r21.xyzw, r13.xyzw
        dp4 r20.w, r22.xyzw, r21.xyzw
        mul r20.w, r20.w, r9.z
        mov r21.xyzw, r15.xyzw
        mov r12.x, r23.y
        mov r12.y, r24.y
        mov r12.z, r25.y
        mov r12.w, r26.y
        mov r27.xyzw, r12.xyzw
        dp4 r21.x, r21.xyzw, r27.xyzw
        mov r27.xyzw, r15.xyzw
        mov r11.x, r23.z
        mov r11.y, r24.z
        mov r11.z, r25.z
        mov r11.w, r26.z
        mov r28.xyzw, r11.xyzw
        dp4 r21.y, r27.xyzw, r28.xyzw
        mov r10.x, r23.w
        mov r10.y, r24.w
        mov r10.z, r25.w
        mov r10.w, r26.w
        mov r23.xyzw, r10.xyzw
        dp4 r21.z, r22.xyzw, r23.xyzw
        mul r21.z, r21.z, r9.z
        add r20.w, r20.z, -r20.w
        add r21.w, r21.z, c83.x
        mul r21.w, r21.w, c83.y
        mov_sat r21.w, r21.w
        add r22.x, r21.z, c83.z
        mul r22.x, r22.x, c83.w
        mov_sat r22.x, r22.x
        add r22.x, -r22.x, c68.z
        add r22.y, -r21.w, c68.z
        mul r22.y, c31.w, r22.y
        add r22.z, -r21.z, c84.x
        mul r22.y, r22.y, r22.z
        add r22.y, r21.z, r22.y
        max r22.y, r22.y, c84.y
        rcp r9.y, r22.y
        mul r22.y, r20.w, r9.y
        add r22.y, -r22.y, c68.z
        mov_sat r22.y, r22.y
        cmp r22.z, r20.w, c68.y, c68.z
        add r22.z, -r22.z, c68.z
        cmp r19.w, -r22.z, c68.y, r19.w
        mul r21.y, r21.y, c31.w
        add r21.x, r21.x, r21.y
        max r21.x, r21.x, c68.y
        mul r21.x, r21.x, r22.y
        mul r21.x, r21.x, r22.y
        add r21.y, c32.z, -c32.w
        mul r21.y, r21.w, r21.y
        add r21.y, c32.w, r21.y
        max r21.y, r21.y, c84.y
        rcp r9.x, r21.y
        mul r21.y, r20.w, r9.x
        add r21.y, -r21.y, c68.z
        mov_sat r21.y, r21.y
        add r22.y, c32.x, -c32.y
        mul r21.w, r21.w, r22.y
        add r21.w, c32.y, r21.w
        add r22.y, c22.w, -r21.w
        mul r22.x, r22.x, r22.y
        add r21.w, r21.w, r22.x
        mad r21.z, r21.z, c84.z, c70.x
        mov_sat r21.z, r21.z
        mul r21.z, r21.w, r21.z
        add r21.z, c59.w, r21.z
        mul r21.z, r21.z, r21.y
        mul r21.y, r21.z, r21.y
        mul r19.x, r19.y, c70.z
        add r19.x, r19.x, -c58.x
        mul r19.x, r19.x, c58.y
        mov_sat r19.x, r19.x
        mul r19.y, r19.x, c73.x
        add r19.y, -r19.y, c81.w
        mul r19.y, r19.x, r19.y
        mul r19.x, r19.x, r19.y
        add r19.y, r21.x, r21.y
        max r19.y, r19.y, c68.y
        mul r19.y, r19.y, r19.w
        mul r19.x, r19.y, r19.x
        cmp r19.y, -r19.x, c68.y, c68.z
        if_ne r19.y, -r19.y
            mul r19.x, -r19.x, r19.z
            mul r19.x, r19.x, c84.w
            exp r8.w, r19.x
            mov r19.x, -r8.w
            add r19.x, r19.x, c68.z
            mul r19.y, r20.w, c85.x
            mov_sat r19.y, r19.y
            mul r19.z, r19.y, c73.x
            add r19.z, -r19.z, c81.w
            mul r19.z, r19.y, r19.z
            mul r19.y, r19.y, r19.z
            dp3 r19.z, r3.xyzx, c82.xyzx
            cmp r19.z, -r19.z, c68.y, c68.z
            if_ne r19.z, -r19.z
                mov r19.z, r0.w
                mul r19.z, r19.z, r19.x
                mul r21.xyz, r19.z, r3.xyzx
                mul r22.xyz, r20.x, c11.xyzx
                mul r23.xyz, r20.y, c12.xyzx
                add r22.xyz, r22.xyzx, r23.xyzx
                mul r20.xyz, r20.z, c13.xyzx
                add r20.xyz, r22.xyzx, r20.xyzx
                add r20.xyz, r20.xyzx, c14.xyzx
                mul r19.z, r20.x, c70.z
                add r19.z, r19.z, c70.z
                mul r19.w, r20.y, c70.z
                add r19.w, -r19.w, c70.z
                mov r8.y, r19.z
                mov r8.z, r19.w
                mov r19.zw, r8.xxyz
                mul r20.w, c2.w, c70.z
                add r19.zw, r19.xxzw, r20.w
                cmp r22.xy, r19.zwzz, c69.xyxx, c82.xyxx
                max r20.w, r22.x, r22.y
                add r22.xy, -r19.zwzz, c82.xyxx
                cmp r22.xy, r22.xyxx, c69.xyxx, c82.xyxx
                max r21.w, r22.x, r22.y
                max r20.w, r20.w, r21.w
                cmp r21.w, r20.z, c68.y, c68.z
                max r20.w, r20.w, r21.w
                add r21.w, -r20.z, c68.z
                cmp r21.w, r21.w, c68.y, c68.z
                max r20.w, r20.w, r21.w
                mov r21.w, r8.x
                cmp r21.w, -r20.w, r21.w, c68.z
                mov r8.x, r21.w
                add r20.w, -r20.w, c68.z
                if_ne r20.w, -r20.w
                    add r22.xy, r20.xyxx, -c14.xyxx
                    mov r7.z, c2.z
                    mov r7.w, -c2.z
                    mov r22.zw, r7.xxzw
                    mul r22.xy, r22.xyxx, r22.zwzz
                    mul r22.xy, r22.xyxx, c85.yzyy
                    frc r22.xy, r22.xyxx
                    mul r22.zw, r22.xxxy, r22.xxxy
                    mul r23.xy, r22.xyxx, c86.xyxx
                    add r23.xy, -r23.xyxx, c86.zwzz
                    mul r22.zw, r22.xxzw, r23.xxxy
                    mul r20.w, c24.x, c85.w
                    add r20.x, r20.z, -r20.w
                    mov r7.y, c68.y
                    mov r7.x, c68.y
                    rep i0.xyzw
                        mov r20.y, r7.x
                        add r20.y, r20.y, c87.x
                        cmp r20.y, r20.y, c68.y, c68.z
                        add r20.y, -r20.y, c68.z
                        if_ne r20.y, -r20.y
                            break
                        else
                        endif
                        mov r20.y, r7.x
                        mul r20.y, r20.y, c70.z
                        frc r23.xyzw, r20.y
                        add r20.y, r20.y, -r23.x
                        mov r20.z, r7.x
                        mul r20.w, r20.y, c73.x
                        add r20.z, r20.z, -r20.w
                        mov r4.y, r20.z
                        mov r4.w, r20.y
                        mov r20.yz, r4.xywx
                        add r20.yz, r20.xyzx, -r22.xxyx
                        mul r20.w, c2.z, c73.x
                        rcp r9.w, r20.w
                        mul r20.w, r9.w, c80.x
                        mul r20.yz, r20.xyzx, r20.w
                        add r20.yz, r19.xzwx, r20.xyzx
                        mov r6.xyzw, c71.xyzw
                        mov r6.xy, r20.yzyy
                        mov r23.xyzw, r6.xyzw
                        texldl r23.xyzw, r23.xyzw, s3
                        add r20.yz, -r22.xzwx, c82.xxyx
                        mov r24.xy, r4.ywyy
                        add r24.zw, r22.xxzw, -r20.xxyz
                        mul r24.xy, r24.xyxx, r24.zwzz
                        add r20.yz, r20.xyzx, r24.xxyx
                        add r20.w, r23.x, -r20.x
                        cmp r20.w, r20.w, c68.y, c68.z
                        add r20.w, -r20.w, c68.z
                        cmp r20.w, -r20.w, c68.y, c68.z
                        mul r20.w, r20.w, r20.y
                        mul r20.y, r20.w, r20.z
                        mov r20.z, r7.y
                        add r20.y, r20.z, r20.y
                        mov r7.y, r20.y
                        mov r20.y, r7.x
                        add r20.y, r20.y, c68.z
                        mov r7.x, r20.y
                    endrep
                    mov r19.z, r7.y
                    mov r8.x, r19.z
                else
                endif
                mov r19.z, r8.x
                mul r19.y, r19.z, r19.y
                mul r19.yzw, r21.xxyz, r19.y
                mov r20.xyz, r1.yzwy
                add r19.yzw, r20.xxyz, r19.xyzw
                mov r1.yzw, r19.xyzw
            else
            endif
            add r19.x, -r19.x, c68.z
            mov r19.y, r0.w
            mul r19.x, r19.y, r19.x
            mov r0.w, r19.x
        else
        endif
        mov r19.x, r4.x
        add r19.x, r19.x, c68.z
        mov r4.x, r19.x
    endrep
    mov r0.xyz, r1.yzwy
    mov r3.xyz, r1.yzwy
    mov r4.xyz, r1.yzwy
    max r1.x, r3.y, r4.z
    max r0.x, r0.x, r1.x
    max r0.y, c21.z, c87.y
    add r0.x, r0.y, r0.x
    rcp r0.x, r0.x
    mul r0.x, r0.y, r0.x
    mov r1.xyz, r1.yzwy
    mul r0.xyz, r1.xyzx, r0.x
    max r1.xyz, c18.xyzx, c69.xyzx
    mov_sat r3.xyz, c22.xyzx
    mul r1.xyz, r1.xyzx, r3.xyzx
    mul r1.w, c27.x, c87.z
    mul r1.xyz, r1.xyzx, r1.w
    mov r1.w, r0.w
    add r1.w, -r1.w, c68.z
    mul r1.xyz, r1.xyzx, r1.w
    add r0.xyz, r1.xyzx, r0.xyzx
    mov r1.xyz, r0.xyzx
    mov r1.w, r0.w
    mov r0.xyzw, r1.xyzw
    mov r2.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r2.xyzw
