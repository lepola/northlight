// Must match NorthlightMaterialHeight::value. A centered field supports both
// raised details and cavities; colour accents can rise above a lighter base.
float inferredHeight(float3 c,float4 profile) {
    float warm=saturate((c.r-c.g)/max(c.r,.05)*4-.1)*saturate((c.g-c.b)/max(c.g,.05)*4-.1);
    float green=saturate((c.g-c.r)/max(c.g,.05)*4-.1)*saturate((c.g-c.b)/max(c.g,.05)*4-.1);
    float base=.5+(dot(c,float3(.299,.587,.114))-profile.x)*.7*profile.z;
    float accent=max(warm,green)*profile.y;
    return clamp(max(base,lerp(base,.5+.35*accent,accent))+profile.w,.02,.98);
}
