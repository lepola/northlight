#include "owned_scene_renderer.h"
#include "log.h"
#include "math/projection.h"
#include "scenery_plane_transform.h"
#include "screen_pass.h"
#include "vs_owned_scene.h"
#include "ps_owned_scene.h"
#include "vs_owned_world.h"
#include "vs_owned_world_instanced.h"
#include "vs_owned_world_terrain.h"
#include "ps_owned_world.h"
#include "vs_owned_receiver.h"
#include "ps_owned_receiver.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace water
{
namespace
{
template<class T> void Drop(T*& object) { if (object) object->Release(); object = nullptr; }
}
bool OwnedSceneRenderer::Device(IDirect3DDevice9* device)
{
    if (!Owner() || !device || (m_device && m_device != device)) return false;
    m_device = device;
    return true;
}

void OwnedSceneRenderer::ReleaseGeometry()
{
    assert(Owner());
    Drop(m_vertices); Drop(m_indices);
    m_world.Release();m_worldReady = m_worldUploadFailed = false;
    m_uploadedScene = nullptr;
    Drop(m_receiverSurface);Drop(m_receiverMask);m_maskDrawn=false;
    m_geometryBytes = 0; m_vertexCount = m_indexCount = 0; m_drawn = false;
}
void OwnedSceneRenderer::ReleaseDefaultPool()
{
    assert(Owner());
    ReleaseGeometry();
    Drop(m_state); Drop(m_colourSurface); Drop(m_depthSurface);
    Drop(m_colour); Drop(m_depth);
    m_width = m_height = 0;
}
void OwnedSceneRenderer::ReleaseAll()
{
    assert(Owner());
    ReleaseDefaultPool();
    Drop(m_vertexShader); Drop(m_pixelShader); Drop(m_declaration);
    Drop(m_worldVs); Drop(m_worldPs); Drop(m_worldDeclaration);
    Drop(m_instancedVs); Drop(m_instancedDeclaration); Drop(m_terrainVs); Drop(m_terrainDeclaration);
    Drop(m_receiverVs);Drop(m_receiverPs);Drop(m_receiverDeclaration);
    m_device = nullptr;
}

bool OwnedSceneRenderer::Upload(IDirect3DDevice9* device, const OwnedCpuScene& scene, unsigned byteBudget, std::size_t targetReservation,const OwnedSceneRenderer* reusable)
{
    if (!Device(device)) return false;
    if (scene.worldScene) {
        if (!scene.worldRender || (m_uploadedScene && m_uploadedScene != &scene)) return false;
        const auto targets=std::max(TargetBytes(),targetReservation);
        const auto available=targets<OwnedWorldGpu::ResidencyLimit?OwnedWorldGpu::ResidencyLimit-targets:0;
        const auto result=m_world.Upload(device,*scene.worldScene,*scene.worldRender,byteBudget,available,reusable?&reusable->m_world:nullptr);
        m_worldUploadFailed=result==OwnedWorldUpload::Failed;
        if (result!=OwnedWorldUpload::Ready) return false;
        if (!m_worldReady) {
            ++m_uploads;
            WATER_LOG_INFO("owned GPU upload textures_reused=%u reused_bytes=%llu geometry_reused_bytes=%llu uploaded_bytes=%llu resident_bytes=%llu",
                m_world.ReusedTextures(),static_cast<unsigned long long>(m_world.ReusedTextureBytes()),
                static_cast<unsigned long long>(m_world.ReusedGeometryBytes()),
                static_cast<unsigned long long>(m_world.Bytes()-m_world.ReusedTextureBytes()-m_world.ReusedGeometryBytes()),static_cast<unsigned long long>(m_world.Bytes()));
        }
        m_worldReady=true;m_uploadedScene=&scene;return true;
    }
    if (scene.vertices.empty() || scene.indices.empty() || scene.vertices.size() > 65536) return false;
    const auto vertexBytes = scene.vertices.size() * sizeof(OwnedSceneVertex);
    if (vertexBytes > byteBudget || scene.indices.size() > (byteBudget-vertexBytes)/sizeof(uint16_t)) return false;
    const auto indexBytes = scene.indices.size() * sizeof(uint16_t);
    for (auto index : scene.indices) if (index >= scene.vertices.size()) return false;
    IDirect3DVertexBuffer9* vertices = nullptr;
    IDirect3DIndexBuffer9* indices = nullptr;
    bool ok = SUCCEEDED(device->CreateVertexBuffer(static_cast<UINT>(vertexBytes), D3DUSAGE_WRITEONLY, 0,
                                                   D3DPOOL_DEFAULT, &vertices, nullptr)) &&
              SUCCEEDED(device->CreateIndexBuffer(static_cast<UINT>(indexBytes), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                                                  D3DPOOL_DEFAULT, &indices, nullptr));
    void* destination = nullptr;
    if (ok) {
        ok = SUCCEEDED(vertices->Lock(0, static_cast<UINT>(vertexBytes), &destination, 0));
        if (ok) { std::memcpy(destination, scene.vertices.data(), vertexBytes); ok = SUCCEEDED(vertices->Unlock()); }
    }
    if (ok) {
        ok = SUCCEEDED(indices->Lock(0, static_cast<UINT>(indexBytes), &destination, 0));
        if (ok) { std::memcpy(destination, scene.indices.data(), indexBytes); ok = SUCCEEDED(indices->Unlock()); }
    }
    if (ok) {
        ReleaseGeometry();
        m_vertices = vertices; m_indices = indices;
        m_uploadedScene = &scene;
        vertices = nullptr; indices = nullptr;
        m_vertexCount = static_cast<unsigned>(scene.vertices.size());
        m_indexCount = static_cast<unsigned>(scene.indices.size());
        m_geometryBytes = vertexBytes + indexBytes;
        ++m_uploads;
    }
    Drop(vertices); Drop(indices);
    return ok;
}

bool OwnedSceneRenderer::EnsureTargets(IDirect3DDevice9* device, unsigned width, unsigned height)
{
    if (!Device(device) || !width || !height || width > 2048 || height > 2048) return false;
    const auto geometry=m_geometryBytes+m_world.Bytes();
    const bool receiving=m_worldReady && !m_uploadedScene->worldRender->views.empty();
    const auto bytes=std::size_t(width)*height*(receiving?12:8);
    if(geometry>OwnedWorldGpu::ResidencyLimit||bytes>OwnedWorldGpu::ResidencyLimit-geometry)return false;
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0}, D3DDECL_END()};
    if ((!m_vertexShader && FAILED(p_vs_owned_scene.Create(device, &m_vertexShader))) ||
        (!m_pixelShader && FAILED(p_ps_owned_scene.Create(device, &m_pixelShader))) ||
        (!m_declaration && FAILED(device->CreateVertexDeclaration(elements, &m_declaration)))) return false;
    if (m_worldReady) {
        const D3DVERTEXELEMENT9 worldElements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,32,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
        // Models drawn many at once: stream 1 carries each copy's placement rows and draw mode (OwnedWorldInstance).
        const D3DVERTEXELEMENT9 instancedElements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,32,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
            {1,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},
            {1,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,3},
            {1,32,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,4},
            {1,48,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,5},D3DDECL_END()};
        static_assert(sizeof(OwnedWorldInstance)==64,"instanced declaration");
        // Terrain: stream 1 carries each vertex's alpha atlas coordinate, behind the tile's vertices in the same buffer.
        const D3DVERTEXELEMENT9 terrainElements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {2,0,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
            {1,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},
            {1,8,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        if ((!m_terrainVs && FAILED(p_vs_owned_world_terrain.Create(device,&m_terrainVs))) ||
            (!m_terrainDeclaration && FAILED(device->CreateVertexDeclaration(terrainElements,&m_terrainDeclaration)))) return false;
        if ((!m_instancedVs && FAILED(p_vs_owned_world_instanced.Create(device,&m_instancedVs))) ||
            (!m_instancedDeclaration && FAILED(device->CreateVertexDeclaration(instancedElements,&m_instancedDeclaration)))) return false;
        if ((!m_worldVs && FAILED(p_vs_owned_world.Create(device,&m_worldVs))) ||
            (!m_worldPs && FAILED(p_ps_owned_world.Create(device,&m_worldPs))) ||
            (!m_worldDeclaration && FAILED(device->CreateVertexDeclaration(worldElements,&m_worldDeclaration)))) return false;
    }
    if(receiving){
        const D3DVERTEXELEMENT9 receiverElements[]={
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        if((!m_receiverVs&&FAILED(p_vs_owned_receiver.Create(device,&m_receiverVs))) ||
           (!m_receiverPs&&FAILED(p_ps_owned_receiver.Create(device,&m_receiverPs))) ||
           (!m_receiverDeclaration&&FAILED(device->CreateVertexDeclaration(receiverElements,&m_receiverDeclaration))))return false;
    }
    if (m_width != width || m_height != height) {
        Drop(m_receiverSurface);Drop(m_receiverMask);m_maskDrawn=false;
        Drop(m_state); Drop(m_colourSurface); Drop(m_depthSurface);
        Drop(m_colour); Drop(m_depth);
        m_width = m_height = 0; m_drawn = false;
    }
    if (!m_colour || !m_depth) {
        const auto intz = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
        if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                        D3DPOOL_DEFAULT, &m_colour, nullptr)) ||
            FAILED(m_colour->GetSurfaceLevel(0, &m_colourSurface)) ||
            FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, intz,
                                        D3DPOOL_DEFAULT, &m_depth, nullptr)) ||
            FAILED(m_depth->GetSurfaceLevel(0, &m_depthSurface))) {
            Drop(m_colourSurface); Drop(m_depthSurface);
            Drop(m_colour); Drop(m_depth);
            return false;
        }
        m_width = width; m_height = height;
    }
    if(receiving && !m_receiverMask){
        if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&m_receiverMask,nullptr)) ||
           FAILED(m_receiverMask->GetSurfaceLevel(0,&m_receiverSurface))){Drop(m_receiverSurface);Drop(m_receiverMask);return false;}
    }
    return EnsureScreenPassStateBlock(device, m_state);
}

bool OwnedSceneRenderer::Draw(IDirect3DDevice9* device, const FrameInputs& frame, const OwnedCpuScene& scene)
{
    if (!Owner()) return false;
    m_drawn = false;
    if (!Device(device) || !HasGeometry() || !m_colourSurface || !m_depthSurface || !m_state ||
        &scene != m_uploadedScene || bool(scene.worldScene)!=m_worldReady ||
        (!m_worldReady && (scene.vertices.size() != m_vertexCount || scene.indices.size() != m_indexCount || scene.draws.empty())) ||
        (m_worldReady && (!scene.worldRender || !m_world.Ready(*scene.worldScene))) ||
        !math::ClientToDeviceProjection(frame.glProjection, m_projection)) return false;
    for (float value : frame.cameraPosition) if (!std::isfinite(value)) return false;
    for (float value : frame.cameraRelativeView) if (!std::isfinite(value)) return false;
    ScreenPassState saved;
    if (!saved.Capture(device, m_state)) return false;
    bool ok = SetScreenPassState(device, m_worldReady ? m_worldVs : m_vertexShader, m_worldReady ? m_worldDeclaration : m_declaration);
    for (DWORD i = 1; i < 4; ++i) device->SetRenderTarget(i, nullptr);
    for (DWORD i = 0; i < 16; ++i) ok = SUCCEEDED(device->SetTexture(i, nullptr)) && ok;
    ok = SUCCEEDED(device->SetRenderTarget(0, m_colourSurface)) &&
         SUCCEEDED(device->SetDepthStencilSurface(m_depthSurface)) && ok;
    const D3DVIEWPORT9 viewport = {0, 0, m_width, m_height, 0, 1};
    ok = SUCCEEDED(device->SetViewport(&viewport)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_DEPTHBIAS, 0)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, 0)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_CLIPPING, TRUE)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_WRAP0, 0)) &&
         SUCCEEDED(device->SetRenderState(D3DRS_WRAP1, 0)) &&
         SUCCEEDED(device->SetPixelShader(m_worldReady ? m_worldPs : m_pixelShader)) &&
         (m_worldReady || (SUCCEEDED(device->SetStreamSource(0, m_vertices, 0, sizeof(OwnedSceneVertex))) &&
                          SUCCEEDED(device->SetIndices(m_indices)))) &&
         SUCCEEDED(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0)) && ok;
    const float camera[] = {frame.cameraPosition[0], frame.cameraPosition[1], frame.cameraPosition[2], 0};
    const float fogRange[] = {frame.fogStart, frame.fogEnd > frame.fogStart ? 1.f / (frame.fogEnd-frame.fogStart) : 0, 0, 0};
    const float fog[] = {((frame.fogColor >> 16) & 255) / 255.f, ((frame.fogColor >> 8) & 255) / 255.f,
                        (frame.fogColor & 255) / 255.f, 1};
    ok = SUCCEEDED(device->SetVertexShaderConstantF(4, camera, 1)) &&
         SUCCEEDED(device->SetPixelShaderConstantF(0, fogRange, 1)) &&
         SUCCEEDED(device->SetPixelShaderConstantF(1, fog, 1)) && ok;
    if(m_worldReady && m_receiverMask && !m_maskDrawn && ok){
        ok=SUCCEEDED(device->SetRenderTarget(0,m_receiverSurface)) &&
            SUCCEEDED(device->SetVertexShader(m_receiverVs)) && SUCCEEDED(device->SetPixelShader(m_receiverPs)) &&
            SUCCEEDED(device->SetVertexDeclaration(m_receiverDeclaration)) &&
            SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0)) &&
            m_world.DrawReceivers(device,frame);
        m_maskDrawn=ok;
        ok=SUCCEEDED(device->SetRenderTarget(0,m_colourSurface)) && SUCCEEDED(device->SetPixelShader(m_worldPs)) &&
            SUCCEEDED(device->SetVertexShader(m_worldVs)) && SUCCEEDED(device->SetVertexDeclaration(m_worldDeclaration)) &&
            SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0)) && ok;
    }
    if (m_worldReady && ok) {
        const auto before=m_world.Draws();
        ok=m_world.Draw(device,frame,*scene.worldScene,*scene.worldRender,{m_worldVs,m_worldDeclaration,m_instancedVs,m_instancedDeclaration,m_terrainVs,m_terrainDeclaration});
        m_draws+=m_world.Draws()-before;
    }
    for (const auto& draw : scene.draws) {
        if (!ok) break;
        if (!draw.indexCount || draw.indexCount % 3 || draw.firstIndex > m_indexCount ||
            draw.indexCount > m_indexCount-draw.firstIndex) { ok = false; break; }
        double matrix[4][4] = {};
        float distance = draw.plane[3];
        for (unsigned axis = 0; axis < 3; ++axis) distance -= draw.plane[axis] * frame.cameraPosition[axis];
        ok = reflection::WaterMirrorClip(frame.cameraRelativeView, frame.cameraPosition, 0, m_projection,
                                         matrix, draw.plane.data(), distance);
        float columns[16];
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row) columns[column*4+row] = static_cast<float>(matrix[row][column]);
        ok = SUCCEEDED(device->SetVertexShaderConstantF(0, columns, 4)) &&
             SUCCEEDED(device->SetVertexShaderConstantF(5, draw.plane.data(), 1)) && ok;
        if (ok) {
            ok = SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, m_vertexCount,
                                                        draw.firstIndex, draw.indexCount/3));
            if (ok) ++m_draws;
        }
    }
    m_drawn = saved.Restore() && ok;
    return m_drawn;
}
}
