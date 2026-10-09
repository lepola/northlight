#!/usr/bin/env python3
"""Particle mask patch of one original pixel shader: the reference oracle for
src/proxy/particle_shader_patch.h (0.3.203), which the DLL runs at the first eligible draw.
The game draws its translucent M2 particles with its own ps_2_0/ps_3_0 shaders, so the mask
cannot come from a replacement of the fixed-function stage. Like water_shader_patch.py this
renames oC0 to a spare temporary rT and appends before END `mov oC0, rT` (RT0 stays exactly the
game's) plus the mask write to oC1 for one blend kind (the values rain_mask.hlsl writes):
  kind 0  alpha over         oC1 = (1,1,1,sat(a))                 needs a free constant (def) for the 1s
  kind 1  additive by alpha  oC1 = (1,1,1,sat(a)*sat(max(rgb)))   a free constant and a second free temporary
  kind 2  additive by colour oC1 = sat(max(rgb)).xxxx             a second free temporary
  kind 3  additive by colour for SRCCOLOR/ONE: kind 2 without the fog write below
A ps_3_0 that receives the game's fog factor (dcl_fog0 vN.c, D3DDECLUSAGE_FOG index 0) also writes it to oC1.x (0.3.203, particle fog): the mask's RED then follows the same
recurrence as BLUE with f as the value, so red/blue is the weighted mean of f (the composite turns f into the particle's distance). sat(f) is written as:
  kind 0  oC1 = (f,1,1,sat(a))        over: R' = a*f + (1-a)*R against B' = a + (1-a)*B
  kind 1  oC1 = (f,1,1,w)             SRCALPHA/ONE, w = sat(a)*sat(max rgb): R' = R + w*f against B' = B + w
  kind 2  oC1 = (m*f,m,m,m)           ONE/ONE, m = sat(max rgb): R' = R + m*f against B' = B + m
Shaders without the input (and every fixed-function / rain variant, which write 1 / m to oC1.x) leave red equal to blue: f = 1, no extra fog. SRCCOLOR/ONE (kind 3) squares
the written value in blue, so it would square f in red: it writes no f.
Everything it cannot prove safe is a ValueError whose text is the reason (REASONS, the C++
port's names): the water patch's set, with if/ifc/else/endif accepted as long as oC0 is written
outside them. Pure: no MPQ, client, device.
"""
import struct

REASONS=('ok','empty','unsupported shader model','bad END','truncated','missing END','predicated/coissued','bad DCL','bad DEF','unsupported control/opcode','unbalanced control flow',
         'operand count','parameter marker','conditional output','MRT/depth write','relative dest','bad relative','extra operands','incomplete output','no free temporary',
         'relative constant','no free constant','unknown kind')
def register(token):return ((token>>28)&7)|((token>>8)&24), token&2047
ARITY={0:0,1:2,2:3,3:3,4:4,5:3,6:2,7:2,8:3,9:3,10:3,11:3,12:3,13:3,14:2,15:2,16:2,17:3,18:4,19:2,20:3,21:3,22:3,23:3,24:3,27:2,29:0,32:3,33:3,34:4,35:2,36:2,40:1,41:2,42:0,43:0,46:2,65:1,66:3,78:2,79:2,88:4,89:3,90:4,91:2,92:2,93:5,94:3,95:3}
SAT=0x00100000
def reg(t,kind,index):return (t&~0x70001fff)|((kind&7)<<28)|((kind&24)<<8)|index
def dst(kind,index,mask=15):return reg(0x80000000|(mask<<16),kind,index)
def src(kind,index,swizzle=0xe4):return reg(0x80000000|(swizzle<<16),kind,index)
def parse(code):
    if not code:raise ValueError('empty')
    w=list(struct.unpack('<%dI'%(len(code)//4),code)); out=[];p=1
    if w[0] not in (0xffff0200,0xffff0300):raise ValueError('unsupported shader model')
    while p<len(w):
        op=w[p]&65535
        if op==65535:
            if w[p]!=65535 or p+1!=len(w):raise ValueError('bad END')
            return w,out,p
        n=((w[p]>>16)&32767) if op==65534 else ((w[p]>>24)&15)
        if p+n>=len(w):raise ValueError('truncated')
        out.append((p,op,w[p+1:p+1+n]));p+=n+1
    raise ValueError('missing END')
def patch(code,kind):
    """Returns (patched bytes, info). kind: 0 alpha over, 1 additive by alpha, 2 additive by colour."""
    if kind not in (0,1,2,3):raise ValueError('unknown kind')
    w,ops,end=parse(code);major=(w[0]>>8)&255
    used={};params=[];depth=0;mask=0;firstOp=None;relativeConst=False;fog=None
    for p,op,a in ops:
        if op==65534:continue
        if w[p]&0xf0000000:raise ValueError('predicated/coissued')
        if firstOp is None:firstOp=p
        if op==31:
            if len(a)!=2:raise ValueError('bad DCL')
            k,i=register(a[1]);used.setdefault(k,set()).add(i)
            fmask=(a[1]>>16)&15
            if major==3 and (a[0]&31)==11 and ((a[0]>>16)&15)==0 and k==1 and fmask and kind<3 and fog is None:fog=(i,(fmask&-fmask).bit_length()-1)
            continue
        if op==81:
            if len(a)!=5:raise ValueError('bad DEF')
            k,i=register(a[0]);used.setdefault(k,set()).add(i);continue
        if op in (48,47):continue
        arity=2 if op==37 and major==3 else 4 if op==37 else ARITY.get(op)
        if arity is None:raise ValueError('unsupported control/opcode')
        if op in (27,40,41):depth+=1
        if op in (29,43):
            depth-=1
            if depth<0:raise ValueError('unbalanced control flow')
        q=0
        for operand in range(arity):
            if q>=len(a):raise ValueError('operand count')
            t=a[q];k,i=register(t)
            if not t&0x80000000:raise ValueError('parameter marker')
            used.setdefault(k,set()).add(i);params.append(p+1+q)
            if operand==0 and op not in (27,40,41,65):
                if (k,i)==(8,0):
                    if depth:raise ValueError('conditional output')
                    mask|=(t>>16)&15
                elif k in (8,9):raise ValueError('MRT/depth write')
                if t&0x2000:raise ValueError('relative dest')
            if operand>0 and k==2 and t&0x2000:relativeConst=True
            if operand==2 and 20<=op<=24:
                rows=4 if op in (20,22) else 2 if op==24 else 3
                used.setdefault(k,set()).update(range(i,i+rows))
            q+=1
            if t&0x2000:
                if q>=len(a) or register(a[q])[0] not in (3,15):raise ValueError('bad relative')
                used.setdefault(register(a[q])[0],set()).add(register(a[q])[1]);q+=1
        if q!=len(a):raise ValueError('extra operands')
    if depth:raise ValueError('unbalanced control flow')
    if mask!=15:raise ValueError('incomplete output')
    limit=32 if major==3 else 12
    free=[i for i in range(limit) if i not in used.get(0,set())]
    if not free:raise ValueError('no free temporary')
    spare=free[0];spare2=None
    if kind:
        if len(free)<2:raise ValueError('no free temporary')
        spare2=free[1]
    const=None
    if kind<2:
        if relativeConst:raise ValueError('relative constant')
        climit=224 if major==3 else 32
        const=next((i for i in range(climit-1,-1,-1) if i not in used.get(2,set())),None)
        if const is None:raise ValueError('no free constant')
    for p in params:
        if register(w[p])==(8,0):w[p]=reg(w[p],0,spare)
    T=lambda sw:src(0,spare,sw)
    S2=lambda sw:src(0,spare2,sw)
    suffix=[0x02000001,dst(8,0),src(0,spare)]
    F=src(1,fog[0],fog[1]*0x55) if fog else None
    if kind<2:
        if fog:suffix+=[0x02000001,dst(8,1,1)|SAT,F,0x02000001,dst(8,1,6),src(2,const)]
        else:suffix+=[0x02000001,dst(8,1,7),src(2,const)]
    if kind==0:suffix+=[0x02000001,dst(8,1,8)|SAT,T(0xff)]
    else:
        # s2.x = sat(max(r,g,b)) of the game's colour
        suffix+=[0x0300000b,dst(0,spare2,1)|SAT,T(0x00),T(0x55),0x0300000b,dst(0,spare2,1)|SAT,S2(0x00),T(0xaa)]
        if kind==1:suffix+=[0x02000001,dst(0,spare2,2)|SAT,T(0xff),0x03000005,dst(8,1,8),S2(0x00),S2(0x55)]
        elif fog:suffix+=[0x02000001,dst(0,spare2,2)|SAT,F,0x03000005,dst(8,1,1),S2(0x00),S2(0x55),0x02000001,dst(8,1,14),S2(0x00)]
        else:suffix+=[0x02000001,dst(8,1),S2(0x00)]
    suffix.append(65535)
    w[end:end+1]=suffix
    if const is not None:w[firstOp:firstOp]=[0x05000051,dst(2,const),0x3f800000,0x3f800000,0x3f800000,0x3f800000]
    return struct.pack('<%dI'%len(w),*w),{'model':major,'kind':kind,'temp':spare,'temp2':spare2,'const':const,'fog':fog is not None}
