ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = -5.00000000e-01, 1.00000000e+00, 9.99999975e-06, 1.20000001e-02
def c71 = 6.99999975e-04, 3.00000003e-03, -9.99989986e-01, 1.00000002e+20
def c72 = -5.00000000e-01, -5.00000000e-01, -4.00000000e+00, 2.00000000e+00
def c73 = 1.00000000e+00, 1.00000000e+00, 1.50000006e-01, 9.99999978e-03
def c74 = 1.44269502e+00, 2.00000003e-01, 1.99999996e-02, -1.99999996e-02
def c75 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 9.99999975e-05
def c76 = 1.50000006e-01, 1.50000006e-01, 1.50000006e-01, -2.00000000e+00
def c77 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, 3.00000000e+00
def c78 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c79 = 9.99999996e-13, 9.99999997e-07, 6.39999986e-01, 1.36000001e+00
def c80 = 1.20000005e+00, -1.50000000e+00, 2.50000000e-01, -3.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s12
dcl_2d s1
dcl_2d s9
dcl_2d s8
dcl_2d s13
dcl_2d s0
dcl_2d s11
mov r0.xyzw, c68.xyzw
mov r0.xy, v0.xyxx
texldl r0.xyzw, r0.xyzw, s0
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
add r2.x, r2.x, -c1.w
mul r2.x, r2.x, c2.x
mov_sat r2.x, r2.x
add r2.y, c30.x, c70.x
cmp r2.y, r2.y, c68.x, c70.y
mov r2.z, r2.w
cmp r2.z, -r2.y, r2.z, c68.x
mov r2.w, r2.z
add r2.z, -r2.y, c70.y
if_ne r2.z, -r2.z
    mov r3.xyzw, c68.xyzw
    mov r3.xy, r1.xyxx
    texldl r3.xyzw, r3.xyzw, s11
    add r4.x, c0.z, -r3.x
    cmp r4.x, r4.x, c68.x, c70.y
    mul r4.y, c0.z, c0.w
    add r4.z, c0.w, -c0.z
    mul r4.z, r2.x, r4.z
    add r4.z, c0.w, -r4.z
    max r4.z, r4.z, c70.z
    rcp r4.z, r4.z
    mul r4.y, r4.y, r4.z
    mul r4.z, r3.x, c71.x
    max r4.z, r4.z, c70.w
    add r4.y, r4.y, r4.z
    add r4.y, r4.y, -r3.x
    cmp r4.y, r4.y, c68.x, c70.y
    add r4.y, -r4.y, c70.y
    min r4.x, r4.x, r4.y
    add r4.y, -r3.y, c71.y
    cmp r4.y, r4.y, c68.x, c70.y
    min r4.x, r4.x, r4.y
    cmp r3.x, -r4.x, c68.x, r3.x
    mov r2.w, r3.x
else
endif
mov r3.x, r2.w
cmp r3.y, -r3.x, c68.x, c70.y
mul r3.z, c0.z, c0.w
add r3.w, c0.w, -c0.z
mul r4.x, r2.x, r3.w
add r4.x, c0.w, -r4.x
max r4.x, r4.x, c70.z
rcp r4.y, r4.x
mul r4.y, r3.z, r4.y
cmp r3.x, -r3.y, r4.y, r3.x
mul r4.y, r3.x, c1.z
mad r4.y, r4.y, c25.x, c25.y
max r4.y, r4.y, c68.x
log r4.y, r4.y
mul r4.y, c25.z, r4.y
exp r4.y, r4.y
mov_sat r4.y, r4.y
add r4.y, r4.y, c69.x
mad r4.y, c25.w, r4.y, c70.y
add r2.x, r2.x, c71.z
cmp r2.x, r2.x, c68.x, c70.y
add r3.y, -r3.y, c70.y
min r4.z, r2.x, r3.y
mov r5.x, c68.x
mov r5.y, c68.x
mov r5.z, c68.x
mov r6.x, c68.x
mov r6.y, c68.x
mov r6.z, c68.x
mov r4.w, c68.x
mov r5.w, c68.x
mov r6.w, c71.w
mov r7.x, c68.x
mov r7.y, c68.x
mov r7.z, c68.x
mov r7.w, c68.x
mov r8.x, c68.x
mov r8.y, c68.x
mov r8.z, c68.x
mov r8.w, c68.x
mov r9.x, c70.y
mov r10.x, c68.x
mov r10.y, c68.x
mov r10.z, c68.x
mov r10.w, c68.x
mov r11.x, c68.x
mov r11.y, c68.x
mov r11.z, c68.x
mov r11.w, c70.y
mov r9.y, c68.x
mov r9.z, c71.w
mul r12.xy, v0.xyxx, c33.zwzz
add r12.xy, r12.xyxx, c72.xyxx
frc r13.xyzw, r12.xyxx
add r12.zw, r12.xxxy, -r13.xxxy
add r12.xy, r12.xyxx, -r12.zwzz
rcp r4.x, r4.x
mul r4.x, r3.z, r4.x
mov r9.w, c68.x
rep i0.xyzw
    mov r20.w, r9.w
    add r20.w, r20.w, c72.z
    cmp r20.w, r20.w, c68.x, c70.y
    add r20.w, -r20.w, c70.y
    if_ne r20.w, -r20.w
        break
    else
    endif
    mov r20.w, r9.w
    mul r20.w, r20.w, c69.z
    frc r22.xyzw, r20.w
    add r20.w, r20.w, -r22.x
    mov r22.x, r9.w
    mul r22.y, r20.w, c72.w
    add r22.x, r22.x, -r22.y
    mov r15.x, r22.x
    mov r15.y, r20.w
    mov r22.xy, r15.xyxx
    add r22.xy, r12.zwzz, r22.xyxx
    add r22.zw, c33.xxzw, c69.xxxy
    max r22.xy, r22.xyxx, c68.xyxx
    min r22.xy, r22.xyxx, r22.zwzz
    add r22.xy, r22.xyxx, c69.zwzz
    rcp r20.y, c33.z
    rcp r20.z, c33.w
    mul r22.xy, r22.xyxx, r20.yzyy
    mul r22.zw, r22.xxxy, c33.xxxy
    frc r23.xyzw, r22.zwzz
    add r22.zw, r22.xxzw, -r23.xxxy
    max r22.zw, r22.xxzw, c68.xxxy
    min r22.zw, r22.xxzw, r1.xxzw
    add r22.zw, r22.xxzw, c69.xxzw
    mul r22.zw, r22.xxzw, c0.xxxy
    mov r13.xyzw, c68.xyzw
    mov r13.xy, r22.zwzz
    mov r23.xyzw, r13.xyzw
    texldl r23.xyzw, r23.xyzw, s1
    add r20.w, r23.x, -c1.w
    mul r20.w, r20.w, c2.x
    mov_sat r20.w, r20.w
    add r23.xy, -r12.xyxx, c73.xyxx
    mov r23.zw, r15.xxxy
    add r24.xy, r12.xyxx, -r23.xyxx
    mul r23.zw, r23.xxzw, r24.xxxy
    add r23.xy, r23.xyxx, r23.zwzz
    if_ne r4.z, -r4.z
        mul r23.z, r20.w, r3.w
        add r23.z, c0.w, -r23.z
        max r23.z, r23.z, c70.z
        rcp r14.x, r23.z
        mul r23.z, r3.z, r14.x
        add r23.z, r23.z, -r4.x
        abs r23.z, r23.z
        mul r23.w, r23.x, r23.y
        mul r24.x, r4.x, c73.w
        max r24.x, r24.x, c73.z
        rcp r14.y, r24.x
        mul r24.x, -r23.z, r14.y
        mul r24.x, r24.x, c74.x
        exp r14.z, r24.x
        mul r23.w, r23.w, r14.z
        mov r21.xyzw, c68.xyzw
        mov r21.xy, r22.xyxx
        mov r24.xyzw, r21.xyzw
        texldl r24.xyzw, r24.xyzw, s8
        mov r16.xyzw, c68.xyzw
        mov r16.xy, r22.xyxx
        mov r25.xyzw, r16.xyzw
        texldl r25.xyzw, r25.xyzw, s12
        mov r17.xyzw, c68.xyzw
        mov r17.xy, r22.xyxx
        mov r26.xyzw, r17.xyzw
        texldl r26.xyzw, r26.xyzw, s10
        mul r27.xyz, r24.xyzx, r23.w
        mov r28.xyz, r6.xyzx
        add r27.xyz, r28.xyzx, r27.xyzx
        mov r6.xyz, r27.xyzx
        mul r27.x, r24.w, r23.w
        mov r27.y, r4.w
        add r27.x, r27.y, r27.x
        mov r4.w, r27.x
        mul r27.xyz, r25.xyzx, r23.w
        mov r28.xyz, r5.xyzx
        add r27.xyz, r28.xyzx, r27.xyzx
        mov r5.xyz, r27.xyzx
        mul r27.x, r26.w, r23.w
        mov r27.y, r8.w
        add r27.x, r27.y, r27.x
        mov r8.w, r27.x
        mov r27.x, r5.w
        add r23.w, r27.x, r23.w
        mov r5.w, r23.w
        mov r23.w, r6.w
        add r23.w, r23.z, -r23.w
        cmp r23.w, r23.w, c68.x, c70.y
        mov r27.x, r6.w
        cmp r23.z, -r23.w, r27.x, r23.z
        mov r6.w, r23.z
        mov r27.xyzw, r7.xyzw
        cmp r24.xyzw, -r23.w, r27.xyzw, r24.xyzw
        mov r7.xyzw, r24.xyzw
        mov r24.xyz, r8.xyzx
        cmp r24.xyz, -r23.w, r24.xyzx, r25.xyzx
        mov r8.xyz, r24.xyzx
        mov r23.z, r9.x
        cmp r23.z, -r23.w, r23.z, r26.w
        mov r9.x, r23.z
    else
    endif
    mov r23.z, r2.w
    cmp r23.z, -r2.y, r23.z, c68.x
    mov r2.w, r23.z
    if_ne r2.z, -r2.z
        mov r18.xyzw, c68.xyzw
        mov r18.xy, r22.zwzz
        mov r24.xyzw, r18.xyzw
        texldl r24.xyzw, r24.xyzw, s11
        add r22.z, c0.z, -r24.x
        cmp r22.z, r22.z, c68.x, c70.y
        mul r22.w, r20.w, r3.w
        add r22.w, c0.w, -r22.w
        max r22.w, r22.w, c70.z
        rcp r14.w, r22.w
        mul r22.w, r3.z, r14.w
        mul r23.z, r24.x, c71.x
        max r23.z, r23.z, c70.w
        add r22.w, r22.w, r23.z
        add r22.w, r22.w, -r24.x
        cmp r22.w, r22.w, c68.x, c70.y
        add r22.w, -r22.w, c70.y
        min r22.z, r22.z, r22.w
        add r22.w, -r24.y, c71.y
        cmp r22.w, r22.w, c68.x, c70.y
        min r22.z, r22.z, r22.w
        cmp r22.z, -r22.z, c68.x, r24.x
        mov r2.w, r22.z
    else
    endif
    mov r22.z, r2.w
    cmp r22.w, -r22.z, c68.x, c70.y
    mul r20.w, r20.w, r3.w
    add r20.w, c0.w, -r20.w
    max r20.w, r20.w, c70.z
    rcp r15.z, r20.w
    mul r20.w, r3.z, r15.z
    cmp r20.w, -r22.w, r20.w, r22.z
    add r20.w, r20.w, -r3.x
    abs r20.w, r20.w
    mov r19.xyzw, c68.xyzw
    mov r19.xy, r22.xyxx
    mov r22.xyzw, r19.xyzw
    texldl r22.xyzw, r22.xyzw, s9
    mul r23.x, r23.x, r23.y
    mul r23.y, r3.x, c74.z
    max r23.y, r23.y, c74.y
    rcp r15.w, r23.y
    mul r23.y, -r20.w, r15.w
    mul r23.y, r23.y, c74.x
    exp r20.x, r23.y
    mul r23.x, r23.x, r20.x
    mul r24.xyzw, r22.xyzw, r23.x
    mov r25.xyzw, r10.xyzw
    add r24.xyzw, r25.xyzw, r24.xyzw
    mov r10.xyzw, r24.xyzw
    mov r23.y, r9.y
    add r23.x, r23.y, r23.x
    mov r9.y, r23.x
    mov r23.x, r9.z
    add r23.x, r20.w, -r23.x
    cmp r23.x, r23.x, c68.x, c70.y
    mov r23.y, r9.z
    cmp r20.w, -r23.x, r23.y, r20.w
    mov r9.z, r20.w
    mov r24.xyzw, r11.xyzw
    cmp r22.xyzw, -r23.x, r24.xyzw, r22.xyzw
    mov r11.xyzw, r22.xyzw
    mov r20.w, r9.w
    add r20.w, r20.w, c70.y
    mov r9.w, r20.w
endrep
mov r1.z, r9.y
add r1.z, -r1.z, c70.z
cmp r1.z, r1.z, c68.x, c70.y
mov r1.w, r9.y
rcp r1.w, r1.w
mov r12.xyzw, r1.w
mul r10.xyzw, r10.xyzw, r12.xyzw
cmp r10.xyzw, -r1.z, r11.xyzw, r10.xyzw
mov r1.z, r5.w
add r1.z, r1.z, c74.w
cmp r1.z, r1.z, c68.x, c70.y
mov r1.w, r9.x
mov r2.y, r8.w
mov r2.z, r5.w
rcp r2.z, r2.z
mul r2.y, r2.y, r2.z
cmp r1.z, -r1.z, r2.y, r1.w
cmp r1.z, -r4.z, c70.y, r1.z
mov r9.xyzw, c68.xyzw
mov r9.xy, v0.xyxx
texldl r9.xyzw, r9.xyzw, s10
mul r2.yzw, r0.xxyz, r1.z
mov_sat r11.xyz, r2.yzwy
add r11.xyz, -r11.xyzx, c75.xyzx
mad r2.yzw, r9.xxyz, r11.xxyz, r2.xyzw
mov_sat r2.yzw, r2.xyzw
mov r9.xyz, r2.yzwy
add r1.z, -r4.y, c70.y
mul r11.xyz, r1.z, c26.xyzx
min r11.xyz, r11.xyzx, r2.yzwy
if_ne r4.z, -r4.z
    mov r1.z, r5.w
    add r1.z, r1.z, c74.w
    cmp r1.z, r1.z, c68.x, c70.y
    mov r12.xyzw, r7.xyzw
    cmp r6.xyz, -r1.z, r6.xyzx, r12.xyzx
    mov r1.w, r4.w
    cmp r1.w, -r1.z, r1.w, r7.w
    mov r4.xzw, r8.xxyz
    cmp r4.xzw, -r1.z, r5.xxyz, r4.xxzw
    mov r3.z, r5.w
    cmp r1.z, -r1.z, r3.z, c70.y
    max r1.z, r1.z, c75.w
    rcp r1.z, r1.z
    mov r5.xyz, r1.z
    mul r5.xyz, r6.xyzx, r5.xyzx
    mul r1.w, r1.w, r1.z
    mov r6.xyz, r1.z
    mul r4.xzw, r4.xxzw, r6.xxyz
    max r4.xzw, r4.xxzw, c76.xxyz
    add r6.xyz, r2.yzwy, -r11.xyzx
    max r6.xyz, r6.xyzx, c68.xyzx
    rcp r7.x, r4.x
    rcp r7.y, r4.z
    rcp r7.z, r4.w
    mul r4.xzw, r6.xxyz, r7.xxyz
    min r4.xyz, r4.xzwx, r4.y
    mad r4.xyz, r4.xyzx, r5.xyzx, r2.yzwy
    mov r9.xyz, r4.xyzx
    mad r2.yzw, c77.xxyz, r6.xxyz, r2.xyzw
    max r2.yzw, r4.xxyz, r2.xyzw
    mov r9.xyz, r2.yzwy
    add r1.z, c24.z, c69.x
    abs r1.z, r1.z
    add r1.z, -r1.z, -r1.z
    cmp r1.z, r1.z, c68.x, c70.y
    add r1.z, -r1.z, c70.y
    cmp r2.yzw, -r1.z, r2.xyzw, r1.w
    mov r9.xyz, r2.yzwy
    add r1.z, c24.z, c76.w
    abs r1.z, r1.z
    add r1.z, -r1.z, -r1.z
    cmp r1.z, r1.z, c68.x, c70.y
    add r1.z, -r1.z, c70.y
    add r4.xyz, c18.xyzx, r5.xyzx
    max r4.xyz, r4.xyzx, c68.xyzx
    cmp r2.yzw, -r1.z, r2.xyzw, r4.xxyz
    mov r9.xyz, r2.yzwy
else
endif
add r1.z, c24.z, c70.x
cmp r1.z, r1.z, c68.x, c70.y
if_ne r1.z, -r1.z
    mov r2.yzw, r9.xxyz
    mov r4.xyzw, c68.xyzw
    mov r4.xy, v0.xyxx
    texldl r4.xyzw, r4.xyzw, s13
    mov r5.xyz, r9.xyzx
    add r1.z, -r2.x, c70.y
    min r1.z, r1.z, r3.y
    mov r1.w, c68.x
    add r2.x, r3.x, -c57.y
    mul r2.x, r2.x, c57.z
    mov_sat r2.x, r2.x
    cmp r1.z, -r1.z, r2.x, c70.y
    cmp r2.x, -r1.z, c68.x, c70.y
    add r2.x, -r2.x, c70.y
    cmp r3.x, -c34.w, c68.x, c70.y
    add r3.x, -r3.x, c70.y
    max r2.x, r2.x, r3.x
    if_ne r2.x, -r2.x
        mov r3.xyz, r5.xyzx
        mov r1.w, c70.y
    else
    endif
    add r1.w, -r1.w, c70.y
    if_ne r1.w, -r1.w
        mul r6.xy, c0.xyxx, c69.zwzz
        add r1.xy, r1.xyxx, -r6.xyxx
        mul r1.xy, r1.xyxx, c78.xyxx
        add r1.xy, r1.xyxx, c78.zwzz
        rcp r6.x, c1.x
        rcp r6.y, c1.y
        mul r1.xy, r1.xyxx, r6.xyxx
        mov r6.xy, r1.xyxx
        mov r6.z, c1.z
        mul r7.xyz, r1.x, c3.xyzx
        mul r1.xyw, r1.y, c4.xyxz
        add r1.xyw, r7.xyxz, r1.xyxw
        mul r7.xyz, c1.z, c5.xyzx
        add r1.xyw, r1.xyxw, r7.xyxz
        mov r7.xyz, r6.xyzx
        dp3 r2.x, r7.xyzx, r6.xyzx
        rsq r2.x, r2.x
        mul r2.x, r1.w, r2.x
        max r2.x, r2.x, c68.x
        mul r2.x, -r2.x, c57.w
        exp r2.x, r2.x
        mul r3.w, r1.z, r1.z
        mul r1.z, r1.z, c72.w
        add r1.z, -r1.z, c77.w
        mul r1.z, r3.w, r1.z
        mul r2.x, -c34.w, r2.x
        exp r2.x, r2.x
        mov r2.x, -r2.x
        add r2.x, r2.x, c70.y
        mul r1.z, r1.z, r2.x
        dp2add r2.x, c67.zwzz, c67.zwzz, c68.x
        rsq r2.x, r2.x
        rcp r2.x, r2.x
        dp2add r3.w, r1.xyxx, c67.zwzz, c68.x
        dp2add r1.x, r1.xyxx, r1.xyxx, c68.x
        max r1.x, r1.x, c79.x
        rsq r1.x, r1.x
        mul r1.x, r3.w, r1.x
        max r1.y, r2.x, c79.y
        rcp r1.y, r1.y
        mul r1.x, r1.x, r1.y
        mul r1.y, r2.x, c79.z
        mul r1.x, r1.x, c80.x
        add r1.x, -r1.x, c79.w
        log r1.x, r1.x
        mul r1.x, r1.x, c80.y
        exp r1.x, r1.x
        mul r1.x, r1.y, r1.x
        min r1.x, r1.x, c80.z
        mad r1.xyw, r1.x, c35.yzxw, c75.xyxz
        mul r1.xyw, c34.xyxz, r1.xyxw
        add r1.xyw, r1.xyxw, -r5.xyxz
        mul r1.xyz, r1.z, r1.xywx
        add r1.xyz, r5.xyzx, r1.xyzx
        mov r3.xyz, r1.xyzx
    else
    endif
    mov r1.xyz, r3.xyzx
    mad r1.xyz, r1.xyzx, r10.w, r10.xyzx
    add r2.xyz, r2.yzwy, -r1.xyzx
    mul r2.xyz, r4.w, r2.xyzx
    add r1.xyz, r1.xyzx, r2.xyzx
    mov r9.xyz, r1.xyzx
else
endif
add r1.x, c24.z, c80.w
abs r1.x, r1.x
add r1.x, -r1.x, -r1.x
cmp r1.x, r1.x, c68.x, c70.y
add r1.x, -r1.x, c70.y
mov r1.yzw, r9.xxyz
cmp r1.xyz, -r1.x, r1.yzwy, r10.xyzx
max r1.xyz, r1.xyzx, c68.xyzx
mov r1.w, r0.w
mov oC0.xyzw, r1.xyzw
