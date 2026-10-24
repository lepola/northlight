// Generated texture-space normals, recorded in view space with depth and coverage.
float3 autoBumpNormal(float2 uv,float3 position,float3 normal,float distance,float3 row0,float3 row1,float3 row2) {
    if(BumpControl.x<=0)return normal;
    // The zero-fade path still normalizes the stored (FP16) normal.
    [branch]if(distance>=BumpControl.y)return normalize(normal);
    float4 detail=tex2Dlod(BumpMaterial,float4(uv,0,0));
    float inverseCover=1/max(detail.a,.001);
    float2 oct=detail.rg*inverseCover*2-1;
    float3 view=float3(oct,1-abs(oct.x)-abs(oct.y));
    float fold=saturate(-view.z);view.xy+=float2(view.x>=0?-fold:fold,view.y>=0?-fold:fold);
    view*=rsqrt(max(dot(view,view),1e-8));
    float3 mapped=view.x*row0+view.y*row1+view.z*row2;
    float capturedZ=detail.b*inverseCover;
    float valid=detail.a>.02&&abs(capturedZ-distance)<max(.025,distance*.0015);
    float fade=saturate((BumpControl.y-distance)/max(BumpControl.y*.25,1));
    return normalize(lerp(normal,mapped,BumpControl.x*valid*fade));
}
