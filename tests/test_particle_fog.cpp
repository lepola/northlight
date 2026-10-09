// 0.3.203 (particle fog): NorthlightLegacyFog::particleDistanceScale, the constant WorldComposite uses to turn the game's fog factor into a particle distance. Prints "scale x" lines.
#include "legacy_fog.h"
#include <cstdio>
using namespace NorthlightLegacyFog;
int main(){
    const struct{float p[4];float proj;} cases[]={
        {{-0.0019f,1,1,1},1.f},{{0.0019f,1,1,1},-1.f},   // linear fog from the camera (f = sat(z x projectionZ x X + 1)), both handednesses: X x projectionZ is negative
        {{-0.0019f,1,1,0},1.f},                            // not validated enabled
        {{0.f,1,1,0},1.f},                                 // the identity upload
        {{-0.0019f,1,2,1},1.f},                            // exponent 2
        {{-0.0019f,1.4f,1,1},1.f},                         // fog starts away from the camera
        {{0.0019f,1,1,1},1.f},                             // fog factor rising with distance: not a fog
    };
    for(const auto& c:cases)std::printf("%.9g\n",particleDistanceScale(c.p,c.proj));
}
