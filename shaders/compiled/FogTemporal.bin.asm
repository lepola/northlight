ps_3_0
dcl_texcoord0 v0
def c68 = 0.00000000e+00, 5.00000000e-01, 5.00000000e-01, 1.00000000e+00
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c70 = -1.00000000e+00, -1.00000000e+00, -9.99989986e-01, 9.99999975e-06
def c71 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c72 = 5.00000000e-01, -5.00000000e-01, 1.00000000e+00, 1.00000000e+00
def c73 = -5.00000000e-01, -5.00000000e-01, 2.50000000e-01, 2.99999993e-02
def c74 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c75 = 0.00000000e+00, -1.00000000e+00, 1.00000000e+00, -1.00000000e+00
def c76 = -1.00000000e+00, 0.00000000e+00, 1.00000000e+00, 0.00000000e+00
dcl_2d s1
dcl_2d s15
dcl_2d s9
dcl_2d s14
mov r0.x, c68.x
mul r0.yz, v0.xxyx, c33.xzwx
frc r1.xyzw, r0.yzyy
add r0.yz, r0.xyzx, -r1.xxyx
add r1.xy, r0.yzyy, c68.yzyy
rcp r1.z, c33.z
rcp r1.w, c33.w
mul r1.xy, r1.xyxx, r1.zwzz
mov r2.xyzw, c69.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s9
cmp r0.w, -c64.y, c68.x, c68.w
add r0.w, -r0.w, c68.w
mov r3.xyzw, r4.xyzw
cmp r3.xyzw, -r0.w, r3.xyzw, r2.xyzw
mov r4.xyzw, r3.xyzw
mov r0.x, r0.w
add r1.z, -r0.w, c68.w
if_ne r1.z, -r1.z
    mul r1.xy, r1.xyxx, c33.xyxx
    frc r5.xyzw, r1.xyxx
    add r1.xy, r1.xyxx, -r5.xyzw
    add r1.zw, c33.xxxy, c70.xxxy
    max r1.xy, r1.xyxx, c69.xyxx
    min r1.xy, r1.xyxx, r1.zwzz
    add r1.xy, r1.xyxx, c68.yzyy
    mul r1.xy, r1.xyxx, c0.xyxx
    mov r5.xyzw, c69.xyzw
    mov r5.xy, r1.xyxx
    texldl r5.xyzw, r5.xyzw, s1
    add r1.z, r5.x, -c1.w
    mul r1.z, r1.z, c2.x
    mov_sat r1.z, r1.z
    add r1.w, r1.z, c70.z
    cmp r1.w, r1.w, c68.x, c68.w
    add r1.w, -r1.w, c68.w
    if_ne r1.w, -r1.w
        mul r5.xy, c0.xyxx, c68.yzyy
        add r5.xy, r1.xyxx, -r5.xyxx
        mul r5.xy, r5.xyxx, c71.xyxx
        add r5.xy, r5.xyxx, c71.zwzz
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
        mul r7.xyz, r5.x, c53.xyzx
        mul r8.xyz, r5.y, c54.xyzx
        add r7.xyz, r7.xyzx, r8.xyzx
        mul r5.xyz, r5.z, c55.xyzx
        add r5.xyz, r7.xyzx, r5.xyzx
    else
        mul r5.w, c0.z, c0.w
        add r6.w, c0.w, -c0.z
        mul r1.z, r1.z, r6.w
        add r1.z, c0.w, -r1.z
        max r1.z, r1.z, c70.w
        rcp r1.z, r1.z
        mul r1.z, r5.w, r1.z
        mul r7.xy, c0.xyxx, c68.yzyy
        add r1.xy, r1.xyxx, -r7.xyxx
        mul r1.xy, r1.xyxx, c71.xyxx
        add r1.xy, r1.xyxx, c71.zwzz
        rcp r7.x, c1.x
        rcp r7.y, c1.y
        mul r1.xy, r1.xyxx, r7.xyxx
        mov r6.xy, r1.xyxx
        mov r6.z, c1.z
        mul r1.xyz, r6.xyzx, r1.z
        mul r6.xyz, r1.x, c3.xyzx
        mul r7.xyz, r1.y, c4.xyzx
        add r6.xyz, r6.xyzx, r7.xyzx
        mul r1.xyz, r1.z, c5.xyzx
        add r1.xyz, r6.xyzx, r1.xyzx
        add r1.xyz, r1.xyzx, c6.xyzx
        mul r6.xyz, r1.x, c53.xyzx
        mul r7.xyz, r1.y, c54.xyzx
        add r6.xyz, r6.xyzx, r7.xyzx
        mul r1.xyz, r1.z, c55.xyzx
        add r1.xyz, r6.xyzx, r1.xyzx
        add r1.xyz, r1.xyzx, c56.xyzx
        mov r5.xyz, r1.xyzx
    endif
    mov r1.xyz, r5.xyzx
    mul r1.x, r1.z, c1.z
    add r1.y, c0.z, -r1.x
    cmp r1.y, r1.y, c68.x, c68.w
    add r1.y, -r1.y, c68.w
    cmp r3.xyzw, -r1.y, r3.xyzw, r2.xyzw
    mov r4.xyzw, r3.xyzw
    cmp r0.w, -r1.y, r0.w, c68.w
    mov r0.x, r0.w
    add r1.y, -r0.w, c68.w
    if_ne r1.y, -r1.y
        mul r1.yz, r5.xxyx, c1.xxyx
        rcp r5.x, r1.x
        rcp r5.y, r1.x
        mul r1.yz, r1.xyzx, r5.xxyx
        mul r1.yz, r1.xyzx, c72.xxyx
        add r1.yz, r1.xyzx, c68.xyzx
        cmp r5.xy, r1.yzyy, c69.xyxx, c72.zwzz
        max r5.x, r5.x, r5.y
        add r5.yz, -r1.xyzx, c72.xzwx
        cmp r5.yz, r5.xyzx, c69.xxyx, c72.xzwx
        max r5.y, r5.y, r5.z
        max r5.x, r5.x, r5.y
        cmp r3.xyzw, -r5.x, r3.xyzw, r2.xyzw
        mov r4.xyzw, r3.xyzw
        cmp r0.w, -r5.x, r0.w, c68.w
        mov r0.x, r0.w
        add r5.x, -r0.w, c68.w
        if_ne r5.x, -r5.x
            mul r5.xy, r1.yzyy, c33.zwzz
            frc r6.xyzw, r5.xyxx
            add r5.zw, r5.xxxy, -r6.xxxy
            add r6.xy, c33.zwzz, c70.xyxx
            max r5.zw, r5.xxzw, c69.xxxy
            min r5.zw, r5.xxzw, r6.xxxy
            add r5.zw, r5.xxzw, c68.xxyz
            rcp r6.z, c33.z
            rcp r6.w, c33.w
            mul r5.zw, r5.xxzw, r6.xxzw
            rcp r6.z, c33.z
            rcp r6.w, c33.w
            add r5.xy, r5.xyxx, c73.xyxx
            frc r7.xyzw, r5.xyxx
            add r5.xy, r5.xyxx, -r7.xyzw
            add r5.xy, r5.xyxx, c68.yzyy
            mul r5.xy, r5.xyxx, r6.zwzz
            mul r7.x, r1.x, c73.w
            max r7.x, r7.x, c73.z
            mov r7.y, c68.w
            add r1.w, -r1.w, c68.w
            if_ne r1.w, -r1.w
                mov r8.xyzw, c69.xyzw
                mov r8.xy, r5.zwzz
                texldl r8.xyzw, r8.xyzw, s15
                add r1.w, r8.x, -r1.x
                abs r1.w, r1.w
                add r1.w, r7.x, -r1.w
                cmp r1.w, r1.w, c68.x, c68.w
                cmp r3.xyzw, -r1.w, r3.xyzw, r2.xyzw
                mov r4.xyzw, r3.xyzw
                cmp r0.w, -r1.w, r0.w, c68.w
                mov r0.x, r0.w
                add r0.w, -r0.w, c68.w
                if_ne r0.w, -r0.w
                    mov r3.xyzw, c69.xyzw
                    mov r3.xy, r5.xyxx
                    texldl r3.xyzw, r3.xyzw, s15
                    mov r0.w, r6.z
                    mov r7.z, r0.w
                    mov r7.w, c68.x
                    add r7.zw, r5.xxxy, r7.xxzw
                    mov r8.xyzw, c69.xyzw
                    mov r8.xy, r7.zwzz
                    texldl r8.xyzw, r8.xyzw, s15
                    mov r7.z, c68.x
                    mov r0.w, r6.xxxw
                    mov r7.w, r0.w
                    add r7.zw, r5.xxxy, r7.xxzw
                    mov r9.xyzw, c69.xyzw
                    mov r9.xy, r7.zwzz
                    texldl r9.xyzw, r9.xyzw, s15
                    add r5.xy, r5.xyxx, r6.zwzz
                    mov r10.xyzw, c69.xyzw
                    mov r10.xy, r5.xyxx
                    texldl r10.xyzw, r10.xyzw, s15
                    mov r3.y, r8.x
                    mov r3.z, r9.x
                    mov r3.w, r10.x
                    add r3.xyzw, r3.xyzw, -r1.x
                    abs r3.xyzw, r3.xyzw
                    add r3.xyzw, r7.x, -r3.xyzw
                    cmp r3.xyzw, r3.xyzw, c69.xyzw, c74.xyzw
                    add r3.xyzw, -r3.xyzw, c74.xyzw
                    min r0.w, r3.x, r3.y
                    min r0.w, r0.w, r3.z
                    min r0.w, r0.w, r3.w
                    mov r7.y, r0.w
                else
                endif
            else
            endif
            add r0.x, -r0.x, c68.w
            if_ne r0.x, -r0.x
                mov r0.x, r7.y
                cmp r0.xw, -r0.x, r5.zxxw, r1.yxxz
                mov r1.xyzw, c69.xyzw
                mov r1.xy, r0.xwxx
                texldl r1.xyzw, r1.xyzw, s14
                add r0.xw, r0.yxxz, c70.xxxy
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r3.x, c33.z
                rcp r3.y, c33.w
                mul r0.xw, r0.xxxw, r3.xxxy
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r5.xyzw, r3.xyzw
                texldl r5.xyzw, r5.xyzw, s9
                min r7.xyzw, r2.xyzw, r5.xyzw
                max r5.xyzw, r2.xyzw, r5.xyzw
                add r0.xw, r0.yxxz, c75.xxxy
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xw, r0.yxxz, c75.zxxw
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xw, r0.yxxz, c76.xxxy
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xw, r0.yxxz, c76.zxxw
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xw, r0.yxxz, c71.zxxw
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xw, r0.yxxz, c76.yxxz
                max r0.xw, r0.xxxw, c69.xxxy
                min r0.xw, r0.xxxw, r6.xxxy
                add r0.xw, r0.xxxw, c68.yxxz
                rcp r6.z, c33.z
                rcp r6.w, c33.w
                mul r0.xw, r0.xxxw, r6.zxxw
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xwxx
                mov r8.xyzw, r3.xyzw
                texldl r8.xyzw, r8.xyzw, s9
                min r7.xyzw, r7.xyzw, r8.xyzw
                max r5.xyzw, r5.xyzw, r8.xyzw
                add r0.xy, r0.yzyy, c72.zwzz
                max r0.xy, r0.xyxx, c69.xyxx
                min r0.xy, r0.xyxx, r6.xyxx
                add r0.xy, r0.xyxx, c68.yzyy
                rcp r0.z, c33.z
                rcp r0.w, c33.w
                mul r0.xy, r0.xyxx, r0.zwzz
                mov r3.xyzw, c69.xyzw
                mov r3.xy, r0.xyxx
                mov r0.xyzw, r3.xyzw
                texldl r0.xyzw, r0.xyzw, s9
                min r3.xyzw, r7.xyzw, r0.xyzw
                max r0.xyzw, r5.xyzw, r0.xyzw
                max r1.xyzw, r1.xyzw, r3.xyzw
                min r0.xyzw, r1.xyzw, r0.xyzw
                add r0.xyzw, r0.xyzw, -r2.xyzw
                mul r0.xyzw, c64.y, r0.xyzw
                add r0.xyzw, r2.xyzw, r0.xyzw
                mov r4.xyzw, r0.xyzw
            else
            endif
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r4.xyzw
