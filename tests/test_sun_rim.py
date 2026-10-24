# northlight-test: requires=client
"""Numeric sun rim / visibility regression; no GPU or game."""
import itertools,json,math,struct
from pathlib import Path
import sys; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
root=fp.client_root()
raw=(root/'world-cache/celestial/sun.fct').read_bytes()
assert len(set(raw[19::4]))==16

# soft edge, opaque core to .44 radius (was .64), so the glare washes it out.
def opacity(radius):
 a=max(0,min(1,(1-radius)/.56));return a*a*(3-2*a)
values=[opacity(i/10000) for i in range(11001)]
assert values[0]==1 and values[4400]==1 and values[4401]<1 and values[10000]==0
assert all(a>=b for a,b in zip(values,values[1:]))
assert max(a-b for a,b in zip(values,values[1:]))<.00042
# New shadow-depth ownership excludes every translucent sun rim sample, so
# later sky color can fill that pixel before the late soft-alpha composite.
for alpha in values:
 if alpha<.99999:assert alpha-.99999<0
# Refactored glare visibility keeps the previous geometry/water gates.
cases=0
for valid,depth,water_on,dist,alpha in itertools.product([0,1],[0,.94,.9999999,1],[0,1],[0,.1,.2,.3,100,1000],[0,.001,.003,.004,1]):
 old=0 if valid<.5 or depth<.99999994 else 1
 if water_on and dist>.2 and dist<=100 and alpha>.003:old=0
 new=int(valid>=.5 and depth>=.99999994)
 if water_on:new*=1-int(dist>.2 and dist<=100 and alpha>.003)
 assert new==old;cases+=1
# The sun's glare must peak at the center and decay without a bright ring;
# the lunar texture exclusion remains unchanged. Both use source visibility.
# a broad solar term to the 20R support and a wider lunar skirt (6R);
# the terrain masks span the same 20R/6R (0.3.217; narrower masks left a square edge).
# 0.3.164 (user: 0.3.163 "like a football with sharp edges"): the glare is an
# optical depth x (small hot core + long soft tail) written as 1-exp(-x*colour)
# through a screen blend, so no plateau/clip rim anywhere. Constants are read
# from celestial_glow.h, so the model cannot drift from the host.
import re
glow=fp.src('celestial_glow.h').read_text()
def constant(name):
 return float(re.search(r'\b'+name+r'=([0-9.]+)f',glow).group(1))
CORE,RATE,TAIL,TAIL_LOW,MOON=(constant(n) for n in ('SunCoreWeight','SunCoreRate','SunTailWeight','SunTailWeightLow','MoonGlareStrength'))
def smooth(a,b,x):
 t=max(0,min(1,(x-a)/(b-a)));return t*t*(3-2*t)
def tail(r):r2=r*r;return .25*2**(-.12*r2)+.35*2**(-.025*r2)+.40*2**(-.008*r2)
def glare(radius, sun, visible=1, wrap=0, ring=0, tailWeight=None):
 # the core only by the disc taps; the ring (x wrap) only for the core-free
 # tail shifted out by 3R, tail(sqrt(r^2+9)): monotonic for any visibilities.
 disc=visible*(2-visible);tailed=max(disc*tail(radius),wrap*ring*(2-ring)*tail(math.sqrt(radius*radius+9)))
 support=20 if sun else 6
 edge=1-smooth(.35*support,support,radius)
 if sun:halo=CORE*2**(-RATE*radius*radius)*disc+(TAIL if tailWeight is None else tailWeight)*tailed
 else:halo=MOON*.35*2**(-1.11*max(radius-1,0))*disc
 return edge*halo*(1 if sun else 1-opacity(radius))
def core_weight(r):return smooth(0,1,2**(-RATE*r*r))
def final(radius,sky,hue,core,**k):
 w=core_weight(radius);x=glare(radius,True,**k)
 return [1-(1-d)*math.exp(-x*(h+(c-h)*w)) for d,h,c in zip(sky,hue,core)]
solar=[glare(i/10000,True) for i in range(200001)]
assert solar[0]>1 and solar[-1]==0
assert all(a>=b for a,b in zip(solar,solar[1:]))
assert max(a-b for a,b in zip(solar,solar[1:]))<.001
for radius in (0,.64,.8,1,2,3.9,12):
 assert glare(radius,True,0)==0 and glare(radius,False,0)==0
assert glare(0,False)==0
# Wrap: a trunk hiding the disc keeps a partial TAIL glow from the ring around
# it (the tail shifted out by 3R: a soft plateau, never the core, so no core
# blob over a near mountain); a roof or hill hiding both leaves none.
for radius in (0,1,3,8):
 assert math.isclose(glare(radius,True,0,.6,1),.6*TAIL*tail(math.sqrt(radius*radius+9))*(1-smooth(7,20,radius)),rel_tol=1e-12,abs_tol=1e-15)
 assert glare(radius,True,0,.6,0)==0
 assert glare(radius,True,1,.6,1)==glare(radius,True)
assert glare(0,True,0,.6,1)<.25*glare(0,True)  # no core over a hidden disc
# Monotonic for every (disc, ring) visibility: a barely hidden sun has no dark
# ring around it (a radial fade-in of the ring rose again after a dip at ~2R).
for a in range(11):
 for b in range(11):
  values=[glare(i*.1,True,a/10,.6,b/10) for i in range(121)]
  assert all(y<=x+1e-12 for x,y in zip(values,values[1:])),(a,b)
lunar=[glare(i/10000,False) for i in range(10000,60001)]
assert all(a>=b for a,b in zip(lunar,lunar[1:])) and lunar[-1]==0
# Partial cover retains more of the apparent glow footprint without changing
# either body's radius. Fully visible and fully hidden endpoints stay exact.
assert .5*(2-.5)==.75
curve=[(i/10000)*(2-i/10000) for i in range(10001)]
assert curve[0]==0 and curve[-1]==1
assert all(a<=b for a,b in zip(curve,curve[1:]))
assert max(b-a for a,b in zip(curve,curve[1:]))<.0002
for sun in (False,True):
 for radius in (.8,1,1.5,2):
  full=glare(radius,sun)
  assert math.isclose(glare(radius,sun,.5),full*.75,abs_tol=1e-14)
# Hot core: the colour follows the core's own falloff to warm white, so a
# saturated sunset hue is not red-cored; beyond ~2R it is the hue.
assert core_weight(0)==1 and core_weight(1)<.5 and core_weight(1.5)<.1 and core_weight(2)<.01
# No football: sample the final colour (screen blend over the sky, the 1-exp
# shoulder) every .25R from 0 to 12R, sun up and low sun, over a green
# (Tirisfal) and a warm (Durotar) sky. Luminance falls monotonically; the first
# difference changes smoothly (no kink); the second difference has no spike
# beyond the core (the old clip rim sat at ~4R and showed as a jump).
profiles={}
for name,sky,hue,core,tailWeight in (('tirisfal',(.30,.50,.40),(.76,1,.64),(.95,1,.74),TAIL),
                                   ('durotar_low',(.85,.55,.35),(1,.57,.24),(1,.91,.66),TAIL_LOW)):
 lum=[]
 for i in range(49):
  f=final(i*.25,sky,hue,core,tailWeight=tailWeight);assert all(0<=v<1 for v in f)  # never clipped
  lum.append(.2126*f[0]+.7152*f[1]+.0722*f[2])
 d1=[b-a for a,b in zip(lum,lum[1:])];d2=[b-a for a,b in zip(d1,d1[1:])]
 assert all(v<=0 for v in d1),name
 outer=[abs(v) for v in d2[6:]]  # r >= 1.5R
 assert max(outer)<.012 and max(abs(b-a) for a,b in zip(outer,outer[1:]))<.006,name
 assert max(abs(v) for v in d2)<.08,name
 profiles[name]={r:round(glare(r,True,tailWeight=tailWeight),3) for r in (1,1.5,2,3,4,6,10)}
# The old 0.3.163 model (additive, alpha clipped at 1 by the target) shows the rim this check rejects.
old=[min(1,2.4*((.70*2**(-.9*r*r)+.23*2**(-.10*r*r)+.10*2**(-.02*r*r)+.30*2**(-.008*r*r))*(1-smooth(7,20,r)))) for r in (i*.25 for i in range(49))]
old_d2=[(c-b)-(b-a) for a,b,c in zip(old,old[1:],old[2:])]
assert max(abs(v) for v in old_d2[6:])>.012
shader=fp.src('celestial_disc_effects.hlsl').read_text()
host=fp.src('celestial_disc_renderer.h').read_text()
assert 'current=current*(2-current);' in shader
assert shader.index('current=current*(2-current);')<shader.index('value=lerp(previous,current')
assert 'constexpr float SunCoreWeight=2.4f,SunCoreRate=1.1f,SunTailWeight=.9f,SunTailWeightLow=1.8f,MoonGlareStrength=1.f;' in glow
# The occlusion-only pass suppresses the compact solar core; the colour pass keeps this baseline.
assert 'c[12][2]=i?NorthlightCelestialGlow::MoonGlareStrength:(occlude?0.f:NorthlightCelestialGlow::SunCoreWeight);' in host
assert '{D3DRS_SRCBLEND,DWORD(glare?D3DBLEND_INVDESTCOLOR:D3DBLEND_SRCALPHA)},{D3DRS_DESTBLEND,DWORD(D3DBLEND_ONE)}' in host
assert 'if(DiscRepair.w<.5)alpha*=1-texel.a*saturate(30*ray.z/forward);' in shader
assert 'if(DiscPolicy.w>.5&&depth<.99999994)return 0;' in shader
assert 'if(DiscRepair.w>.5){float a=saturate((1-length(local))/.56);texel.a=a*a*(3-2*a);}' in shader
assert 'if(DiscRepair.w>.5)return (DiscEmission.z*exp2(-DiscEmission.x*r2)*disc+' in shader
assert 'float3 tail=float3(exp2(-.12*r2),exp2(-.025*r2),exp2(-.008*r2));' in shader
assert 'float tailed=max(disc*dot(tail,float3(.25,.35,.40)),ring*dot(tail,float3(.118257,.299458,.380527)));' in shader
assert 'return (DiscEmission.z*exp2(-DiscEmission.x*r2)*disc+DiscGlowHue.w*tailed)*edge;' in shader
assert 'return DiscEmission.z*.35*exp2(-1.11*max(radius-1,0))*edge*disc;' in shader
assert 'float edge=1-smoothstep(.35*DiscEmission.y,DiscEmission.y,radius);' in shader
assert 'return saturate(lerp(DiscGlowHue.rgb,DiscGlowCore.rgb,smoothstep(0,1,exp2(-DiscEmission.x*radius*radius))));' in shader
assert 'float3 glowShoulder(float x,float3 color){return 1-exp2(-1.442695*x*color);}' in shader
assert 'if(DiscEmission.w>.5)encoded=glowShoulder(alpha,encoded);' in shader
assert 'constexpr float SunGlareSupport=20.f,MoonGlareSupport=6.f;' in glow
# 0.3.217: the terrain mask spans the whole glare support (no square edge on distant mountains).
assert 'constexpr float SunMaskRadius=SunGlareSupport,MoonMaskRadius=MoonGlareSupport;' in glow
assert 'c[12][1]=i?MoonGlareSupport:SunGlareSupport;' in host
assert 'halo.tangentRadius*=i?MoonGlareSupport:SunGlareSupport;' in host
# The terrain-mask gate, mask matrix and c46 extent use the mask radius (20R/6R).
assert 'sourceTaps(disc,inverseView,projection,width,height,i?MoonMaskRadius:SunMaskRadius,false,c);' in host
assert 'NorthlightCelestialTerrain::matrix(disc,view+12,body?MoonMaskRadius:SunMaskRadius,size,matrix,nearCut);' in host
assert 'const float terrainPolicy[]={terrainReady[i]?1.f:0.f,i?MoonMaskRadius:SunMaskRadius,' in host
# Small hot core (x >= 1 only to ~1.6R with the sun up), tinted tail past 10R,
# zero at the 20R support; a low sun only widens the tail.
assert glare(1.5,True)>1 and glare(2,True)<1 and glare(4,True)<.7 and glare(10,True)>.2 and glare(15,True)>.03 and glare(19.99,True)<.000001
assert glare(4,True,tailWeight=TAIL_LOW)>glare(4,True) and glare(1,True,tailWeight=TAIL_LOW)-glare(1,True)<1
assert glare(20,True)==glare(21,True)==0
assert glare(2.4,False)>glare(2.4,False,0) and glare(3,False)>.02 and glare(6,False)==0
# Moon at the corrected M1 target: .27 at 1.33R, .17 at 1.9R (one .35 peak, not two gains).
assert abs(glare(1.33,False)-.27)<.005 and abs(glare(1.9,False)-.175)<.005
print(json.dumps({'status':'PASS','sun_glare_x':profiles,'sun_glare_monotonic_samples':len(solar),'moon_glare_monotonic_samples':len(lunar),'native_sun_alpha_levels':16,'visibility_equivalence_cases':cases,'continuous_rim_samples':len(values),'game_or_gpu_launched':False},indent=2))
