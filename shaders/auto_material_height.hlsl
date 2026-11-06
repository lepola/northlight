// Must match NorthlightMaterialHeight::value. A centered field supports both
// raised details and cavities; colour accents can rise above a lighter base.
float inferredHeight(float3 c,float4 profile) {
    float base=.5+(dot(c,float3(.299,.587,.114))-profile.x)*.7*profile.z;
    [branch]if(profile.y==0)return clamp(base+profile.w,.02,.98);
    // The opposite red/green differences cannot both survive saturation.
    float colorEdge=saturate(abs(c.r-c.g)/max(max(c.r,c.g),.05)*4-.1);
    [branch]if(colorEdge<=0)return clamp(base+profile.w,.02,.98);
    float accent=colorEdge*saturate((c.g-c.b)/max(c.g,.05)*4-.1)*profile.y;
    return clamp(max(base,lerp(base,.5+.35*accent,accent))+profile.w,.02,.98);
}
