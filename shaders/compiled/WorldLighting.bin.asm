ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = 1.50000006e-01, 1.50000006e-01, 1.50000006e-01, 1.00000000e+00
def c71 = -9.99989986e-01, -5.00000000e-01, 9.99999975e-06, 1.20000001e-02
def c72 = 6.99999975e-04, 3.00000003e-03, 2.00000000e+00, -2.00000000e+00
def c73 = -1.00000000e+00, 1.00000000e+00, 7.99999982e-02, 9.76562500e-04
def c74 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, -7.20000029e-01
def c75 = 5.55555534e+00, -3.00000003e-03, -3.00000003e-03, 2.68799996e+00
def c76 = 9.96999979e-01, 9.96999979e-01, 1.07519999e+01, 5.00000000e+00
def c77 = 4.00000000e+00, -5.00000000e-01, -5.00000000e-01, -2.50000000e+01
def c78 = 8.33333397e+00, 9.99999975e-05, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s14
dcl_2d s3
dcl_2d s2
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
mov r2.x, c68.x
mov r2.y, c68.x
mov r2.z, c68.x
mov r2.w, c27.y
mov r3.xyzw, r2.xyzw
max r4.xyz, c18.xyzx, c70.xyzx
mov r4.w, c70.w
add r1.w, r1.z, c71.x
cmp r1.w, r1.w, c68.x, c70.w
add r1.w, -r1.w, c70.w
add r5.x, c30.x, c71.y
cmp r5.x, r5.x, c68.x, c70.w
mov r5.y, r5.z
cmp r5.y, -r5.x, r5.y, c68.x
mov r5.z, r5.y
add r5.x, -r5.x, c70.w
if_ne r5.x, -r5.x
    mov r6.xyzw, c68.xyzw
    mov r6.xy, r1.xyxx
    texldl r6.xyzw, r6.xyzw, s11
    add r5.x, c0.z, -r6.x
    cmp r5.x, r5.x, c68.x, c70.w
    mul r5.y, c0.z, c0.w
    add r5.w, c0.w, -c0.z
    mul r5.w, r1.z, r5.w
    add r5.w, c0.w, -r5.w
    max r5.w, r5.w, c71.z
    rcp r5.w, r5.w
    mul r5.y, r5.y, r5.w
    mul r5.w, r6.x, c72.x
    max r5.w, r5.w, c71.w
    add r5.y, r5.y, r5.w
    add r5.y, r5.y, -r6.x
    cmp r5.y, r5.y, c68.x, c70.w
    add r5.y, -r5.y, c70.w
    min r5.x, r5.x, r5.y
    add r5.y, -r6.y, c72.y
    cmp r5.y, r5.y, c68.x, c70.w
    min r5.x, r5.x, r5.y
    cmp r5.x, -r5.x, c68.x, r6.x
    mov r5.z, r5.x
else
endif
mov r5.x, r5.z
cmp r5.x, -r5.x, c68.x, c70.w
max r1.w, r1.w, r5.x
mov r5.xyzw, r6.xyzw
cmp r2.xyzw, -r1.w, r5.xyzw, r2.xyzw
mov r6.xyzw, r2.xyzw
mov r2.xyzw, r5.xyzw
cmp r2.xyzw, -r1.w, r2.xyzw, r4.xyzw
mov r5.xyzw, r2.xyzw
add r1.w, -r1.w, c70.w
if_ne r1.w, -r1.w
    mul r1.w, c0.z, c0.w
    add r2.x, c0.w, -c0.z
    mul r1.z, r1.z, r2.x
    add r1.z, c0.w, -r1.z
    max r1.z, r1.z, c71.z
    rcp r1.z, r1.z
    mul r1.z, r1.w, r1.z
    mul r2.xy, c0.xyxx, c69.zwzz
    add r1.xy, r1.xyxx, -r2.xyxx
    mul r1.xy, r1.xyxx, c72.zwzz
    add r1.xy, r1.xyxx, c73.xyxx
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
    mov r1.w, c70.w
    dp3 r2.x, c17.xyzx, c74.xyzx
    cmp r2.x, -r2.x, c68.x, c70.w
    if_ne r2.x, -r2.x
        mul r2.x, r0.w, c73.z
        mul r2.xyz, r0.xyzx, r2.x
        add r2.xyz, r1.xyzx, r2.xyzx
        dp3 r2.w, r0.xyzx, c16.xyzx
        abs r4.xyz, r2.w
        mov_sat r4.w, r4.x
        add r4.w, -r4.w, c70.w
        mul r4.w, r4.w, r0.w
        mul r4.w, r4.w, c72.z
        mul r4.w, r4.w, c73.w
        mul r7.x, r4.w, c2.y
        mul r7.xyz, r0.xyzx, r7.x
        add r7.xyz, r2.xyzx, r7.xyzx
        mul r8.xyz, r7.x, c7.xyzx
        mul r9.xyz, r7.y, c8.xyzx
        add r8.xyz, r8.xyzx, r9.xyzx
        mul r7.xyz, r7.z, c9.xyzx
        add r7.xyz, r8.xyzx, r7.xyzx
        add r7.xyz, r7.xyzx, c10.xyzx
        mul r4.w, r4.w, c2.z
        mul r8.xyz, r0.xyzx, r4.w
        add r2.xyz, r2.xyzx, r8.xyzx
        mul r8.xyz, r2.x, c11.xyzx
        mul r9.xyz, r2.y, c12.xyzx
        add r8.xyz, r8.xyzx, r9.xyzx
        mul r2.xyz, r2.z, c13.xyzx
        add r2.xyz, r8.xyzx, r2.xyzx
        add r2.xyz, r2.xyzx, c14.xyzx
        abs r8.xy, r7.xyxx
        max r4.w, r8.x, r8.y
        add r4.w, r4.w, c74.w
        mul r4.w, r4.w, c75.x
        mov_sat r4.w, r4.w
        mov r8.x, c7.x
        mov r8.y, c8.x
        mov r8.z, c9.x
        dp3 r7.w, r0.xyzx, r8.xyzx
        mov r8.x, c7.y
        mov r8.y, c8.y
        mov r8.z, c9.y
        dp3 r8.x, r0.xyzx, r8.xyzx
        mov r8.y, r7.w
        mov r8.z, -r8.x
        mov r8.xy, r8.yzyy
        mul r8.xy, r8.xyxx, c2.y
        mul r7.w, r0.w, c72.z
        mul r7.w, r7.w, c35.x
        cmp r2.w, r2.w, c68.x, c70.w
        max r8.z, r4.y, c70.x
        max r4.x, r4.z, c70.x
        cmp r2.w, -r2.w, r4.x, -r8.z
        rcp r2.w, r2.w
        mul r2.w, r7.w, r2.w
        mul r4.xy, r8.xyxx, r2.w
        mov r2.w, c68.x
        mov r4.z, c68.x
        rep i0.xyzw
            mov r13.z, r4.z
            add r13.z, r13.z, c72.w
            cmp r13.z, r13.z, c68.x, c70.w
            add r13.z, -r13.z, c70.w
            if_ne r13.z, -r13.z
                break
            else
            endif
            mov r13.z, r4.z
            abs r13.z, r13.z
            add r13.z, -r13.z, -r13.z
            cmp r13.z, r13.z, c68.x, c70.w
            add r13.z, -r13.z, c70.w
            cmp r13.w, -r13.z, c2.z, c2.y
            add r14.x, -r4.w, c70.w
            cmp r14.x, -r13.z, r4.w, r14.x
            cmp r14.y, -r14.x, c68.x, c70.w
            if_ne r14.y, -r14.y
                cmp r14.yzw, -r13.z, r2.xxyz, r7.xxyz
                mul r15.xy, r4.xyxx, r13.w
                mul r13.w, r14.y, c69.z
                add r13.w, r13.w, c69.z
                mul r15.z, r14.z, c69.z
                add r15.z, -r15.z, c69.z
                mov r10.x, r13.w
                mov r10.y, r15.z
                mov r15.zw, r10.xxxy
                mul r13.w, c2.w, c69.z
                add r15.zw, r15.xxzw, r13.w
                add r16.xy, r15.zwzz, c75.yzyy
                cmp r16.xy, r16.xyxx, c68.xyxx, c74.xyxx
                max r13.w, r16.x, r16.y
                add r16.xy, -r15.zwzz, c76.xyxx
                cmp r16.xy, r16.xyxx, c68.xyxx, c74.xyxx
                max r16.x, r16.x, r16.y
                max r13.w, r13.w, r16.x
                cmp r16.x, -r14.w, c68.x, c70.w
                add r16.x, -r16.x, c70.w
                max r13.w, r13.w, r16.x
                add r16.x, r14.w, c69.x
                cmp r16.x, r16.x, c68.x, c70.w
                add r16.x, -r16.x, c70.w
                max r13.w, r13.w, r16.x
                mov r16.x, r10.z
                cmp r16.x, -r13.w, r16.x, c70.w
                mov r10.z, r16.x
                add r13.w, -r13.w, c70.w
                if_ne r13.w, -r13.w
                    cmp r13.w, -r13.z, c72.z, c70.w
                    mul r13.w, c24.x, r13.w
                    add r13.w, r14.w, -r13.w
                    cmp r16.x, -r13.z, c76.z, c75.w
                    mul r16.x, r16.x, c35.x
                    rcp r10.w, c2.w
                    mul r16.x, r16.x, r10.w
                    max r15.xy, r15.xyxx, -r16.x
                    min r15.xy, r15.xyxx, r16.x
                    cmp r16.x, -r13.z, c77.x, c76.w
                    mul r16.y, r16.x, c69.z
                    rcp r11.x, c2.w
                    rcp r11.y, c2.w
                    mul r16.zw, r15.xxzw, r11.xxxy
                    add r16.zw, r16.xxzw, c77.xxyz
                    cmp r17.x, -r13.z, c68.x, c69.z
                    add r17.xy, r16.zwzz, r17.x
                    frc r18.xyzw, r17.xyxx
                    add r17.xy, r17.xyxx, -r18.xyzw
                    add r16.zw, r16.xxzw, -r17.xxxy
                    cmp r17.z, -r13.z, c69.x, c72.w
                    mov r11.z, c68.x
                    mov r11.w, c68.x
                    mov r12.x, c68.x
                    rep i0.xyzw
                        mov r17.w, r12.x
                        add r17.w, r17.w, c77.w
                        cmp r17.w, r17.w, c68.x, c70.w
                        add r17.w, -r17.w, c70.w
                        if_ne r17.w, -r17.w
                            break
                        else
                        endif
                        mov r17.w, r12.x
                        mul r18.x, r16.x, r16.x
                        add r17.w, r17.w, -r18.x
                        cmp r17.w, r17.w, c68.x, c70.w
                        add r17.w, -r17.w, c70.w
                        if_ne r17.w, -r17.w
                            break
                        else
                        endif
                        mov r17.w, r12.x
                        add r17.w, r17.w, c69.z
                        rcp r12.y, r16.x
                        mul r17.w, r17.w, r12.y
                        frc r18.xyzw, r17.w
                        add r17.w, r17.w, -r18.x
                        mov r18.x, r12.x
                        mul r18.y, r17.w, r16.x
                        add r18.x, r18.x, -r18.y
                        mov r12.z, r18.x
                        mov r12.w, r17.w
                        mov r18.xy, r12.zwzz
                        add r18.xy, r18.xyxx, r17.z
                        add r18.zw, r17.xxxy, r18.xxxy
                        add r18.zw, r18.xxzw, c69.xxzw
                        mul r18.zw, r18.xxzw, c2.w
                        add r18.xy, r18.xyxx, -r16.zwzz
                        abs r18.xy, r18.xyxx
                        add r18.xy, r16.y, -r18.xyxx
                        max r18.xy, r18.xyxx, c68.xyxx
                        mul r17.w, r18.x, r18.y
                        add r18.xy, r18.zwzz, -r15.zwzz
                        dp2add r18.x, r15.xyxx, r18.xyxx, c68.x
                        add r18.x, r13.w, r18.x
                        mov r13.x, c68.x
                        if_ne r13.z, -r13.z
                            mov r9.xyzw, c68.xyzw
                            mov r9.xy, r18.zwzz
                            mov r19.xyzw, r9.xyzw
                            texldl r19.xyzw, r19.xyzw, s2
                            mov r7.w, r19.x
                            mov r13.x, c70.w
                        else
                        endif
                        mov r18.y, r13.x
                        add r18.y, -r18.y, c70.w
                        if_ne r18.y, -r18.y
                            mov r8.xyzw, c68.xyzw
                            mov r8.xy, r18.zwzz
                            mov r19.xyzw, r8.xyzw
                            texldl r19.xyzw, r19.xyzw, s3
                            mov r7.w, r19.x
                            mov r13.x, c70.w
                        else
                        endif
                        mov r18.y, r7.w
                        add r18.x, r18.y, -r18.x
                        cmp r18.x, r18.x, c68.x, c70.w
                        add r18.x, -r18.x, c70.w
                        cmp r18.x, -r18.x, c68.x, c70.w
                        mul r18.x, r18.x, r17.w
                        mov r18.y, r11.z
                        add r18.x, r18.y, r18.x
                        mov r11.z, r18.x
                        mov r18.x, r11.w
                        add r17.w, r18.x, r17.w
                        mov r11.w, r17.w
                        mov r17.w, r12.x
                        add r17.w, r17.w, c70.w
                        mov r12.x, r17.w
                    endrep
                    abs r13.zw, r14.xxyz
                    max r13.z, r13.z, r13.w
                    add r13.z, -r13.z, c70.w
                    mul r13.z, r13.z, c78.x
                    mov_sat r13.z, r13.z
                    mov r13.w, r11.z
                    mov r14.y, r11.w
                    max r14.y, r14.y, c78.y
                    rcp r13.y, r14.y
                    mul r13.w, r13.w, r13.y
                    add r13.w, r13.w, c69.x
                    mul r13.z, r13.z, r13.w
                    add r13.z, r13.z, c70.w
                    mov r10.z, r13.z
                else
                endif
                mov r13.z, r10.z
                mul r13.z, r13.z, r14.x
                mov r13.w, r2.w
                add r13.z, r13.w, r13.z
                mov r2.w, r13.z
            else
            endif
            mov r13.z, r4.z
            add r13.z, r13.z, c70.w
            mov r4.z, r13.z
        endrep
        mov r2.x, r2.w
        mov r1.w, r2.x
    else
    endif
    mov r2.x, r1.w
    add r2.x, r2.x, c69.x
    mul r2.x, c20.z, r2.x
    add r2.x, r2.x, c70.w
    dp3 r2.y, r0.xyzx, c28.xyzx
    mov_sat r2.z, r2.y
    mul r4.xyz, c29.xyzx, r2.z
    mul r7.xyz, r4.xyzx, c27.y
    mov r2.z, r1.w
    mov r2.w, r2.x
    cmp r4.w, -c59.z, c68.x, c70.w
    if_ne r4.w, -r4.w
        mov r4.w, r1.w
        add r7.w, -c59.z, c70.w
        add r4.w, r4.w, c69.x
        mul r4.w, r7.w, r4.w
        add r4.w, r4.w, c70.w
        mov r2.z, r4.w
        mov r4.w, r1.w
        mul r7.w, c20.z, r7.w
        add r4.w, r4.w, c69.x
        mul r4.w, r7.w, r4.w
        add r4.w, r4.w, c70.w
        mov r2.w, r4.w
    else
    endif
    dp3 r0.x, r0.xyzx, c16.xyzx
    mov_sat r0.x, r0.x
    mul r0.xyz, c17.xyzx, r0.x
    mov r0.w, r2.z
    mul r0.xyz, r0.xyzx, r0.w
    mov_sat r0.w, r2.y
    mul r8.xyz, c17.xyzx, r0.w
    mov r0.w, r2.w
    mul r2.yzw, r8.xxyz, r0.w
    add r0.xyz, r0.xyzx, -r2.yzwy
    mul r0.xyz, c16.w, r0.xyzx
    add r0.xyz, r2.yzwy, r0.xyzx
    add r0.xyz, r0.xyzx, -r7.xyzx
    mov r0.w, r1.w
    add r0.w, r0.w, -r2.x
    mul r0.w, c16.w, r0.w
    add r0.w, r2.x, r0.w
    mul r0.w, r0.w, c27.y
    mov r7.xyz, r0.xyzx
    mov r7.w, r0.w
    mov r3.xyzw, r7.xyzw
    add r0.w, c24.z, c72.w
    abs r0.w, r0.w
    add r0.w, -r0.w, -r0.w
    cmp r0.w, r0.w, c68.x, c70.w
    add r0.w, -r0.w, c70.w
    cmp r0.xyz, -r0.w, r0.xyzx, c68.xyzx
    mov r3.xyz, r0.xyzx
    mul r0.xyz, r1.x, c11.xyzx
    mul r2.yzw, r1.y, c12.xxyz
    add r0.xyz, r0.xyzx, r2.yzwy
    mul r1.xyz, r1.z, c13.xyzx
    add r0.xyz, r0.xyzx, r1.xyzx
    add r0.xyz, r0.xyzx, c14.xyzx
    abs r1.xy, r0.xyxx
    max r0.w, r1.x, r1.y
    add r0.w, -r0.w, c70.w
    cmp r0.w, r0.w, c68.x, c70.w
    add r0.w, -r0.w, c70.w
    cmp r1.x, r0.z, c68.x, c70.w
    add r1.x, -r1.x, c70.w
    min r0.w, r0.w, r1.x
    add r0.x, -r0.z, c70.w
    cmp r0.x, r0.x, c68.x, c70.w
    add r0.x, -r0.x, c70.w
    min r0.x, r0.w, r0.x
    cmp r0.x, -r0.x, c68.x, c70.w
    add r0.yzw, r4.xxyz, c18.xxyz
    max r0.yzw, r0.xyzw, c70.xxyz
    mov r1.x, r1.w
    add r1.x, r1.x, -r2.x
    mul r1.x, c16.w, r1.x
    add r1.x, r2.x, r1.x
    mul r0.x, r1.x, r0.x
    mov r1.xyz, r0.yzwy
    mov r1.w, r0.x
    mov r0.xyzw, r1.xyzw
    mov r1.xyzw, r3.xyzw
    mov r6.xyzw, r1.xyzw
    mov r5.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r6.xyzw
mov oC1.xyzw, r5.xyzw
