ps_3_0
dcl_texcoord0 v0
def c68 = -5.00000007e-02, 0.00000000e+00, 1.00000000e+00, 1.99218750e+00
def c69 = -2.00000009e-03, -1.00000000e+00, -1.00000000e+00, -5.00000000e-01
def c70 = 0.00000000e+00, 0.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c71 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c72 = 9.99999975e-06, 1.20000001e-02, 6.99999975e-04, 3.00000003e-03
def c73 = -9.99989986e-01, 1.00000002e+20, -5.00000000e-01, -5.00000000e-01
def c74 = -4.00000000e+00, 2.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c75 = 1.50000006e-01, 9.99999978e-03, 1.44269502e+00, 2.00000003e-01
def c76 = 1.99999996e-02, -1.99999996e-02, 9.99999975e-05, -2.00000000e+00
def c77 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 3.00000000e+00
def c78 = 1.50000006e-01, 1.50000006e-01, 1.50000006e-01, 9.99999996e-13
def c79 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, 9.99999997e-07
def c80 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c81 = 6.39999986e-01, 1.36000001e+00, 1.20000005e+00, -1.50000000e+00
def c82 = 2.50000000e-01, -3.00000000e+00, 0.00000000e+00, 0.00000000e+00
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
add r2.x, r1.w, c68.x
cmp r2.x, r2.x, c68.y, c68.z
mul r2.y, r1.w, c68.w
cmp r2.x, -r2.x, r2.y, c68.z
rcp r2.y, r2.x
mov r2.yzw, r2.y
mul r2.yzw, r0.xxyz, r2.xyzw
add r3.x, r1.z, c69.x
cmp r3.x, r3.x, c68.y, c68.z
texld r4.xyzw, v0.xyxx, s14
cmp r3.xyz, -r3.x, r4.xyzx, r2.yzwy
mul r4.xyz, r3.xyzx, r2.x
mul r5.xy, v0.xyxx, c33.xyxx
frc r6.xyzw, r5.xyxx
add r5.xy, r5.xyxx, -r6.xyzw
add r5.zw, c33.xxxy, c69.xxyz
max r5.xy, r5.xyxx, c70.xyxx
min r5.xy, r5.xyxx, r5.zwzz
add r5.xy, r5.xyxx, c70.zwzz
mul r5.xy, r5.xyxx, c0.xyxx
mov r6.xyzw, c71.xyzw
mov r6.xy, r5.xyxx
texldl r6.xyzw, r6.xyzw, s1
add r2.x, r6.x, -c1.w
mul r2.x, r2.x, c2.x
mov_sat r2.x, r2.x
add r3.w, c30.x, c69.w
cmp r3.w, r3.w, c68.y, c68.z
mov r4.w, r6.x
cmp r4.w, -r3.w, r4.w, c68.y
mov r6.x, r4.w
add r4.w, -r3.w, c68.z
if_ne r4.w, -r4.w
    mov r7.xyzw, c71.xyzw
    mov r7.xy, r5.xyxx
    texldl r7.xyzw, r7.xyzw, s11
    add r6.y, c0.z, -r7.x
    cmp r6.y, r6.y, c68.y, c68.z
    mul r6.z, c0.z, c0.w
    add r6.w, c0.w, -c0.z
    mul r6.w, r2.x, r6.w
    add r6.w, c0.w, -r6.w
    max r6.w, r6.w, c72.x
    rcp r6.w, r6.w
    mul r6.z, r6.z, r6.w
    mul r6.w, r7.x, c72.z
    max r6.w, r6.w, c72.y
    add r6.z, r6.z, r6.w
    add r6.z, r6.z, -r7.x
    cmp r6.z, r6.z, c68.y, c68.z
    add r6.z, -r6.z, c68.z
    min r6.y, r6.y, r6.z
    add r6.z, -r7.y, c72.w
    cmp r6.z, r6.z, c68.y, c68.z
    min r6.y, r6.y, r6.z
    cmp r6.y, -r6.y, c68.y, r7.x
    mov r6.x, r6.y
else
endif
mov r6.y, r6.x
cmp r6.z, -r6.y, c68.y, c68.z
mul r6.w, c0.z, c0.w
add r7.x, c0.w, -c0.z
mul r7.y, r2.x, r7.x
add r7.y, c0.w, -r7.y
max r7.y, r7.y, c72.x
rcp r7.z, r7.y
mul r7.z, r6.w, r7.z
cmp r6.y, -r6.z, r7.z, r6.y
mul r7.z, r6.y, c1.z
mad r7.z, r7.z, c25.x, c25.y
max r7.z, r7.z, c68.y
log r7.z, r7.z
mul r7.z, c25.z, r7.z
exp r7.z, r7.z
mov_sat r7.z, r7.z
add r7.z, r7.z, c69.y
mad r7.z, c25.w, r7.z, c68.z
add r2.x, r2.x, c73.x
cmp r2.x, r2.x, c68.y, c68.z
add r6.z, -r6.z, c68.z
min r7.w, r2.x, r6.z
mov r8.x, c68.y
mov r8.y, c68.y
mov r8.z, c68.y
mov r9.x, c68.y
mov r9.y, c68.y
mov r9.z, c68.y
mov r8.w, c68.y
mov r9.w, c68.y
mov r10.x, c73.y
mov r11.x, c68.y
mov r11.y, c68.y
mov r11.z, c68.y
mov r11.w, c68.y
mov r10.y, c68.y
mov r10.z, c68.y
mov r10.w, c68.y
mov r12.x, c68.y
mov r12.y, c68.z
mov r13.x, c68.y
mov r13.y, c68.y
mov r13.z, c68.y
mov r13.w, c68.y
mov r14.x, c68.y
mov r14.y, c68.y
mov r14.z, c68.y
mov r14.w, c68.z
mov r12.z, c68.y
mov r12.w, c73.y
mul r15.xy, v0.xyxx, c33.zwzz
add r15.xy, r15.xyxx, c73.zwzz
frc r16.xyzw, r15.xyxx
add r15.zw, r15.xxxy, -r16.xxxy
add r15.xy, r15.xyxx, -r15.zwzz
rcp r7.y, r7.y
mul r7.y, r6.w, r7.y
mov r16.x, c68.y
rep i0.xyzw
    mov r25.x, r16.x
    add r25.x, r25.x, c74.x
    cmp r25.x, r25.x, c68.y, c68.z
    add r25.x, -r25.x, c68.z
    if_ne r25.x, -r25.x
        break
    else
    endif
    mov r25.x, r16.x
    mul r25.x, r25.x, c70.z
    frc r26.xyzw, r25.x
    add r25.x, r25.x, -r26.xyzw
    mov r25.y, r16.x
    mul r25.z, r25.x, c74.y
    add r25.y, r25.y, -r25.z
    mov r23.x, r25.y
    mov r23.y, r25.x
    mov r25.xy, r23.xyxx
    add r25.xy, r15.zwzz, r25.xyxx
    add r25.zw, c33.xxzw, c69.xxyz
    max r25.xy, r25.xyxx, c70.xyxx
    min r25.xy, r25.xyxx, r25.zwzz
    add r25.xy, r25.xyxx, c70.zwzz
    rcp r18.z, c33.z
    rcp r18.w, c33.w
    mul r25.xy, r25.xyxx, r18.zwzz
    mul r25.zw, r25.xxxy, c33.xxxy
    frc r26.xyzw, r25.zwzz
    add r25.zw, r25.xxzw, -r26.xxxy
    max r25.zw, r25.xxzw, c70.xxxy
    min r25.zw, r25.xxzw, r5.xxzw
    add r25.zw, r25.xxzw, c70.xxzw
    mul r25.zw, r25.xxzw, c0.xxxy
    mov r21.xyzw, c71.xyzw
    mov r21.xy, r25.zwzz
    mov r26.xyzw, r21.xyzw
    texldl r26.xyzw, r26.xyzw, s1
    add r26.x, r26.x, -c1.w
    mul r26.x, r26.x, c2.x
    mov_sat r26.x, r26.x
    add r26.yz, -r15.xxyx, c74.xzwx
    mov r27.xy, r23.xyxx
    add r27.zw, r15.xxxy, -r26.xxyz
    mul r27.xy, r27.xyxx, r27.zwzz
    add r26.yz, r26.xyzx, r27.xxyx
    if_ne r7.w, -r7.w
        mul r26.w, r26.x, r7.x
        add r26.w, c0.w, -r26.w
        max r26.w, r26.w, c72.x
        rcp r23.w, r26.w
        mul r26.w, r6.w, r23.w
        add r26.w, r26.w, -r7.y
        abs r26.w, r26.w
        mul r27.x, r26.y, r26.z
        mul r27.y, r7.y, c75.y
        max r27.y, r27.y, c75.x
        rcp r16.w, r27.y
        mul r27.y, -r26.w, r16.w
        mul r27.y, r27.y, c75.z
        exp r16.y, r27.y
        mul r27.x, r27.x, r16.y
        mov r24.xyzw, c71.xyzw
        mov r24.xy, r25.xyxx
        mov r28.xyzw, r24.xyzw
        texldl r28.xyzw, r28.xyzw, s8
        mov r22.xyzw, c71.xyzw
        mov r22.xy, r25.xyxx
        mov r29.xyzw, r22.xyzw
        texldl r29.xyzw, r29.xyzw, s12
        mov r19.xyzw, c71.xyzw
        mov r19.xy, r25.xyxx
        mov r30.xyzw, r19.xyzw
        texldl r30.xyzw, r30.xyzw, s10
        mul r27.yzw, r28.xxyz, r27.x
        mov r31.xyz, r9.xyzx
        add r27.yzw, r31.xxyz, r27.xyzw
        mov r9.xyz, r27.yzwy
        mul r27.y, r28.w, r27.x
        mov r27.z, r8.w
        add r27.y, r27.z, r27.y
        mov r8.w, r27.y
        mul r27.yzw, r29.xxyz, r27.x
        mov r31.xyz, r8.xyzx
        add r27.yzw, r31.xxyz, r27.xyzw
        mov r8.xyz, r27.yzwy
        mul r27.y, r30.w, r27.x
        mov r27.z, r12.x
        add r27.y, r27.z, r27.y
        mov r12.x, r27.y
        mov r27.y, r9.w
        add r27.x, r27.y, r27.x
        mov r9.w, r27.x
        mov r27.x, r10.x
        add r27.x, r26.w, -r27.x
        cmp r27.x, r27.x, c68.y, c68.z
        mov r27.y, r10.x
        cmp r26.w, -r27.x, r27.y, r26.w
        mov r10.x, r26.w
        mov r31.xyzw, r11.xyzw
        cmp r28.xyzw, -r27.x, r31.xyzw, r28.xyzw
        mov r11.xyzw, r28.xyzw
        mov r27.yzw, r10.xyzw
        cmp r27.yzw, -r27.x, r27.xyzw, r29.xxyz
        mov r10.yzw, r27.xyzw
        mov r26.w, r12.y
        cmp r26.w, -r27.x, r26.w, r30.w
        mov r12.y, r26.w
    else
    endif
    mov r26.w, r6.x
    cmp r26.w, -r3.w, r26.w, c68.y
    mov r6.x, r26.w
    if_ne r4.w, -r4.w
        mov r20.xyzw, c71.xyzw
        mov r20.xy, r25.zwzz
        mov r27.xyzw, r20.xyzw
        texldl r27.xyzw, r27.xyzw, s11
        add r25.z, c0.z, -r27.x
        cmp r25.z, r25.z, c68.y, c68.z
        mul r25.w, r26.x, r7.x
        add r25.w, c0.w, -r25.w
        max r25.w, r25.w, c72.x
        rcp r16.z, r25.w
        mul r25.w, r6.w, r16.z
        mul r26.w, r27.x, c72.z
        max r26.w, r26.w, c72.y
        add r25.w, r25.w, r26.w
        add r25.w, r25.w, -r27.x
        cmp r25.w, r25.w, c68.y, c68.z
        add r25.w, -r25.w, c68.z
        min r25.z, r25.z, r25.w
        add r25.w, -r27.y, c72.w
        cmp r25.w, r25.w, c68.y, c68.z
        min r25.z, r25.z, r25.w
        cmp r25.z, -r25.z, c68.y, r27.x
        mov r6.x, r25.z
    else
    endif
    mov r25.z, r6.x
    cmp r25.w, -r25.z, c68.y, c68.z
    mul r26.x, r26.x, r7.x
    add r26.x, c0.w, -r26.x
    max r26.x, r26.x, c72.x
    rcp r18.x, r26.x
    mul r26.x, r6.w, r18.x
    cmp r25.z, -r25.w, r26.x, r25.z
    add r25.z, r25.z, -r6.y
    abs r25.z, r25.z
    mov r17.xyzw, c71.xyzw
    mov r17.xy, r25.xyxx
    mov r27.xyzw, r17.xyzw
    texldl r27.xyzw, r27.xyzw, s9
    mul r25.x, r26.y, r26.z
    mul r25.y, r6.y, c76.x
    max r25.y, r25.y, c75.w
    rcp r18.y, r25.y
    mul r25.y, -r25.z, r18.y
    mul r25.y, r25.y, c75.z
    exp r23.z, r25.y
    mul r25.x, r25.x, r23.z
    mul r26.xyzw, r27.xyzw, r25.x
    mov r28.xyzw, r13.xyzw
    add r26.xyzw, r28.xyzw, r26.xyzw
    mov r13.xyzw, r26.xyzw
    mov r25.y, r12.z
    add r25.x, r25.y, r25.x
    mov r12.z, r25.x
    mov r25.x, r12.w
    add r25.x, r25.z, -r25.x
    cmp r25.x, r25.x, c68.y, c68.z
    mov r25.y, r12.w
    cmp r25.y, -r25.x, r25.y, r25.z
    mov r12.w, r25.y
    mov r26.xyzw, r14.xyzw
    cmp r25.xyzw, -r25.x, r26.xyzw, r27.xyzw
    mov r14.xyzw, r25.xyzw
    mov r25.x, r16.x
    add r25.x, r25.x, c68.z
    mov r16.x, r25.x
endrep
mov r3.w, r12.z
add r3.w, -r3.w, c72.x
cmp r3.w, r3.w, c68.y, c68.z
mov r4.w, r12.z
rcp r4.w, r4.w
mov r15.xyzw, r4.w
mul r13.xyzw, r13.xyzw, r15.xyzw
cmp r13.xyzw, -r3.w, r14.xyzw, r13.xyzw
mov r3.w, r9.w
add r3.w, r3.w, c76.y
cmp r3.w, r3.w, c68.y, c68.z
mov r4.w, r12.y
mov r5.z, r12.x
mov r5.w, r9.w
rcp r5.w, r5.w
mul r5.z, r5.z, r5.w
cmp r3.w, -r3.w, r5.z, r4.w
cmp r3.w, -r7.w, c68.z, r3.w
mov r12.xyzw, c71.xyzw
mov r12.xy, v0.xyxx
texldl r12.xyzw, r12.xyzw, s10
mul r4.xyz, r4.xyzx, r3.w
mov_sat r14.xyz, r4.xyzx
add r14.xyz, -r14.xyzx, c77.xyzx
mad r4.xyz, r12.xyzx, r14.xyzx, r4.xyzx
mov_sat r4.xyz, r4.xyzx
mov r12.xyz, r4.xyzx
add r3.w, -r7.z, c68.z
mul r14.xyz, r3.w, c26.xyzx
min r14.xyz, r14.xyzx, r4.xyzx
if_ne r7.w, -r7.w
    mov r3.w, r9.w
    add r3.w, r3.w, c76.y
    cmp r3.w, r3.w, c68.y, c68.z
    mov r15.xyzw, r11.xyzw
    mov r7.xyw, r9.xyxz
    cmp r7.xyw, -r3.w, r7.xyxw, r15.xyxz
    mov r4.w, r8.w
    cmp r4.w, -r3.w, r4.w, r11.w
    mov r9.xyz, r10.yzwy
    cmp r8.xyz, -r3.w, r8.xyzx, r9.xyzx
    mov r5.z, r9.w
    cmp r3.w, -r3.w, r5.z, c68.z
    max r3.w, r3.w, c76.z
    rcp r3.w, r3.w
    mov r9.xyz, r3.w
    mul r7.xyw, r7.xyxw, r9.xyxz
    mul r4.w, r4.w, r3.w
    mov r9.xyz, r3.w
    mul r8.xyz, r8.xyzx, r9.xyzx
    max r8.xyz, r8.xyzx, c78.xyzx
    add r9.xyz, r4.xyzx, -r14.xyzx
    max r9.xyz, r9.xyzx, c71.xyzx
    rcp r10.x, r8.x
    rcp r10.y, r8.y
    rcp r10.z, r8.z
    mul r8.xyz, r9.xyzx, r10.xyzx
    min r8.xyz, r8.xyzx, r7.z
    mad r8.xyz, r8.xyzx, r7.xywx, r4.xyzx
    mov r12.xyz, r8.xyzx
    mad r4.xyz, c79.xyzx, r9.xyzx, r4.xyzx
    max r4.xyz, r8.xyzx, r4.xyzx
    mov r12.xyz, r4.xyzx
    add r3.w, c24.z, c69.y
    abs r3.w, r3.w
    add r3.w, -r3.w, -r3.w
    cmp r3.w, r3.w, c68.y, c68.z
    add r3.w, -r3.w, c68.z
    cmp r4.xyz, -r3.w, r4.xyzx, r4.w
    mov r12.xyz, r4.xyzx
    add r3.w, c24.z, c76.w
    abs r3.w, r3.w
    add r3.w, -r3.w, -r3.w
    cmp r3.w, r3.w, c68.y, c68.z
    add r3.w, -r3.w, c68.z
    add r7.xyz, c18.xyzx, r7.xywx
    max r7.xyz, r7.xyzx, c71.xyzx
    cmp r4.xyz, -r3.w, r4.xyzx, r7.xyzx
    mov r12.xyz, r4.xyzx
else
endif
add r3.w, c24.z, c69.w
cmp r3.w, r3.w, c68.y, c68.z
if_ne r3.w, -r3.w
    add r1.x, -r1.y, c68.z
    mov r1.yzw, r12.xxyz
    add r2.x, -r2.x, c68.z
    min r2.x, r2.x, r6.z
    mov r3.w, c68.y
    add r4.x, r6.y, -c57.y
    mul r4.x, r4.x, c57.z
    mov_sat r4.x, r4.x
    cmp r2.x, -r2.x, r4.x, c68.z
    cmp r4.x, -r2.x, c68.y, c68.z
    add r4.x, -r4.x, c68.z
    cmp r4.y, -c34.w, c68.y, c68.z
    add r4.y, -r4.y, c68.z
    max r4.x, r4.x, r4.y
    if_ne r4.x, -r4.x
        mov r4.xyz, r1.yzwy
        mov r3.w, c68.z
    else
    endif
    add r3.w, -r3.w, c68.z
    if_ne r3.w, -r3.w
        mul r5.zw, c0.xxxy, c70.xxzw
        add r5.xy, r5.xyxx, -r5.zwzz
        mul r5.xy, r5.xyxx, c80.xyxx
        add r5.xy, r5.xyxx, c80.zwzz
        rcp r5.z, c1.x
        rcp r5.w, c1.y
        mul r5.xy, r5.xyxx, r5.zwzz
        mov r6.xy, r5.xyxx
        mov r6.z, c1.z
        mul r7.xyz, r5.x, c3.xyzx
        mul r5.xyz, r5.y, c4.xyzx
        add r5.xyz, r7.xyzx, r5.xyzx
        mul r7.xyz, c1.z, c5.xyzx
        add r5.xyz, r5.xyzx, r7.xyzx
        mov r7.xyz, r6.xyzx
        dp3 r3.w, r7.xyzx, r6.xyzx
        rsq r3.w, r3.w
        mul r3.w, r5.z, r3.w
        max r3.w, r3.w, c68.y
        mul r3.w, -r3.w, c57.w
        exp r3.w, r3.w
        mul r4.w, r2.x, r2.x
        mul r2.x, r2.x, c74.y
        add r2.x, -r2.x, c77.w
        mul r2.x, r4.w, r2.x
        mul r3.w, -c34.w, r3.w
        exp r3.w, r3.w
        mov r3.w, -r3.w
        add r3.w, r3.w, c68.z
        mul r2.x, r2.x, r3.w
        dp2add r3.w, c67.zwzz, c67.zwzz, c68.y
        rsq r3.w, r3.w
        rcp r3.w, r3.w
        dp2add r4.w, r5.xyxx, c67.zwzz, c68.y
        dp2add r5.x, r5.xyxx, r5.xyxx, c68.y
        max r5.x, r5.x, c78.w
        rsq r5.x, r5.x
        mul r4.w, r4.w, r5.x
        max r5.x, r3.w, c79.w
        rcp r5.x, r5.x
        mul r4.w, r4.w, r5.x
        mul r3.w, r3.w, c81.x
        mul r4.w, r4.w, c81.z
        add r4.w, -r4.w, c81.y
        log r4.w, r4.w
        mul r4.w, r4.w, c81.w
        exp r4.w, r4.w
        mul r3.w, r3.w, r4.w
        min r3.w, r3.w, c82.x
        mad r5.xyz, r3.w, c35.yzwy, c77.xyzx
        mul r5.xyz, c34.xyzx, r5.xyzx
        add r5.xyz, r5.xyzx, -r1.yzwy
        mul r5.xyz, r2.x, r5.xyzx
        add r1.yzw, r1.xyzw, r5.xxyz
        mov r4.xyz, r1.yzwy
    else
    endif
    mov r1.yzw, r4.xxyz
    mad r1.yzw, r1.xyzw, r13.w, r13.xxyz
    add r1.yzw, r1.xyzw, -r3.xxyz
    mad r1.xyz, r1.x, r1.yzwy, r2.yzwy
    mov r12.xyz, r1.xyzx
else
endif
add r1.x, c24.z, c82.y
abs r1.x, r1.x
add r1.x, -r1.x, -r1.x
cmp r1.x, r1.x, c68.y, c68.z
add r1.x, -r1.x, c68.z
mov r1.yzw, r12.xxyz
cmp r1.xyz, -r1.x, r1.yzwy, r13.xyzx
max r1.xyz, r1.xyzx, c71.xyzx
mov r1.w, r0.w
mov oC0.xyzw, r1.xyzw
