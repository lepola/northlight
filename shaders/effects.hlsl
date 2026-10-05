// WoW 3.3.5 D3D9 screen-space lighting extension. Target: ps_3_0.
// This approximates local contact shading and nearby colour bounce; it does
// not replace the engine's lights, shadow maps or world-space illumination.
// s1 and s2 MUST use POINT filtering. All samplers MUST use CLAMP addressing.
// Ambient dimensions MUST be max(1, floor(full-resolution dimensions / 2)).
// Feed full-resolution texture UVs, including the D3D9 half-texel correction.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);
sampler2D Ambient : register(s2);
sampler2D WaterMask : register(s3);
float4 WaterInfo : register(c4); // coherent actual-liquid mask available

float4 ImageAndClip : register(c0); // 1/width, 1/height, near, far
float4 Lighting : register(c1);    // projection x/y scales, AO, indirect
float4 Options : register(c2);     // bloom, view-space radius, debug, reserved
float4 DepthRange : register(c3);  // viewport MinZ, inverse (MaxZ-MinZ), MaxZ, reserved

static const float SKY_DEPTH = 0.99999;

float ReadDepth(float2 uv)
{
    // WoW partitions hardware depth for world/sky passes (world can end at .94).
    // Undo the terrain viewport mapping before perspective reconstruction.
    float raw = tex2Dlod(Depth, float4(uv, 0, 0)).r;
    return saturate((raw - DepthRange.x) * DepthRange.y);
}

float LinearDepth(float d)
{
    float n = ImageAndClip.z;
    float f = ImageAndClip.w;
    return n * f / max(f - d * (f - n), 0.00001);
}

bool IsWater(float2 uv,float depth) {
    if(WaterInfo.x<.5)return false;
    float2 m=tex2Dlod(WaterMask,float4(uv,0,0)).rg;
    return m.x>ImageAndClip.z && m.x<=LinearDepth(depth)+max(.012,m.x*.0007) && m.y>.003;
}

float3 Position(float2 uv, float d)
{
    float z = LinearDepth(d);
    return float3((uv.x * 2.0 - 1.0) * z / Lighting.x,
                  (1.0 - uv.y * 2.0) * z / Lighting.y, z);
}

float3 SafeNormal(float3 n)
{
    float len2 = dot(n, n);
    // Degenerate geometry is treated as a camera-facing plane.
    n = len2 > 1e-14 ? n * rsqrt(max(len2, 1e-14)) : float3(0, 0, -1);
    return n.z > 0.0 ? -n : n;
}

// 0.3.189: snap a uv to the centre of an explicit full-resolution depth texel. The half-resolution
// AO pass samples at (i+.5)/(w/2), exactly the boundary between full-res texels 2i and 2i+1, where
// POINT sampling depends on GPU interpolation rounding and can flip from row to row.
// +0.25: in the half-res pass uv*size is about 2i+1 and picks texel 2i+1 unambiguously (the nominal
// texel of depthUV() in world_effects.hlsl); in a full-res pass uv*size is j+.5 and picks j.
float2 DepthTexelUV(float2 uv)
{
    // (floor(t) + .5) / size with t = uv*size + .25, written as uv + (.75 - frac(t)) / size (slot
    // budget). The centre is kept one texel inside every edge, so CLAMP never folds SurfaceNormal's
    // +-1 neighbour onto it (a zero tangent on the last column/row would give a camera-facing normal).
    uv = mad(0.75 - frac(mad(uv, 1.0 / ImageAndClip.xy, 0.25)), ImageAndClip.xy, uv);
    return clamp(uv, 1.5 * ImageAndClip.xy, 1.0 - 1.5 * ImageAndClip.xy);
}

// uv must already be a full-resolution texel centre (DepthTexelUV): the neighbours are exactly one texel away.
float3 SurfaceNormal(float2 uv, float3 p)
{
    float2 dx = float2(ImageAndClip.x, 0);
    float2 dy = float2(0, ImageAndClip.y);
    float3 l = Position(uv - dx, ReadDepth(uv - dx));
    float3 r = Position(uv + dx, ReadDepth(uv + dx));
    float3 u = Position(uv - dy, ReadDepth(uv - dy));
    float3 b = Position(uv + dy, ReadDepth(uv + dy));
    // Select the same-surface neighbour at silhouettes instead of taking a
    // derivative across the foreground/background depth discontinuity.
    float3 tx = abs(r.z - p.z) < abs(p.z - l.z) ? r - p : p - l;
    float3 ty = abs(b.z - p.z) < abs(p.z - u.z) ? b - p : p - u;
    // 0.3.189: a zero-length tangent (neighbour resolved to the centre texel) would fall back to a
    // camera-facing normal and turn ground taps into occluders (dark AO row); span both sides instead.
    // Vertical only (slot budget): the rows are the half-res pass's ambiguous axis on flat ground.
    ty = dot(ty, ty) < 1e-14 ? b - u : ty;
    return SafeNormal(cross(tx, ty));
}

float4 AOImpl(float2 uv, bool colorBounce)
{
    // 0.3.189: the centre, its normal neighbours and the noise rotation use one explicit depth texel;
    // kernel taps (suv) stay unsnapped.
    uv = DepthTexelUV(uv);
    float d = ReadDepth(uv);
    if (d >= SKY_DEPTH || IsWater(uv,d))
        return float4(0, 0, 0, 1);

    float3 p = Position(uv, d);
    float3 normal = SurfaceNormal(uv, p);
    float radius = max(Options.y, 0.05);
    float2 uvRadius = 0.5 * radius * Lighting.xy / max(p.z, ImageAndClip.z);
    // Bound the screen footprint near the camera and leave a nonzero contact
    // footprint at long distances. World-space rejection still limits reach.
    uvRadius = clamp(uvRadius, ImageAndClip.xy * 2.0, float2(0.05, 0.05));
    // Rotate the fixed kernel per pixel (interleaved gradient noise, stable
    // between frames). An unrotated kernel copies every compact occluder
    // (hands, shoulders) onto the ground at each tap offset as faint ghost
    // shadows; rotation turns those copies into fine noise the composite's
    // depth/normal filter averages away.
    float2 pixel = uv / ImageAndClip.xy;
    float2 rot = frac(float2(52.9829189, 37.4136) * frac(dot(pixel, float2(0.06711056, 0.00583715)))) * 2.0 - 1.0;
    rot *= rsqrt(max(dot(rot, rot), 1e-4));
    float2 axisX = rot * uvRadius, axisY = float2(-rot.y, rot.x) * uvRadius;

    static const float2 kernel[8] = {
        float2( 0.3234,  0.1339), float2(-0.2488,  0.6005),
        float2(-0.8315, -0.3444), float2( 0.3827, -0.9239),
        float2( 0.8315, -0.3444), float2( 0.2488,  0.6005),
        float2(-0.3234,  0.1339), float2(-0.3827, -0.9239)
    };
    float obscurance = 0.0;
    float3 bounce = 0.0;

    [loop] for (int i = 0; i < 8; ++i)
    {
        float2 suv = uv + kernel[i].x * axisX + kernel[i].y * axisY;
        float sd = ReadDepth(suv);
        float3 q = Position(suv, sd);
        float3 v = q - p;
        float dist2 = dot(v, v);
        float dist = sqrt(max(dist2, 1e-12));
        // The angular bias rejects nearly coplanar geometry and avoids
        // treating flat surfaces as their own occluders.
        float horizon = saturate((dot(normal, v / dist) - 0.08) / 0.92);
        // Quadratic falloff: an occluder near the radius edge contributes
        // little, so a raised hand does not darken a full tap's worth.
        float range = saturate(1.0 - dist / radius); range *= range;
        float valid = (sd < SKY_DEPTH && dist2 > 1e-10 &&
                       suv.x > 0 && suv.y > 0 && suv.x < 1 && suv.y < 1) ? 1.0 : 0.0;
        float weight = horizon * range * valid;
        obscurance += weight;
        // Only visible neighbouring surfaces contribute; missing off-screen
        // geometry cannot produce bounce in this screen-space approximation.
        if (colorBounce)
            bounce += tex2Dlod(Scene, float4(suv, 0, 0)).rgb * weight;
    }
    return float4(saturate(bounce * 0.125),
                  saturate(1.0 - Lighting.z * obscurance * 0.25));
}

float4 AO(float2 uv : TEXCOORD0) : COLOR0 { return AOImpl(uv, true); }

float3 Bright(float3 c)
{
    float luminance = dot(c, float3(0.2126, 0.7152, 0.0722));
    return c * saturate((luminance - 0.72) / 0.28);
}

// World GI already supplies the colour bounce: this entry drops the unused colour fetches,
// retaining the identical AO alpha. 0.3.174: rgb carries Composite's bloom, pre-weighted
// (.125 x Options.x), for every pixel including sky and water, so the world composite can
// fold the AO composite; Composite itself ignores it (Lighting.w is 0 with a ready world).
// Bright() as one weight: saturate((luminance-.72)/.28) written as a single mad (slot budget).
float3 BrightTap(float3 c, float3 sum)
{
    return mad(c, saturate(mad(dot(c, float3(0.2126, 0.7152, 0.0722)), 1.0 / 0.28, -0.72 / 0.28)), sum);
}

float4 AOContactBloom(float2 uv : TEXCOORD0) : COLOR0
{
    // Composite's bloom taps: the centre (x4) and 4 px along each axis. Scene has one level,
    // so tex2D (texld, before any flow control) reads what tex2Dlod does, in fewer slots.
    float4 b = ImageAndClip.xyxy * float4(4, 0, 0, 4);
    float3 bloom = BrightTap(tex2D(Scene, uv).rgb, 0) * 4.0;
    bloom = BrightTap(tex2D(Scene, uv + b.xy).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv - b.xy).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv + b.zw).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv - b.zw).rgb, bloom);
    return float4(bloom * (0.125 * Options.x), AOImpl(uv, false).a);
}

float3 NeighbourNormal(float2 uv, float3 p)
{
    float2 rightUV = uv + float2(ImageAndClip.x, 0);
    float2 downUV = uv + float2(0, ImageAndClip.y);
    float3 right = Position(rightUV, ReadDepth(rightUV));
    float3 down = Position(downUV, ReadDepth(downUV));
    return SafeNormal(cross(right - p, down - p));
}

float4 Composite(float2 uv : TEXCOORD0) : COLOR0
{
    float4 original = tex2Dlod(Scene, float4(uv, 0, 0));
    float depth = ReadDepth(uv);
    // Native water already contains the refracted/attenuated bed. Never apply
    // bed-derived AO or diffuse correction to this translucent surface again.
    float3 p = Position(uv, depth);
    float4 ambient = float4(0, 0, 0, 1);
    // The original path overwrote the filter result with this exact identity
    // on sky/water. Keep bloom and debug output below, skip only unused work.
    if (depth < SKY_DEPTH && !IsWater(uv,depth)) {
    float3 n = NeighbourNormal(uv, p);
    ambient = float4(0, 0, 0, 0);
    float sumWeight = 0.0;
    float2 ambientSize = max(floor(0.5 / ImageAndClip.xy + 0.01), float2(1, 1));
    float2 ambientTexel = 1.0 / ambientSize;
    float2 ambientCenter = (floor(uv * ambientSize) + 0.5) * ambientTexel;
    static const float2 taps[5] = {
        float2(0, 0), float2(-1, 0), float2(1, 0),
        float2(0, -1), float2(0, 1)
    };
    // A cross bilateral upsample uses actual scene depth and normal, so
    // contact shading does not spread freely across silhouettes or creases.
    [loop] for (int j = 0; j < 5; ++j)
    {
        float2 auv = ambientCenter + taps[j] * ambientTexel;
        float ad = ReadDepth(auv);
        float3 q = Position(auv, ad);
        // Explicit neighbours remain valid when several full-resolution
        // pixels map onto the same snapped ambient texel.
        float3 qn = NeighbourNormal(auv, q);
        float planeDistance = abs(dot(q - p, n));
        float depthTolerance = max(0.025, min(Options.y * 0.1, p.z * 0.002));
        float normalAgreement = saturate(dot(n, qn));
        float weight = exp2(-planeDistance / depthTolerance * 2.0);
        weight *= normalAgreement * normalAgreement;
        weight *= j == 0 ? 2.0 : 1.0;
        weight *= (ad < SKY_DEPTH) == (depth < SKY_DEPTH) ? 1.0 : 0.0;
        ambient += tex2Dlod(Ambient, float4(auv, 0, 0)) * weight;
        sumWeight += weight;
    }
    ambient = sumWeight > 0.00001 ? ambient / max(sumWeight, 0.00001)
                                 : float4(0, 0, 0, 1);
    }

    if (Options.z > 1.5)
    {
        float viewDepth = saturate(log2(1.0 + p.z) / log2(1.0 + ImageAndClip.w));
        return float4(viewDepth.xxx, original.a);
    }
    if (Options.z > 0.5)
        return float4(ambient.aaa, original.a);

    // A small screen-space glow, bounded to bright neighbouring pixels.
    // This is LDR bloom: it cannot recover clipped HDR light energy.
    float2 b = ImageAndClip.xy * 4.0;
    float3 bloom = Bright(original.rgb) * 4.0;
    bloom += Bright(tex2Dlod(Scene, float4(uv + float2( b.x, 0), 0, 0)).rgb);
    bloom += Bright(tex2Dlod(Scene, float4(uv + float2(-b.x, 0), 0, 0)).rgb);
    bloom += Bright(tex2Dlod(Scene, float4(uv + float2(0,  b.y), 0, 0)).rgb);
    bloom += Bright(tex2Dlod(Scene, float4(uv + float2(0, -b.y), 0, 0)).rgb);
    bloom *= 0.125;
    float3 lit = original.rgb * ambient.a;
    lit += ambient.rgb * Lighting.w * saturate(original.rgb + 0.1);
    lit += bloom * Options.x * (1.0 - saturate(lit));
    return float4(saturate(lit), original.a);
}
