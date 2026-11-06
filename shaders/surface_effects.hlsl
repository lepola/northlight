// Northlight-owned supplemental precipitation. Native water rendering is untouched.
sampler2D FinalDepth : register(s3);
sampler2D FrostMaterial : register(s1);
sampler2D SnowMaterial : register(s2);
sampler2D WeatherNoise : register(s4);
float4 Clip : register(c0);       // inverse image size, near, far
float4 Proj : register(c1);       // scale x/y, forward sign, animation time
float4 DepthRange : register(c2); // viewport min, reciprocal extent
float4 Weather : register(c3);    // amount, snow flag, particle radius scale minus one, reserved
float4 Inv0 : register(c4);
float4 Inv1 : register(c5);
float4 Inv2 : register(c6);
float4 Inv3 : register(c7);
float4 Ambient : register(c8);
float4 Direct : register(c9);

float linearDepth(float d) {
    d=saturate((d-DepthRange.x)*DepthRange.y);
    return Clip.z*Clip.w/max(Clip.w-d*(Clip.w-Clip.z),.00001);
}
float3 position(float2 uv,float z) {
    float2 xy=((uv-.5*Clip.xy)*float2(2,-2)+float2(-1,1))/Proj.xy;
    return float3(xy,Proj.z)*z;
}
float3 toWorld(float3 v) {return v.x*Inv0.xyz+v.y*Inv1.xyz+v.z*Inv2.xyz+Inv3.xyz;}
float3 random3(float3 cell) {return frac(sin(float3(dot(cell,float3(127.1,311.7,74.7)),dot(cell,float3(269.5,183.3,246.1)),dot(cell,float3(113.5,271.9,124.6))))*43758.5453);}
float2 snowCoordinates(float2 p,float seed) {
    float angle=seed*6.2831853;
    p=float2(dot(p,float2(cos(angle),-sin(angle))),dot(p,float2(sin(angle),cos(angle))));
    p*=float2(lerp(.75,1.65,frac(seed*7.3)),lerp(.7,1.25,frac(seed*3.7)));
    return p*.25+.5;
}
float snowAtlas(float2 uv,float index) {
    index=frac(index/16)*16;float row=floor(index*.25);
    float2 tile=float2(index-row*4,row);
    float2 edge=min(uv,1-uv);
    return tex2D(SnowMaterial,(tile+clamp(uv,.01,.99))*.25).r*smoothstep(0,.035,min(edge.x,edge.y));
}
float snowFloc(float2 p,float seed) {
    float ice=snowAtlas(snowCoordinates(p,seed),floor(seed*15.999));
    return ice*ice*1.4;
}
float snowMorph(float2 p,float seed,float phase) {
    float2 uv=snowCoordinates(p,seed);float index=floor(phase),blend=smoothstep(0,1,frac(phase));
    float ice=lerp(snowAtlas(uv,index),snowAtlas(uv,index+1),blend);
    return ice*ice*1.4;
}
float4 precipitation(float2 uv,int layers,bool snow) {
    float scene=linearDepth(tex2D(FinalDepth,uv).r);float opacity=0;
    [unroll] for(int layer=0;layer<3;++layer) {
        if(layer>=layers)break;
        float distance=layer==0?3.5:layer==1?11:26;
        float weight=layer==0?.45:layer==1?.35:.20;
        float3 p=toWorld(position(uv,distance));
        float time=Proj.w,scale=snow?.8:1.2;
        p+=float3(time*.5,time*.2,time*(snow?1.3:19));p*=scale;
        float3 cell=floor(p),seed=random3(cell);float3 delta=frac(p)-seed;
        if(snow)delta.xy+=sin(time*.7+cell.z+seed.xy*6.28)*.08;
        float radius=(snow?.036+.026*seed.x:.014)*clamp(1+Weather.z,.25,3);
        float shape=snow?snowFloc(delta.xy/radius,seed.y)*saturate(1-abs(delta.z)/(radius*.9)):saturate(1-length(delta.xy)/radius)*saturate(1-abs(delta.z)/.32);
        shape*=smoothstep(distance-.5,distance+.5,scene)*step(seed.z,Weather.x);
        opacity+=shape*weight;
    }
    float alpha=saturate(opacity*.45*Weather.x);
    float3 color=saturate(float3(.45,.53,.62)+Ambient.rgb*.45+Direct.rgb*.15);
    if(snow)color=lerp(color,1,.55);
    return float4(color*alpha,alpha);
}
float4 RainQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3,false);}
float4 SnowQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3,true);}
float4 RainPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2,false);}
float4 SnowPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2,true);}

// Camera optics share the embedded snow and frost materials with the standalone lab.
sampler2D LensScene : register(s0);
float4 Lens : register(c10); // integrated animation seconds, visibility, aspect, weather intensity
float4 Coating : register(c11); // frost coverage, sand amount, integrated time, grain size
float3 lensRandom3(float2 cell,float cycle) {
    float3 p=frac(float3(cell,cycle)*float3(.1031,.11369,.13787));
    p+=dot(p.zxy,p.yzx+19.19);
    return frac((p.xxy+p.yzz)*p.zyx);
}
float4 lensPattern(float2 cell,bool snow) {
    float3 seed=lensRandom3(cell,0);
    float age=Lens.x*(snow?.035:.22)*(.65+.7*seed.y)+seed.z;
    float phase=frac(age),cycle=floor(age);
    // Every invisible birth changes the position and occupancy of this cell.
    // Independent rates and empty cells prevent rows of equally spaced marks.
    float3 random=lensRandom3(cell,cycle+1);
    float chance=(snow?.16:.28)+(snow?.50:.55)*saturate(Lens.w);
    float life=smoothstep(0,snow?.12:.08,phase)*(1-smoothstep(snow?.62:.82,1,phase));
    life*=1-smoothstep(chance-.08,chance+.08,random.z);
    float2 center;
    center.x=(random.x-.5)*(snow?.56:.64);
    center.y=snow?(random.y-.5)*.32+.15*phase*phase-.075:
                     -.28+(random.y-.5)*.12+(.52+.10*random.z)*phase*phase;
    float radius=snow?.07+.075*random.z:.045+.04*random.y;
    return float4(center,radius,life);
}
float4 RainLens(float2 uv:TEXCOORD0):COLOR0 {
    float2 scale=float2(13*Lens.z,13),grid=uv*scale,cell=floor(grid),local=frac(grid)-.5;
    float4 pattern=lensPattern(cell,false);float life=pattern.w;
    float2 d=local-pattern.xy;float radius=pattern.z;float distance=length(d);
    float body=(1-smoothstep(radius*.82,radius,distance))*life;
    float trail=(1-smoothstep(.005,.012,abs(d.x)))*(1-smoothstep(.02,.16,-d.y))*step(d.y,0)*life;
    // Bend rays toward the droplet center; the lens strength fades with weather.
    float2 normal=d/(distance+.0001);float4 base=tex2D(LensScene,uv),bent=tex2D(LensScene,uv-normal*(.022*body*Lens.y));
    float rim=(smoothstep(radius*.45,radius*.76,distance)-smoothstep(radius*.76,radius,distance))*life;
    float highlight=pow(saturate(normal.x*.35-normal.y*.65),5)*body*.36;
    base.rgb=lerp(base.rgb,bent.rgb,body);base.rgb+=(rim*.025+trail*.018+highlight)*Lens.y;return base;
}
float4 SnowLens(float2 uv:TEXCOORD0):COLOR0 {
    float2 grid=uv*float2(6*Lens.z,6),cell=floor(grid),local=frac(grid)-.5;
    float4 pattern=lensPattern(cell,true);float life=pattern.w;
    float2 d=local-pattern.xy;float size=pattern.z;float r=length(d*float2(1,.82));
    float soft=1-smoothstep(size*.25,size*1.4,r);
    float chip1=1-smoothstep(size*.25,size*.75,length(d-float2(size*.48,size*.22)));
    float chip2=1-smoothstep(size*.18,size*.65,length(d+float2(size*.28,size*.44)));
    float alpha=saturate((soft*.30+chip1*.24+chip2*.20)*life*Lens.y);
    float4 color=tex2D(LensScene,uv);color.rgb=lerp(color.rgb,float3(.68,.72,.76),alpha);return color;
}

// Sparse, persistent beads and snow clumps. The CPU conserves their masses;
// TEXCOORD2 describes the merged head and its curved, gravity-driven rivulet.
float4 WeatherLensParticle(float2 uv:TEXCOORD0,float2 d:TEXCOORD1,float4 flow:TEXCOORD2,float4 state:TEXCOORD3):COLOR0 {
    float radius=flow.x,tail=flow.y,bend=flow.z,snow=flow.w;
    if(Lens.x>1.5){
        float2 local=d/max(radius,.0001),sampleUV=local*.03+float2(state.y,state.y*.73);
        float grain=tex2D(WeatherNoise,sampleUV).r;
        float shape=1-smoothstep(.20,1,length(local*float2(.8+.4*state.y,1)));
        float alpha=shape*(.25+.55*grain)*state.x*Lens.y;
        return float4(lerp(float3(.19,.14,.08),float3(.58,.46,.29),grain),alpha);
    }
    float distance=length(d),body=1-smoothstep(radius*.75,radius,distance);
    float2 axis=float2(sin(state.w),cos(state.w)),cross=float2(axis.y,-axis.x);
    float2 trailD=float2(dot(d,cross),dot(d,axis));
    float t=saturate(-trailD.y/max(tail,.00001));
    float streamX=trailD.x-bend*t*t;
    float width=radius*lerp(.40,.15,t);
    float stream=(1-smoothstep(width*.5,width,abs(streamX)))*step(trailD.y,0)*(1-smoothstep(tail*.85,tail+.00001,-trailD.y));
    float water=max(body,stream*.95)*(1-snow);
    float frost=snowMorph(d/max(radius,.0001),state.y,state.z)*snow;
    float2 normal=d/max(distance,.0001);
    normal=lerp(cross*(streamX/max(width,.0001)),normal,body);
    float3 refracted=tex2D(LensScene,uv-normal*float2(.012/Lens.z,.012)*water).rgb;
    float rim=(smoothstep(radius*.45,radius*.76,distance)-smoothstep(radius*.76,radius,distance));
    float shine=pow(saturate(normal.x*.35-normal.y*.65),5)*body;
    refracted+=(rim*.035+shine*.18)*(1-snow);
    float opacity=saturate((water*.92+frost*.72)*state.x*Lens.y);
    float light=.58+.42*saturate(dot(refracted,float3(.2126,.7152,.0722))*1.4);
    float3 color=lerp(refracted,float3(.88,.92,.96)*light,saturate(frost/max(water+frost,.0001)));
    return float4(color,opacity);
}

float coatingHash(float2 p) {
    p=frac(p*float2(.1031,.11369));p+=dot(p,p.yx+33.33);
    return frac((p.x+p.y)*p.x);
}
float coatingNoise(float2 p) {
    float2 cell=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(coatingHash(cell),coatingHash(cell+float2(1,0)),f.x),
                lerp(coatingHash(cell+float2(0,1)),coatingHash(cell+1),f.x),f.y);
}
float edgeDistance(float2 uv) {float2 d=min(uv,1-uv);return min(d.x*Lens.z,d.y);}

// Original frost-on-glass material; accumulation reveals the irregular corner deposits.
float4 FrostVignette(float2 uv:TEXCOORD0):COLOR0 {
    float coverage=saturate(Coating.x),edge=edgeDistance(uv);
    clip(.31-edge);
    float2 materialUV=uv;
    float ice=tex2D(FrostMaterial,materialUV).r;
    float reveal=smoothstep(lerp(.82,.02,coverage),lerp(.82,.02,coverage)+.18,ice);
    // This pass writes RGB with SRCALPHA/INVSRCALPHA and no depth/stencil writes.
    clip(reveal>0?1:-1);
    float2 gradient=ice-float2(tex2D(FrostMaterial,materialUV+float2(.00195,0)).r,
                              tex2D(FrostMaterial,materialUV+float2(0,.00347)).r);
    float facets=min(.22,length(gradient)*2.5)*saturate((ice-.1)*1.6);
    float alpha=saturate(.08*ice+.65*facets+.20*ice*ice)*coverage*reveal;
    float2 deflection=gradient*float2(.006/Lens.z,.006)*coverage;
    float3 glass=tex2D(LensScene,uv+deflection).rgb;
    float3 color=lerp(glass,float3(.88,.91,.94),.50+.35*facets);
    return float4(color,alpha);
}

// A sparse film on the glass, fixed in lens coordinates at every camera angle.
float4 SandLens(float2 uv:TEXCOORD0):COLOR0 {
    float2 p=uv*float2(Lens.z,1);
    float broad=tex2D(WeatherNoise,p*.035+float2(.23,.61)).r;
    [branch]if(broad<=.58)return float4(.45,.35,.22,0);
    float fine=tex2Dlod(WeatherNoise,float4(p*.16+float2(.71,.17),0,0)).r;
    float deposit=smoothstep(.58,.88,broad)*smoothstep(.45,.75,fine);
    float edge=1-smoothstep(0,.18,edgeDistance(uv));
    float alpha=Coating.y*deposit*(.055+.08*edge);
    return float4(.45,.35,.22,alpha);
}
