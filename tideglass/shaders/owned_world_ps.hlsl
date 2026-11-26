sampler2D Texture0 : register(s0);
sampler2D Texture1 : register(s1);
sampler2D Texture2 : register(s2);
sampler2D Texture3 : register(s3);
sampler2D AlphaAtlas : register(s4);
float4 cFogRange : register(c0);
float4 cFogColour : register(c1);
float4 cMaterial : register(c2); // terrain flag (-1: distant fog silhouette), alpha cutoff, shadow strength
float4 cAmbient : register(c4);
float4 cDirect : register(c5);
float4 cLayer[4] : register(c6); // per-layer UV offset and emissive flag
float4 cEmission : register(c10); // authored night emission, scaled by current night fraction
float4 main(float2 uv : TEXCOORD0,float above : TEXCOORD1,float distance : TEXCOORD2,float diffuse : TEXCOORD3,float3 specular : TEXCOORD5,
            float2 alphaUv : TEXCOORD6,float3 painted : TEXCOORD7) : COLOR0
{
    clip(above);
    float horizon=cMaterial.x<0;
    float terrain=max(cMaterial.x,0);
    float2 coordinate=uv*lerp(1,8,terrain);
    coordinate.y+=terrain>.5?0:cMaterial.z;
    float4 colour=tex2D(Texture0,coordinate+terrain*cLayer[0].xy);
    float3 emitted=colour.rgb*(terrain*cLayer[0].z);
    float shadow=1,specularVisibility=0;
    float3 light=saturate(cAmbient.rgb+cDirect.rgb*diffuse);
    if (terrain>.5) {
        float4 alpha=tex2D(AlphaAtlas,alphaUv);
        float4 layer1=tex2D(Texture1,coordinate+cLayer[1].xy);
        float4 layer2=tex2D(Texture2,coordinate+cLayer[2].xy);
        float4 layer3=tex2D(Texture3,coordinate+cLayer[3].xy);
        float baseWeight=saturate(1-dot(alpha.rgb,1));
        colour=colour*baseWeight+layer1*alpha.r+layer2*alpha.g+layer3*alpha.b;
        colour.rgb*=painted;
        emitted=emitted*baseWeight+layer1.rgb*cLayer[1].z*alpha.r+
                layer2.rgb*cLayer[2].z*alpha.g+layer3.rgb*cLayer[3].z*alpha.b;
        shadow=1-cMaterial.z*alpha.a;
        specularVisibility=1-alpha.a;
    } else {
        light*=painted;
    }
    clip(colour.a-cMaterial.y);
    // Common material flags: unlit=1, unfogged=2; two-sidedness is device state.
    light=lerp(light,1,fmod(cMaterial.w,2));
    float fog=saturate((distance-cFogRange.x)*cFogRange.y)*(1-fmod(floor(cMaterial.w*.5),2));
    // WDL has heights, not materials. It is valid only beyond complete native
    // fog; never introduce a textureless shape into the clear detailed scene.
    clip(lerp(1,fog-.9999,horizon));
    fog=max(fog,horizon);
    float3 material=(colour.rgb*saturate(light+cEmission.rgb)+(1-light)*emitted)*shadow;
    material+=specular*colour.a*specularVisibility;
    float4 shaded=float4(lerp(material,cFogColour.rgb,fog),1);
    return shaded;
}
