ps_3_0
dcl_texcoord0 v0
def c68 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c70 = 9.99999975e-06, 1.00000000e+00, -9.99989986e-01, 3.90625000e-03
def c71 = 1.00000005e-03, 1.50000006e-01, 1.50000006e-01, 1.50000006e-01
def c72 = 3.00000000e+00, 1.60000000e+01, -1.44269502e+00, 2.50000000e-01
def c73 = 2.99999993e-02, 2.00000000e+00, -2.40000000e+01, 4.16666679e-02
def c74 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, -7.37368822e-01
def c75 = 6.75490320e-01, 7.37368822e-01, 2.00000000e+00, -2.00000000e+00
def c76 = -1.00000000e+00, 1.00000000e+00, 5.00000000e-01, -5.00000000e-01
def c77 = 1.00000000e+00, 1.00000000e+00, -5.00000000e-01, -5.00000000e-01
def c78 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c79 = -1.00000000e+00, 0.00000000e+00, 1.00000000e+00, 0.00000000e+00
def c80 = 0.00000000e+00, -1.00000000e+00, 4.00000000e+00, 9.99999975e-05
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s12
dcl_2d s1
dcl_2d s15
dcl_2d s14
dcl_2d s8
dcl_2d s0
mul r0.xy, v0.xyxx, c33.zwzz
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, r0.xxxy, c68.xxxy
rcp r1.x, c33.z
rcp r1.y, c33.w
mul r1.xy, r0.zwzz, r1.xyxx
mov r2.xyzw, c69.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s8
mov r3.xyzw, r2.xyzw
mul r1.zw, r1.xxxy, c33.xxxy
frc r4.xyzw, r1.zwzz
add r1.zw, r1.xxzw, -r4.xxxy
add r4.xy, c33.xyxx, c68.zwzz
max r1.zw, r1.xxzw, c69.xxxy
min r1.zw, r1.xxzw, r4.xxxy
add r1.zw, r1.xxzw, c68.xxxy
mul r1.zw, r1.xxzw, c0.xxxy
mov r5.xyzw, c69.xyzw
mov r5.xy, r1.zwzz
texldl r5.xyzw, r5.xyzw, s1
add r4.z, r5.x, -c1.w
mul r4.z, r4.z, c2.x
mov_sat r4.z, r4.z
mul r4.w, c0.z, c0.w
add r5.x, c0.w, -c0.z
mul r5.y, r4.z, r5.x
add r5.y, c0.w, -r5.y
max r5.y, r5.y, c70.x
rcp r5.z, r5.y
mul r5.z, r4.w, r5.z
cmp r5.w, -c30.y, c69.x, c70.y
add r4.z, r4.z, c70.z
cmp r4.z, r4.z, c69.x, c70.y
min r5.w, r5.w, r4.z
if_ne r5.w, -r5.w
    mul r5.w, r2.w, c30.z
    add r5.w, -r5.w, c70.y
    rcp r6.x, c20.z
    mul r5.w, r5.w, r6.x
    mov_sat r5.w, r5.w
    mov r6.xyz, r2.xyzx
    add r6.w, -r5.w, c70.w
    cmp r6.w, r6.w, c69.x, c70.y
    if_ne r6.w, -r6.w
        mul r6.w, r5.z, c1.z
        mad r6.w, r6.w, c25.x, c25.y
        max r6.w, r6.w, c69.x
        log r6.w, r6.w
        mul r6.w, c25.z, r6.w
        exp r6.w, r6.w
        mov_sat r6.w, r6.w
        add r6.w, r6.w, c68.z
        mad r6.w, c25.w, r6.w, c70.y
        max r6.w, r6.w, c71.x
        add r7.x, -r6.w, c70.y
        mul r7.xyz, r7.x, c26.xyzx
        mov r8.xyzw, c69.xyzw
        mov r8.xy, r1.xyxx
        texldl r8.xyzw, r8.xyzw, s12
        mov r9.xyzw, c69.xyzw
        mov r9.xy, r1.xyxx
        texldl r9.xyzw, r9.xyzw, s0
        mov r10.xyzw, c69.xyzw
        mov r10.xy, r1.xyxx
        texldl r10.xyzw, r10.xyzw, s10
        mul r9.xyz, r9.xyzx, r10.w
        max r8.xyz, r8.xyzx, c71.yzwy
        mul r8.xyz, r6.w, r8.xyzx
        min r10.xyz, r7.xyzx, r9.xyzx
        add r9.xyz, r9.xyzx, -r10.xyzx
        max r8.xyz, r8.xyzx, r9.xyzx
        mul r9.xyz, r2.xyzx, r6.w
        rcp r10.x, r8.x
        rcp r10.y, r8.y
        rcp r10.z, r8.z
        mul r9.xyz, r9.xyzx, r10.xyzx
        rcp r1.x, r5.z
        mul r1.x, c30.w, r1.x
        max r1.x, r1.x, c72.x
        min r1.x, r1.x, c72.y
        mul r1.y, r5.z, c73.x
        max r1.y, r1.y, c72.w
        rcp r1.y, r1.y
        mul r1.y, r1.y, c72.z
        mul r7.w, c30.z, c73.y
        rcp r8.w, c20.z
        mul r7.w, r7.w, r8.w
        max r10.xyz, r9.xyzx, c74.xyzx
        mov r8.w, c70.y
        mov r9.w, c68.x
        mov r11.x, c70.y
        mov r11.y, c69.x
        mov r10.w, c69.x
        rep i0.xyzw
            mov r14.z, r10.w
            add r14.z, r14.z, c73.z
            cmp r14.z, r14.z, c69.x, c70.y
            add r14.z, -r14.z, c70.y
            if_ne r14.z, -r14.z
                break
            else
            endif
            mov r14.zw, r11.xxxy
            mov r19.w, r9.w
            mul r19.w, r19.w, c73.w
            rsq r14.x, r19.w
            rcp r11.z, r14.x
            mul r19.w, r11.z, r1.x
            mul r14.zw, r14.xxzw, r19.w
            add r14.zw, r0.xxzw, r14.xxzw
            frc r20.xyzw, r14.zwzz
            add r14.zw, r14.xxzw, -r20.xxxy
            add r20.xy, c33.zwzz, c68.zwzz
            max r14.zw, r14.xxzw, c69.xxxy
            min r14.zw, r14.xxzw, r20.xxxy
            add r14.zw, r14.xxzw, c68.xxxy
            rcp r13.z, c33.z
            rcp r13.w, c33.w
            mul r14.zw, r14.xxzw, r13.xxzw
            mov r15.xyzw, c69.xyzw
            mov r15.xy, r14.zwzz
            mov r20.xyzw, r15.xyzw
            texldl r20.xyzw, r20.xyzw, s8
            mul r21.xy, r14.zwzz, c33.xyxx
            frc r22.xyzw, r21.xyxx
            add r21.xy, r21.xyxx, -r22.xyzw
            max r21.xy, r21.xyxx, c69.xyxx
            min r21.xy, r21.xyxx, r4.xyxx
            add r21.xy, r21.xyxx, c68.xyxx
            mul r21.xy, r21.xyxx, c0.xyxx
            mov r12.xyzw, c69.xyzw
            mov r12.xy, r21.xyxx
            mov r21.xyzw, r12.xyzw
            texldl r21.xyzw, r21.xyzw, s1
            add r19.w, r21.x, -c1.w
            mul r19.w, r19.w, c2.x
            mov_sat r19.w, r19.w
            mul r19.w, r19.w, r5.x
            add r19.w, c0.w, -r19.w
            max r19.w, r19.w, c70.x
            rcp r14.y, r19.w
            mul r19.w, r4.w, r14.y
            add r19.w, r19.w, -r5.z
            abs r19.w, r19.w
            mul r19.w, r19.w, r1.y
            exp r11.w, r19.w
            add r19.w, r20.w, -r2.w
            abs r19.w, r19.w
            mul r19.w, r19.w, r7.w
            add r19.w, -r19.w, c70.y
            mov_sat r19.w, r19.w
            mul r19.w, r11.w, r19.w
            mul r20.xyz, r20.xyzx, r6.w
            mov r16.xyzw, c69.xyzw
            mov r16.xy, r14.zwzz
            mov r21.xyzw, r16.xyzw
            texldl r21.xyzw, r21.xyzw, s12
            mov r17.xyzw, c69.xyzw
            mov r17.xy, r14.zwzz
            mov r22.xyzw, r17.xyzw
            texldl r22.xyzw, r22.xyzw, s0
            mov r18.xyzw, c69.xyzw
            mov r18.xy, r14.zwzz
            mov r23.xyzw, r18.xyzw
            texldl r23.xyzw, r23.xyzw, s10
            mul r22.xyz, r22.xyzx, r23.w
            max r21.xyz, r21.xyzx, c71.yzwy
            mul r21.xyz, r6.w, r21.xyzx
            min r23.xyz, r7.xyzx, r22.xyzx
            add r22.xyz, r22.xyzx, -r23.xyzx
            max r21.xyz, r21.xyzx, r22.xyzx
            rcp r19.x, r21.x
            rcp r19.y, r21.y
            rcp r19.z, r21.z
            mul r20.xyz, r20.xyzx, r19.xyzx
            max r20.xyz, r20.xyzx, c74.xyzx
            mul r20.xyz, r20.xyzx, r19.w
            mov r21.xyz, r10.xyzx
            add r20.xyz, r21.xyzx, r20.xyzx
            mov r10.xyz, r20.xyzx
            mov r14.z, r8.w
            add r14.z, r14.z, r19.w
            mov r8.w, r14.z
            mov r14.zw, r11.xxxy
            mul r14.z, r14.z, c74.w
            mov r20.xy, r11.xyxx
            mul r14.w, r20.y, c75.x
            add r14.z, r14.z, -r14.w
            mov r20.xy, r11.xyxx
            mul r14.w, r20.x, c75.x
            mov r20.xy, r11.xyxx
            mul r19.w, r20.y, c75.y
            add r14.w, r14.w, -r19.w
            mov r13.x, r14.z
            mov r13.y, r14.w
            mov r14.zw, r13.xxxy
            mov r11.xy, r14.zwzz
            mov r14.z, r9.w
            add r14.z, r14.z, c70.y
            mov r9.w, r14.z
            mov r14.z, r10.w
            add r14.z, r14.z, c70.y
            mov r10.w, r14.z
        endrep
        mov r7.xyz, r10.xyzx
        mov r1.x, r8.w
        rcp r10.x, r1.x
        rcp r10.y, r1.x
        rcp r10.z, r1.x
        mul r7.xyz, r7.xyzx, r10.xyzx
        add r7.xyz, r7.xyzx, -r9.xyzx
        mul r7.xyz, r5.w, r7.xyzx
        add r7.xyz, r9.xyzx, r7.xyzx
        mul r7.xyz, r7.xyzx, r8.xyzx
        rcp r8.x, r6.w
        rcp r8.y, r6.w
        rcp r8.z, r6.w
        mul r7.xyz, r7.xyzx, r8.xyzx
        mov r6.xyz, r7.xyzx
    else
    endif
    mov r3.xyz, r6.xyzx
else
endif
mov r6.xyzw, r3.xyzw
mov r7.x, r5.z
mov r7.y, c69.x
mov r7.z, c69.x
mov r7.w, c70.y
cmp r1.x, -c57.x, c69.x, c70.y
add r1.x, -r1.x, c70.y
add r1.y, -r4.z, c70.y
max r1.x, r1.x, r1.y
mov r8.xyzw, r9.xyzw
cmp r8.xyzw, -r1.x, r8.xyzw, r6.xyzw
mov r9.xyzw, r8.xyzw
mov r10.xyzw, r11.xyzw
cmp r10.xyzw, -r1.x, r10.xyzw, r7.xyzw
mov r11.xyzw, r10.xyzw
add r1.y, -r1.x, c70.y
if_ne r1.y, -r1.y
    rcp r1.y, r5.y
    mul r1.y, r4.w, r1.y
    mul r4.xy, c0.xyxx, c68.xyxx
    add r1.zw, r1.xxzw, -r4.xxxy
    mul r1.zw, r1.xxzw, c75.xxzw
    add r1.zw, r1.xxzw, c76.xxxy
    rcp r4.x, c1.x
    rcp r4.y, c1.y
    mul r1.zw, r1.xxzw, r4.xxxy
    mov r4.xy, r1.zwzz
    mov r4.z, c1.z
    mul r1.yzw, r4.xxyz, r1.y
    mul r4.xyz, r1.y, c3.xyzx
    mul r5.xyz, r1.z, c4.xyzx
    add r4.xyz, r4.xyzx, r5.xyzx
    mul r1.yzw, r1.w, c5.xxyz
    add r1.yzw, r4.xxyz, r1.xyzw
    add r1.yzw, r1.xyzw, c6.xxyz
    mul r4.xyz, r1.y, c53.xyzx
    mul r5.xyz, r1.z, c54.xyzx
    add r4.xyz, r4.xyzx, r5.xyzx
    mul r1.yzw, r1.w, c55.xxyz
    add r1.yzw, r4.xxyz, r1.xyzw
    add r1.yzw, r1.xyzw, c56.xxyz
    mul r4.x, r1.w, c1.z
    add r4.y, c0.z, -r4.x
    cmp r4.y, r4.y, c69.x, c70.y
    add r4.y, -r4.y, c70.y
    cmp r5.xyzw, -r4.y, r8.xyzw, r6.xyzw
    mov r9.xyzw, r5.xyzw
    cmp r8.xyzw, -r4.y, r10.xyzw, r7.xyzw
    mov r11.xyzw, r8.xyzw
    cmp r1.x, -r4.y, r1.x, c70.y
    add r4.y, -r1.x, c70.y
    if_ne r4.y, -r4.y
        mul r1.yz, r1.xyzx, c1.xxyx
        rcp r4.y, r4.x
        rcp r4.z, r4.x
        mul r1.yz, r1.xyzx, r4.xyzx
        mul r1.yz, r1.xyzx, c76.xzwx
        add r1.yz, r1.xyzx, c68.xxyx
        cmp r4.yz, r1.xyzx, c69.xxyx, c77.xxyx
        max r1.w, r4.y, r4.z
        add r4.yz, -r1.xyzx, c77.xxyx
        cmp r4.yz, r4.xyzx, c69.xxyx, c77.xxyx
        max r4.y, r4.y, r4.z
        max r1.w, r1.w, r4.y
        cmp r5.xyzw, -r1.w, r5.xyzw, r6.xyzw
        mov r9.xyzw, r5.xyzw
        cmp r8.xyzw, -r1.w, r8.xyzw, r7.xyzw
        mov r11.xyzw, r8.xyzw
        cmp r1.x, -r1.w, r1.x, c70.y
        add r1.w, -r1.x, c70.y
        if_ne r1.w, -r1.w
            mul r4.yz, r1.xyzx, c33.xzwx
            frc r10.xyzw, r4.yzyy
            add r10.xy, r4.yzyy, -r10.xyzw
            add r10.zw, c33.xxzw, c68.xxzw
            max r10.xy, r10.xyxx, c69.xyxx
            min r10.xy, r10.xyxx, r10.zwzz
            add r10.xy, r10.xyxx, c68.xyxx
            rcp r12.x, c33.z
            rcp r12.y, c33.w
            mul r10.xy, r10.xyxx, r12.xyxx
            mov r12.xyzw, c69.xyzw
            mov r12.xy, r10.xyxx
            texldl r12.xyzw, r12.xyzw, s15
            mul r1.w, r4.x, c73.x
            max r1.w, r1.w, c72.w
            add r4.w, r12.x, -r4.x
            abs r4.w, r4.w
            add r4.w, r1.w, -r4.w
            cmp r4.w, r4.w, c69.x, c70.y
            cmp r5.xyzw, -r4.w, r5.xyzw, r6.xyzw
            mov r9.xyzw, r5.xyzw
            cmp r5.xyzw, -r4.w, r8.xyzw, r7.xyzw
            mov r11.xyzw, r5.xyzw
            cmp r1.x, -r4.w, r1.x, c70.y
            add r1.x, -r1.x, c70.y
            if_ne r1.x, -r1.x
                rcp r5.x, c33.z
                rcp r5.y, c33.w
                add r5.zw, r4.xxyz, c77.xxzw
                frc r6.xyzw, r5.zwzz
                add r5.zw, r5.xxzw, -r6.xxxy
                add r5.zw, r5.xxzw, c68.xxxy
                mul r5.zw, r5.xxzw, r5.xxxy
                mov r6.xyzw, c69.xyzw
                mov r6.xy, r5.zwzz
                texldl r6.xyzw, r6.xyzw, s15
                mov r1.x, r5.x
                mov r8.x, r1.x
                mov r8.y, c69.x
                add r8.xy, r5.zwzz, r8.xyxx
                mov r12.xyzw, c69.xyzw
                mov r12.xy, r8.xyxx
                mov r8.xyzw, r12.xyzw
                texldl r8.xyzw, r8.xyzw, s15
                mov r12.x, c69.x
                mov r1.x, r5.yxxx
                mov r12.y, r1.x
                add r12.xy, r5.zwzz, r12.xyxx
                mov r13.xyzw, c69.xyzw
                mov r13.xy, r12.xyxx
                mov r12.xyzw, r13.xyzw
                texldl r12.xyzw, r12.xyzw, s15
                add r5.xy, r5.zwzz, r5.xyxx
                mov r13.xyzw, c69.xyzw
                mov r13.xy, r5.xyxx
                mov r5.xyzw, r13.xyzw
                texldl r5.xyzw, r5.xyzw, s15
                mov r6.y, r8.x
                mov r6.z, r12.x
                mov r6.w, r5.x
                mov r5.xyzw, r6.xyzw
                add r5.xyzw, r5.xyzw, -r4.x
                abs r5.xyzw, r5.xyzw
                add r5.xyzw, r1.w, -r5.xyzw
                cmp r5.xyzw, r5.xyzw, c69.xyzw, c78.xyzw
                add r5.xyzw, -r5.xyzw, c78.xyzw
                min r1.x, r5.x, r5.y
                min r1.x, r1.x, r5.z
                min r1.x, r1.x, r5.w
                cmp r1.xy, -r1.x, r10.xyxx, r1.yzyy
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r1.xyxx
                mov r1.xyzw, r5.xyzw
                texldl r1.xyzw, r1.xyzw, s14
                mov r5.xyzw, r3.xyzw
                mov r5.w, r2.w
                mov r6.xyzw, r3.xyzw
                mov r6.w, r2.w
                add r2.xy, r0.xyxx, c79.xyxx
                max r2.xy, r2.xyxx, c69.xyxx
                min r2.xy, r2.xyxx, r10.zwzz
                add r2.xy, r2.xyxx, c68.xyxx
                rcp r2.z, c33.z
                rcp r2.w, c33.w
                mul r2.xy, r2.xyxx, r2.zwzz
                mov r8.xyzw, c69.xyzw
                mov r8.xy, r2.xyxx
                mov r2.xyzw, r8.xyzw
                texldl r2.xyzw, r2.xyzw, s8
                min r5.xyzw, r5.xyzw, r2.xyzw
                max r2.xyzw, r6.xyzw, r2.xyzw
                add r4.xw, r0.xxxy, c79.zxxw
                max r4.xw, r4.xxxw, c69.xxxy
                min r4.xw, r4.xxxw, r10.zxxw
                add r4.xw, r4.xxxw, c68.xxxy
                rcp r6.x, c33.z
                rcp r6.y, c33.w
                mul r4.xw, r4.xxxw, r6.xxxy
                mov r8.xyzw, c69.xyzw
                mov r8.xy, r4.xwxx
                mov r6.xyzw, r8.xyzw
                texldl r6.xyzw, r6.xyzw, s8
                min r5.xyzw, r5.xyzw, r6.xyzw
                max r2.xyzw, r2.xyzw, r6.xyzw
                add r4.xw, r0.xxxy, c80.xxxy
                max r4.xw, r4.xxxw, c69.xxxy
                min r4.xw, r4.xxxw, r10.zxxw
                add r4.xw, r4.xxxw, c68.xxxy
                rcp r6.x, c33.z
                rcp r6.y, c33.w
                mul r4.xw, r4.xxxw, r6.xxxy
                mov r8.xyzw, c69.xyzw
                mov r8.xy, r4.xwxx
                mov r6.xyzw, r8.xyzw
                texldl r6.xyzw, r6.xyzw, s8
                min r5.xyzw, r5.xyzw, r6.xyzw
                max r2.xyzw, r2.xyzw, r6.xyzw
                add r0.xy, r0.xyxx, c79.yzyy
                max r0.xy, r0.xyxx, c69.xyxx
                min r0.xy, r0.xyxx, r10.zwzz
                add r0.xy, r0.xyxx, c68.xyxx
                rcp r4.x, c33.z
                rcp r4.w, c33.w
                mul r0.xy, r0.xyxx, r4.xwxx
                mov r8.xyzw, c69.xyzw
                mov r8.xy, r0.xyxx
                mov r6.xyzw, r8.xyzw
                texldl r6.xyzw, r6.xyzw, s8
                min r5.xyzw, r5.xyzw, r6.xyzw
                max r2.xyzw, r2.xyzw, r6.xyzw
                max r6.xyzw, r1.xyzw, r5.xyzw
                min r6.xyzw, r6.xyzw, r2.xyzw
                add r0.xy, r4.yzyy, -r0.zwzz
                dp2add r0.x, r0.xyxx, r0.xyxx, c69.x
                rsq r0.x, r0.x
                rcp r0.x, r0.x
                mul r0.x, r0.x, c80.z
                add r0.x, -r0.x, c73.y
                mov_sat r0.x, r0.x
                max r0.y, c30.z, c80.w
                rcp r0.y, r0.y
                mul r0.y, r0.y, c71.y
                mul r0.y, r0.y, c30.y
                mul r0.x, r0.y, r0.x
                add r0.y, r5.w, -r0.x
                add r0.x, r2.w, r0.x
                max r0.y, r1.w, r0.y
                min r0.x, r0.y, r0.x
                add r0.y, r0.x, -r6.w
                abs r0.y, r0.y
                mul r0.y, r0.y, c30.z
                mul r0.y, r0.y, c15.w
                add r4.xyz, r5.xyzx, -r0.y
                add r0.yzw, r2.xxyz, r0.y
                max r1.xyz, r1.xyzx, r4.xyzx
                min r0.yzw, r1.xxyz, r0.xyzw
                mov r1.xyz, r0.yzwy
                mov r1.w, r0.x
                mov r0.xyzw, r1.xyzw
                add r0.xyzw, r0.xyzw, -r3.xyzw
                mul r0.xyzw, c57.x, r0.xyzw
                add r0.xyzw, r3.xyzw, r0.xyzw
                mov r9.xyzw, r0.xyzw
                mov r11.xyzw, r7.xyzw
            else
            endif
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r9.xyzw
mov oC1.xyzw, r11.xyzw
