#pragma once
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Build 12340's camera, read through the caller's memory reader: the view and projection the client holds for the
// frame it is about to draw (preferPending) or its globals, accepted only when they form a usable perspective camera.
namespace water::client12340
{
inline constexpr uintptr_t devicePointer = 0x00C5DF88;
inline constexpr uintptr_t viewGlobal = 0x00ADF5E8, projectionGlobal = 0x00ADF628;
inline constexpr uint32_t viewIndex = 0x1AF8, views = 0x1B00, projection = 0xF88, depthRange = 0xF80;
inline constexpr uint32_t viewCount = 64;

inline bool AllFinite(const float* values, int count)
{
    return values && count >= 0 && std::all_of(values, values + count, [](float value) { return std::isfinite(value); });
}
inline bool ClientPerspective(const float* matrix)
{
    if (!AllFinite(matrix, 16)) return false;
    const bool homogeneous = std::abs(double(matrix[11]) - 1) < .001f && std::abs(matrix[15]) < .001f;
    return homogeneous && std::abs(matrix[0]) > .0001f && std::abs(matrix[5]) > .0001f;
}
struct Camera
{
    bool valid = false;
    D3DVIEWPORT9 viewport = {};
    float cameraRelativeView[16] = {}, glProjection[16] = {};
};
inline bool CameraValid(const Camera& camera)
{
    return camera.viewport.Width && camera.viewport.Height && std::isfinite(camera.viewport.MinZ) &&
           std::isfinite(camera.viewport.MaxZ) && camera.viewport.MaxZ > camera.viewport.MinZ &&
           AllFinite(camera.cameraRelativeView, 16) && ClientPerspective(camera.glProjection);
}
template<class Read> bool ReadDevice(Read read, uint32_t device, uint32_t offset, void* output, size_t bytes)
{
    constexpr auto maximum = std::numeric_limits<uint32_t>::max();
    return device && offset <= maximum - device && bytes <= maximum - device - offset &&
           read(uintptr_t(device) + offset, output, bytes);
}
template<class Read> bool ReadGlobals(Read read, float* view, float* matrix)
{
    float orientation[16], perspective[16];
    if (!view || !matrix || !read(viewGlobal, orientation, sizeof(orientation)) ||
        !read(projectionGlobal, perspective, sizeof(perspective)) || !AllFinite(orientation, 16) ||
        !ClientPerspective(perspective)) return false;
    std::memcpy(view, orientation, sizeof(orientation));
    std::memcpy(matrix, perspective, sizeof(perspective));
    return true;
}
template<class Read> bool ReadCamera(Read read, const D3DVIEWPORT9& viewport, bool preferPending, Camera& output)
{
    Camera candidate;
    candidate.viewport = viewport;
    uint32_t device = 0;
    if (!read(devicePointer, &device, sizeof(device))) device = 0;
    if (device) {
        float range[2];
        if (ReadDevice(read, device, depthRange, range, sizeof(range)) && AllFinite(range, 2) &&
            range[0] >= 0 && range[1] <= 1 && range[1] - range[0] > .01f) {
            candidate.viewport.MinZ = range[0];
            candidate.viewport.MaxZ = range[1];
        }
    }
    bool pending = false;
    if (preferPending && device) {
        uint32_t index = 0;
        pending = ReadDevice(read, device, viewIndex, &index, sizeof(index)) && index < viewCount &&
            ReadDevice(read, device, views + index * sizeof(candidate.cameraRelativeView),
                       candidate.cameraRelativeView, sizeof(candidate.cameraRelativeView)) &&
            ReadDevice(read, device, projection, candidate.glProjection, sizeof(candidate.glProjection)) &&
            AllFinite(candidate.cameraRelativeView, 16) && ClientPerspective(candidate.glProjection);
    }
    if (!pending && !ReadGlobals(read, candidate.cameraRelativeView, candidate.glProjection)) return false;
    if (!CameraValid(candidate)) return false;
    candidate.valid = true;
    output = candidate;
    return true;
}
}
