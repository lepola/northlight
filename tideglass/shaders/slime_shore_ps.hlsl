// How near each point of the slime is to its edge, for the waves that lap at it (water_common's ReadShoreWaves).
// Slime lies in rooms and canals the ground map knows nothing of, so its edge is what the contact marks have found of
// it and remembered on the slime's ripple grid (contact_ps, contact_spread_ps: the grid's a), less what merely stands
// in the slime. The distance to the nearest of it is taken across the grid one way and then the other: the first gives how far along each row
// the nearest mark is, the second the nearest of those over the rows near by, which together is the distance itself.
// Kept as nearness (the reach less the distance), so that a map with nothing on it, or none bound, reads as no edge.
// A last pass softens it: the marks lie on whole cells, and wave fronts drawn from them followed the cells' corners.

// x, y: one cell of this map in uv, along the way taken; z: which pass, 0 to pick the edge out of the marks, 1 for the
// first way, 2 for the second, 3 to soften; w: one cell in uv.
float4 cPass : register(c0);
// The edge: the slime's ripple grid. First way: the edge. Second way: the first way's result. Softening: the second's.
sampler2D sSource : register(s0);

// How far the edge is looked for (cells of this map, a cell being kCell yards: twice the ripple grid's).
static const int kReach = 16;
static const float kCell = 0.3;
// A cell of this map covers four of the ripple grid, read as their average: this much of it marked is an edge (a mark
// grows only as it stays, contact_spread_ps, so one that comes and goes never is), and below this it has all been
// looked at and found clear (kSeenOpen there).
static const float kMarked = 0.2;
static const float kOpen = -0.012;
static const float kIslandNear = 5.0;
static const float kIslandFar = 10.0;
// The waves lap at what holds the slime (the walls and steps of a canal, a bank), not at what stands or rises in it
// (a bubble, the foot of a fall, a post, a character): those keep their glow but are left out of the edge. What holds
// the slime has ground behind it that the camera can never look at as slime: a mark is part of the edge where, of
// eight sides, more than one shows nothing known this far out and this far (cells). One is allowed, because whatever
// stands in the slime hides the slime behind it; clear slime or another mark that way says nothing either way.
static const float kSomeMark = 0.03;

float Marked(float2 uv)
{
    return tex2Dlod(sSource, float4(uv, 0, 0)).a;
}

float4 main(float2 cell : VPOS) : COLOR0
{
    float2 uv = (cell + 0.5) * cPass.w;
    float result;
    // (Every pixel of a draw takes the same pass.)
    [branch] if (cPass.z < 0.5)
    {
        result = 0.0;
        [branch] if (Marked(uv) > kMarked)
        {
            float unknown = 0.0;
            [unroll] for (int side = 0; side < 8; ++side)
            {
                float angle = side * 0.78539816;
                float2 way = float2(cos(angle), sin(angle)) * cPass.w;
                float nearer = Marked(uv + way * kIslandNear);
                float further = Marked(uv + way * kIslandFar);
                bool open = nearer < kOpen || further < kOpen;
                bool met = nearer > kSomeMark || further > kSomeMark;
                if (!open && !met)
                    unknown += 1.0;
            }
            result = unknown > 1.5 ? 1.0 : 0.0;
        }
    }
    else [branch] if (cPass.z < 1.5)
    {
        float along = kReach + 1.0;
        [unroll] for (int k = -kReach; k <= kReach; ++k)
            if (tex2Dlod(sSource, float4(uv + cPass.xy * k, 0, 0)).r > 0.5)
                along = min(along, abs(k));
        result = along;
    }
    else [branch] if (cPass.z > 2.5)
    {
        // Four filtered reads between the cells: each cell with the eight round it, the nearer the heavier.
        float2 reach = cPass.w * 0.75;
        result = (tex2Dlod(sSource, float4(uv + reach, 0, 0)).r + tex2Dlod(sSource, float4(uv - reach, 0, 0)).r +
                  tex2Dlod(sSource, float4(uv + float2(reach.x, -reach.y), 0, 0)).r +
                  tex2Dlod(sSource, float4(uv - float2(reach.x, -reach.y), 0, 0)).r) * 0.25;
    }
    else
    {
        float nearest = (kReach + 1.0) * (kReach + 1.0);
        [unroll] for (int k = -kReach; k <= kReach; ++k)
        {
            float along = tex2Dlod(sSource, float4(uv + cPass.xy * k, 0, 0)).r;
            nearest = min(nearest, along * along + k * k);
        }
        result = max(kReach - sqrt(nearest), 0.0) * kCell;
    }
    return float4(result, 0.0, 0.0, 0.0);
}
