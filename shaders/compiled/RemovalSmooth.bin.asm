ps_3_0
dcl_texcoord0 v0
def c68 = 5.00000000e-01, 5.00000000e-01, -1.00000000e+00, -1.00000000e+00
def c69 = 0.00000000e+00, 0.00000000e+00, 0.00000000e+00, 0.00000000e+00
def c70 = 1.00000000e+00, -9.99989986e-01, 9.99999975e-06, 3.90625000e-03
def c71 = 1.00000005e-03, 1.50000006e-01, 1.50000006e-01, 1.50000006e-01
def c72 = 3.00000000e+00, 1.60000000e+01, -1.44269502e+00, 2.50000000e-01
def c73 = 2.99999993e-02, 2.00000000e+00, -2.40000000e+01, 4.16666679e-02
def c74 = -4.49999988e-01, -4.49999988e-01, -4.49999988e-01, -7.65999973e-01
def c75 = 7.13000011e+00, -7.37368822e-01, 6.75490320e-01, 7.37368822e-01
defi i0 = 255, 0, 0, 0
dcl_2d s10
dcl_2d s12
dcl_2d s1
dcl_2d s8
dcl_2d s14
dcl_2d s0
mul r0.xy, v0.xyxx, c33.zwzz
frc r1.xyzw, r0.xyxx
add r0.xy, r0.xyxx, -r1.xyzw
add r0.xy, r0.xyxx, c68.xyxx
rcp r0.z, c33.z
rcp r0.w, c33.w
mul r0.zw, r0.xxxy, r0.xxzw
mov r1.xyzw, c69.xyzw
mov r1.xy, r0.zwzz
texldl r1.xyzw, r1.xyzw, s8
mov r2.xyzw, r1.xyzw
mul r3.xy, r0.zwzz, c33.xyxx
frc r4.xyzw, r3.xyxx
add r3.xy, r3.xyxx, -r4.xyzw
add r3.zw, c33.xxxy, c68.xxzw
max r3.xy, r3.xyxx, c69.xyxx
min r3.xy, r3.xyxx, r3.zwzz
add r3.xy, r3.xyxx, c68.xyxx
mul r3.xy, r3.xyxx, c0.xyxx
mov r4.xyzw, c69.xyzw
mov r4.xy, r3.xyxx
texldl r4.xyzw, r4.xyzw, s1
add r3.x, r4.x, -c1.w
mul r3.x, r3.x, c2.x
mov_sat r3.x, r3.x
cmp r3.y, -c30.y, c69.x, c70.x
add r4.x, r3.x, c70.y
cmp r4.x, r4.x, c69.x, c70.x
min r3.y, r3.y, r4.x
if_ne r3.y, -r3.y
    mul r3.y, c0.z, c0.w
    add r4.x, c0.w, -c0.z
    mul r3.x, r3.x, r4.x
    add r3.x, c0.w, -r3.x
    max r3.x, r3.x, c70.z
    rcp r3.x, r3.x
    mul r3.x, r3.y, r3.x
    mul r4.y, r1.w, c30.z
    add r4.y, -r4.y, c70.x
    rcp r4.z, c20.z
    mul r4.y, r4.y, r4.z
    mov_sat r4.y, r4.y
    mov r5.xyz, r1.xyzx
    add r4.z, -r4.y, c70.w
    cmp r4.z, r4.z, c69.x, c70.x
    if_ne r4.z, -r4.z
        mul r4.z, r3.x, c1.z
        mad r4.z, r4.z, c25.x, c25.y
        max r4.z, r4.z, c69.x
        log r4.z, r4.z
        mul r4.z, c25.z, r4.z
        exp r4.z, r4.z
        mov_sat r4.z, r4.z
        add r4.z, r4.z, c68.z
        mad r4.z, c25.w, r4.z, c70.x
        max r4.z, r4.z, c71.x
        add r4.w, -r4.z, c70.x
        mul r6.xyz, r4.w, c26.xyzx
        mov r7.xyzw, c69.xyzw
        mov r7.xy, r0.zwzz
        texldl r7.xyzw, r7.xyzw, s12
        mov r8.xyzw, c69.xyzw
        mov r8.xy, r0.zwzz
        texldl r8.xyzw, r8.xyzw, s0
        mov r9.xyzw, c69.xyzw
        mov r9.xy, r0.zwzz
        texldl r9.xyzw, r9.xyzw, s10
        mul r8.xyz, r8.xyzx, r9.w
        max r7.xyz, r7.xyzx, c71.yzwy
        mul r7.xyz, r4.z, r7.xyzx
        min r9.xyz, r6.xyzx, r8.xyzx
        add r8.xyz, r8.xyzx, -r9.xyzx
        max r7.xyz, r7.xyzx, r8.xyzx
        mul r8.xyz, r1.xyzx, r4.z
        rcp r9.x, r7.x
        rcp r9.y, r7.y
        rcp r9.z, r7.z
        mul r8.xyz, r8.xyzx, r9.xyzx
        rcp r4.w, r3.x
        mul r4.w, c30.w, r4.w
        max r4.w, r4.w, c72.x
        min r4.w, r4.w, c72.y
        mul r5.w, r3.x, c73.x
        max r5.w, r5.w, c72.w
        rcp r5.w, r5.w
        mul r5.w, r5.w, c72.z
        mul r6.w, c30.z, c73.y
        rcp r7.w, c20.z
        mul r6.w, r6.w, r7.w
        mov r9.xyzw, c69.xyzw
        mov r9.xy, r0.zwzz
        texldl r9.xyzw, r9.xyzw, s14
        max r10.xyz, r8.xyzx, c74.xyzx
        mov r0.z, c70.x
        mov r0.w, c68.x
        mov r11.x, c70.x
        mov r11.y, c69.x
        mov r7.w, c69.x
        rep i0.xyzw
            mov r19.w, r7.w
            add r19.w, r19.w, c73.z
            cmp r19.w, r19.w, c69.x, c70.x
            add r19.w, -r19.w, c70.x
            if_ne r19.w, -r19.w
                break
            else
            endif
            mov r20.xy, r11.xyxx
            mov r19.w, r0.w
            mul r19.w, r19.w, c73.w
            rsq r11.z, r19.w
            rcp r11.w, r11.z
            mul r19.w, r11.w, r4.w
            mul r20.xy, r20.xyxx, r19.w
            add r20.xy, r0.xyxx, r20.xyxx
            frc r21.xyzw, r20.xyxx
            add r20.xy, r20.xyxx, -r21.xyzw
            add r20.zw, c33.xxzw, c68.xxzw
            max r20.xy, r20.xyxx, c69.xyxx
            min r20.xy, r20.xyxx, r20.zwzz
            add r20.xy, r20.xyxx, c68.xyxx
            rcp r17.x, c33.z
            rcp r17.y, c33.w
            mul r20.xy, r20.xyxx, r17.xyxx
            mov r12.xyzw, c69.xyzw
            mov r12.xy, r20.xyxx
            mov r21.xyzw, r12.xyzw
            texldl r21.xyzw, r21.xyzw, s8
            mov r18.xyzw, c69.xyzw
            mov r18.xy, r20.xyxx
            mov r22.xyzw, r18.xyzw
            texldl r22.xyzw, r22.xyzw, s14
            dp3 r19.w, r9.xyzx, r22.xyzx
            add r19.w, r19.w, c74.w
            mul r19.w, r19.w, c75.x
            mov_sat r19.w, r19.w
            mul r20.zw, r20.xxxy, c33.xxxy
            frc r22.xyzw, r20.zwzz
            add r20.zw, r20.xxzw, -r22.xxxy
            max r20.zw, r20.xxzw, c69.xxxy
            min r20.zw, r20.xxzw, r3.xxzw
            add r20.zw, r20.xxzw, c68.xxxy
            mul r20.zw, r20.xxzw, c0.xxxy
            mov r13.xyzw, c69.xyzw
            mov r13.xy, r20.zwzz
            mov r22.xyzw, r13.xyzw
            texldl r22.xyzw, r22.xyzw, s1
            add r20.z, r22.x, -c1.w
            mul r20.z, r20.z, c2.x
            mov_sat r20.z, r20.z
            mul r20.z, r20.z, r4.x
            add r20.z, c0.w, -r20.z
            max r20.z, r20.z, c70.z
            rcp r8.w, r20.z
            mul r20.z, r3.y, r8.w
            add r20.z, r20.z, -r3.x
            abs r20.z, r20.z
            mul r20.z, r20.z, r5.w
            exp r10.w, r20.z
            add r20.z, r21.w, -r1.w
            abs r20.z, r20.z
            mul r20.z, r20.z, r6.w
            add r20.z, -r20.z, c70.x
            mov_sat r20.z, r20.z
            mul r20.z, r10.w, r20.z
            add r20.w, -r9.w, c68.x
            cmp r20.w, r20.w, c69.x, c70.x
            cmp r19.w, -r20.w, c70.x, r19.w
            mul r19.w, r20.z, r19.w
            mul r21.xyz, r21.xyzx, r4.z
            mov r14.xyzw, c69.xyzw
            mov r14.xy, r20.xyxx
            mov r22.xyzw, r14.xyzw
            texldl r22.xyzw, r22.xyzw, s12
            mov r15.xyzw, c69.xyzw
            mov r15.xy, r20.xyxx
            mov r23.xyzw, r15.xyzw
            texldl r23.xyzw, r23.xyzw, s0
            mov r16.xyzw, c69.xyzw
            mov r16.xy, r20.xyxx
            mov r20.xyzw, r16.xyzw
            texldl r20.xyzw, r20.xyzw, s10
            mul r20.xyz, r23.xyzx, r20.w
            max r22.xyz, r22.xyzx, c71.yzwy
            mul r22.xyz, r4.z, r22.xyzx
            min r23.xyz, r6.xyzx, r20.xyzx
            add r20.xyz, r20.xyzx, -r23.xyzx
            max r20.xyz, r22.xyzx, r20.xyzx
            rcp r19.x, r20.x
            rcp r19.y, r20.y
            rcp r19.z, r20.z
            mul r20.xyz, r21.xyzx, r19.xyzx
            max r20.xyz, r20.xyzx, c74.xyzx
            mul r20.xyz, r20.xyzx, r19.w
            mov r21.xyz, r10.xyzx
            add r20.xyz, r21.xyzx, r20.xyzx
            mov r10.xyz, r20.xyzx
            mov r20.x, r0.z
            add r19.w, r20.x, r19.w
            mov r0.z, r19.w
            mov r20.xy, r11.xyxx
            mul r19.w, r20.x, c75.y
            mov r20.xy, r11.xyxx
            mul r20.x, r20.y, c75.z
            add r19.w, r19.w, -r20.x
            mov r20.xy, r11.xyxx
            mul r20.x, r20.x, c75.z
            mov r20.yz, r11.xxyx
            mul r20.y, r20.z, c75.w
            add r20.x, r20.x, -r20.y
            mov r17.z, r19.w
            mov r17.w, r20.x
            mov r20.xy, r17.zwzz
            mov r11.xy, r20.xyxx
            mov r19.w, r0.w
            add r19.w, r19.w, c70.x
            mov r0.w, r19.w
            mov r19.w, r7.w
            add r19.w, r19.w, c70.x
            mov r7.w, r19.w
        endrep
        mov r0.xyw, r10.xyxz
        rcp r1.x, r0.z
        rcp r1.y, r0.z
        rcp r1.z, r0.z
        mul r0.xyz, r0.xywx, r1.xyzx
        add r0.xyz, r0.xyzx, -r8.xyzx
        mul r0.xyz, r4.y, r0.xyzx
        add r0.xyz, r8.xyzx, r0.xyzx
        mul r0.xyz, r0.xyzx, r7.xyzx
        rcp r1.x, r4.z
        rcp r1.y, r4.z
        rcp r1.z, r4.z
        mul r0.xyz, r0.xyzx, r1.xyzx
        mov r5.xyz, r0.xyzx
    else
    endif
    mov r0.xyz, r5.xyzx
    mov r2.xyz, r0.xyzx
else
endif
mov oC0.xyzw, r2.xyzw
