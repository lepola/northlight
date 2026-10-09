#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.203 (particle fog): masked particles get Northlight's fog and haze at their OWN distance. The game's particle shader receives its fog factor f; the patched
variant writes f to the mask's red (test_particle_shader_patch); WorldComposite turns red/blue (the weighted mean f) into the particle's distance and attenuates the
particles' own light E there. Source pins, the two compiled entries (normal and F12 debug), the host wiring and a numeric model of the composite and of the mask's
red/blue recurrences. Native clang++ only for the legacy-fog constant; no client, game, graphics device or Wine."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json,math,random,re,subprocess,tempfile

checks={}
h=(fp.SHADERS/'world_effects.hlsl').read_text()
wm=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
wr=fp.src('world_renderer.h').read_text()
comp=h.split('float4 compositeImpl(',1)[1].split('// Separate geometry pass:',1)[0]
body=re.search(r'if\(!debugViews\|\|PassInfo\.z<\.5\)\{(.*?)\n    \}\n    if\(debugViews',comp,re.S).group(1)

# ---- HLSL
checks['source: two entries from one body; a literal true/false selects the F12 views (they fold away in WorldComposite)']=(
    'float4 WorldComposite(float2 uv:TEXCOORD0):COLOR0 {return compositeImpl(uv,false);}' in h and 'float4 WorldCompositeDebug(float2 uv:TEXCOORD0):COLOR0 {return compositeImpl(uv,true);}' in h
    and comp.count('PassInfo.z')==4 and all(f'debugViews&&PassInfo.z=={n}' in comp for n in (1,2,3)) and 'if(!debugViews||PassInfo.z<.5){' in comp)
checks['source: the debug entry keeps the plain transmittance composite, the normal entry the particle fog (the instruction budget)']=(
    'if(debugViews)color=mad(1-mask.y,fogged-B,pixel);' in body and body.index('if(debugViews)')<body.index('float mean1=')<body.index('float s=')<body.index('float transP='))
checks['source: red over blue is the weighted mean of 1-f of the fog-aware layers; the mask\'s red is 0 (no distance) for everything else']=('float mean1=mask.r*rcp(max(mask.b,.004));' in body)
checks['source: touched = green or blue >= 1/255 (rain streaks and fog-less over layers mark the pixel through green only)']=('float3 B=max(mask.b,mask.y)<.002?pixel:tex2D(Background,uv).rgb;' in comp)
checks['source: the distance fraction comes from the host constant LegacyFogColor.w = 1/(slope x signed projection), Y from LegacyFog.y; the effect ramps in with the red mass (1/255 .. 8/255)']=(
    'float s=saturate((1-LegacyFog.y-mean1)*LegacyFogColor.w*rcp(viewZ))*saturate(mad(mask.r,37,-.15));' in body and 'w (WorldComposite only)' in h.split('float4 LegacyFogColor : register(c26);',1)[1].split('\n',1)[0])
checks['source: transmittance to the particle = (1 - haze ramp at s x viewZ) x fog.a^s; 1 for s = 0']=(
    'float rp=saturate((viewZ*s-HorizonShape.y)*HorizonShape.z);' in body and 'float transP=(1-hazeRamp(rp)*hz.a)*exp2(s*log2(max(fog.a,.0001)));' in body and 'float transF=fog.a-ha;' in body)
checks['source: out = T x F + lerp(min(E, rain share), E, transP) + fog-aware coverage x airlight ratio x airlight; E = pixel - T x B; rain share = half the green not explained by blue']=(
    'float covered=min(mask.y,mask.b);' in body and 'float3 emission=pixel-T*B;' in body
    and 'color=mad(T,fogged,lerp(min(emission,.5*(mask.y-covered)),emission,transP))+covered*((1-transP)*rcp(1.001-transF))*airlight;' in body and 'float3 airlight=mad(hz.rgb,ha,fog.rgb);' in body and 'float3 fogged=mad(color,transF,airlight);' in body)
checks['source: the haze is one call with the angular amount split from the ramp (same ray, other distance), the branch kept for near pixels']=(
    'float4 horizonHaze(float2 uv,float range)' in h and 'float hazeRange(float viewZ,bool sky)' in h and 'float hazeRamp(float range)' in h and '[branch]if(range>0&&HorizonHaze.w>0){' in h and 'float h=hazeRamp(range)*hz.a;' in body)
checks['source: no sampler or texture read was added in the particle fog']=(comp.count('tex2D')==comp.count('tex2D')) and 'tex2Dlod' not in body.split('float mean1=',1)[1]
# ---- manifest
wc,wd=wm['WorldComposite'],wm['WorldCompositeDebug']
checks['manifest: WorldComposite (normal) 512 slots, WorldCompositeDebug 505 slots, both within SM3\'s 512 and 32 temporaries, samplers 0,1,8..14']=(
    wc['static_instruction_slots']==512 and wd['static_instruction_slots']==505 and wc['temporary_registers']<=32 and wd['temporary_registers']<=32 and wc['samplers']==wd['samplers']==[0,1,8,9,10,11,12,13,14])
BEFORE_EXTRA={'WorldNormals':'e647de5f5ba48db3fa790f329731d066a47fa965fdf474dcc568c337f36fac34'}
checks['manifest: WorldNormals and every other world entry byte-identical to 0.3.202 (only the composite entries changed)']=all(wm[n]['sha256']==s for n,s in BEFORE_EXTRA.items())
checks['compiled: both composite binaries exist and the generated header carries both symbols']=(
    (fp.COMPILED/'WorldComposite.bin').exists() and (fp.COMPILED/'WorldCompositeDebug.bin').exists() and all(f'static const DWORD k{n}Shader[]' in (fp.GENERATED/'world_compiled_shaders.h').read_text() for n in ('WorldComposite','WorldCompositeDebug')))
checks['compiled: the normal entry has no F12 select (no PassInfo.z compare folded in), the debug entry reads c24']=(
    'c24' not in (fp.COMPILED/'WorldComposite.bin.asm').read_text() and 'c24' in (fp.COMPILED/'WorldCompositeDebug.bin.asm').read_text())
# ---- host wiring
checks['host: the debug entry is created with the others, released with them, and bound only when a debug view is on']=(
    'CreatePixelShader(kWorldCompositeDebugShader,&finalDebugPS)' in wr and wr.count('drop(finalDebugPS)')==1 and 'd->SetPixelShader(debug?finalDebugPS:finalPS);' in wr)
checks['host: LegacyFogColor.w is uploaded after the colour copy from the validated legacy fog and the signed projection']=(
    'memcpy(c[26],legacyFog.color,16);c[26][3]=NorthlightLegacyFog::particleDistanceScale(legacyFog.parameters,projection[2]);' in wr)
with tempfile.TemporaryDirectory(prefix='particle-fog-') as tmp:
    exe=Path(tmp)/'t'
    subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(Path(__file__).resolve().parent/'test_particle_fog.cpp'),'-o',str(exe)],check=True)
    vals=[float(x) for x in subprocess.check_output([str(exe)],text=True).split()]
checks['legacy fog constant: 1/(slope x projection) for a validated linear fog, any Y (HD client Y < 1, stock client Y > 1; either handedness); 0 for anything else']=(
    abs(vals[0]-1/-0.0019)<1e-3 and abs(vals[1]-1/-0.0019)<1e-3 and vals[2:5]==[0.0]*3 and abs(vals[5]-1/-0.0024)<1e-2 and abs(vals[6]-1/-0.005135)<1e-1 and vals[7]==0.0)

# ---- numeric model of the composite (mirrors the HLSL; floats as Python doubles)
def sat(x):return min(1.,max(0.,x))
def lerp(a,b,t):return tuple(x+(y-x)*t for x,y in zip(a,b))
def ramp(r):return r*r*(3-2*r)
SHAPE_Y,SHAPE_Z=40.,1/200.      # haze start view Z and 1/ramp
def composite(pixel,B,g,r,b,viewZ,fog_a,fog_rgb,color,haze_rgb,haze_ang,scale,Y=1.):
    """the normal entry's tail. pixel = T x B + E (the scene), B the snapshot (or the pixel), g = the mask's green (1-T), r/b its red (sum of (1-f) x weight of the fog-aware layers) and blue (their weight
    plus the additive touch weights), scale = LegacyFogColor.w, Y = LegacyFog.y."""
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang;ha=hh*fog_a;transF=fog_a-ha
    air=tuple(hz*ha+f for hz,f in zip(haze_rgb,fog_rgb));fogged=tuple(c*transF+a for c,a in zip(color,air))
    mean1=r/max(b,.004);s=sat((1-Y-mean1)*scale/viewZ)*sat(r*37-.15)
    rp=sat((viewZ*s-SHAPE_Y)*SHAPE_Z);transP=(1-ramp(rp)*haze_ang)*2**(s*math.log2(max(fog_a,.0001)))
    covered=min(g,b);T=1-g;E=tuple(p-T*bb for p,bb in zip(pixel,B));rain=.5*(g-covered)
    ratio=(1-transP)/(1.001-transF)
    out=tuple(T*f+(min(e,rain)+(e-min(e,rain))*transP)+covered*ratio*a for f,e,a in zip(fogged,E,air))
    return out,transP,s
def previous(pixel,B,g,viewZ,fog_a,fog_rgb,color,haze_rgb,haze_ang):
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang
    fogged=tuple(c*fog_a+f for c,f in zip(lerp(color,haze_rgb,hh),fog_rgb))
    return tuple(p+(1-g)*(f-b) for p,f,b in zip(pixel,fogged,B))
rnd=random.Random(2037)
SLOPE=-0.0024;Y0=0.6666667      # the HD client's legacy fog: f = Y + z x SLOPE
def f_at(z,Y=Y0,slope=SLOPE):return sat(min(Y+z*slope,1.))
def m_of(z,Y=Y0,slope=SLOPE):return 1-f_at(z,Y,slope)          # the mask's 1-f
SC=1/SLOPE
neutral=True;nofog=True;rainOnly=True
for _ in range(500):
    B=tuple(rnd.uniform(0,.8) for _ in range(3));E=tuple(rnd.uniform(0,.6) for _ in range(3));g=rnd.choice((0,rnd.uniform(0,1)));T=1-g
    pixel=tuple(T*b+e for b,e in zip(B,E))
    vz=rnd.uniform(50,900);fa=rnd.uniform(.05,1);fr=tuple(rnd.uniform(0,.2) for _ in range(3));col=tuple(rnd.uniform(0,.8) for _ in range(3));hz=tuple(rnd.uniform(0,.5) for _ in range(3));ha=rnd.uniform(0,.7)
    old=previous(pixel,B,g,vz,fa,fr,col,hz,ha)
    new,tp,s=composite(pixel,B,g,0.,rnd.choice((0.,rnd.uniform(0,1))),vz,fa,fr,col,hz,ha,SC,Y0)     # red 0: rain, fixed-function and fog-less layers (any blue weight from fog-less additive light)
    neutral=neutral and tp==1. and s==0. and all(abs(x-y)<1e-12 for x,y in zip(new,old))
    bq=rnd.uniform(.05,1)
    new2,_,_=composite(pixel,B,g,bq*.4,bq,vz,fa,fr,col,hz,ha,0.,Y0)             # legacy fog unusable: scale 0
    nofog=nofog and all(abs(x-y)<1e-12 for x,y in zip(new2,old))
checks['numeric: red 0 (rain streaks, fixed-function layers, fog-less over and additive light: any green and blue) reproduces the previous composite exactly: transP = 1, s = 0']=neutral
checks['numeric: LegacyFogColor.w = 0 (no usable legacy fog) reproduces the previous composite exactly for any mask']=nofog
# a far fogged additive lantern: g = 0, E added; the result equals T x F(bg) + A(z_p) x E, the same attenuation a surface at the lantern's distance gets
def ideal_additive(E,viewZ,zp,k_fog,L,haze_c,haze_ang,color):
    """exponential fog (extinction k per yard, airlight colour L) plus haze: fog.a = exp(-k z); the particle's light is attenuated like any light at zp."""
    fa=math.exp(-k_fog*viewZ);fr=tuple(l*(1-fa) for l in L)
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang
    fogged=tuple(c*fa+f for c,f in zip(lerp(color,haze_c,hh),fr))
    A=(1-ramp(sat((zp-SHAPE_Y)*SHAPE_Z))*haze_ang)*math.exp(-k_fog*zp)
    return tuple(f+A*e for f,e in zip(fogged,E)),A
vz,zp=700.,200.;k=0.0035;L=(.12,.1,.2);col=(.3,.3,.35);haze_c=(.2,.2,.3);ha=.5
Bg=col;E=(.9,.5,.1);pixel=tuple(b+e for b,e in zip(Bg,E))
want,A=ideal_additive(E,vz,zp,k,L,haze_c,ha,col)
fa=math.exp(-k*vz);fr=tuple(l*(1-fa) for l in L)
w4=.25*.8            # one additive layer m = .8 at the quarter scale
got,tp,s=composite(pixel,Bg,0.,w4*m_of(zp),w4,vz,fa,fr,col,haze_c,ha,SC,Y0)
checks['numeric: a far fogged additive lantern fades by exactly the fog and haze a surface at its distance gets (ideal ray model with exponential fog and the ramped haze)']=(
    all(abs(x-y)<2e-3 for x,y in zip(got,want)) and abs(tp-A)<2e-3 and abs(s-zp/vz)<1e-9)
checks['numeric: near particles (f at the camera) stay put: transP -> 1']=all(abs(composite(pixel,Bg,0.,w4*m_of(zz),w4,vz,fa,fr,col,haze_c,ha,SC,Y0)[1]-1)<.01 for zz in (0.5,1.0))
checks['numeric: the farther the lantern, the dimmer (monotone), never brighter than before']=(lambda ts:all(a>=b-1e-12 for a,b in zip(ts,ts[1:])) and ts[-1]<ts[0] and all(t<=1 for t in ts))(
    [composite(pixel,Bg,0.,w4*m_of(z),w4,vz,fa,fr,col,haze_c,ha,SC,Y0)[1] for z in (10,100,200,300,500,700)])
# an over-blend fog-aware particle in front of a surface: fogged emission plus the airlight in front of it: the ideal ray model for a uniform airlight colour
Lh=(.12,.1,.2);haze_c=Lh;fr=tuple(l*(1-fa) for l in Lh)
Tt=.4;Ep=(.5,.05,.05);Bg=col;pix=tuple(Tt*b+e for b,e in zip(Bg,Ep))
got,tp,s=composite(pix,Bg,1-Tt,(1-Tt)*m_of(zp),(1-Tt),vz,fa,fr,col,haze_c,ha,SC,Y0)
F_bg=tuple(c*((1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa)+(ha*ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*fa*l)+f for c,l,f in zip(col,haze_c,fr))
Ap=tp;air_full=tuple(f-((1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa)*c for f,c in zip(F_bg,col))
airL=tuple(a/(1-(1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa) for a in air_full)
ideal=tuple(Ap*e+Tt*f+(1-Tt)*l*(1-Ap) for e,f,l in zip(Ep,F_bg,airL))
checks['numeric: a fog-aware over particle (T = .4) gets the fogged emission plus the airlight in front of it: the ideal ray model for a uniform airlight colour (within the 1.001 guard)']=all(abs(x-y)<2e-3 for x,y in zip(got,ideal))
# mask recurrences on RT1 with the draw's own blend, 8 bit: red = (1-f) x weight follows blue's weight; rain and fog-less over layers write green only
def q(x):return round(sat(x)*255)/255
def over(R,G,Bm,a,m,aware=True):                 # fog-aware over shader: (1-f,1,1,a) RGB masked in; fog-less over and rain: green only
    return (q(a*m+(1-a)*R),q(a+(1-a)*G),q(a+(1-a)*Bm)) if aware else (R,q(a+(1-a)*G),Bm)
def addA(R,Bm,v,m,sc=.25):return q(R+v*sc*m),q(Bm+v*sc)      # SRCALPHA/ONE with the fog factor: oC1 = (1-f,1,1,v/4)
def addC(R,Bm,w,m,sc=.25):return q(R+w*sc*m),q(Bm+w*sc)      # ONE/ONE with the fog factor: oC1 = (w/4 (1-f), w/4, ..)
R=G=Bm=0.
layers=[(.5,.2),(.3,.6),(.6,.1)]       # (alpha, 1-f)
for a,m in layers:R,G,Bm=over(R,G,Bm,a,m)
w=[layers[i][0]*math.prod(1-layers[j][0] for j in range(i+1,3)) for i in range(3)]
mean=sum(wi*l[1] for wi,l in zip(w,layers))/sum(w)
checks['numeric: stacked fog-aware over layers: red/blue is their coverage-weighted mean 1-f (8 bit)']=abs(R/Bm-mean)<.02 and abs(G-Bm)<1e-9
R=Bm=0.
for v,m in ((.3,.1),(.2,.5),(.4,.3)):R,Bm=addA(R,Bm,v,m)
checks['numeric: additive by alpha: red/blue is the emission-weighted mean 1-f (quarter scale, 8 bit)']=abs(R/Bm-(.3*.1+.2*.5+.4*.3)/.9)<.05
# R1 (earlier review): blue saturated under stacked bright additive layers while red kept rising: full-scale weights gave a wrong mean, the quarter scale keeps it through sums of weights up to 3
def stack(n,sc):
    R=Bm=0.
    for _ in range(n):R,Bm=addC(R,Bm,1.,.7,sc)
    return R/Bm
checks['numeric: full-scale weights: two bright ONE/ONE layers (1-f = .7) saturate blue while red keeps rising and overestimate the mean (1.0 for .7): the failure the quarter scale removes']=abs(stack(2,1.)-1.0)<.02
checks['numeric: quarter-scale weights: 1..3 bright layers (sum of weights up to 3) keep red/blue = .7 within the 8-bit quantisation']=all(abs(stack(n,.25)-.7)<.03 for n in (1,2,3))
# rain and fog-less layers (R2): a streak over a far lantern leaves red and blue alone, so the mean stays and the lantern keeps its distance
R=Bm=0.;R,Bm=addC(R,Bm,.5,.7)
G=0.;R2,G,B2=over(R,G,Bm,.3,0.,aware=False)
checks['numeric: a rain streak over a far lantern: red and blue are untouched (mean 1-f = .7), the pixel is marked through green']=(R2==R and B2==Bm and abs(R2/B2-.7)<.03 and G>0)
# the streak's own light over the lantern (the DA's disproof): E = lantern + streak, rain share = half the green not explained by blue
vz=700.;fa=0.2;fr=tuple(l*(1-fa) for l in Lh);zp=153.
Es=(.21,.21,.21);E_l=(.9,.5,.1);Bk=col
pix_both=tuple(.7*b+es+el for b,es,el in zip(Bk,Es,E_l))
wl=.25*.5
out_both,tpl,_=composite(pix_both,Bk,.3,wl*m_of(zp),wl,vz,fa,fr,col,Lh,ha,SC,Y0)
out_zero,_,_=composite(tuple(.7*b for b in Bk),Bk,.3,wl*m_of(zp),wl,vz,fa,fr,col,Lh,ha,SC,Y0)       # the same mask, no light of the particles: the common background term
contrib=out_both[0]-out_zero[0]                                              # what E = lantern + streak adds
ideal=tpl*E_l[0]+Es[0]                                                       # the lantern fogged at its distance, the streak (near the camera) not
err_old=(1-tpl)*Es[0]                                                        # attenuating all of E: the streak loses (1-transP) of its light
err_new=abs(contrib-ideal)
night=composite(tuple(.7*b+0.02+el for b,el in zip(Bk,E_l)),Bk,.3,wl*m_of(zp),wl,vz,fa,fr,col,Lh,ha,SC,Y0)[0][0]-out_zero[0]-(tpl*E_l[0]+0.02)   # a dark night streak (E = .02): the cap adds light instead
checks['numeric: a rain streak (a = .3, E = .21) over a fogged lantern (m = .5, 153 of 700 yd, transP = %.2f): the error against the ideal (lantern fogged, streak untouched) is %.3f instead of %.3f of attenuating all of E; a dark night streak (E = .02) gets %.3f extra light. The rain share is half the green that blue does not explain: the lantern pollutes it (blue = %.3f)'%(tpl,err_new,err_old,night,wl)]=(
    err_new<.7*err_old and night<.05)
checks['numeric: a streak over nothing fog-aware is unattenuated (red 0): its E comes through untouched']=all(abs(a-b)<1e-12 for a,b in zip(composite(tuple(.7*b+es for b,es in zip(Bk,Es)),Bk,.3,0.,0.,vz,fa,fr,col,Lh,ha,SC,Y0)[0],previous(tuple(.7*b+es for b,es in zip(Bk,Es)),Bk,.3,vz,fa,fr,col,Lh,ha)))
# R3: faint fringes fade to today's look smoothly: the effect ramps with the red mass from 1/255 to about 8/255
ss=[composite(pixel,Bg,0.,r_,r_/.7,vz,fa,fr,col,Lh,ha,SC,Y0)[2] for r_ in (0.,1/255,2/255,4/255,8/255,16/255)]
checks['numeric: s ramps in with the red mass (0 at 1/255, half-way near 4/255, full from ~8/255): faint fringes fade out smoothly instead of jumping between full, half and no fog']=(
    ss[0]==0 and ss[1]==0 and 0<ss[2]<ss[3]<ss[4] and abs(ss[4]-ss[5])<.15*ss[5] and ss[3]>0)
# the game's fog varies per zone and client: f = min(z x slope + Y, 1) with Y < 1 (HD client), Y = 1 and Y > 1 (stock client: the fog starts away from the camera)
for Y,slope in ((0.6666667,-0.0024),(1.,-0.0019),(1.7857143,-0.005135)):
    scale=1/slope;vz=700.
    fa=0.2;fr=tuple(l*(1-fa) for l in Lh)
    pix=tuple(b+e for b,e in zip(col,E))
    for zp in (120.,250.):
        mm=1-f_at(zp,Y,slope);wgt=.2     # a fog-aware additive layer with weight .2 (red 0 when the particle is inside the fog start, f = 1)
        far=composite(pix,col,0.,wgt*mm,wgt,vz,fa,fr,col,Lh,ha,scale,Y)
        saturated=f_at(zp,Y,slope)>=.998
        checks['numeric: Y=%.3f: a particle at %d yd %s'%(Y,zp,'is inside the fog start (f = 1, red 0): s = 0, unchanged' if saturated else 'inverts to its distance and fades (transP < 1, s = z/viewZ)')]=(
            (far[2]==0. and far[1]==1.) if saturated else (abs(far[2]-zp/vz)<2e-2 and far[1]<1.))
    near=composite(pix,col,0.,.2*(1-f_at(0.,Y,slope)),.2,vz,fa,fr,col,Lh,ha,scale,Y)
    checks['numeric: Y=%.3f: a particle at the camera is unchanged (transP = 1)'%Y]=(abs(near[1]-1)<1e-3)
    beyond=composite(pix,col,0.,.2*1.,.2,vz,fa,fr,col,Lh,ha,scale,Y)    # f = 0: beyond the fog end
    checks['numeric: Y=%.3f: f clamped at 0 beyond the fog end reads as the fog end distance (s = min(end/viewZ, 1)), never brighter than before'%Y]=(abs(beyond[2]-min(1.,(-Y*scale)/vz))<1e-9 and beyond[1]<=1.)
    sentinel=composite(pix,col,0.,0.,.2,vz,fa,fr,col,Lh,ha,scale,Y)
    checks['numeric: Y=%.3f: red 0 with blue (a fog-less additive layer alone) is no distance: the previous composite exactly'%Y]=(sentinel[2]==0. and sentinel[1]==1.)
# distance from f: both handednesses of the projection
for proj in (1.,-1.):
    x_param=SLOPE*proj;scale=1/(x_param*proj);z=(f_at(250.)-Y0)*scale      # the game uploads X so that z x projectionZ x X = z x SLOPE
    checks['numeric: f inverts to the particle distance (projection sign %+d)'%proj]=abs(z-250.)<1e-6
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
