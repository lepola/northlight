ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = -9.99989986e-01, 1.00000000e+00, -5.00000000e-01, 9.99999975e-06
def c71 = 1.20000001e-02, 6.99999975e-04, 3.00000003e-03, 4.88602519e-01
def c72 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c73 = 2.50000000e-01, 2.50000000e-01, 2.50000000e-01, 2.82094777e-01
def c74 = 3.14159274e+00, 2.09439516e+00, 2.09439516e+00, 2.09439516e+00
def c75 = -8.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c76 = 2.22222233e+00, 3.00000000e+00, 4.00000000e+00, 5.00000000e+00
def c77 = 6.00000000e+00, 1.99999996e-02, 3.33333325e+00, 3.33333343e-01
def c78 = 3.33333343e-01, 0.00000000e+00, 6.66666687e-01, 0.00000000e+00
def c79 = 3.18309873e-01, 3.18309873e-01, 3.18309873e-01, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s14
dcl_2d s6
dcl_2d s5
dcl_2d s10
dcl_2d s8
dcl_2d s4
dcl_2d s7
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
add r1.w, r1.z, c70.x
cmp r1.w, r1.w, c68.x, c70.y
add r1.w, -r1.w, c70.y
add r2.x, c30.x, c70.z
cmp r2.x, r2.x, c68.x, c70.y
mov r2.y, r2.z
cmp r2.y, -r2.x, r2.y, c68.x
mov r2.z, r2.y
add r2.x, -r2.x, c70.y
if_ne r2.x, -r2.x
    mov r3.xyzw, c68.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s11
    add r2.x, c0.z, -r3.x
    cmp r2.x, r2.x, c68.x, c70.y
    mul r2.y, c0.z, c0.w
    add r2.w, c0.w, -c0.z
    mul r2.w, r1.z, r2.w
    add r2.w, c0.w, -r2.w
    max r2.w, r2.w, c70.w
    rcp r2.w, r2.w
    mul r2.y, r2.y, r2.w
    mul r2.w, r3.x, c71.y
    max r2.w, r2.w, c71.x
    add r2.y, r2.y, r2.w
    add r2.y, r2.y, -r3.x
    cmp r2.y, r2.y, c68.x, c70.y
    add r2.y, -r2.y, c70.y
    min r2.x, r2.x, r2.y
    add r2.y, -r3.y, c71.z
    cmp r2.y, r2.y, c68.x, c70.y
    min r2.x, r2.x, r2.y
    cmp r2.x, -r2.x, c68.x, r3.x
    mov r2.z, r2.x
else
endif
mov r2.x, r2.z
cmp r2.x, -r2.x, c68.x, c70.y
max r1.w, r1.w, r2.x
mov r2.xyzw, r3.xyzw
cmp r2.xyzw, -r1.w, r2.xyzw, c68.xyzw
mov r3.xyzw, r2.xyzw
add r1.w, -r1.w, c70.y
if_ne r1.w, -r1.w
    mul r1.w, c0.z, c0.w
    add r2.x, c0.w, -c0.z
    mul r1.z, r1.z, r2.x
    add r1.z, c0.w, -r1.z
    max r1.z, r1.z, c70.w
    rcp r1.z, r1.z
    mul r1.z, r1.w, r1.z
    mul r2.xy, c0.xyxx, c69.zwzz
    add r1.xy, r1.xyxx, -r2.xyxx
    mul r1.xy, r1.xyxx, c72.xyxx
    add r1.xy, r1.xyxx, c72.zwzz
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
    mul r2.xyz, r0.xyzx, c73.xyzx
    add r1.xyz, r1.xyzx, r2.xyzx
    add r1.w, c20.w, c70.z
    cmp r1.w, r1.w, c68.x, c70.y
    mov r2.xyzw, r4.xyzw
    cmp r2.xyzw, -r1.w, r2.xyzw, c68.xyzw
    mov r4.xyzw, r2.xyzw
    add r1.w, -r1.w, c70.y
    if_ne r1.w, -r1.w
        rcp r2.x, c19.w
        rcp r2.y, c19.w
        rcp r2.z, c19.w
        mul r2.xyz, r1.xyzx, r2.xyzx
        frc r5.xyzw, r2.xyzx
        add r5.xyz, r2.xyzx, -r5.xyzw
        add r2.xyz, r2.xyzx, -r5.xyzx
        mul r1.w, r0.x, c71.w
        mul r2.w, r0.y, c71.w
        mul r5.w, r0.z, c71.w
        mov r6.x, c73.w
        mov r6.y, r1.w
        mov r6.z, r2.w
        mov r6.w, r5.w
        mul r6.xyzw, r6.xyzw, c74.xyzw
        mov r7.x, c68.x
        mov r7.y, c68.x
        mov r7.z, c68.x
        mov r1.w, c68.x
        mov r2.w, c68.x
        mov r5.w, c68.x
        rep i0.xyzw
            mov r20.w, r5.w
            add r20.w, r20.w, c75.x
            cmp r20.w, r20.w, c68.x, c70.y
            add r20.w, -r20.w, c70.y
            if_ne r20.w, -r20.w
                break
            else
            endif
            mov r20.w, r5.w
            mov r23.z, r5.w
            mul r23.z, r23.z, c69.z
            frc r25.xyzw, r23.z
            add r23.z, r23.z, -r25.x
            mul r23.z, r23.z, c72.x
            add r20.w, r20.w, -r23.z
            mov r23.z, r5.w
            mul r23.z, r23.z, c69.z
            frc r25.xyzw, r23.z
            add r23.z, r23.z, -r25.x
            mov r23.w, r5.w
            mul r23.w, r23.w, c73.x
            frc r25.xyzw, r23.w
            add r23.w, r23.w, -r25.x
            mul r23.w, r23.w, c72.x
            add r23.z, r23.z, -r23.w
            mov r23.w, r5.w
            mul r23.w, r23.w, c73.x
            frc r25.xyzw, r23.w
            add r23.w, r23.w, -r25.x
            mov r17.x, r20.w
            mov r17.y, r23.z
            mov r17.z, r23.w
            mov r25.xyz, r17.xyzx
            add r25.xyz, r5.xyzx, r25.xyzx
            rcp r14.x, c20.x
            rcp r14.y, c20.x
            rcp r14.z, c20.x
            mul r26.xyz, r25.xyzx, r14.xyzx
            frc r27.xyzw, r26.xyzx
            add r26.xyz, r26.xyzx, -r27.xyzw
            mul r26.xyz, c20.x, r26.xyzx
            add r26.xyz, r25.xyzx, -r26.xyzx
            mul r20.w, r26.z, c20.x
            add r20.w, r26.x, r20.w
            mov r8.z, r20.w
            mov r8.w, r26.y
            mov r23.zw, r8.xxzw
            add r23.zw, r23.xxzw, c69.xxzw
            mul r20.w, c20.x, c20.x
            mov r8.x, r20.w
            mov r8.y, c20.x
            mov r27.xy, r8.xyxx
            rcp r20.x, r27.x
            rcp r20.y, r27.y
            mul r23.zw, r23.xxzw, r20.xxxy
            mov r22.xyzw, c68.xyzw
            mov r22.xy, r23.zwzz
            mov r27.xyzw, r22.xyzw
            texldl r27.xyzw, r27.xyzw, s10
            add r28.xyz, r27.xyzx, -r25.xyzx
            abs r28.xyz, r28.xyzx
            add r28.xyz, -r28.xyzx, -r28.xyzx
            cmp r28.xyz, r28.xyzx, c68.xyzx, c75.yzwy
            add r28.xyz, -r28.xyzx, c75.yzwy
            min r20.w, r28.x, r28.y
            min r20.w, r20.w, r28.z
            cmp r24.w, r27.w, c68.x, c70.y
            add r24.w, -r24.w, c70.y
            min r20.w, r20.w, r24.w
            if_ne r20.w, -r20.w
                add r28.xyz, -r2.xyzx, c75.yzwy
                mov r29.xyz, r17.xyzx
                add r30.xyz, r2.xyzx, -r28.xyzx
                mul r29.xyz, r29.xyzx, r30.xyzx
                add r28.xyz, r28.xyzx, r29.xyzx
                mul r20.w, r28.x, r28.y
                mul r20.w, r20.w, r28.z
                add r24.w, c24.w, -r27.w
                mul r24.w, r24.w, c76.x
                mov_sat r24.w, r24.w
                mul r20.w, r20.w, r24.w
                mov r24.w, r2.w
                add r24.w, r24.w, r20.w
                mov r2.w, r24.w
                mul r25.xyz, r25.xyzx, c19.w
                add r25.xyz, r1.xyzx, -r25.xyzx
                dp3 r24.w, r25.xyzx, r25.xyzx
                rsq r17.w, r24.w
                rcp r7.w, r17.w
                abs r27.xyz, r25.xyzx
                add r28.xy, r27.x, -r27.yzyy
                cmp r28.xy, r28.xyxx, c68.xyxx, c75.yzyy
                add r28.xy, -r28.xyxx, c75.yzyy
                min r24.w, r28.x, r28.y
                if_ne r24.w, -r24.w
                    cmp r24.w, r25.x, c68.x, c70.y
                    add r24.w, -r24.w, c70.y
                    cmp r24.w, -r24.w, c70.y, c68.x
                    mov r9.w, r24.w
                else
                    add r24.w, r27.y, -r27.z
                    cmp r24.w, r24.w, c68.x, c70.y
                    add r24.w, -r24.w, c70.y
                    cmp r25.w, r25.y, c68.x, c70.y
                    add r25.w, -r25.w, c70.y
                    cmp r25.w, -r25.w, c76.y, c72.x
                    mov r26.w, r9.w
                    cmp r26.w, -r24.w, r26.w, r25.w
                    mov r9.w, r26.w
                    cmp r25.x, r25.z, c68.x, c70.y
                    add r25.x, -r25.x, c70.y
                    cmp r25.x, -r25.x, c76.w, c76.z
                    cmp r24.w, -r24.w, r25.x, r25.w
                    mov r9.w, r24.w
                endif
                mov r24.w, r9.w
                mul r24.w, r24.w, c20.x
                add r24.w, r26.y, r24.w
                add r24.w, r24.w, c69.z
                mul r25.x, c20.x, c77.x
                rcp r14.w, r25.x
                mul r24.w, r24.w, r14.w
                mov r19.x, r23.z
                mov r19.y, r24.w
                mov r19.z, c68.x
                mov r19.w, c68.x
                mov r25.xyzw, r19.xyzw
                mov r21.xyzw, c68.xyzw
                mov r21.xy, r25.xyxx
                mov r25.xyzw, r21.xyzw
                texldl r25.xyzw, r25.xyzw, s7
                mul r24.w, r25.x, r25.x
                add r24.w, r25.y, -r24.w
                max r24.w, r24.w, c77.y
                add r26.x, r7.w, -r25.x
                max r26.x, r26.x, c68.x
                mul r26.x, r26.x, r26.x
                add r26.x, r24.w, r26.x
                rcp r20.z, r26.x
                mul r24.w, r24.w, r20.z
                mul r24.w, r24.w, r24.w
                mul r24.w, r24.w, r25.z
                mul r20.w, r20.w, r24.w
                mov r18.xyzw, c68.xyzw
                mov r18.xy, r23.zwzz
                mov r26.xyzw, r18.xyzw
                texldl r26.xyzw, r26.xyzw, s4
                dp4 r24.w, r26.xyzw, r6.xyzw
                mov r15.xyzw, c68.xyzw
                mov r15.xy, r23.zwzz
                mov r26.xyzw, r15.xyzw
                texldl r26.xyzw, r26.xyzw, s5
                dp4 r26.x, r26.xyzw, r6.xyzw
                mov r16.xyzw, c68.xyzw
                mov r16.xy, r23.zwzz
                mov r27.xyzw, r16.xyzw
                texldl r27.xyzw, r27.xyzw, s6
                dp4 r26.y, r27.xyzw, r6.xyzw
                mov r24.x, r24.w
                mov r24.y, r26.x
                mov r24.z, r26.y
                add r24.w, c24.w, -r25.w
                mul r24.w, r24.w, c77.z
                mov_sat r24.w, r24.w
                add r25.x, r24.w, c69.x
                cmp r25.x, r25.x, c68.x, c70.y
                if_ne r25.x, -r25.x
                    mul r25.x, r23.z, c77.w
                    mov r23.x, r25.x
                    mov r23.y, r23.w
                    mov r13.x, r25.x
                    mov r13.y, r23.w
                    mov r13.z, c68.x
                    mov r13.w, c68.x
                    mov r25.xyzw, r13.xyzw
                    mov r12.xyzw, c68.xyzw
                    mov r12.xy, r25.xyxx
                    mov r25.xyzw, r12.xyzw
                    texldl r25.xyzw, r25.xyzw, s8
                    dp4 r23.z, r25.xyzw, r6.xyzw
                    mov r25.xy, r23.xyxx
                    add r25.xy, r25.xyxx, c78.xyxx
                    mov r11.xyzw, c68.xyzw
                    mov r11.xy, r25.xyxx
                    mov r25.xyzw, r11.xyzw
                    texldl r25.xyzw, r25.xyzw, s8
                    dp4 r23.w, r25.xyzw, r6.xyzw
                    mov r25.xy, r23.xyxx
                    add r25.xy, r25.xyxx, c78.zwzz
                    mov r10.xyzw, c68.xyzw
                    mov r10.xy, r25.xyxx
                    mov r25.xyzw, r10.xyzw
                    texldl r25.xyzw, r25.xyzw, s8
                    dp4 r25.x, r25.xyzw, r6.xyzw
                    mov r9.x, r23.z
                    mov r9.y, r23.w
                    mov r9.z, r25.x
                    mov r25.xyz, r9.xyzx
                    mov r26.xyz, r24.xyzx
                    add r26.xyz, r26.xyzx, -r25.xyzx
                    mul r26.xyz, r24.w, r26.xyzx
                    add r25.xyz, r25.xyzx, r26.xyzx
                    mov r24.xyz, r25.xyzx
                else
                endif
                mov r25.xyz, r24.xyzx
                max r25.xyz, r25.xyzx, c68.xyzx
                mul r25.xyz, r25.xyzx, r20.w
                mov r26.xyz, r7.xyzx
                add r25.xyz, r26.xyzx, r25.xyzx
                mov r7.xyz, r25.xyzx
                mov r23.z, r1.w
                add r20.w, r23.z, r20.w
                mov r1.w, r20.w
            else
            endif
            mov r20.w, r5.w
            add r20.w, r20.w, c70.y
            mov r5.w, r20.w
        endrep
        mov r1.x, r1.w
        add r1.x, -r1.x, c70.w
        cmp r1.x, r1.x, c68.x, c70.y
        mov r2.xyz, r7.xyzx
        mov r1.y, r1.w
        rcp r1.y, r1.y
        mov r1.yzw, r1.y
        mul r1.yzw, r2.xxyz, r1.xyzw
        mov r2.x, r2.w
        mov_sat r2.x, r2.x
        mov r5.xyz, r1.yzwy
        mov r5.w, r2.x
        mov r2.xyzw, r5.xyzw
        cmp r1.xyzw, -r1.x, c68.xyzw, r2.xyzw
        mov r4.xyzw, r1.xyzw
    else
    endif
    mov r1.xyzw, r4.xyzw
    mul r2.xyz, r1.xyzx, c79.xyzx
    add r2.xyz, r2.xyzx, -c18.xyzx
    mul r2.xyz, r2.xyzx, c20.y
    mul r1.xyz, r2.xyzx, r1.w
    min r2.xyz, r1.xyzx, c68.xyzx
    max r1.xyz, r1.xyzx, c68.xyzx
    mul r0.xyz, r1.xyzx, r0.w
    add r0.xyz, r2.xyzx, r0.xyzx
    add r1.xyz, c18.xyzx, r0.xyzx
    max r1.xyz, r1.xyzx, c68.xyzx
    mul r1.xyz, r1.xyzx, c18.w
    add r0.xyz, r0.xyzx, r1.xyzx
    mov r0.w, c68.x
    mov r3.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r3.xyzw
