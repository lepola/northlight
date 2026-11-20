// The geometric surface beneath the waves. A one-pixel derivative amplifies
// depth quantization into alternating tilts, visible as horizontal glint bars.
// Use a wider span and the nearer side of each axis to avoid crossing a bank
// or foreground edge. This changes the base plane, never the wave normals.
float3 LiquidGeometryNormal(float2 pixel, float depth)
{
    float2 first = cViewportPx.xy;
    float2 last = first + cViewportPx.zw - 1.0;
    float2 right = min(pixel + float2(8,0), last);
    float2 left = max(pixel - float2(8,0), first);
    float2 down = min(pixel + float2(0,8), last);
    float2 up = max(pixel - float2(0,8), first);
    float dr = SampleRawDepth(sDepth,right), dl = SampleRawDepth(sDepth,left);
    float dd = SampleRawDepth(sDepth,down), du = SampleRawDepth(sDepth,up);
    // A nearby character can be closer in depth than the next water sample on
    // a slope. Coverage, not just depth proximity, keeps it out of the plane.
    // Keep the full baseline at viewport edges; a shortened one-pixel span restores the noise.
    bool rightWet = pixel.x+8<=last.x && dr<SampleRawDepth(sOpaqueDepth,right);
    bool leftWet = pixel.x-8>=first.x && dl<SampleRawDepth(sOpaqueDepth,left);
    bool downWet = pixel.y+8<=last.y && dd<SampleRawDepth(sOpaqueDepth,down);
    bool upWet = pixel.y-8>=first.y && du<SampleRawDepth(sOpaqueDepth,up);
    bool useRight = rightWet && (!leftWet || abs(dr-depth)<=abs(depth-dl));
    bool useDown = downWet && (!upWet || abs(dd-depth)<=abs(depth-du));
    float2 delta = float2(useRight ? right.x-pixel.x : left.x-pixel.x,
                         useDown ? down.y-pixel.y : up.y-pixel.y);
    float2 neighbour = float2(useRight ? dr : dl, useDown ? dd : du);
    // Recover the view-space plane directly from affine raw-depth gradients.
    // Converting neighbouring samples to view positions first adds rounding and
    // needs many live intermediates in the SM3 water shader.
    float2 gradient = (neighbour-depth)*sign(delta)/max(abs(delta),1.0);
    float2 ray = ViewPosition(pixel,1.0).xy;
    float3 plane = float3(gradient.x*cViewportPx.z*cViewToNdc.x*.5,
                        -gradient.y*cViewportPx.w*cViewToNdc.y*.5,depth-cDepthMapping.x);
    plane.z -= dot(plane.xy,ray);
    float3 crossed = plane.x*cViewToWorld0.xyz + plane.y*cViewToWorld1.xyz + plane.z*cViewToWorld2.xyz;
    crossed *= crossed.z < 0 ? -1 : 1;
    float squared = dot(crossed,crossed);
    float3 normal = crossed*rsqrt(max(squared,1.0e-20));

    // If both measured differences agree with a horizontal plane within
    // depth/storage rounding, there is no reliable tilt. Keep flat water flat.
    // This uses camera-relative rays; a large world origin cannot affect it.
    float facing = dot(float3(ray,1),float3(cViewToWorld0.z,cViewToWorld1.z,cViewToWorld2.z));
    float safeFacing = abs(facing)>1.0e-6 ? facing : 1.0;
    float2 flatGradient = ((depth-cDepthMapping.x)/safeFacing) *
        float2(2*cViewToWorld0.z/(cViewportPx.z*cViewToNdc.x),
              -2*cViewToWorld1.z/(cViewportPx.w*cViewToNdc.y));
    float2 error = abs((neighbour-depth)-flatGradient*delta);
    const float depthRounding = 2.0e-7; // 24-bit depth plus the host's distance/depth conversion
    bool flat = abs(facing)>1.0e-6 && max(error.x,error.y)<=depthRounding;
    if (flat || !(rightWet||leftWet) || !(downWet||upWet) || squared<1.0e-20 || normal.z<0.25)
        normal=float3(0,0,1);
    return normal;
}
