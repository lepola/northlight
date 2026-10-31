#!/usr/bin/env python3
# northlight-test:
"""0.3.208 colour-guided WorldComposite tent: the 1-pixel light line along a wall foot (2D lattice emulation).

One half-res lighting texel cannot serve both the ground pixels and the wall pixels of a contact, and the depth weight alone cannot tell them
apart: at a grazing view the ground depth changes by several tolerances per texel, so a top-row ground pixel is dominated by wall texels (or the
nearest-depth fallback picks one) and keeps the wall's near-zero removal: a light pixel run on the staircase. WorldComposite's lighting, baseline and
AO tent taps (not the fog tent) are now also weighted by 2^(-16 |Lc-Lt|/(Lc+Lt+.02)), Lc the centre pixel's scene luminance, Lt the scene
luminance at the tap's own sample pixel (depthUV of the texel), and the nearest-depth fallback picks the tap with the least
(depth bits / 4 + colour bits): the full depth bits would pick a wall texel again for a top-row ground pixel (measured here). A sand pixel then rejects a dark face turned away and vice versa; an MSAA edge pixel of mixed colour takes both.

Emulation (pinhole 1920x1080, texel i = pixel 2i+1, WorldNormals port, night correction from the normal, smoothRemoval, the exact composite tent
with the .02 fallback, baseline mixing, albedoT clamp, -45% floor; MSAA 4x = mean of the D3D pattern samples, depth = sample 0; albedo noise +-15%).
Error = composite - ideal (every sample relit by its own surface), /255. Also: flat textured ground with a penumbra / a hard shadow edge / lit
(K=16 against K=0 deviation). One lighting-side change ships with the tent: colour-guided one-sided normals in WorldNormals (the 'ncol' option). smoothRemoval
is the original one (a confidence-0 centre skips the normal test; an always-on test was measured and dropped: it regressed some wall
spots in game). The tent and the colour normals together clear most of the staircase. No game, Wine or GPU."""
import sys; from pathlib import Path; sys.path.insert(0,str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json,math
hlsl=fp.src('world_effects.hlsl').read_text()
W,H=1920,1080;F=1217.;HW,HH=W//2,H//2
CH=1.7;AMB=.3;LEGACY=.9;ALB_G=.94;ALB_W=.6
def norm(v):
    m=math.sqrt(sum(a*a for a in v));return tuple(a/m for a in v)
LG=norm((.2,.5,.85))  # toward the source, world (X right, Y up, S forward)
class Scene:
    def __init__(s,S0,pitch_deg,yaw_deg,Hw=60.):
        s.cp=math.cos(math.radians(pitch_deg));s.sp=math.sin(math.radians(pitch_deg))
        s.wx=math.sin(math.radians(yaw_deg));s.ws=math.cos(math.radians(yaw_deg));s.d=S0*s.ws;s.Hw=Hw
        s.zc={}
    def cast(s,px,py):
        key=(px,py)
        if key in s.zc:return s.zc[key]
        px_=min(max(px,0),W-1);py_=min(max(py,0),H-1)
        rx=(px_+.5-W/2)/F;ry=-(py_+.5-H/2)/F
        dY=ry*s.cp-s.sp;dX=rx;dS=ry*s.sp+s.cp
        tg=CH/-dY if dY<0 else None
        den=s.wx*dX+s.ws*dS;tw=s.d/den if den>1e-9 else None
        hit=None
        if tw is not None and 0<=CH+tw*dY<=s.Hw and (tg is None or tw<tg):hit=('w',tw)
        elif tg is not None:hit=('g',tg)
        if hit is None:r=('s',1e4)
        else:r=hit
        s.zc[key]=r;return r
    def pos(s,px,py):
        k,t=s.cast(px,py);px_=min(max(px,0),W-1);py_=min(max(py,0),H-1)
        return ((px_+.5-W/2)/F*t,-(py_+.5-H/2)/F*t,t)
    def true_normal(s,k):
        return (0.,1.,0.) if k=='g' else (-s.wx,0.,-s.ws)
    def orig(s,px,py):
        k,t=s.cast(px,py)
        if k=='s':return 0.
        n=s.true_normal(k);old=LEGACY*max(sum(a*b for a,b in zip(n,LG)),0)
        return (ALB_G if k=='g' else ALB_W)*(AMB+old)

# ---------------- MSAA, normals, lighting, removal, composite ----------------
SAMPLES=((-2,-6),(6,-2),(-6,2),(2,6))   # D3D 4x standard pattern, 1/16 px, y down; sample 0 is the depth the effects read
def cast_s(sc,px,py,si):
    """surface hit of sample si of full-res pixel (px,py) (no MSAA: si=None -> pixel centre)."""
    ox,oy=(0,0) if si is None else SAMPLES[si]
    rx=(px+.5+ox/16-W/2)/F;ry=-(py+.5+oy/16-H/2)/F
    dY=ry*sc.cp-sc.sp;dX=rx;dS=ry*sc.sp+sc.cp
    tg=CH/-dY if dY<0 else None
    den=sc.wx*dX+sc.ws*dS;tw=s_div(sc.d,den)
    if tw is not None and 0<=CH+tw*dY<=sc.Hw and (tg is None or tw<tg):return 'w',tw,(rx,ry)
    if tg is not None:return 'g',tg,(rx,ry)
    return 's',1e4,(rx,ry)
def s_div(a,b):return a/b if b>1e-9 else None
class Emu:
    def __init__(s,sc,msaa=True,opt=(),K=0.,fb='depth',noise=0.,eps=.02,lightfn=None):
        s.K=K;s.fb=fb;s.noise=noise;s.eps=eps;s.lightfn=lightfn;s.opt=opt;s.sc=sc;s.msaa=msaa;s.d0={};s.nrm={};s.light={};s.out={};s.col={}
    # depth read by every effect: sample 0 (or the centre without MSAA)
    def depth(s,px,py):
        px=min(max(px,0),W-1);py=min(max(py,0),H-1)
        k=(px,py)
        if k not in s.d0:
            kind,t,r=cast_s(s.sc,px,py,0 if s.msaa else None);s.d0[k]=(kind,t,(r[0]*t,r[1]*t,t))
        return s.d0[k]
    def nz(s,px,py,kind):
        h=math.sin(px*12.9898+py*78.233+(0 if kind=='g' else 5.1))*43758.5453;h=h-math.floor(h)
        return 1+s.noise*(2*h-1)
    def colour(s,px,py):
        """resolve: mean of the sample colours (each sample its own surface's painted colour)."""
        k=(px,py)
        if k not in s.col:
            tot=0.;n=4 if s.msaa else 1
            for si in (range(4) if s.msaa else (None,)):
                kind,t,r=cast_s(s.sc,px,py,si)
                tot+=0. if kind=='s' else s.nz(px,py,kind)*(ALB_G*(AMB+LEGACY*max(sum(a*b for a,b in zip((0,1,0),LG)),0)) if kind=='g' else ALB_W*(AMB+LEGACY*max(sum(a*b for a,b in zip(s.sc.true_normal('w'),LG)),0)))
            s.col[k]=tot/n
        return s.col[k]
    # ---- WorldNormals port (half-res texel (i,j) reads full pixel (2i+1,2j+1)) ----
    def normal(s,i,j):
        k=(i,j)
        if k in s.nrm:return s.nrm[k]
        px,py=2*i+1,2*j+1;kind,z,p=s.depth(px,py)
        if kind=='s':s.nrm[k]=(0,0,1,1.);return s.nrm[k]
        sub=lambda a,b:[a[t]-b[t] for t in range(3)];dot=lambda a,b:sum(u*v for u,v in zip(a,b))
        cosine=lambda a,b:dot(a,b)/math.sqrt(max(dot(a,a)*dot(b,b),1e-12))
        P=lambda x,y:s.depth(min(max(x,0),W-1),min(max(y,0),H-1))[2]
        pixels=min(max(1.5*F/z,2),40)
        def wide(dx,dy,near):
            qx=min(max(px+int(math.floor(.5+dx*pixels)),0),W-1);qy=min(max(py+int(math.floor(.5+dy*pixels)),0),H-1)
            w=P(qx,qy);a=sub(near,p);b=sub(w,p)
            inner=P(min(max(qx-dx,0),W-1),min(max(qy-dy,0),H-1));end=sub(w,inner)
            bad=abs(w[2]-z)>max(.35,z*.04) or min(cosine(a,b),cosine(a,end))<.95
            return near if bad else w
        def axis(dx,dy):
            a1=P(px-dx,py-dy);b1=P(px+dx,py+dy)
            a=wide(-dx,-dy,a1);b=wide(dx,dy,b1)
            if a==a1 or b==b1:a,b=a1,b1
            da=sub(p,a);db=sub(b,p)
            if cosine(da,db)>=.85:return sub(b,a),1.
            da=sub(p,a1);db=sub(b1,p)
            if cosine(da,db)>=.85:return sub(b1,a1),1.
            if 'ncol' in s.opt:
                # WorldNormals: the one-sided side follows the scene colour (relative luminance like the tent), the depth rule when they differ by < .05
                lc=s.colour(px,py);lb=s.colour(min(max(px+dx,0),W-1),min(max(py+dy,0),H-1));la=s.colour(min(max(px-dx,0),W-1),min(max(py-dy,0),H-1))
                gap=abs(lc-lb)/(lc+lb+.02)-abs(lc-la)/(lc+la+.02)
                return (db if (gap<0 if abs(gap)>.05 else abs(b1[2]-p[2])<abs(a1[2]-p[2])) else da),0.
            return (db if abs(b1[2]-p[2])<abs(a1[2]-p[2]) else da),0.
        tx,c1=axis(1,0);ty,c2=axis(0,1)
        n=[tx[1]*ty[2]-tx[2]*ty[1],tx[2]*ty[0]-tx[0]*ty[2],tx[0]*ty[1]-tx[1]*ty[0]]
        m=math.sqrt(max(dot(n,n),1e-12));n=[v/m for v in n]
        if dot(n,[-v for v in p])<0:n=[-v for v in n]
        # view -> world: view x right, y up, z forward
        sc=s.sc;fwd=(0,-sc.sp,sc.cp);up=(0,sc.cp,sc.sp)
        nw=tuple(n[0]*(1,0,0)[t]+n[1]*up[t]+n[2]*fwd[t] for t in range(3))
        s.nrm[k]=(*nw,min(c1,c2));return s.nrm[k]
    # ---- WorldLighting at night (both surfaces in shadow: correction = -painted): (corr, baseline, alpha) ----
    def lighting(s,i,j):
        if s.lightfn:return s.lightfn(s,i,j)
        n=s.normal(i,j);kind=s.depth(2*i+1,2*j+1)[0]
        if kind=='s':return (0.,max(AMB,.15),1.)
        old=LEGACY*max(sum(a*b for a,b in zip(n[:3],LG)),0)
        return (-old,max(old+AMB,.15),0.)
    def scene_tex(s,i,j):
        return (s.colour(2*i,2*j)+s.colour(2*i+1,2*j)+s.colour(2*i,2*j+1)+s.colour(2*i+1,2*j+1))/4
    def z(s,i,j):return s.depth(2*i+1,2*j+1)[1]
    def rawL(s,i,j):
        i=min(max(i,0),HW-1);j=min(max(j,0),HH-1)
        if (i,j) not in s.light:s.light[(i,j)]=s.lighting(i,j)
        return s.light[(i,j)]
    def scale(s,i,j):return max(s.rawL(i,j)[1],s.scene_tex(i,j))   # removalScale, T=1, no fog
    def smooth_removal(s,i,j):
        cur=s.rawL(i,j);z=s.z(i,j);amount=min(max((1-cur[2]*1.)/.85,0),1)
        if amount<=1/256:return cur[0]
        ratio=cur[0]/s.scale(i,j);radius=min(max(.375*F/z,3),16);ds=-1.442695/max(.25,z*.03);vs=2/.85
        n0=s.normal(i,j);tot=1.;sm=max(ratio,-.45);t=.5;dx,dy=1.,0.
        for _ in range(24):
            r=math.sqrt(t/24)*radius
            tx=min(max(math.floor(i+.5+dx*r),0),HW-1);ty=min(max(math.floor(j+.5+dy*r),0),HH-1)
            l=s.rawL(tx,ty);nt=s.normal(tx,ty)
            agree=min(max((sum(a*b for a,b in zip(n0[:3],nt[:3]))-.6428)*5.67,0),1)
            w=2**(abs(s.z(tx,ty)-z)*ds)*min(max(1-abs(l[2]-cur[2])*vs,0),1)*(agree if n0[3]>.5 else 1)
            sm+=max(l[0]/s.scale(tx,ty),-.45)*w;tot+=w
            dx,dy=dx*-.7373688-dy*.6754903,dx*.6754903-dy*.7373688;t+=1
        return (ratio+(sm/tot-ratio)*amount)*s.scale(i,j)
    def smoothed(s,i,j):
        i=min(max(i,0),HW-1);j=min(max(j,0),HH-1)
        if (i,j) not in s.out:
            if s.depth(2*i+1,2*j+1)[0]=='s':s.out[(i,j)]=s.rawL(i,j)[0]
            else:s.out[(i,j)]=s.smooth_removal(i,j)
        return s.out[(i,j)]
    # ---- WorldComposite, one full-res pixel ----
    def composite(s,px,py):
        kind,z,_=s.depth(px,py)
        O=s.colour(px,py)
        if kind=='s':return O
        grid=((px+.5)/2-.5,(py+.5)/2-.5);bx,by=math.floor(grid[0]),math.floor(grid[1]);fx,fy=grid[0]-bx,grid[1]-by
        bounce=baseline=total=0.;closest=1e20;fb=None
        for t in range(4):
            bit=(t&1,t>>1);ti=min(max(bx+bit[0],0),HW-1);tj=min(max(by+bit[1],0),HH-1)
            bl=((1-fx) if bit[0]==0 else fx)*((1-fy) if bit[1]==0 else fy)
            zn=s.depth(2*ti+1,2*tj+1)[1];delta=abs(zn-z)
            lt=s.colour(2*ti+1,2*tj+1);cd=abs(O-lt)/(O+lt+s.eps)*s.K
            w=bl*math.exp(-delta/max(.15,z*.01))*2**(-cd)
            c=s.smoothed(ti,tj);b=s.rawL(ti,tj)[1]
            bounce+=c*w;baseline+=b*w;total+=w
            metric=delta if s.fb=='depth' else delta/max(.15,z*.01)*1.4427/4+cd
            if metric<closest:closest=metric;fb=(c,b)
        if total<.02:bounce,baseline,total=fb[0],fb[1],1.
        inv=1/max(total,1e-4);bounce*=inv;old=max(baseline*inv,.15)
        albedoT=min(O/old,1.)
        return max(O+albedoT*bounce,O-.45*O)
    def ideal(s,px,py):
        """what a pixel of this resolved colour would be with every sample relit by its own surface (the MSAA ideal)."""
        tot=0.;n=4 if s.msaa else 1
        for si in (range(4) if s.msaa else (None,)):
            kind,t,r=cast_s(s.sc,px,py,si)
            if kind=='s':continue
            if kind=='g':
                old=LEGACY*LG[1];O=s.nz(px,py,'g')*ALB_G*(AMB+old);b=AMB+old;corr=-old
            else:
                nn=s.sc.true_normal('w');old=LEGACY*max(sum(a*b for a,b in zip(nn,LG)),0);O=s.nz(px,py,'w')*ALB_W*(AMB+old);b=max(AMB+old,.15);corr=0.
            tot+=max(O+min(O/b,1)*corr,.55*O)
        return tot/n

def edge(S0,yaw,msaa,K,off,opt=(),noise=.15,x0=960,x1=1040):
    pitch=math.degrees(math.atan(CH/S0))+off
    sc=Scene(S0,pitch,yaw);em=Emu(sc,msaa=msaa,opt=opt,K=K,fb='color' if K else 'depth',noise=noise)
    n8=0;mx=-99;mn=99
    for x in range(x0,x1):
        top=None
        for y in range(100,1000):
            if em.depth(x,y)[0]=='w' and em.depth(x,y+1)[0]=='g':top=y;break
        if top is None:continue
        for y in range(top-3,top+6):
            v=(em.composite(x,y)-em.ideal(x,y))*255
            if v>8:n8+=1
            mx=max(mx,v);mn=min(mn,v)
    return n8,round(mx),round(mn)
def flat(K,fn,S0=100.,noise=.15,opt=()):
    pitch=math.degrees(math.atan(CH/S0))+3.;res=[]
    for k in (0,K):
        sc=Scene(S0,pitch,82.);em=Emu(sc,msaa=True,K=k,fb='color' if k else 'depth',noise=noise,lightfn=fn,opt=opt if k else ())
        res.append([em.composite(x,700) for x in range(900,1100)])
    return round(max(abs(a-b) for a,b in zip(*res))*255,2)
def pen(width):
    def fn(s,i,j):
        if s.depth(2*i+1,2*j+1)[0]=='s':return (0.,.3,1.)
        t=min(max((i-500)/width+.5,0),1);old=LEGACY*LG[1]
        return (-old*(1-t),old+AMB,t)
    return fn
def lit(s,i,j):
    k=s.depth(2*i+1,2*j+1)[0];return (.05,AMB+LEGACY*LG[1],1.) if k=='g' else ((.02,AMB,1.) if k=='w' else (0.,.3,1.))
checks={};report={}
comp=hlsl[hlsl.index('float4 worldComposite('):hlsl.index('// Separate geometry pass')]
checks['WorldComposite: lighting/baseline/AO taps weighted by the colour term, fog tent untouched, no contactPull left']=(
    'float tapLuma=dot(tex2Dlod(Scene,float4(tapUV,0,0)).rgb,float3(.299,.587,.114));' in comp
    and 'float guide=16*abs(centreLuma-tapLuma)/(centreLuma+tapLuma+.02),bits=delta*depthBits;' in comp
    and 'float weight=blend.x*blend.y*exp2(-bits-guide);' in comp and 'float score=mad(bits,.25,guide);' in comp and 'if(score<closest){closest=score;' in comp
    and 'depthBits=1.4427/max(.15,center*.01)' in comp and 'blend.x*blend.y*exp(-fogDelta/max(.2,viewZ*.02))' in comp
    and 'contactPull' not in hlsl and 'if(total<.02){bounce=fallbackLight.rgb;' in comp)
man=json.loads(fp.src('world-shader-build.json').read_text())['shaders']
# Microsoft d3dcompiler_47 counts; Wine's builtin compiler gave WorldNormals 483, WorldComposite 512, RemovalSmooth 269.
checks[f"slots: WorldNormals {man['WorldNormals']['static_instruction_slots']} / {man['WorldNormals']['temporary_registers']} of 32 temporaries (was 434 / 29), WorldComposite {man['WorldComposite']['static_instruction_slots']} (was 509), RemovalSmooth {man['RemovalSmooth']['static_instruction_slots']} (task 43's zero-weight branch), all <= 512"]=(
    man['WorldNormals']['static_instruction_slots']==305 and man['WorldNormals']['temporary_registers']<=32 and man['WorldComposite']['static_instruction_slots']<=512
    and man['RemovalSmooth']['static_instruction_slots']==222)
nrm=hlsl[hlsl.index('float3 tangentAxis('):hlsl.index('struct LightingOutput')]
checks['WorldNormals: a confidence-0 pixel picks its one-sided side by scene colour (relative luminance, one fetch site), the depth rule below .05']=(
    'confidence=0;' in nrm and '[loop]for(int k=0;k<3;++k)lumas+=dot(tex2Dlod(Scene,' in nrm and 'abs(lumas.x-lumas.yz)/(lumas.x+lumas.yz+.02)' in nrm
    and '(abs(gap)>.05?gap<0:abs(b1.z-p.z)<abs(a1.z-p.z))?db:da' in nrm)
smooth=hlsl[hlsl.index('float3 smoothRemoval('):hlsl.index('// Half-res pass before TemporalLight')]
checks['smoothRemoval is the original: a confidence-0 centre skips the normal test']=('*(centreNormal.w>.5?agree:1);' in smooth)
SC=((100.,82.,3.),(100.,85.,12.),(25.,78.,3.),(300.,82.,12.))
print('scene (S0 yaw pitch+)  MSAA: px>8/255, max, min  (K=0 -> K=16)        no MSAA')
tot={True:[0,0],False:[0,0]};worst_min={True:[0,0],False:[0,0]}
for S0,yaw,off in SC:
    row=[]
    for msaa in (True,False):
        a=edge(S0,yaw,msaa,0,off);b=edge(S0,yaw,msaa,16,off);row.append((a,b))
        tot[msaa][0]+=a[0];tot[msaa][1]+=b[0];worst_min[msaa][0]=min(worst_min[msaa][0],a[2]);worst_min[msaa][1]=min(worst_min[msaa][1],b[2])
    print(f'{S0:5g} {yaw:3g} {off:3g}   {row[0][0]} -> {row[0][1]}     {row[1][0]} -> {row[1][1]}',flush=True)
    report[f'{S0},{yaw},{off}']=row
report['totals']={'msaa':tot[True],'off':tot[False]}
checks[f"bright top-row pixels (>8/255) over the scenes: MSAA {tot[True][0]} -> {tot[True][1]}, no MSAA {tot[False][0]} -> {tot[False][1]} (each at least 35% fewer)"]=tot[True][1]<=.65*tot[True][0] and tot[False][1]<=.65*tot[False][0]
checks[f"dark pixels at the wall bottom do not get worse (worst {worst_min[True][0]} -> {worst_min[True][1]} MSAA, {worst_min[False][0]} -> {worst_min[False][1]} off)"]=worst_min[True][1]>=worst_min[True][0] and worst_min[False][1]>=worst_min[False][0]
dev_pen=flat(16,pen(6));dev_hard=flat(16,pen(.01));dev_lit=flat(16,lit)
print(f'flat textured ground, K=16 vs K=0 max deviation /255: 6-texel penumbra {dev_pen}, hard shadow edge {dev_hard}, lit {dev_lit}')
report['flat']={'penumbra':dev_pen,'hard_edge':dev_hard,'lit':dev_lit}
checks[f'flat textured ground: penumbra deviation {dev_pen}/255 (<= 2), hard shadow edge {dev_hard}/255 (<= 5), lit scene {dev_lit}/255 (0)']=dev_pen<=2 and dev_hard<=5 and dev_lit==0
FULL=('ncol',)   # shipped: colour-picked one-sided normals (WorldNormals) with the original smoothRemoval
print('scene              before(K=0, 0.3.207 lighting)        tent only (K=16)          shipped (K=16 + colour normals)   [px>8/255, max, min]  MSAA | no MSAA')
sweep=(((100.,82.,3.),(100.,85.,12.),(25.,78.,3.),(300.,82.,12.),(100.,78.,3.),(25.,85.,12.),(300.,85.,3.),(300.,78.,12.)))
res={True:[],False:[]}
for S0,yaw,off in sweep:
    for msaa in (True,False):
        res[msaa].append((edge(S0,yaw,msaa,0,off),edge(S0,yaw,msaa,16,off),edge(S0,yaw,msaa,16,off,opt=FULL)))
    print(f'{S0:5g} {yaw:3g} {off:3g}  MSAA {res[True][-1]}   off {res[False][-1]}',flush=True)
report['sweep']={'msaa':res[True],'off':res[False]}
sum_n=lambda m,i:sum(r[i][0] for r in res[m])
mx=lambda m,i:max(r[i][1] for r in res[m]);mn=lambda m,i:min(r[i][2] for r in res[m])
clean=[edge(S0,yaw,False,16,off,opt=FULL,noise=0.) for S0,yaw,off in sweep]
report['msaa_off_untextured']=clean
print(f"MSAA off, shipped, without texture noise: {sum(c[0] for c in clean)} px over 8/255, max {max(c[1] for c in clean)}/255")
checks[f"MSAA off, shipped: {sum_n(False,2)} px over 8/255 (before {sum_n(False,0)}, tent only {sum_n(False,1)}), max {mx(False,2)}/255 (before {mx(False,0)}): at most 30% of before, not more than tent only"]=(
    sum_n(False,2)<=.3*sum_n(False,0) and sum_n(False,2)<=sum_n(False,1) and mx(False,2)<mx(False,0))
print(f"MSAA on, shipped: {sum_n(True,2)} px over 8/255, max {mx(True,2)}/255 (before {sum_n(True,0)} px, max {mx(True,0)}; tent only {sum_n(True,1)} px, max {mx(True,1)}); darkest {mn(True,2)} (before {mn(True,0)})")
checks[f"MSAA on, shipped: {sum_n(True,2)} px over 8/255 (before {sum_n(True,0)}, tent only {sum_n(True,1)}): at most 60% of before, not more than tent only, max {mx(True,2)} (before {mx(True,0)}) and below tent only's {mx(True,1)}"]=(
    sum_n(True,2)<=.6*sum_n(True,0) and sum_n(True,2)<=sum_n(True,1) and mx(True,2)<mx(True,0) and mx(True,2)<mx(True,1))
checks[f"dark pixels at the wall bottom do not get worse with the shipped changes (worst {mn(True,0)} -> {mn(True,2)} MSAA, {mn(False,0)} -> {mn(False,2)} off)"]=mn(True,2)>=mn(True,0) and mn(False,2)>=mn(False,0)
dev_pen2=flat(16,pen(6),opt=FULL);dev_hard2=flat(16,pen(.01),opt=FULL);dev_lit2=flat(16,lit,opt=FULL)
print(f'flat textured ground, shipped K=16 vs K=0 plain tent: penumbra {dev_pen2}, hard edge {dev_hard2}, lit {dev_lit2}')
report['flat_shipped']={'penumbra':dev_pen2,'hard_edge':dev_hard2,'lit':dev_lit2}
checks[f'flat textured ground, shipped: penumbra {dev_pen2}/255 (<= 2), hard shadow edge {dev_hard2}/255 (<= 5), lit {dev_lit2}/255 (0)']=dev_pen2<=2 and dev_hard2<=5 and dev_lit2==0
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
(fp.output_dir()/'colour-tent-2d.json').write_text(json.dumps({'checks':checks,**report,'game_or_gpu_launched':False},indent=2))
assert all(checks.values())
print('PASS colour-guided tent: lattice emulation with MSAA 4x')
