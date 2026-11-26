#pragma once
#include "frame/frame_inputs.h"
#include "reflection/owned_scene_preparation.h"
#include "reflection/owned_world_gpu.h"
#include <d3d9.h>
#include <cassert>

namespace water
{
// Device-owner operations only. CPU preparation, scene policy and publication
// belong to the controller. This renderer never sees game pointers or a host queue.
class OwnedSceneRenderer
{
public:
    OwnedSceneRenderer() : m_thread(GetCurrentThreadId()) {}
    ~OwnedSceneRenderer() { ReleaseAll(); }
    OwnedSceneRenderer(const OwnedSceneRenderer&) = delete;
    OwnedSceneRenderer& operator=(const OwnedSceneRenderer&) = delete;
    bool Upload(IDirect3DDevice9* device, const OwnedCpuScene& scene, unsigned byteBudget, std::size_t targetReservation = 0,
                const OwnedSceneRenderer* reusable=nullptr);
    bool EnsureTargets(IDirect3DDevice9* device, unsigned width, unsigned height);
    bool Draw(IDirect3DDevice9* device, const FrameInputs& frame, const OwnedCpuScene& scene);
    void BeginFrame() { assert(Owner()); m_drawn = false; m_maskDrawn=false; }
    void ReleaseGeometry();
    void ReleaseDefaultPool();
    void ReleaseAll();
    bool HasGeometry() const { return m_worldReady || (m_vertices && m_indices); }
    bool UploadFailed() const { return m_worldUploadFailed; }
    std::size_t StagingCpuBytes() const { return m_world.StagingBytes(); }
    IDirect3DTexture9* Colour() const { return m_drawn ? m_colour : nullptr; }
    IDirect3DTexture9* ReceiverMask()const{return m_maskDrawn?m_receiverMask:nullptr;}
    IDirect3DTexture9* Depth() const { return m_drawn ? m_depth : nullptr; }
    const float* Projection() const { return m_drawn ? m_projection : nullptr; }
    std::size_t Bytes() const { return m_geometryBytes + m_world.Bytes() + TargetBytes(); }
    unsigned Uploads() const { return m_uploads; }
    unsigned Draws() const { return m_draws; }
    unsigned LiveDraws() const { return m_world.LiveDraws(); }
    void SetOptions(const OwnedWorldDrawOptions& options) { m_world.SetOptions(options); }
    const OwnedWorldDrawStats& Stats() const { return m_world.Stats(); }
    std::size_t ReusedTextureBytes() const {return m_world.ReusedTextureBytes();}
    std::size_t ReusedGeometryBytes() const {return m_world.ReusedGeometryBytes();}
    unsigned ReusedTextures() const {return m_world.ReusedTextures();}
private:
    bool Owner() const { return GetCurrentThreadId() == m_thread; }
    bool Device(IDirect3DDevice9* device);
    std::size_t TargetBytes() const { return std::size_t(m_width)*m_height*(m_receiverMask?12:8); }
    const DWORD m_thread;
    IDirect3DDevice9* m_device = nullptr; // borrowed from the session
    IDirect3DVertexBuffer9* m_vertices = nullptr;
    IDirect3DIndexBuffer9* m_indices = nullptr;
    const OwnedCpuScene* m_uploadedScene = nullptr; // immutable CPU residency is retained by the controller
    IDirect3DTexture9* m_receiverMask=nullptr;
    IDirect3DSurface9* m_receiverSurface=nullptr;
    IDirect3DVertexShader9* m_receiverVs=nullptr;
    IDirect3DPixelShader9* m_receiverPs=nullptr;
    IDirect3DVertexDeclaration9* m_receiverDeclaration=nullptr;
    bool m_maskDrawn=false;
    IDirect3DTexture9* m_colour = nullptr;
    IDirect3DTexture9* m_depth = nullptr;
    IDirect3DSurface9* m_colourSurface = nullptr;
    IDirect3DSurface9* m_depthSurface = nullptr;
    IDirect3DVertexShader9* m_vertexShader = nullptr;
    IDirect3DPixelShader9* m_pixelShader = nullptr;
    IDirect3DVertexDeclaration9* m_declaration = nullptr;
    IDirect3DStateBlock9* m_state = nullptr;
    OwnedWorldGpu m_world;
    IDirect3DVertexShader9* m_worldVs = nullptr;
    IDirect3DPixelShader9* m_worldPs = nullptr;
    IDirect3DVertexDeclaration9* m_worldDeclaration = nullptr;
    IDirect3DVertexShader9* m_instancedVs = nullptr;
    IDirect3DVertexDeclaration9* m_instancedDeclaration = nullptr;
    IDirect3DVertexShader9* m_terrainVs = nullptr;
    IDirect3DVertexDeclaration9* m_terrainDeclaration = nullptr;
    unsigned m_width = 0, m_height = 0, m_vertexCount = 0, m_indexCount = 0;
    unsigned m_uploads = 0, m_draws = 0;
    std::size_t m_geometryBytes = 0;
    float m_projection[16] = {};
    bool m_drawn = false;
    bool m_worldReady = false, m_worldUploadFailed = false;
};
}
