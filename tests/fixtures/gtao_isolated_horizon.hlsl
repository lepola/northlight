// Base GTAO compatibility reference from PR #118 (3977bd7), MIT.
#define NORTHLIGHT_GTAO_ISOLATED_HORIZON 1
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
        [loop] for (int valley = 0; valley < 16; ++valley)
        {
            float ring = floor(valley * .25);
            float angle = (valley - ring * 4) * .78539816339;
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
        [loop] for (int tap = 0; tap < 32; ++tap)
        {
            float directionIndex = floor(tap * .25);
            float step = tap - directionIndex * 4;
            float angle = directionIndex * .78539816339;
            float2 v = float2(cos(angle), sin(angle));
            float2 direction = float2(v.x * rotation.x - v.y * rotation.y, v.x * rotation.y + v.y * rotation.x);
            float4 q = AOSample(uv + direction * uvRadius * ((step + 1) * .2));
            float3 delta = q.xyz - p;
            float distance2 = max(dot(delta, delta), 1e-12);
            float elevation = saturate(dot(normal, delta) * rsqrt(distance2) - .1);
            obscurance += elevation * saturate(1 - distance2 / (radius * radius)) * q.w;
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
            // Both inner taps can resolve to the receiver texel at distance.
            // Reject by footprint, not a nearly-zero reconstructed 3D delta.
            weight *= any(abs(offset) > .51 * ImageAndClip.xy) ? 1 : 0;
            // Match the SSAO coplanar tolerance for grazing D24 derivatives.
            weight *= float2(dot(normal, da) > .08 * distance.x, dot(normal, db) > .08 * distance.y);
            float2 candidate = float2(dot(da, view), dot(db, view)) * inverseDistance;
            float2 solidCandidate = lerp(low, candidate, weight * solidFalloff);
            coverage += float2(solidCandidate.x > low.x + .00001, solidCandidate.y > low.y + .00001);
            solidHorizon = max(solidHorizon, solidCandidate);
            horizon = max(horizon, lerp(low, candidate, weight * thinFalloff));
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
