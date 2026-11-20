#pragma once
#include <algorithm>
#include <cmath>

namespace water::math
{
// The captured client matrix has +Z forward and clip depth [-w,w]. The owned
// geometry pass uses D3D9 [0,w], with a full [0,1] viewport depth range. The main
// view's possibly compressed viewport range does not carry into this target.
inline bool ClientToDeviceProjection(const float* client, float* device)
{
    if (!client || !device) return false;
    for (unsigned i = 0; i < 16; ++i) {
        if (!std::isfinite(client[i])) return false;
        const bool used = i == 0 || i == 5 || i == 8 || i == 9 || i == 10 || i == 11 || i == 14;
        if (!used && std::fabs(client[i]) > .00001f) return false;
    }
    if (client[0] <= 0 || client[5] <= 0 || std::fabs(client[11] - 1) > .00001f || client[14] >= 0) return false;
    std::copy(client, client + 16, device);
    device[10] = .5f * (client[10] + 1);
    device[14] = .5f * client[14];
    return std::isfinite(device[10]) && std::isfinite(device[14]);
}
// Preserve the captured near plane, field of view and off-axis offsets. Used
// only by the owned source target; the main view/depth convention stays intact.
inline bool ExtendClientFarPlane(float* client,float farPlane)
{
    float device[16];
    if(!ClientToDeviceProjection(client,device)||!std::isfinite(farPlane)||farPlane<=0||client[10]<=1)return false;
    const double nearPlane=-double(client[14])/(double(client[10])+1);
    const double oldFar=-double(client[14])/(double(client[10])-1);
    if(nearPlane<=0||farPlane<=nearPlane)return false;
    if(farPlane<=oldFar)return true;
    client[10]=float((farPlane+nearPlane)/(farPlane-nearPlane));
    client[14]=float(-2*farPlane*nearPlane/(farPlane-nearPlane));
    return true;
}
}
