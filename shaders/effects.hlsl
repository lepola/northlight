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
// AOBlur only: the raw AO on s0 (LINEAR, CLAMP: Scene's sampler state; AOBlur never reads Scene) for its bilinear early-out.
sampler2D AmbientLinear : register(s0);
float4 WaterInfo : register(c4); // coherent actual-liquid mask available
float4 LegacyFog : register(c5); // validated signed fog slope folded to positive view Z; identity when unknown
float4 LegacyFogColor : register(c6); // proven native RGB, preserved by AO

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

// 0.3.190: snap a uv to the centre of an explicit full-resolution depth texel. The half-resolution
// AO pass samples at (i+.5)/(w/2), exactly the boundary between full-res texels 2i and 2i+1, where
// POINT sampling depends on GPU interpolation rounding and can flip from row to row.
// +0.25: in the half-res pass uv*size is about 2i+1 and picks texel 2i+1 unambiguously (the nominal
// texel of depthUV() in world_effects.hlsl); in a full-res pass uv*size is j+.5 and picks j.
// Odd sizes: w/2 truncates, so uv*size drifts from 2i+1; where its fraction passes .75 (the last
// quarter of columns/rows) the snap picks 2i+2 while depthUV() keeps 2i+1, and one row/column there
// is a near-tie. Both are single neighbouring texels of the same surface, not a zero tangent.
// size = 1/ImageAndClip.xy (the caller's, computed once).
float2 DepthTexelUV(float2 uv, float2 size)
{
    // (floor(t) + .5) / size with t = uv*size + .25, written as uv + (.75 - frac(t)) / size (slot
    // budget). The centre is kept one texel inside every edge, so CLAMP never folds SurfaceNormal's
    // +-1 neighbour onto it (a tangent in the camera plane on the last column/row would give a
    // camera-facing normal).
    uv = mad(0.75 - frac(mad(uv, size, 0.25)), ImageAndClip.xy, uv);
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
    return SafeNormal(cross(tx, ty));
}

float4 AOImpl(float2 uv, bool colorBounce)
{
    // 0.3.190: the centre, its normal neighbours and the noise rotation use one explicit depth texel;
    // kernel taps (suv) stay unsnapped.
    float2 size = 1.0 / ImageAndClip.xy;
    uv = DepthTexelUV(uv, size);
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
    // shadows; rotation turns those copies into fine noise AOBlur
    // (depth/normal filter) averages away.
    // 0.3.198 (stripes): IGN is built for integer pixel steps, and this pass is half resolution: uv*size is
    // the snapped FULL-res texel (2i+1.5, a step of 2 per AO texel), which advanced the phase by only .11 (x)
    // and .02 (hash y) per texel along a row but by the golden ratio .62 per row: the rotation, and so the
    // occlusion, was constant along each row and different on every row (screen-fixed horizontal bands wherever
    // the AO occludes). Half the coefficients = the AO texel index (+ a constant .75): a step of 1 in each axis.
    float2 pixel = uv * size;
    float2 rot = frac(float2(52.9829189, 37.4136) * frac(dot(pixel, float2(0.03355528, 0.002918575)))) * 2.0 - 1.0;
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

// Composite's bloom taps: the centre (x4) and 4 px along each axis. Scene has one level,
// so tex2D (texld, before any flow control) reads what tex2Dlod does, in fewer slots.
float3 ContactBloomRgb(float2 uv)
{
    float4 b = ImageAndClip.xyxy * float4(4, 0, 0, 4);
    float3 bloom = BrightTap(tex2D(Scene, uv).rgb, 0) * 4.0;
    bloom = BrightTap(tex2D(Scene, uv + b.xy).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv - b.xy).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv + b.zw).rgb, bloom);
    bloom = BrightTap(tex2D(Scene, uv - b.zw).rgb, bloom);
    return bloom * (0.125 * Options.x);
}

float4 AOContactBloom(float2 uv : TEXCOORD0) : COLOR0
{
    return float4(ContactBloomRgb(uv), AOImpl(uv, false).a);
}

// D3D9 adaptations: AMD AOFX valley HDAO, NVIDIA HBAO+ coarse AO,
// and Jimenez et al. GTAO's cosine-weighted horizon integral. See AO-METHODS.md
// and licenses/ao-methods.txt. Shared half-res targets/filter, no SDK dependency.
float2 AORotation(float2 uv)
{
    float noise = frac(dot(floor(uv / ImageAndClip.xy * .5), float2(.06711056, .00583715)));
    float2 rotation = frac(float2(52.9829189, 37.4136) * noise) * 2 - 1;
    return rotation * rsqrt(max(dot(rotation, rotation), 1e-4));
}

float AOAcos(float x)
{
    float a = abs(x);
    float angle = (((-.0187293 * a + .0742610) * a - .2121144) * a + 1.5707288) * sqrt(max(1 - a, 0));
    return x < 0 ? 3.14159265359 - angle : angle;
}

float4 AOSample(float2 uv)
{
    bool inside = min(min(uv.x, uv.y), min(1 - uv.x, 1 - uv.y)) > 0;
    uv = (floor(uv / ImageAndClip.xy) + .5) * ImageAndClip.xy;
    float d = ReadDepth(uv);
    float valid = inside && d < SKY_DEPTH ? 1 : 0;
    return float4(Position(uv, d), valid);
}

float NewAO(float2 uv, int method)
{
    uv = DepthTexelUV(uv, 1 / ImageAndClip.xy);
    float d = ReadDepth(uv);
    if (d >= SKY_DEPTH || IsWater(uv, d)) return 1;
    float3 p = Position(uv, d), normal = SurfaceNormal(uv, p);
    float radius = Options.y; // AORadius is validated to 0.1..10 yards.
    // Cap the screen radius with one common factor: clamping x and y separately would bend the
    // sample direction away from the view-space direction GTAO builds its slice plane from.
    float2 uvRadius = .5 * radius * Lighting.xy / p.z;
    uvRadius *= min(1, .25 / max(uvRadius.x, uvRadius.y));
    // SurfaceNormal forces n.z <= 0, which is wrong for visible faces pointing forward in view Z
    // (camera pitched up, terrain sloping away). Orient toward the eye (receiver-to-eye vector).
    normal *= dot(normal, p) > 0 ? -1 : 1;
    float2 rotation = AORotation(uv);
    float obscurance = 0;

    if (method == 1)
    {
        // Paired samples detect concave valleys. Opposite coplanar directions
        // have dot=-1, so a flat/sloping plane contributes no valley occlusion.
        float centerDistance = length(p);
        [loop] for (int ring = 0; ring < 4; ++ring)
        [loop] for (int sector = 0; sector < 4; ++sector)
        {
            float angle = sector * .78539816339;
            float2 v = float2(cos(angle), sin(angle));
            float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
            float2 offset = direction * uvRadius * ((ring + 1) * .2);
            float4 a = AOSample(uv + offset), b = AOSample(uv - offset);
            float2 delta = centerDistance - float2(length(a.xyz), length(b.xyz));
            float2 accept = saturate((radius - delta) / (radius * .8));
            accept *= float2(delta.x > radius * .01, delta.y > radius * .01);
            float3 va = a.xyz - p, vb = b.xyz - p;
            float angleWeight = saturate(dot(va, vb) * rsqrt(max(dot(va, va) * dot(vb, vb), 1e-12)) + .9) * 1.2;
            obscurance += accept.x * accept.y * angleWeight * angleWeight * angleWeight * a.w * b.w;
        }
        return saturate(1 - Lighting.z * obscurance * (1.0 / 16.0));
    }
    if (method == 2)
    {
        // HBAO+ coarse-kernel adaptation: eight directions, four radial steps,
        // normal bias and squared-distance falloff. No DX11 deinterleaving.
        [loop] for (int directionIndex = 0; directionIndex < 8; ++directionIndex)
        {
            float angle = directionIndex * .78539816339;
            float2 v = float2(cos(angle), sin(angle));
            float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
            [loop] for (int step = 0; step < 4; ++step) {
            float4 q = AOSample(uv + direction * uvRadius * ((step + 1) * .2));
            float3 delta = q.xyz - p;
            [branch] if (any(delta != 0)) {
            float distance2 = max(dot(delta, delta), 1e-12);
            float elevation = saturate(dot(normal, delta) * rsqrt(distance2) - .1);
            obscurance += elevation * saturate(1 - distance2 / (radius * radius)) * q.w;
            }
            }
        }
        return saturate(1 - Lighting.z * obscurance * (2.0 / (32.0 * .9)));
    }

    float3 view = -p * rsqrt(max(dot(p, p), 1e-12));
    float openVisibility = 0;
    [loop] for (int slice = 0; slice < 4; ++slice)
    {
        float angle = slice * .78539816339;
        float2 v = float2(cos(angle), sin(angle));
        float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
        float3 sliceDirection = float3(direction.x, -direction.y, 0);
        float3 tangent = sliceDirection - view * dot(sliceDirection, view);
        float3 axis = cross(tangent, view);
        axis *= rsqrt(max(dot(axis, axis), 1e-12));
        float3 projected = normal - axis * dot(normal, axis);
        float inverseProjectedLength = rsqrt(max(dot(projected, projected), 1e-12));
        float projectedLength = 1/inverseProjectedLength;
        float cosine = saturate(dot(projected, view) * inverseProjectedLength);
        float sine = (dot(tangent, projected) >= 0 ? 1 : -1) * sqrt(max(1 - cosine * cosine, 0));
        float2 low = float2(-sine, sine);
        float2 horizon = low, solidHorizon = low, coverage = 0;
        [loop] for (int step = 0; step < 4; ++step)
        {
            float sampleRadius = (step + 1) * .2;
            float2 offset = direction * uvRadius * sampleRadius * sampleRadius;
            // Sub-texel offsets had zero weight: skip their depth reads and horizon work.
            [branch] if (any(abs(offset) > .51 * ImageAndClip.xy))
            {
            float4 a = AOSample(uv + offset), b = AOSample(uv - offset);
            float3 da = a.xyz - p, db = b.xyz - p;
            float2 inverseDistance = rsqrt(max(float2(dot(da, da), dot(db, db)), 1e-12));
            float2 distance = 1/inverseDistance;
            float2 solidFalloff = saturate((radius - distance) / (radius * .5));
            // A single depth layer overstates thin foreground silhouettes.
            // Stronger depth-separation falloff handles isolated hits; multiple
            // occluder samples retain the original solid-surface horizon.
            float2 thinDistance = sqrt(float2(dot(da, da), dot(db, db)) + 3 * float2(da.z * da.z, db.z * db.z));
            float2 thinFalloff = saturate((radius - thinDistance) / (radius * .5));
            float2 weight = float2(a.w, b.w);
            weight *= float2(distance.x > .00001, distance.y > .00001);
            // Match the SSAO coplanar tolerance for grazing D24 derivatives.
            weight *= float2(dot(normal, da) > .08 * distance.x, dot(normal, db) > .08 * distance.y);
            float2 candidate = float2(dot(da, view), dot(db, view)) * inverseDistance;
            float2 solidCandidate = lerp(low, candidate, weight * solidFalloff);
            coverage += float2(solidCandidate.x > low.x + .00001, solidCandidate.y > low.y + .00001);
            solidHorizon = max(solidHorizon, solidCandidate);
            horizon = max(horizon, lerp(low, candidate, weight * thinFalloff));
            }
        }
        horizon = lerp(horizon, solidHorizon, saturate(coverage - 1));
        float open = projectedLength * (cosine + AOAcos(cosine) * abs(sine));
        [branch] if (max(horizon.x-low.x,horizon.y-low.y)>.00001)
        {
        float2 ch = clamp(horizon.yx, -1, 1);
        float2 h = float2(-AOAcos(ch.x), AOAcos(ch.y));
        float2 sh = float2(-1, 1) * sqrt(max(1 - ch * ch, 0));
        // cos(2*h-n) from the already known horizon cosine and normal sine.
        float2 arc = (cosine + 2 * h * sine - ((2 * ch * ch - 1) * cosine + 2 * sh * ch * sine)) * .25;
        // Normalize against this slice's open hemisphere, so perspective and
        // tilted receiver normals do not turn a flat plane into an occluder.
        float loss = max(0, open - projectedLength * (arc.x + arc.y));
        obscurance += loss;
        }
        openVisibility += open;
    }
    return saturate(1 - Lighting.z * obscurance / max(openVisibility, .00001));
}

float NewAOQuality(float2 uv, int method, int quality)
{
    uv = DepthTexelUV(uv, 1 / ImageAndClip.xy);
    float d = ReadDepth(uv);
    if (d >= SKY_DEPTH || IsWater(uv, d)) return 1;
    float3 p = Position(uv, d), normal = SurfaceNormal(uv, p);
    float radius = Options.y; // AORadius is validated to 0.1..10 yards.
    // Cap the screen radius with one common factor: clamping x and y separately would bend the
    // sample direction away from the view-space direction GTAO builds its slice plane from.
    float2 uvRadius = .5 * radius * Lighting.xy / p.z;
    uvRadius *= min(1, .25 / max(uvRadius.x, uvRadius.y));
    // SurfaceNormal forces n.z <= 0, which is wrong for visible faces pointing forward in view Z
    // (camera pitched up, terrain sloping away). Orient toward the eye (receiver-to-eye vector).
    normal *= dot(normal, p) > 0 ? -1 : 1;
    float2 rotation = AORotation(uv);
    float obscurance = 0;

    if (method == 1)
    {
        // Paired samples detect concave valleys. Opposite coplanar directions
        // have dot=-1, so a flat/sloping plane contributes no valley occlusion.
        float centerDistance = length(p);
        const int pairs = quality == 0 ? 16 : 8;
        [loop] for (int ring = 0; ring < 4; ++ring)
        [loop] for (int sector = 0; sector < (quality == 0 ? 4 : 2); ++sector)
        {
            float angle = quality == 0 ? sector * .78539816339 :
                sector * 1.57079632679 + frac(ring * .5) * 1.57079632679;
            float2 v = float2(cos(angle), sin(angle));
            float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
            // Two directions per ring retain all four radial bands.
            float sampleRadius = (ring + 1) * .2;
            float2 offset = direction * uvRadius * sampleRadius;
            float4 a = AOSample(uv + offset), b = AOSample(uv - offset);
            float2 delta = centerDistance - float2(length(a.xyz), length(b.xyz));
            float2 accept = saturate((radius - delta) / (radius * .8));
            accept *= float2(delta.x > radius * .01, delta.y > radius * .01);
            float3 va = a.xyz - p, vb = b.xyz - p;
            float angleWeight = saturate(dot(va, vb) * rsqrt(max(dot(va, va) * dot(vb, vb), 1e-12)) + .9) * 1.2;
            obscurance += accept.x * accept.y * angleWeight * angleWeight * angleWeight * a.w * b.w;
        }
        return saturate(1 - Lighting.z * obscurance * (1.0 / pairs));
    }
    if (method == 2)
    {
        // HBAO+ coarse-kernel adaptation: eight directions, four radial steps,
        // normal bias and squared-distance falloff. No DX11 deinterleaving.
        const int directions = quality == 0 ? 8 : quality == 1 ? 6 : 4;
        const int steps = quality == 1 ? 3 : 4;
        const int taps = directions * steps;
        [loop] for (int directionIndex = 0; directionIndex < directions; ++directionIndex)
        {
            float angle = directionIndex * (quality == 0 ? .78539816339 : 6.28318530718 / directions);
            float2 v = float2(cos(angle), sin(angle));
            float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
            [loop] for (int step = 0; step < steps; ++step) {
            float4 q = AOSample(uv + direction * uvRadius * ((step + 1) * (quality == 1 ? .25 : .2)));
            float3 delta = q.xyz - p;
            [branch] if (any(delta != 0)) {
            float distance2 = max(dot(delta, delta), 1e-12);
            float elevation = saturate(dot(normal, delta) * rsqrt(distance2) - .1);
            obscurance += elevation * saturate(1 - distance2 / (radius * radius)) * q.w;
            }
            }
        }
        return saturate(1 - Lighting.z * obscurance * (2.0 / (taps * .9)));
    }

    float3 view = -p * rsqrt(max(dot(p, p), 1e-12));
    float openVisibility = 0;
    const int slices = quality == 0 ? 4 : quality == 1 ? 3 : 2;
    [loop] for (int slice = 0; slice < slices; ++slice)
    {
        float angle = slice * (quality == 0 ? .78539816339 : 3.14159265359 / slices);
        float2 v = float2(cos(angle), sin(angle));
        float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
        float3 sliceDirection = float3(direction.x, -direction.y, 0);
        float3 tangent = sliceDirection - view * dot(sliceDirection, view);
        float3 axis = cross(tangent, view);
        axis *= rsqrt(max(dot(axis, axis), 1e-12));
        float3 projected = normal - axis * dot(normal, axis);
        float inverseProjectedLength = rsqrt(max(dot(projected, projected), 1e-12));
        float projectedLength = 1/inverseProjectedLength;
        float cosine = saturate(dot(projected, view) * inverseProjectedLength);
        float sine = (dot(tangent, projected) >= 0 ? 1 : -1) * sqrt(max(1 - cosine * cosine, 0));
        float2 low = float2(-sine, sine);
        float2 horizon = low;
        [loop] for (int step = 0; step < 4; ++step)
        {
            float sampleRadius = (step + 1) * .2;
            float2 offset = direction * uvRadius * sampleRadius * sampleRadius;
            float4 a = AOSample(uv + offset), b = AOSample(uv - offset);
            float3 da = a.xyz - p, db = b.xyz - p;
            float2 inverseDistance = rsqrt(max(float2(dot(da, da), dot(db, db)), 1e-12));
            float2 distance = 1/inverseDistance;
            float2 weight = saturate((radius - distance) / (radius * .5)) * float2(a.w, b.w);
            weight *= float2(distance.x > .00001, distance.y > .00001);
            // Both inner taps can resolve to the receiver texel at distance.
            // Reject by footprint, not a nearly-zero reconstructed 3D delta.
            weight *= any(abs(offset) > .51 * ImageAndClip.xy) ? 1 : 0;
            // Match the SSAO coplanar tolerance for grazing D24 derivatives.
            weight *= float2(dot(normal, da) > .08 * distance.x, dot(normal, db) > .08 * distance.y);
            float2 candidate = float2(dot(da, view), dot(db, view)) * inverseDistance;
            horizon = max(horizon, lerp(low, candidate, weight));
        }
        float open = projectedLength * (cosine + AOAcos(cosine) * abs(sine));
        [branch] if (max(horizon.x-low.x,horizon.y-low.y)>.00001)
        {
        float2 ch = clamp(horizon.yx, -1, 1);
        float2 h = float2(-AOAcos(ch.x), AOAcos(ch.y));
        float2 sh = float2(-1, 1) * sqrt(max(1 - ch * ch, 0));
        // cos(2*h-n) from the already known horizon cosine and normal sine.
        float2 arc = (cosine + 2 * h * sine - ((2 * ch * ch - 1) * cosine + 2 * sh * ch * sine)) * .25;
        // Normalize against this slice's open hemisphere, so perspective and
        // tilted receiver normals do not turn a flat plane into an occluder.
        float loss = max(0, open - projectedLength * (arc.x + arc.y));
        obscurance += loss;
        }
        openVisibility += open;
    }
    return saturate(1 - Lighting.z * obscurance / max(openVisibility, .00001));
}

float4 HDAOContactBloom(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAO(uv, 1)); }
float4 HBAOPlusContactBloom(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAO(uv, 2)); }
float4 GTAOContactBloom(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAO(uv, 3)); }
float4 HDAOContactBloomMedium(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 1, 1)); }
float4 HBAOPlusContactBloomMedium(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 2, 1)); }
float4 GTAOContactBloomMedium(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 3, 1)); }
float4 HDAOContactBloomLow(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 1, 2)); }
float4 HBAOPlusContactBloomLow(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 2, 2)); }
float4 GTAOContactBloomLow(float2 uv : TEXCOORD0) : COLOR0 { return float4(ContactBloomRgb(uv), NewAOQuality(uv, 3, 2)); }

// 0.3.201 (task 18) ContactAO=0 with a ready world: the same bloom with AO 1 and no depth work; the AO pass and AOBlur do not run.
float4 ContactBloom(float2 uv : TEXCOORD0) : COLOR0
{
    return float4(ContactBloomRgb(uv), 1);
}

float3 NeighbourNormal(float2 uv, float3 p)
{
    float2 rightUV = uv + float2(ImageAndClip.x, 0);
    float2 downUV = uv + float2(0, ImageAndClip.y);
    float3 right = Position(rightUV, ReadDepth(rightUV));
    float3 down = Position(downUV, ReadDepth(downUV));
    return SafeNormal(cross(right - p, down - p));
}

// 0.3.201 (task 18): the IGN-rotated 8-tap AO left a regular dark diamond/fishnet lattice in occluded
// creases (character skin) because nothing filtered it: WorldComposite only bilinear-upsampled it. This
// half-resolution pass averages a symmetric 5x5 AO-texel neighbourhood (.5,1,1,1,.5 per axis, covering
// IGN's period) on the centre's plane and normal, so silhouettes stay sharp. Same bindings as the AO pass
// (s2 = the RAW AO texture, POINT); bloom rgb passes through untouched, sky and water return the raw texel.
// Cost: (1) unoccluded early-out: AO is saturate(1 - ...), so an unoccluded texel is exactly 1.0 (and
// A16B16G16R16F stores 1.0 exactly); when the whole 5x5 is (nearly) 1 the raw texel is returned before any depth
// work (open ground, sky, most of the screen). Task 43: 9 bilinear reads of the raw AO (s0 LINEAR) instead of 25 POINT
// reads: per axis a texel corner at -1.5 (texels -2,-1), at +.5 (0,+1) and the centre of +2, so the 9 footprints tile
// exactly the 25 taps (the taps' own step 2/w; an odd width shifts every footprint by under 2/w texel, each needed texel
// keeps a weight of about .5 or 1, and 8-bit subtexel filtering rounds that shift away). Each read is the
// mean of 1, 2 or 4 texels, all <= 1: every read >= .9995 (the old per-texel threshold, so every 5x5 the old 25-tap min
// accepted still passes) means every texel >= 1 - 4 * .0005 = .998 (>= .99707 if the GPU filters in fp16 and rounds the
// mean up to 1 - 2^-11), so the weighted 5x5 mean lies within .003 (under 1/255) of the returned raw texel. (2) the silhouette guard is one
// extra depth read per tap instead of NeighbourNormal's two:
// the tap's tangent along the screen axis the centre normal leans towards (x if |n.x| > |n.y|) must lie in the
// centre's plane, weight *= saturate(1 - 2 * sin^2) with sin = dot(r - q, n) / |r - q|. At a grazing limb the
// plane distance alone lets far-wall taps through (their points sit near the limb's tangent plane), but the
// wall's own tangent along the limb normal's axis is steeply off that plane, so its weight goes to zero.
float3 PositionInv(float2 uv, float d, float2 invFocal)
{
    float z = LinearDepth(d);
    return float3((uv.x * 2.0 - 1.0) * z * invFocal.x, (1.0 - uv.y * 2.0) * z * invFocal.y, z);
}

float4 AOBlur(float2 uv : TEXCOORD0) : COLOR0
{
    float2 tapStep = 2.0 * ImageAndClip.xy;
    float4 raw = tex2Dlod(Ambient, float4(uv, 0, 0));
    float minAO = 1.0;
    // Fixed footprints: keep row order and let the compiler fold coordinates.
    [unroll] for (int y = 0; y < 3; ++y)
    [unroll] for (int x = 0; x < 3; ++x)
    {
        float2 footprint = min(float2(x, y) * 2.0 - 1.5, 2.0);
        minAO = min(minAO, tex2Dlod(AmbientLinear, float4(uv + footprint * tapStep, 0, 0)).a);
    }
    [branch] if (minAO >= 0.9995) return raw;
    float2 size = 1.0 / ImageAndClip.xy;
    float2 cuv = DepthTexelUV(uv, size);
    float d = ReadDepth(cuv);
    if (d >= SKY_DEPTH || IsWater(cuv, d)) return raw;
    float3 p = Position(cuv, d);
    float3 n = SurfaceNormal(cuv, p);
    float2 tangentStep = abs(n.x) > abs(n.y) ? float2(ImageAndClip.x, 0) : float2(0, ImageAndClip.y);
    float2 invFocal = 1.0 / Lighting.xy;
    float planeScale = 2.0 / max(0.025, min(Options.y * 0.1, p.z * 0.002));
    float sumAO = 0.0;
    float sumWeight = 0.0;
    // Visit the same 25 taps in row order without per-tap division/floor.
    [loop] for(int y=0;y<5;++y)
    [loop] for(int x=0;x<5;++x)
    {
        float2 o=float2(x-2,y-2);
        float2 tapUV = uv + o * tapStep;
        float2 quv = DepthTexelUV(tapUV, size);
        float qd = ReadDepth(quv);
        float3 q = PositionInv(quv, qd, invFocal);
        float2 a = abs(o);
        float weight = (a.x > 1.5 ? 0.5 : 1.0) * (a.y > 1.5 ? 0.5 : 1.0);
        weight *= exp2(-abs(dot(q - p, n)) * planeScale);
        float3 tangent = PositionInv(quv + tangentStep, ReadDepth(quv + tangentStep), invFocal) - q;
        float tangentOff = dot(tangent, n);
        weight *= saturate(1.0 - 2.0 * tangentOff * tangentOff / dot(tangent, tangent));
        // Sky and water taps carry no usable AO.
        weight *= qd < SKY_DEPTH && !IsWater(quv, qd) ? 1.0 : 0.0;
        sumAO += tex2Dlod(Ambient, float4(tapUV, 0, 0)).a * weight;
        sumWeight += weight;
    }
    return float4(raw.rgb, sumWeight > 1e-5 ? sumAO / sumWeight : raw.a);
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
    // 0.3.190: an ambient texel centre is a full-resolution texel boundary; a quarter full-res texel
    // inward makes the depth and NeighbourNormal reads there pick one texel on every GPU at even
    // sizes. Odd sizes drift the tap toward a boundary near three quarters across (a near-tie there);
    // depth and NeighbourNormal move together and both candidates lie in the same ambient texel.
    float2 ambientCenter = mad(floor(uv * ambientSize) + 0.5, ambientTexel, 0.25 * ImageAndClip.xy);
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
    float legacyT=mad(LegacyFog.w,saturate(pow(max(mad(p.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1);
    float3 fogPart=min((1-legacyT)*LegacyFogColor.rgb,original.rgb);
    float3 lit = mad(original.rgb-fogPart,ambient.a,fogPart);
    lit += ambient.rgb * Lighting.w * saturate(original.rgb + 0.1) * legacyT;
    lit += bloom * Options.x * (1.0 - saturate(lit));
    return float4(saturate(lit), original.a);
}
