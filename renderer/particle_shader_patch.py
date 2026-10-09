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
A ps_3_0 that receives the game's fog factor (dcl_fog0 vN.c, D3DDECLUSAGE_FOG index 0) also writes 1-f to oC1.x (0.3.203, particle fog): the mask's RED then follows the same
recurrence as BLUE with 1-f as the value (the composite turns the weighted mean f into the particle's distance). The values:
  kind 0  oC1 = (1-f,1,1,sat(a))      over: R' = a*(1-f) + (1-a)*R against B' = a + (1-a)*B
  kind 1  oC1 = (1-f,1,1,w/4)         SRCALPHA/ONE, w = sat(a)*sat(max rgb): R' = R + w/4*(1-f) against B' = B + w/4
  kind 2  oC1 = (m/4*(1-f),m/4,m/4,m/4) ONE/ONE, m = sat(max rgb): R' = R + m/4*(1-f) against B' = B + m/4
The mask's red carries (1-f) times the layer's weight, so 0 means "no distance" (the sentinel of every layer that does not write red) and red / blue is the weighted mean of 1-f.
An additive kind with the fog factor writes its weight at 1/4 (the constant's w): blue saturates after about four full layers instead of two while red keeps the mean exact.
Additive layers without the fog factor (and kind 3, whose blend squares the value in blue) are the original blue-only writes at full weight, byte for byte, and need no
constant (kind 2, 3); they must not write red. The renderer's colour mask for the variant decides which channels land: over without the fog factor writes green only; the
fogged over shader writes red, green and blue; additive variants red + blue (fogged) or blue only.
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
    # the constant cK = (1,1,1,scale): the over and additive-by-alpha kinds need the 1s; an additive kind with the fog factor also its weight scale
    needConst=kind<2 or (kind==2 and fog is not None)
    const=None
    if needConst:
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
    NEGF=(F|(1<<24)) if fog else None      # -f: the mask carries 1-f (0 = no distance)
    scaled=fog is not None and kind in (1,2)
    if kind<2:
        if fog:suffix+=[0x03000002,dst(8,1,1)|SAT,src(2,const,0x00),NEGF,0x02000001,dst(8,1,6),src(2,const)]
        else:suffix+=[0x02000001,dst(8,1,7),src(2,const)]
    if kind==0:suffix+=[0x02000001,dst(8,1,8)|SAT,T(0xff)]
    else:
        # s2.x = sat(max(r,g,b)) of the game's colour
        suffix+=[0x0300000b,dst(0,spare2,1)|SAT,T(0x00),T(0x55),0x0300000b,dst(0,spare2,1)|SAT,S2(0x00),T(0xaa)]
        if scaled:suffix+=[0x03000005,dst(0,spare2,1),S2(0x00),src(2,const,0xff)]    # x the weight scale (the constant's w)
        if kind==1:suffix+=[0x02000001,dst(0,spare2,2)|SAT,T(0xff),0x03000005,dst(8,1,8),S2(0x00),S2(0x55)]
        elif fog:suffix+=[0x03000002,dst(0,spare2,2)|SAT,src(2,const,0x00),NEGF,0x03000005,dst(8,1,1),S2(0x00),S2(0x55),0x02000001,dst(8,1,14),S2(0x00)]
        else:suffix+=[0x02000001,dst(8,1),S2(0x00)]
    suffix.append(65535)
    w[end:end+1]=suffix
    scale=.25 if scaled else 1.0
    if const is not None:w[firstOp:firstOp]=[0x05000051,dst(2,const),0x3f800000,0x3f800000,0x3f800000,struct.unpack('<I',struct.pack('<f',scale))[0]]
    return struct.pack('<%dI'%len(w),*w),{'model':major,'kind':kind,'temp':spare,'temp2':spare2,'const':const,'fog':fog is not None,'scale':scale}
