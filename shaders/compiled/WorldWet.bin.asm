ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c69 = -1.00000000e+00, -1.00000000e+00, 5.00000000e-01, 5.00000000e-01
def c70 = -9.99989986e-01, 1.00000000e+00, -5.00000000e-01, 9.99999975e-06
def c71 = 1.20000001e-02, 6.99999975e-04, 3.00000003e-03, -5.50000012e-01
def c72 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c73 = 2.85714293e+00, -4.00000000e+00, 2.22222233e+00, 4.00000000e+00
def c74 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 6.00000000e+00
def c75 = 5.00000000e+00, 1.99999996e-02, 2.50000000e-01, -2.40000000e+01
def c76 = 2.08333340e-02, 9.59999979e-01, 3.99999991e-02, 1.50000006e-01
def c77 = -3.49999994e-01, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
defi i0 = 255, 0, 0, 0
dcl_2d s1
dcl_2d s14
dcl_2d s10
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
    add r1.w, r0.z, c71.w
    mul r1.w, r1.w, c73.x
    mov_sat r1.w, r1.w
    rcp r2.x, c19.w
    rcp r2.y, c19.w
    rcp r2.z, c19.w
    mul r2.xyz, r1.xyzx, r2.xyzx
    frc r4.xyzw, r2.xyzx
    add r4.xyz, r2.xyzx, -r4.xyzw
    mov r5.xyz, r4.xyzx
    add r2.w, r4.z, c70.y
    mov r5.z, r2.w
    add r2.xy, r2.xyxx, -r4.xyxx
    mov r2.z, c68.x
    mov r2.w, c68.x
    add r4.x, c20.w, c70.z
    cmp r4.x, r4.x, c68.x, c70.y
    add r4.x, -r4.x, c70.y
    if_ne r4.x, -r4.x
        mov r4.x, c68.x
        rep i0.xyzw
            mov r13.z, r4.x
            add r13.z, r13.z, c73.y
            cmp r13.z, r13.z, c68.x, c70.y
            add r13.z, -r13.z, c70.y
            if_ne r13.z, -r13.z
                break
            else
            endif
            mov r13.z, r4.x
            mov r13.w, r4.x
            mul r13.w, r13.w, c69.z
            frc r15.xyzw, r13.w
            add r13.w, r13.w, -r15.x
            mul r13.w, r13.w, c72.x
            add r13.z, r13.z, -r13.w
            mov r13.w, r4.x
            mul r13.w, r13.w, c69.z
            frc r15.xyzw, r13.w
            add r13.w, r13.w, -r15.x
            mov r10.z, r13.z
            mov r10.w, r13.w
            mov r15.xyz, r5.xyzx
            mov r14.x, r13.z
            mov r14.y, r13.w
            mov r14.z, c68.x
            mov r16.xyz, r14.xyzx
            add r15.xyz, r15.xyzx, r16.xyzx
            rcp r6.x, c20.x
            rcp r6.y, c20.x
            rcp r6.z, c20.x
            mul r16.xyz, r15.xyzx, r6.xyzx
            frc r17.xyzw, r16.xyzx
            add r16.xyz, r16.xyzx, -r17.xyzw
            mul r16.xyz, c20.x, r16.xyzx
            add r16.xyz, r15.xyzx, -r16.xyzx
            mul r13.z, r16.z, c20.x
            add r13.z, r16.x, r13.z
            mov r4.y, r13.z
            mov r4.z, r16.y
            mov r13.zw, r4.xxyz
            add r13.zw, r13.xxzw, c69.xxzw
            mul r14.w, c20.x, c20.x
            mov r13.x, r14.w
            mov r13.y, c20.x
            mov r17.xy, r13.xyxx
            rcp r10.x, r17.x
            rcp r10.y, r17.y
            mul r13.zw, r13.xxzw, r10.xxxy
            mov r11.xyzw, c68.xyzw
            mov r11.xy, r13.zwzz
            mov r17.xyzw, r11.xyzw
            texldl r17.xyzw, r17.xyzw, s10
            add r18.xyz, r17.xyzx, -r15.xyzx
            abs r18.xyz, r18.xyzx
            add r18.xyz, -r18.xyzx, -r18.xyzx
            cmp r18.xyz, r18.xyzx, c68.xyzx, c74.xyzx
            add r18.xyz, -r18.xyzx, c74.xyzx
            min r14.w, r18.x, r18.y
            min r14.w, r14.w, r18.z
            cmp r15.w, r17.w, c68.x, c70.y
            add r15.w, -r15.w, c70.y
            min r14.w, r14.w, r15.w
            if_ne r14.w, -r14.w
                add r18.xy, -r2.xyxx, c74.xyxx
                mov r18.zw, r10.xxzw
                add r19.xy, r2.xyxx, -r18.xyxx
                mul r18.zw, r18.xxzw, r19.xxxy
                add r18.xy, r18.xyxx, r18.zwzz
                mul r14.w, r18.x, r18.y
                add r15.w, c24.w, -r17.w
                mul r15.w, r15.w, c73.z
                mov_sat r15.w, r15.w
                mul r14.w, r14.w, r15.w
                mul r15.w, c20.x, c73.w
                add r15.w, r16.y, r15.w
                add r15.w, r15.w, c69.z
                mul r16.w, c20.x, c74.w
                rcp r6.w, r16.w
                mul r15.w, r15.w, r6.w
                mov r8.x, r13.z
                mov r8.y, r15.w
                mov r8.z, c68.x
                mov r8.w, c68.x
                mov r17.xyzw, r8.xyzw
                mov r12.xyzw, c68.xyzw
                mov r12.xy, r17.xyxx
                mov r17.xyzw, r12.xyzw
                texldl r17.xyzw, r17.xyzw, s7
                mul r15.w, c20.x, c75.x
                add r15.w, r16.y, r15.w
                add r15.w, r15.w, c69.z
                rcp r4.w, r16.w
                mul r15.w, r15.w, r4.w
                mov r7.x, r13.z
                mov r7.y, r15.w
                mov r7.z, c68.x
                mov r7.w, c68.x
                mov r16.xyzw, r7.xyzw
                mov r9.xyzw, c68.xyzw
                mov r9.xy, r16.xyxx
                mov r16.xyzw, r9.xyzw
                texldl r16.xyzw, r16.xyzw, s7
                mul r13.z, r15.z, c19.w
                add r13.z, r13.z, -r1.z
                mul r13.w, r16.x, r16.x
                add r13.w, r16.y, -r13.w
                max r13.w, r13.w, c75.y
                add r13.z, r13.z, -r16.x
                mul r15.x, c19.w, c75.z
                add r13.z, r13.z, -r15.x
                max r13.z, r13.z, c68.x
                mul r13.z, r13.z, r13.z
                add r13.z, r13.w, r13.z
                rcp r5.w, r13.z
                mul r13.z, r13.w, r5.w
                mul r13.z, r13.z, r13.z
                mul r13.z, r13.z, r16.z
                mul r13.z, r14.w, r13.z
                add r13.w, r17.x, c75.w
                mul r13.w, r13.w, c76.x
                mov_sat r13.w, r13.w
                mul r13.w, r13.w, r17.z
                mul r13.w, r13.w, r13.z
                mov r14.w, r2.z
                add r13.w, r14.w, r13.w
                mov r2.z, r13.w
                mov r13.w, r2.w
                add r13.z, r13.w, r13.z
                mov r2.w, r13.z
            else
            endif
            mov r13.z, r4.x
            add r13.z, r13.z, c70.y
            mov r4.x, r13.z
        endrep
    else
    endif
    mov r2.x, r2.w
    add r2.x, -r2.x, c70.w
    cmp r2.x, r2.x, c68.x, c70.y
    mov r2.y, r2.z
    mov r2.z, r2.w
    rcp r2.z, r2.z
    mul r2.y, r2.y, r2.z
    cmp r2.x, -r2.x, c68.x, r2.y
    mul r1.w, c59.y, r1.w
    mul r1.w, r1.w, r2.x
    mul r1.w, r1.w, r0.w
    mov_sat r1.w, r1.w
    add r1.xyz, c15.xyzx, -r1.xyzx
    dp3 r2.x, r1.xyzx, r1.xyzx
    rsq r2.x, r2.x
    mov r2.xyz, r2.x
    mul r1.xyz, r2.xyzx, r1.xyzx
    dp3 r0.x, r0.xyzx, r1.xyzx
    mov_sat r0.x, r0.x
    add r0.x, -r0.x, c70.y
    log r0.x, r0.x
    mul r0.x, r0.x, c75.x
    exp r0.x, r0.x
    mul r0.x, r0.x, c76.y
    add r0.x, r0.x, c76.z
    mul r0.yzw, r1.w, c18.xxyz
    mul r0.x, r0.x, c76.w
    add r0.x, r0.x, c77.x
    mul r0.xyz, r0.yzwy, r0.x
    mov r0.w, c68.x
    mov r3.xyzw, r0.xyzw
else
endif
mov oC0.xyzw, r3.xyzw
