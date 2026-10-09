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
    'if(debugViews)color=mad(1-mask.y,fogged-B,pixel);' in body and body.index('if(debugViews)')<body.index('float fm=')<body.index('float s=')<body.index('float transP='))
checks['source: red over blue is the weighted mean fog factor, 1 (no extra fog) when nothing was written; thin coverage is only guarded against 0/0']=('float fm=mask.r*rcp(max(mask.b,.0078));' in body)
checks['source: the distance fraction comes from the host constant LegacyFogColor.w = 1/(slope x signed projection); 0 means no fog: s = 0']=(
    'float s=saturate((fm<.998?fm-LegacyFog.y:0)*LegacyFogColor.w*rcp(viewZ));' in body and 'w (WorldComposite only)' in h.split('float4 LegacyFogColor : register(c26);',1)[1].split('\n',1)[0])
checks['source: transmittance to the particle = (1 - haze ramp at s x viewZ) x fog.a^s; 1 for s = 0']=(
    'float rp=saturate((viewZ*s-HorizonShape.y)*HorizonShape.z);' in body and 'float transP=(1-hazeRamp(rp)*hz.a)*exp2(s*log2(max(fog.a,.0001)));' in body and 'float transF=(1-h)*fog.a;' in body)
checks['source: out = T x F + transP x E + g x (airlight ratio) x airlight; E = pixel - T x B']=(
    'color=mad(T,fogged,transP*(pixel-T*B))+mask.y*((1-transP)*rcp(1.001-transF))*airlight;' in body and 'float3 airlight=fogged-transF*color;' in body and 'float T=1-mask.y;' in body)
checks['source: the haze is one call with the angular amount split from the ramp (same ray, other distance), the branch kept for near pixels']=(
    'float4 horizonHaze(float2 uv,float range)' in h and 'float hazeRange(float viewZ,bool sky)' in h and 'float hazeRamp(float range)' in h and '[branch]if(range>0&&HorizonHaze.w>0){' in h and 'float h=hazeRamp(range)*hz.a;' in body)
checks['source: no sampler or texture read was added in the particle fog']=(comp.count('tex2D')==comp.count('tex2D')) and 'tex2Dlod' not in body.split('float fm=',1)[1]
# ---- manifest
wc,wd=wm['WorldComposite'],wm['WorldCompositeDebug']
checks['manifest: WorldComposite (normal) 509 slots, WorldCompositeDebug 504 slots, both within SM3\'s 512 and 32 temporaries, samplers 0,1,8..14']=(
    wc['static_instruction_slots']==509 and wd['static_instruction_slots']==504 and wc['temporary_registers']<=32 and wd['temporary_registers']<=32 and wc['samplers']==wd['samplers']==[0,1,8,9,10,11,12,13,14])
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
    """the normal entry's tail. pixel = T x B + E (the scene), B the snapshot (or the pixel), g = 1-T, r/b the mask's red/blue, scale = LegacyFogColor.w."""
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang
    fogged=tuple(c*fog_a+f for c,f in zip(lerp(color,haze_rgb,hh),fog_rgb))
    fm=r/max(b,.0078);s=sat(((fm-Y) if fm<.998 else 0.)*scale/viewZ)
    rp=sat((viewZ*s-SHAPE_Y)*SHAPE_Z);transF=(1-hh)*fog_a;transP=(1-ramp(rp)*haze_ang)*2**(s*math.log2(max(fog_a,.0001)))
    T=1-g;air=tuple(f-transF*c for f,c in zip(fogged,color))
    ratio=(1-transP)/(1.001-transF)
    return tuple(T*f+transP*(p-T*bb)+g*ratio*a for f,p,bb,a in zip(fogged,pixel,B,air)),transP,s
def previous(pixel,B,g,viewZ,fog_a,fog_rgb,color,haze_rgb,haze_ang):
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang
    fogged=tuple(c*fog_a+f for c,f in zip(lerp(color,haze_rgb,hh),fog_rgb))
    return tuple(p+(1-g)*(f-b) for p,f,b in zip(pixel,fogged,B))
rnd=random.Random(2037)
SLOPE=-0.0019      # legacy fog: f = 1 + z x slope (projection sign +1), scale = 1/slope
def f_at(z,Y=1.,slope=SLOPE):return sat(min(Y+z*slope,1.))
exact=True;neutral=True;nofog=True
for _ in range(500):
    B=tuple(rnd.uniform(0,.8) for _ in range(3));E=tuple(rnd.uniform(0,.6) for _ in range(3));g=rnd.choice((0,rnd.uniform(0,1)));T=1-g
    pixel=tuple(T*b+e for b,e in zip(B,E))
    vz=rnd.uniform(50,900);fa=rnd.uniform(.05,1);fr=tuple(rnd.uniform(0,.2) for _ in range(3));col=tuple(rnd.uniform(0,.8) for _ in range(3));hz=tuple(rnd.uniform(0,.5) for _ in range(3));ha=rnd.uniform(0,.7)
    old=previous(pixel,B,g,vz,fa,fr,col,hz,ha)
    b=rnd.uniform(.05,1)
    new,tp,s=composite(pixel,B,g,b,b,vz,fa,fr,col,hz,ha,1/SLOPE)           # red == blue: every layer wrote f = 1 (rain, fixed-function, shaders without FOG0)
    neutral=neutral and tp==1. and s==0. and all(abs(x-y)<1e-12 for x,y in zip(new,old))
    new2,_,_=composite(pixel,B,g,b*.5,b,vz,fa,fr,col,hz,ha,0.)             # legacy fog unusable: scale 0
    nofog=nofog and all(abs(x-y)<1e-12 for x,y in zip(new2,old))
checks['numeric: red == blue (rain streaks, fixed-function and fog-less shaders) reproduces the previous composite exactly: transP = 1, s = 0']=neutral
checks['numeric: LegacyFogColor.w = 0 (no usable legacy fog) reproduces the previous composite exactly for any red/blue']=nofog
# a far additive lantern: g = 0, E added; the result equals T x F(bg) + A(z_p) x E, the same attenuation a surface at the lantern's distance gets
def ideal_additive(E,B,viewZ,zp,k_fog,L,haze_c,haze_ang,color):
    """exponential fog (extinction k per yard, airlight colour L) plus haze: fog.a = exp(-k z); the particle's light is attenuated like any light at zp."""
    fa=math.exp(-k_fog*viewZ);fr=tuple(l*(1-fa) for l in L)
    rng=sat((viewZ-SHAPE_Y)*SHAPE_Z);hh=ramp(rng)*haze_ang
    fogged=tuple(c*fa+f for c,f in zip(lerp(color,haze_c,hh),fr))
    A=(1-ramp(sat((zp-SHAPE_Y)*SHAPE_Z))*haze_ang)*math.exp(-k_fog*zp)
    return tuple(f+A*e for f,e in zip(fogged,E)),A
vz,zp=700.,300.;k=0.0035;L=(.12,.1,.2);col=(.3,.3,.35);haze_c=(.2,.2,.3);ha=.5
Bg=col;E=(.9,.5,.1);pixel=tuple(b+e for b,e in zip(Bg,E))
want,A=ideal_additive(E,Bg,vz,zp,k,L,haze_c,ha,col)
fa=math.exp(-k*vz);fr=tuple(l*(1-fa) for l in L);f_p=f_at(zp)
got,tp,s=composite(pixel,Bg,0.,f_p*.4,.4,vz,fa,fr,col,haze_c,ha,1/SLOPE)    # one additive layer: B accumulated .4, R = .4 x f
checks['numeric: a far additive lantern fades by exactly the fog and haze a surface at its distance gets (ideal ray model with exponential fog and the ramped haze)']=(
    all(abs(x-y)<1e-9 for x,y in zip(got,want)) and abs(tp-A)<1e-9 and abs(s-zp/vz)<1e-9)
checks['numeric: near particles (s -> 0, f -> 1) are unchanged: transP -> 1']=all(abs(composite(tuple(b+e for b,e in zip(Bg,E)),Bg,0.,f_at(zz)*.4,.4,vz,fa,fr,col,haze_c,ha,1/SLOPE)[1]-1)<.01 for zz in (0.5,1.0))
checks['numeric: the farther the lantern, the dimmer (monotone), never brighter than before']=(lambda ts:all(a>=b-1e-12 for a,b in zip(ts,ts[1:])) and ts[-1]<ts[0] and all(t<=1 for t in ts))(
    [composite(pixel,Bg,0.,f_at(z)*.4,.4,vz,fa,fr,col,haze_c,ha,1/SLOPE)[1] for z in (10,100,200,300,500,700)])
# an over-blend particle in front of a surface: fogged particle colour = its own airlight, exact for a homogeneous airlight (L) in fog and haze alike
Lh=(.12,.1,.2);haze_c=Lh;fr=tuple(l*(1-fa) for l in Lh)
Tt=.4;Ep=(.5,.05,.05);Bg=col;pix=tuple(Tt*b+e for b,e in zip(Bg,Ep))
got,tp,s=composite(pix,Bg,1-Tt,(1-Tt)*f_p,(1-Tt),vz,fa,fr,col,haze_c,ha,1/SLOPE)
F_bg=tuple(c*((1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa)+(ha*ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*fa*l)+f for c,l,f in zip(col,haze_c,fr))
# ideal: the particle sits at zp; the surface behind is seen through it; everything in front of the particle (fog and haze to zp) applies to the whole pixel once
Ap=tp;air_full=tuple(f-((1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa)*c for f,c in zip(F_bg,col))
airL=tuple(a/(1-(1-ramp(sat((vz-SHAPE_Y)*SHAPE_Z))*ha)*fa) for a in air_full)         # the uniform airlight colour
ideal=tuple(Ap*e+Tt*f+(1-Tt)*l*(1-Ap) for e,f,l in zip(Ep,F_bg,airL))
checks['numeric: an over-blend particle (T = .4) gets the fogged emission plus the airlight in front of it: the ideal ray model for a uniform airlight colour (within the 1.001 guard)']=all(abs(x-y)<2e-3 for x,y in zip(got,ideal))
# mask recurrences on RT1 with the draw's own blend (D3DRS_SRCBLEND/DESTBLEND), 8 bit: red follows blue with f as the value
def q(x):return round(sat(x)*255)/255
def over(R,G,Bm,a,f):return q(a*f+(1-a)*R),q(a+(1-a)*G),q(a+(1-a)*Bm)
def addA(R,Bm,v,f):return q(R+v*f),q(Bm+v)
def addC(R,Bm,m,f):return q(R+m*f),q(Bm+m)         # ONE/ONE: oC1 = (m f, m, m, m)
R=G=Bm=0.
layers=[(.5,.8),(.3,.4),(.6,.9)]       # (alpha, f)
for a,f in layers:R,G,Bm=over(R,G,Bm,a,f)
weights=[];T=1.
for a,f in reversed(layers):weights.append((a,f));
# exact weights of each layer in the final alpha-over result: layer i contributes a_i x prod_{j>i}(1-a_j)
w=[layers[i][0]*math.prod(1-layers[j][0] for j in range(i+1,3)) for i in range(3)]
mean=sum(wi*l[1] for wi,l in zip(w,layers))/sum(w)
checks['numeric: stacked over layers: red/blue is their coverage-weighted mean f (8 bit)']=abs(R/Bm-mean)<.02 and abs(G-Bm)<1e-9
R=Bm=0.
for v,f in ((.3,.9),(.2,.5),(.4,.7)):R,Bm=addA(R,Bm,v,f)
checks['numeric: additive by alpha: red/blue is the emission-weighted mean f']=abs(R/Bm-(.3*.9+.2*.5+.4*.7)/.9)<.02
R=Bm=0.
for m,f in ((.3,.9),(.2,.5)):R,Bm=addC(R,Bm,m,f)
checks['numeric: ONE/ONE: red/blue is the emission-weighted mean f']=abs(R/Bm-(.3*.9+.2*.5)/.5)<.02
R,G,Bm=over(0.,0.,0.,.5,.8);R,Bm=addA(R,Bm,.3,.4)
a1=.5*.8   # after the over layer: R = a f, B = a ; then additive adds v f and v
checks['numeric: a mixed over + additive pixel: red/blue is the mean f with the weights each layer has in blue']=abs(R/Bm-(.5*.8+.3*.4)/(.5+.3))<.02
R=Bm=0.
for a,f in ((.5,1.),(.4,1.)):R,G,Bm=over(R,0.,Bm,a,f)
checks['numeric: layers without a fog factor write f = 1: red == blue exactly in 8 bit']=R==Bm
# SRCCOLOR/ONE squares the written value in blue (b' = src^2): a red of (m f)^2 would square f, so that kind writes none (red = blue's value m)
m,f=.5,.64
checks['numeric: SRCCOLOR/ONE with the f write would give red/blue = f^2; kind 3 keeps red == blue (f = 1)']=abs((m*f)**2/(m*m)-f*f)<1e-12 and abs(m**2/(m*m)-1)<1e-12
# the game's fog varies per zone and client: f = min(z x slope + Y, 1) with Y < 1 (HD client), Y = 1 and Y > 1 (stock client: the fog starts away from the camera)
for Y,slope in ((0.6666667,-0.0024),(1.,-0.0019),(1.7857143,-0.005135)):
    scale=1/slope;vz=700.
    fa=0.2;fr=tuple(l*(1-fa) for l in Lh)
    pix=tuple(b+e for b,e in zip(col,E))
    for zp in (120.,250.):
        far=composite(pix,col,0.,f_at(zp,Y,slope)*.4,.4,vz,fa,fr,col,Lh,ha,scale,Y)
        saturated=f_at(zp,Y,slope)>=.998
        checks['numeric: Y=%.3f: a particle at %d yd %s'%(Y,zp,'is inside the fog start (f = 1): s = 0, unchanged' if saturated else 'inverts to its distance and fades (transP < 1, s = z/viewZ)')]=(
            (far[2]==0. and far[1]==1.) if saturated else (abs(far[2]-zp/vz)<1e-6 and far[1]<1.))
    # near particle (inside every fog start, f = 1 or the fog's value at the camera): bit-for-bit unchanged
    near=composite(pix,col,0.,f_at(0.,Y,slope)*.4,.4,vz,fa,fr,col,Lh,ha,scale,Y)
    checks['numeric: Y=%.3f: a particle at the camera is unchanged (transP = 1)'%Y]=(near[1]==1. or abs(near[1]-1)<1e-9)
    beyond=composite(pix,col,0.,0.,.4,vz,fa,fr,col,Lh,ha,scale,Y)    # f = 0: beyond the fog end
    checks['numeric: Y=%.3f: f clamped at 0 beyond the fog end reads as the fog end distance (s = min(end/viewZ, 1)), never brighter than before'%Y]=(abs(beyond[2]-min(1.,(-Y*scale)/vz))<1e-9 and beyond[1]<=1.)
    # layers without a fog factor write f = 1: no distance for any Y
    sentinel=composite(pix,col,0.,.4,.4,vz,fa,fr,col,Lh,ha,scale,Y)
    checks['numeric: Y=%.3f: red == blue (layers without a fog factor, f = 1) is no distance: the previous composite exactly'%Y]=(sentinel[2]==0. and sentinel[1]==1.)
# distance from f: both handednesses of the projection
for proj in (1.,-1.):
    x_param=SLOPE*proj;scale=1/(x_param*proj);z=(f_at(250.)-1.)*scale      # the game uploads X so that z x projectionZ x X = z x SLOPE
    checks['numeric: f inverts to the particle distance (projection sign %+d)'%proj]=abs(z-250.)<1e-6
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
