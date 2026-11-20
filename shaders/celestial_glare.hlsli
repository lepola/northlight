// Northlight's procedural atmospheric glare, shared by sky and water adapters.
#ifndef NORTHLIGHT_CELESTIAL_GLARE
#define NORTHLIGHT_CELESTIAL_GLARE
float CelestialGlareProfile(float radius,bool solar,float4 emission,float4 hue,float4 core,float disc,float ring)
{
    float r2=radius*radius;
    float edge=1-smoothstep(.35*emission.y,emission.y,radius);
    float3 tail=float3(exp2(-.12*r2),exp2(-.025*r2),exp2(-.008*r2));
    float tailed=max(disc*dot(tail,float3(.25,.35,.40)),core.w*ring*dot(tail,float3(.118257,.299458,.380527)));
    // Preserve the native branch order and expression grouping for sky parity.
    if(solar)return (emission.z*exp2(-emission.x*r2)*disc+hue.w*tailed)*edge;
    return emission.z*.35*exp2(-1.11*max(radius-1,0))*edge*disc;
}
float3 CelestialGlareColour(float radius,float4 emission,float4 hue,float4 core)
{return saturate(lerp(hue.rgb,core.rgb,smoothstep(0,1,exp2(-emission.x*radius*radius))));}
float3 CelestialGlowShoulder(float x,float3 colour)
{return 1-exp2(-1.442695*x*colour);}
float3 CelestialGlareBackground(float3 background,float3 direction,float4 body,float4 emission,float4 hue,float4 core,float4 control,float2 visibility)
{
    float forward=dot(direction,body.xyz);
    float radius=sqrt(max(1/max(forward*forward,1e-10)-1,0))/max(body.w,1e-6);
    float alpha=control.x*(forward>0?1:0)*CelestialGlareProfile(radius,control.y>.5,emission,hue,core,saturate(visibility.x),saturate(visibility.y));
    return background+(1-background)*CelestialGlowShoulder(alpha,CelestialGlareColour(radius,emission,hue,core));
}
#endif
