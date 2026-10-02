ps_3_0
dcl_texcoord0 v0
def c68 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c70 = 9.99999975e-06, 1.00000000e+00, -9.99989986e-01, 2.50000000e-01
def c71 = 2.00000000e+00, -2.00000000e+00, -1.00000000e+00, 1.00000000e+00
def c72 = 5.00000000e-01, -5.00000000e-01, 1.00000000e+00, 1.00000000e+00
def c73 = 2.99999993e-02, -5.00000000e-01, -5.00000000e-01, 4.00000000e+00
def c74 = 1.00000000e+00, 1.00000000e+00, 1.00000000e+00, 1.00000000e+00
def c75 = -1.00000000e+00, 0.00000000e+00, 1.00000000e+00, 0.00000000e+00
def c76 = 0.00000000e+00, -1.00000000e+00, 1.50000006e-01, 9.99999975e-05
dcl_2d s1
dcl_2d s15
dcl_2d s14
dcl_2d s8
dcl_2d s9
mul r0.xy, v0.xyxx, c33.zwzz
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.zw, r0.xxxy, c68.xxxy
rcp r1.x, c33.z
rcp r1.y, c33.w
mul r1.xy, r0.zwzz, r1.xyxx
mov r2.xyzw, c69.xyzw
mov r2.xy, r1.xyxx
texldl r2.xyzw, r2.xyzw, s9
mul r1.xy, r1.xyxx, c33.xyxx
frc r3.xyzw, r1.xyxx
add r1.xy, r1.xyxx, -r3.xyzw
add r1.zw, c33.xxxy, c68.xxzw
max r1.xy, r1.xyxx, c69.xyxx
min r1.xy, r1.xyxx, r1.zwzz
add r1.xy, r1.xyxx, c68.xyxx
mul r1.xy, r1.xyxx, c0.xyxx
mov r3.xyzw, c69.xyzw
mov r3.xy, r1.xyxx
texldl r3.xyzw, r3.xyzw, s1
add r1.z, r3.x, -c1.w
mul r1.z, r1.z, c2.x
mov_sat r1.z, r1.z
mul r1.w, c0.z, c0.w
add r3.x, c0.w, -c0.z
mul r3.x, r1.z, r3.x
add r3.x, c0.w, -r3.x
max r3.x, r3.x, c70.x
rcp r3.y, r3.x
mul r3.y, r1.w, r3.y
mov r4.x, r3.y
mov r4.y, c69.x
mov r4.z, c69.x
mov r4.w, c70.y
cmp r3.y, -c57.x, c69.x, c70.y
add r3.y, -r3.y, c70.y
add r1.z, r1.z, c70.z
cmp r1.z, r1.z, c69.x, c70.y
add r1.z, -r1.z, c70.y
max r1.z, r3.y, r1.z
mov r5.xyzw, r6.xyzw
cmp r5.xyzw, -r1.z, r5.xyzw, r2.xyzw
mov r6.xyzw, r5.xyzw
mov r7.xyzw, r8.xyzw
cmp r7.xyzw, -r1.z, r7.xyzw, r4.xyzw
mov r8.xyzw, r7.xyzw
add r3.y, -r1.z, c70.y
if_ne r3.y, -r3.y
    rcp r3.x, r3.x
    mul r1.w, r1.w, r3.x
    mul r3.xy, c0.xyxx, c68.xyxx
    add r1.xy, r1.xyxx, -r3.xyxx
    mul r1.xy, r1.xyxx, c71.xyxx
    add r1.xy, r1.xyxx, c71.zwzz
    rcp r3.x, c1.x
    rcp r3.y, c1.y
    mul r1.xy, r1.xyxx, r3.xyxx
    mov r3.xy, r1.xyxx
    mov r3.z, c1.z
    mul r1.xyw, r3.xyxz, r1.w
    mul r3.xyz, r1.x, c3.xyzx
    mul r9.xyz, r1.y, c4.xyzx
    add r3.xyz, r3.xyzx, r9.xyzx
    mul r1.xyw, r1.w, c5.xyxz
    add r1.xyw, r3.xyxz, r1.xyxw
    add r1.xyw, r1.xyxw, c6.xyxz
    mul r3.xyz, r1.x, c53.xyzx
    mul r9.xyz, r1.y, c54.xyzx
    add r3.xyz, r3.xyzx, r9.xyzx
    mul r1.xyw, r1.w, c55.xyxz
    add r1.xyw, r3.xyxz, r1.xyxw
    add r1.xyw, r1.xyxw, c56.xyxz
    mul r3.x, r1.w, c1.z
    add r3.y, c0.z, -r3.x
    cmp r3.y, r3.y, c69.x, c70.y
    add r3.y, -r3.y, c70.y
    cmp r5.xyzw, -r3.y, r5.xyzw, r2.xyzw
    mov r6.xyzw, r5.xyzw
    cmp r7.xyzw, -r3.y, r7.xyzw, r4.xyzw
    mov r8.xyzw, r7.xyzw
    cmp r1.z, -r3.y, r1.z, c70.y
    add r3.y, -r1.z, c70.y
    if_ne r3.y, -r3.y
        mul r1.xy, r1.xyxx, c1.xyxx
        rcp r3.y, r3.x
        rcp r3.z, r3.x
        mul r1.xy, r1.xyxx, r3.yzyy
        mul r1.xy, r1.xyxx, c72.xyxx
        add r1.xy, r1.xyxx, c68.xyxx
        cmp r3.yz, r1.xxyx, c69.xxyx, c72.xzwx
        max r1.w, r3.y, r3.z
        add r3.yz, -r1.xxyx, c72.xzwx
        cmp r3.yz, r3.xyzx, c69.xxyx, c72.xzwx
        max r3.y, r3.y, r3.z
        max r1.w, r1.w, r3.y
        cmp r5.xyzw, -r1.w, r5.xyzw, r2.xyzw
        mov r6.xyzw, r5.xyzw
        cmp r7.xyzw, -r1.w, r7.xyzw, r4.xyzw
        mov r8.xyzw, r7.xyzw
        cmp r1.z, -r1.w, r1.z, c70.y
        add r1.w, -r1.z, c70.y
        if_ne r1.w, -r1.w
            mul r3.yz, r1.xxyx, c33.xzwx
            frc r9.xyzw, r3.yzyy
            add r9.xy, r3.yzyy, -r9.xyzw
            add r9.zw, c33.xxzw, c68.xxzw
            max r9.xy, r9.xyxx, c69.xyxx
            min r9.xy, r9.xyxx, r9.zwzz
            add r9.xy, r9.xyxx, c68.xyxx
            rcp r10.x, c33.z
            rcp r10.y, c33.w
            mul r9.xy, r9.xyxx, r10.xyxx
            mov r10.xyzw, c69.xyzw
            mov r10.xy, r9.xyxx
            texldl r10.xyzw, r10.xyzw, s15
            mul r1.w, r3.x, c73.x
            max r1.w, r1.w, c70.w
            add r3.w, r10.x, -r3.x
            abs r3.w, r3.w
            add r3.w, r1.w, -r3.w
            cmp r3.w, r3.w, c69.x, c70.y
            cmp r5.xyzw, -r3.w, r5.xyzw, r2.xyzw
            mov r6.xyzw, r5.xyzw
            cmp r5.xyzw, -r3.w, r7.xyzw, r4.xyzw
            mov r8.xyzw, r5.xyzw
            cmp r1.z, -r3.w, r1.z, c70.y
            add r1.z, -r1.z, c70.y
            if_ne r1.z, -r1.z
                rcp r5.x, c33.z
                rcp r5.y, c33.w
                add r5.zw, r3.xxyz, c73.xxyz
                frc r7.xyzw, r5.zwzz
                add r5.zw, r5.xxzw, -r7.xxxy
                add r5.zw, r5.xxzw, c68.xxxy
                mul r5.zw, r5.xxzw, r5.xxxy
                mov r7.xyzw, c69.xyzw
                mov r7.xy, r5.zwzz
                texldl r7.xyzw, r7.xyzw, s15
                mov r1.z, r5.x
                mov r10.x, r1.z
                mov r10.y, c69.x
                add r10.xy, r5.zwzz, r10.xyxx
                mov r11.xyzw, c69.xyzw
                mov r11.xy, r10.xyxx
                mov r10.xyzw, r11.xyzw
                texldl r10.xyzw, r10.xyzw, s15
                mov r11.x, c69.x
                mov r1.z, r5.xxyx
                mov r11.y, r1.z
                add r11.xy, r5.zwzz, r11.xyxx
                mov r12.xyzw, c69.xyzw
                mov r12.xy, r11.xyxx
                mov r11.xyzw, r12.xyzw
                texldl r11.xyzw, r11.xyzw, s15
                add r5.xy, r5.zwzz, r5.xyxx
                mov r12.xyzw, c69.xyzw
                mov r12.xy, r5.xyxx
                mov r5.xyzw, r12.xyzw
                texldl r5.xyzw, r5.xyzw, s15
                mov r7.y, r10.x
                mov r7.z, r11.x
                mov r7.w, r5.x
                mov r5.xyzw, r7.xyzw
                add r5.xyzw, r5.xyzw, -r3.x
                abs r5.xyzw, r5.xyzw
                add r5.xyzw, r1.w, -r5.xyzw
                cmp r5.xyzw, r5.xyzw, c69.xyzw, c74.xyzw
                add r5.xyzw, -r5.xyzw, c74.xyzw
                min r1.z, r5.x, r5.y
                min r1.z, r1.z, r5.z
                min r1.z, r1.z, r5.w
                cmp r1.xy, -r1.z, r9.xyxx, r1.xyxx
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r1.xyxx
                mov r1.xyzw, r5.xyzw
                texldl r1.xyzw, r1.xyzw, s14
                add r3.xw, r0.xxxy, c75.xxxy
                max r3.xw, r3.xxxw, c69.xxxy
                min r3.xw, r3.xxxw, r9.zxxw
                add r3.xw, r3.xxxw, c68.xxxy
                rcp r5.x, c33.z
                rcp r5.y, c33.w
                mul r3.xw, r3.xxxw, r5.xxxy
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r3.xwxx
                mov r7.xyzw, r5.xyzw
                texldl r7.xyzw, r7.xyzw, s8
                min r10.xyzw, r2.xyzw, r7.xyzw
                max r7.xyzw, r2.xyzw, r7.xyzw
                add r3.xw, r0.xxxy, c75.zxxw
                max r3.xw, r3.xxxw, c69.xxxy
                min r3.xw, r3.xxxw, r9.zxxw
                add r3.xw, r3.xxxw, c68.xxxy
                rcp r9.x, c33.z
                rcp r9.y, c33.w
                mul r3.xw, r3.xxxw, r9.xxxy
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r3.xwxx
                mov r11.xyzw, r5.xyzw
                texldl r11.xyzw, r11.xyzw, s8
                min r10.xyzw, r10.xyzw, r11.xyzw
                max r7.xyzw, r7.xyzw, r11.xyzw
                add r3.xw, r0.xxxy, c76.xxxy
                max r3.xw, r3.xxxw, c69.xxxy
                min r3.xw, r3.xxxw, r9.zxxw
                add r3.xw, r3.xxxw, c68.xxxy
                rcp r9.x, c33.z
                rcp r9.y, c33.w
                mul r3.xw, r3.xxxw, r9.xxxy
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r3.xwxx
                mov r11.xyzw, r5.xyzw
                texldl r11.xyzw, r11.xyzw, s8
                min r10.xyzw, r10.xyzw, r11.xyzw
                max r7.xyzw, r7.xyzw, r11.xyzw
                add r0.xy, r0.xyxx, c75.yzyy
                max r0.xy, r0.xyxx, c69.xyxx
                min r0.xy, r0.xyxx, r9.zwzz
                add r0.xy, r0.xyxx, c68.xyxx
                rcp r3.x, c33.z
                rcp r3.w, c33.w
                mul r0.xy, r0.xyxx, r3.xwxx
                mov r5.xyzw, c69.xyzw
                mov r5.xy, r0.xyxx
                texldl r5.xyzw, r5.xyzw, s8
                min r9.xyzw, r10.xyzw, r5.xyzw
                max r5.xyzw, r7.xyzw, r5.xyzw
                max r7.xyzw, r1.xyzw, r9.xyzw
                min r7.xyzw, r7.xyzw, r5.xyzw
                add r0.xy, r3.yzyy, -r0.zwzz
                dp2add r0.x, r0.xyxx, r0.xyxx, c69.x
                rsq r0.x, r0.x
                rcp r0.x, r0.x
                mul r0.x, r0.x, c73.w
                add r0.x, -r0.x, c71.x
                mov_sat r0.x, r0.x
                max r0.y, c30.z, c76.w
                rcp r0.y, r0.y
                mul r0.y, r0.y, c76.z
                mul r0.y, r0.y, c30.y
                mul r0.x, r0.y, r0.x
                add r0.y, r9.w, -r0.x
                add r0.x, r5.w, r0.x
                max r0.y, r1.w, r0.y
                min r0.x, r0.y, r0.x
                add r0.y, r0.x, -r7.w
                abs r0.y, r0.y
                mul r0.y, r0.y, c30.z
                mul r0.y, r0.y, c15.w
                add r3.xyz, r9.xyzx, -r0.y
                add r0.yzw, r5.xxyz, r0.y
                max r1.xyz, r1.xyzx, r3.xyzx
                min r0.yzw, r1.xxyz, r0.xyzw
                mov r1.xyz, r0.yzwy
                mov r1.w, r0.x
                mov r0.xyzw, r1.xyzw
                add r0.xyzw, r0.xyzw, -r2.xyzw
                mul r0.xyzw, c57.x, r0.xyzw
                add r0.xyzw, r2.xyzw, r0.xyzw
                mov r6.xyzw, r0.xyzw
                mov r8.xyzw, r4.xyzw
            else
            endif
        else
        endif
    else
    endif
else
endif
mov oC0.xyzw, r6.xyzw
mov oC1.xyzw, r8.xyzw
