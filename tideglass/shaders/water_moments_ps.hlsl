// The water composite on a host that supplies its environment: the filtered material with the wave moments the surface
// detail pass measured (see Renderer::PrepareWaveMoments).
#define WATER_FILTERED_MATERIAL 1
#define REFLECTION_PRECOMPUTED_NORMALS 1
#define WATER_WAVE_MOMENT_MATERIAL 1
#include "water_ps.hlsl"
