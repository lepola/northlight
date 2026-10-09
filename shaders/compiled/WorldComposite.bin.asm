ps_3_0
dcl_texcoord0 v0
def c68 = 2.55000000e+02, -1.00000000e+00, -1.00000000e+00, -5.00000000e-01
def c69 = 0.00000000e+00, 0.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c71 = 1.00000000e+00, 9.99999975e-06, 1.20000001e-02, 6.99999975e-04
def c72 = 3.00000003e-03, -9.99989986e-01, 1.00000002e+20, -4.00000000e+00
def c73 = -5.00000000e-01, -5.00000000e-01, 2.00000000e+00, 1.50000006e-01
def c74 = 1.00000000e+00, 1.00000000e+00, 9.99999978e-03, 1.44269502e+00
def c75 = 2.00000003e-01, 1.99999996e-02, -1.99999996e-02, 9.99999975e-05
def c76 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, -2.00000000e+00
def c77 = 1.50000006e-01, 1.50000006e-01, 1.50000006e-01, 3.00000000e+00
def c78 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, 9.99999996e-13
def c79 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c80 = 9.99999997e-07, 6.39999986e-01, 1.36000001e+00, 1.20000005e+00
def c81 = -1.50000000e+00, 2.50000000e-01, -3.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s14
dcl_2d s12
dcl_2d s1
dcl_2d s9
dcl_2d s8
dcl_2d s13
dcl_2d s0
dcl_2d s11
texld r0.xyzw, v0.xyxx, s0
texld r1.xyzw, v0.xyxx, s13
texld r2.xyzw, v0.xyxx, s14
mul r3.x, r1.z, c68.x
mov_sat r3.x, r3.x
add r2.xyz, r2.xyzx, -r0.xyzx
mul r2.xyz, r3.x, r2.xyzx
add r2.xyz, r0.xyzx, r2.xyzx
mul r3.xy, v0.xyxx, c33.xyxx
frc r4.xyzw, r3.xyxx
add r3.xy, r3.xyxx, -r4.xyzw
add r3.zw, c33.xxxy, c68.xxyz
max r3.xy, r3.xyxx, c69.xyxx
min r3.xy, r3.xyxx, r3.zwzz
add r3.xy, r3.xyxx, c69.zwzz
mul r3.xy, r3.xyxx, c0.xyxx
mov r4.xyzw, c70.xyzw
mov r4.xy, r3.xyxx
texldl r4.xyzw, r4.xyzw, s1
add r2.w, r4.x, -c1.w
mul r2.w, r2.w, c2.x
mov_sat r2.w, r2.w
add r4.x, c30.x, c68.w
cmp r4.x, r4.x, c69.x, c71.x
mov r4.y, r4.z
cmp r4.y, -r4.x, r4.y, c69.x
mov r4.z, r4.y
add r4.y, -r4.x, c71.x
if_ne r4.y, -r4.y
    mov r5.xyzw, c70.xyzw
    mov r5.xy, r3.xyxx
    texldl r5.xyzw, r5.xyzw, s11
    add r4.w, c0.z, -r5.x
    cmp r4.w, r4.w, c69.x, c71.x
    mul r6.x, c0.z, c0.w
    add r6.y, c0.w, -c0.z
    mul r6.y, r2.w, r6.y
    add r6.y, c0.w, -r6.y
    max r6.y, r6.y, c71.y
    rcp r6.y, r6.y
    mul r6.x, r6.x, r6.y
    mul r6.y, r5.x, c71.w
    max r6.y, r6.y, c71.z
    add r6.x, r6.x, r6.y
    add r6.x, r6.x, -r5.x
    cmp r6.x, r6.x, c69.x, c71.x
    add r6.x, -r6.x, c71.x
    min r4.w, r4.w, r6.x
    add r6.x, -r5.y, c72.x
    cmp r6.x, r6.x, c69.x, c71.x
    min r4.w, r4.w, r6.x
    cmp r4.w, -r4.w, c69.x, r5.x
    mov r4.z, r4.w
else
endif
mov r4.w, r4.z
cmp r5.x, -r4.w, c69.x, c71.x
mul r5.y, c0.z, c0.w
add r5.z, c0.w, -c0.z
mul r5.w, r2.w, r5.z
add r5.w, c0.w, -r5.w
max r5.w, r5.w, c71.y
rcp r6.x, r5.w
mul r6.x, r5.y, r6.x
cmp r4.w, -r5.x, r6.x, r4.w
mul r6.x, r4.w, c1.z
mad r6.x, r6.x, c25.x, c25.y
max r6.x, r6.x, c69.x
log r6.x, r6.x
mul r6.x, c25.z, r6.x
exp r6.x, r6.x
mov_sat r6.x, r6.x
add r6.x, r6.x, c68.y
mad r6.x, c25.w, r6.x, c71.x
add r2.w, r2.w, c72.y
cmp r2.w, r2.w, c69.x, c71.x
add r5.x, -r5.x, c71.x
min r6.y, r2.w, r5.x
mov r7.x, c69.x
mov r7.y, c69.x
mov r7.z, c69.x
mov r8.x, c69.x
mov r8.y, c69.x
mov r8.z, c69.x
mov r6.z, c69.x
mov r6.w, c69.x
mov r7.w, c72.z
mov r9.x, c69.x
mov r9.y, c69.x
mov r9.z, c69.x
mov r9.w, c69.x
mov r10.x, c69.x
mov r10.y, c69.x
mov r10.z, c69.x
mov r8.w, c69.x
mov r10.w, c71.x
mov r11.x, c69.x
mov r11.y, c69.x
mov r11.z, c69.x
mov r11.w, c69.x
mov r12.x, c69.x
mov r12.y, c69.x
mov r12.z, c69.x
mov r12.w, c71.x
mov r13.x, c69.x
mov r13.y, c72.z
mul r13.zw, v0.xxxy, c33.xxzw
add r13.zw, r13.xxzw, c73.xxxy
frc r14.xyzw, r13.zwzz
add r14.xy, r13.zwzz, -r14.xyzw
add r13.zw, r13.xxzw, -r14.xxxy
rcp r5.w, r5.w
mul r5.w, r5.y, r5.w
mov r14.z, c69.x
rep i0.xyzw
    mov r22.z, r14.z
    add r22.z, r22.z, c72.w
    cmp r22.z, r22.z, c69.x, c71.x
    add r22.z, -r22.z, c71.x
    if_ne r22.z, -r22.z
        break
    else
    endif
    mov r22.z, r14.z
    mul r22.z, r22.z, c69.z
    frc r24.xyzw, r22.z
    add r22.z, r22.z, -r24.x
    mov r22.w, r14.z
    mul r24.x, r22.z, c73.z
    add r22.w, r22.w, -r24.x
    mov r22.x, r22.w
    mov r22.y, r22.z
    mov r22.zw, r22.xxxy
    add r22.zw, r14.xxxy, r22.xxzw
    add r24.xy, c33.zwzz, c68.yzyy
    max r22.zw, r22.xxzw, c69.xxxy
    min r22.zw, r22.xxzw, r24.xxxy
    add r22.zw, r22.xxzw, c69.xxzw
    rcp r21.y, c33.z
    rcp r21.z, c33.w
    mul r22.zw, r22.xxzw, r21.xxyz
    mul r24.xy, r22.zwzz, c33.xyxx
    frc r25.xyzw, r24.xyxx
    add r24.xy, r24.xyxx, -r25.xyzw
    max r24.xy, r24.xyxx, c69.xyxx
    min r24.xy, r24.xyxx, r3.zwzz
    add r24.xy, r24.xyxx, c69.zwzz
    mul r24.xy, r24.xyxx, c0.xyxx
    mov r20.xyzw, c70.xyzw
    mov r20.xy, r24.xyxx
    mov r25.xyzw, r20.xyzw
    texldl r25.xyzw, r25.xyzw, s1
    add r24.z, r25.x, -c1.w
    mul r24.z, r24.z, c2.x
    mov_sat r24.z, r24.z
    add r25.xy, -r13.zwzz, c74.xyxx
    mov r25.zw, r22.xxxy
    add r26.xy, r13.zwzz, -r25.xyxx
    mul r25.zw, r25.xxzw, r26.xxxy
    add r25.xy, r25.xyxx, r25.zwzz
    if_ne r6.y, -r6.y
        mul r24.w, r24.z, r5.z
        add r24.w, c0.w, -r24.w
        max r24.w, r24.w, c71.y
        rcp r15.x, r24.w
        mul r24.w, r5.y, r15.x
        add r24.w, r24.w, -r5.w
        abs r24.w, r24.w
        mul r25.z, r25.x, r25.y
        mul r25.w, r5.w, c74.z
        max r25.w, r25.w, c73.w
        rcp r21.w, r25.w
        mul r25.w, -r24.w, r21.w
        mul r25.w, r25.w, c74.w
        exp r14.w, r25.w
        mul r25.z, r25.z, r14.w
        mov r23.xyzw, c70.xyzw
        mov r23.xy, r22.zwzz
        mov r26.xyzw, r23.xyzw
        texldl r26.xyzw, r26.xyzw, s8
        mov r16.xyzw, c70.xyzw
        mov r16.xy, r22.zwzz
        mov r27.xyzw, r16.xyzw
        texldl r27.xyzw, r27.xyzw, s12
        mov r17.xyzw, c70.xyzw
        mov r17.xy, r22.zwzz
        mov r28.xyzw, r17.xyzw
        texldl r28.xyzw, r28.xyzw, s10
        mul r29.xyz, r26.xyzx, r25.z
        mov r30.xyz, r8.xyzx
        add r29.xyz, r30.xyzx, r29.xyzx
        mov r8.xyz, r29.xyzx
        mul r25.w, r26.w, r25.z
        mov r29.x, r6.z
        add r25.w, r29.x, r25.w
        mov r6.z, r25.w
        mul r29.xyz, r27.xyzx, r25.z
        mov r30.xyz, r7.xyzx
        add r29.xyz, r30.xyzx, r29.xyzx
        mov r7.xyz, r29.xyzx
        mul r25.w, r28.w, r25.z
        mov r29.x, r8.w
        add r25.w, r29.x, r25.w
        mov r8.w, r25.w
        mov r25.w, r6.w
        add r25.z, r25.w, r25.z
        mov r6.w, r25.z
        mov r25.z, r7.w
        add r25.z, r24.w, -r25.z
        cmp r25.z, r25.z, c69.x, c71.x
        mov r25.w, r7.w
        cmp r24.w, -r25.z, r25.w, r24.w
        mov r7.w, r24.w
        mov r29.xyzw, r9.xyzw
        cmp r26.xyzw, -r25.z, r29.xyzw, r26.xyzw
        mov r9.xyzw, r26.xyzw
        mov r26.xyz, r10.xyzx
        cmp r26.xyz, -r25.z, r26.xyzx, r27.xyzx
        mov r10.xyz, r26.xyzx
        mov r24.w, r10.w
        cmp r24.w, -r25.z, r24.w, r28.w
        mov r10.w, r24.w
    else
    endif
    mov r24.w, r4.z
    cmp r24.w, -r4.x, r24.w, c69.x
    mov r4.z, r24.w
    if_ne r4.y, -r4.y
        mov r18.xyzw, c70.xyzw
        mov r18.xy, r24.xyxx
        mov r26.xyzw, r18.xyzw
        texldl r26.xyzw, r26.xyzw, s11
        add r24.x, c0.z, -r26.x
        cmp r24.x, r24.x, c69.x, c71.x
        mul r24.y, r24.z, r5.z
        add r24.y, c0.w, -r24.y
        max r24.y, r24.y, c71.y
        rcp r15.y, r24.y
        mul r24.y, r5.y, r15.y
        mul r24.w, r26.x, c71.w
        max r24.w, r24.w, c71.z
        add r24.y, r24.y, r24.w
        add r24.y, r24.y, -r26.x
        cmp r24.y, r24.y, c69.x, c71.x
        add r24.y, -r24.y, c71.x
        min r24.x, r24.x, r24.y
        add r24.y, -r26.y, c72.x
        cmp r24.y, r24.y, c69.x, c71.x
        min r24.x, r24.x, r24.y
        cmp r24.x, -r24.x, c69.x, r26.x
        mov r4.z, r24.x
    else
    endif
    mov r24.x, r4.z
    cmp r24.y, -r24.x, c69.x, c71.x
    mul r24.z, r24.z, r5.z
    add r24.z, c0.w, -r24.z
    max r24.z, r24.z, c71.y
    rcp r15.z, r24.z
    mul r24.z, r5.y, r15.z
    cmp r24.x, -r24.y, r24.z, r24.x
    add r24.x, r24.x, -r4.w
    abs r24.x, r24.x
    mov r19.xyzw, c70.xyzw
    mov r19.xy, r22.zwzz
    mov r26.xyzw, r19.xyzw
    texldl r26.xyzw, r26.xyzw, s9
    mul r22.z, r25.x, r25.y
    mul r22.w, r4.w, c75.y
    max r22.w, r22.w, c75.x
    rcp r15.w, r22.w
    mul r22.w, -r24.x, r15.w
    mul r22.w, r22.w, c74.w
    exp r21.x, r22.w
    mul r22.z, r22.z, r21.x
    mul r25.xyzw, r26.xyzw, r22.z
    mov r27.xyzw, r11.xyzw
    add r25.xyzw, r27.xyzw, r25.xyzw
    mov r11.xyzw, r25.xyzw
    mov r22.w, r13.x
    add r22.z, r22.w, r22.z
    mov r13.x, r22.z
    mov r22.z, r13.y
    add r22.z, r24.x, -r22.z
    cmp r22.z, r22.z, c69.x, c71.x
    mov r22.w, r13.y
    cmp r22.w, -r22.z, r22.w, r24.x
    mov r13.y, r22.w
    mov r24.xyzw, r12.xyzw
    cmp r24.xyzw, -r22.z, r24.xyzw, r26.xyzw
    mov r12.xyzw, r24.xyzw
    mov r22.z, r14.z
    add r22.z, r22.z, c71.x
    mov r14.z, r22.z
endrep
mov r3.z, r13.x
add r3.z, -r3.z, c71.y
cmp r3.z, r3.z, c69.x, c71.x
mov r3.w, r13.x
rcp r3.w, r3.w
mov r13.xyzw, r3.w
mul r11.xyzw, r11.xyzw, r13.xyzw
cmp r11.xyzw, -r3.z, r12.xyzw, r11.xyzw
mov r3.z, r6.w
add r3.z, r3.z, c75.z
cmp r3.z, r3.z, c69.x, c71.x
mov r3.w, r10.w
mov r4.x, r8.w
mov r4.y, r6.w
rcp r4.y, r4.y
mul r4.x, r4.x, r4.y
cmp r3.z, -r3.z, r4.x, r3.w
cmp r3.z, -r6.y, c71.x, r3.z
mov r12.xyzw, c70.xyzw
mov r12.xy, v0.xyxx
texldl r12.xyzw, r12.xyzw, s10
mul r4.xyz, r2.xyzx, r3.z
mov_sat r5.yzw, r4.xxyz
add r5.yzw, -r5.xyzw, c76.xxyz
mad r4.xyz, r12.xyzx, r5.yzwy, r4.xyzx
mov_sat r4.xyz, r4.xyzx
mov r5.yzw, r4.xxyz
add r3.z, -r6.x, c71.x
mul r12.xyz, r3.z, c26.xyzx
min r12.xyz, r12.xyzx, r4.xyzx
if_ne r6.y, -r6.y
    mov r3.z, r6.w
    add r3.z, r3.z, c75.z
    cmp r3.z, r3.z, c69.x, c71.x
    mov r13.xyzw, r9.xyzw
    cmp r8.xyz, -r3.z, r8.xyzx, r13.xyzx
    mov r3.w, r6.z
    cmp r3.w, -r3.z, r3.w, r9.w
    mov r9.xyz, r10.xyzx
    cmp r7.xyz, -r3.z, r7.xyzx, r9.xyzx
    mov r6.y, r6.w
    cmp r3.z, -r3.z, r6.y, c71.x
    max r3.z, r3.z, c75.w
    rcp r3.z, r3.z
    mov r6.yzw, r3.z
    mul r6.yzw, r8.xxyz, r6.xyzw
    mul r3.w, r3.w, r3.z
    mov r8.xyz, r3.z
    mul r7.xyz, r7.xyzx, r8.xyzx
    max r7.xyz, r7.xyzx, c77.xyzx
    add r8.xyz, r4.xyzx, -r12.xyzx
    max r8.xyz, r8.xyzx, c70.xyzx
    rcp r9.x, r7.x
    rcp r9.y, r7.y
    rcp r9.z, r7.z
    mul r7.xyz, r8.xyzx, r9.xyzx
    min r7.xyz, r7.xyzx, r6.x
    mad r7.xyz, r7.xyzx, r6.yzwy, r4.xyzx
    mov r5.yzw, r7.xxyz
    mad r4.xyz, c78.xyzx, r8.xyzx, r4.xyzx
    max r4.xyz, r7.xyzx, r4.xyzx
    mov r5.yzw, r4.xxyz
    add r3.z, c24.z, c68.y
    abs r3.z, r3.z
    add r3.z, -r3.z, -r3.z
    cmp r3.z, r3.z, c69.x, c71.x
    add r3.z, -r3.z, c71.x
    cmp r4.xyz, -r3.z, r4.xyzx, r3.w
    mov r5.yzw, r4.xxyz
    add r3.z, c24.z, c76.w
    abs r3.z, r3.z
    add r3.z, -r3.z, -r3.z
    cmp r3.z, r3.z, c69.x, c71.x
    add r3.z, -r3.z, c71.x
    add r6.xyz, c18.xyzx, r6.yzwy
    max r6.xyz, r6.xyzx, c70.xyzx
    cmp r4.xyz, -r3.z, r4.xyzx, r6.xyzx
    mov r5.yzw, r4.xxyz
else
endif
add r3.z, c24.z, c68.w
cmp r3.z, r3.z, c69.x, c71.x
if_ne r3.z, -r3.z
    mov r4.xyz, r5.yzwy
    mov r6.xyz, r5.yzwy
    add r2.w, -r2.w, c71.x
    min r2.w, r2.w, r5.x
    mov r3.z, c69.x
    add r3.w, r4.w, -c57.y
    mul r3.w, r3.w, c57.z
    mov_sat r3.w, r3.w
    cmp r2.w, -r2.w, r3.w, c71.x
    cmp r3.w, -r2.w, c69.x, c71.x
    add r3.w, -r3.w, c71.x
    cmp r4.w, -c34.w, c69.x, c71.x
    add r4.w, -r4.w, c71.x
    max r3.w, r3.w, r4.w
    if_ne r3.w, -r3.w
        mov r7.xyz, r6.xyzx
        mov r3.z, c71.x
    else
    endif
    add r3.z, -r3.z, c71.x
    if_ne r3.z, -r3.z
        mul r3.zw, c0.xxxy, c69.xxzw
        add r3.xy, r3.xyxx, -r3.zwzz
        mul r3.xy, r3.xyxx, c79.xyxx
        add r3.xy, r3.xyxx, c79.zwzz
        rcp r3.z, c1.x
        rcp r3.w, c1.y
        mul r3.xy, r3.xyxx, r3.zwzz
        mov r8.xy, r3.xyxx
        mov r8.z, c1.z
        mul r9.xyz, r3.x, c3.xyzx
        mul r3.xyz, r3.y, c4.xyzx
        add r3.xyz, r9.xyzx, r3.xyzx
        mul r9.xyz, c1.z, c5.xyzx
        add r3.xyz, r3.xyzx, r9.xyzx
        mov r9.xyz, r8.xyzx
        dp3 r3.w, r9.xyzx, r8.xyzx
        rsq r3.w, r3.w
        mul r3.w, r3.z, r3.w
        max r3.w, r3.w, c69.x
        mul r3.w, -r3.w, c57.w
        exp r3.w, r3.w
        mul r4.w, r2.w, r2.w
        mul r2.w, r2.w, c73.z
        add r2.w, -r2.w, c77.w
        mul r2.w, r4.w, r2.w
        mul r3.w, -c34.w, r3.w
        exp r3.w, r3.w
        mov r3.w, -r3.w
        add r3.w, r3.w, c71.x
        mul r2.w, r2.w, r3.w
        dp2add r3.w, c67.zwzz, c67.zwzz, c69.x
        rsq r3.w, r3.w
        rcp r3.w, r3.w
        dp2add r4.w, r3.xyxx, c67.zwzz, c69.x
        dp2add r3.x, r3.xyxx, r3.xyxx, c69.x
        max r3.x, r3.x, c78.w
        rsq r3.x, r3.x
        mul r3.x, r4.w, r3.x
        max r3.y, r3.w, c80.x
        rcp r3.y, r3.y
        mul r3.x, r3.x, r3.y
        mul r3.y, r3.w, c80.y
        mul r3.x, r3.x, c80.w
        add r3.x, -r3.x, c80.z
        log r3.x, r3.x
        mul r3.x, r3.x, c81.x
        exp r3.x, r3.x
        mul r3.x, r3.y, r3.x
        min r3.x, r3.x, c81.y
        mad r3.xyz, r3.x, c35.yzwy, c76.xyzx
        mul r3.xyz, c34.xyzx, r3.xyzx
        add r3.xyz, r3.xyzx, -r6.xyzx
        mul r3.xyz, r2.w, r3.xyzx
        add r3.xyz, r6.xyzx, r3.xyzx
        mov r7.xyz, r3.xyzx
    else
    endif
    mov r3.xyz, r7.xyzx
    mad r3.xyz, r3.xyzx, r11.w, r11.xyzx
    add r4.xyz, r4.xyzx, -r3.xyzx
    mul r4.xyz, r1.x, r4.xyzx
    add r3.xyz, r3.xyzx, r4.xyzx
    mov r5.yzw, r3.xxyz
    add r1.x, -r1.y, c71.x
    add r1.yzw, r3.xxyz, -r2.xxyz
    mad r1.xyz, r1.x, r1.yzwy, r0.xyzx
    mov r5.yzw, r1.xxyz
else
endif
add r1.x, c24.z, c81.z
abs r1.x, r1.x
add r1.x, -r1.x, -r1.x
cmp r1.x, r1.x, c69.x, c71.x
add r1.x, -r1.x, c71.x
mov r1.yzw, r5.xyzw
cmp r1.xyz, -r1.x, r1.yzwy, r11.xyzx
max r1.xyz, r1.xyzx, c70.xyzx
mov r1.w, r0.w
mov oC0.xyzw, r1.xyzw
