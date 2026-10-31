#pragma once
#include "scene_native_store.h"
#include "static_shadow_gpu.h"
#include "live_terrain_gpu.h"
#include "terrain_capture_bounds.h"
#include "mesh_buffer_store.h"
#include "mesh_pool_trim.h"
#include <stdexcept>

class NorthlightAutoNormalCache;
struct NorthlightDisplacementStorage;
namespace NorthlightGI {class SceneSourceCache;}
namespace NorthlightGpuScene {
// Device-scoped backing scene. Passes retain typed views and their existing
// admission limits; native geometry has one identity, allocation and upload.
// World pages, compact casters and native streams have different vertex
// contracts and must never be aliased merely because they share an asset.
struct Scene {
 IDirect3DDevice9* const device;
 NativeStore native;
 NorthlightDrawSnapshot::Frame::SharedSources captureSources;
 NativeClient materials{native,Consumer::Material},shadows{native,Consumer::Shadow};
 StaticShadow::GpuCache staticCasters;
 struct MeshBuffers {IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;D3DPOOL vbPool=D3DPOOL_MANAGED,ibPool=D3DPOOL_MANAGED;bool vbFresh=false,ibFresh=false;};
 NorthlightMeshBufferStore::Store meshStore;
 std::vector<MeshBuffers> meshBuffers;
 NorthlightMeshPoolTrim::State meshPoolTrim;
 NorthlightLiveTerrainGPU::Cache<NorthlightTerrainCapture::MeshSnapshot,StaticShadow::Vertex> terrain;
 NorthlightTerrainCapture::FrameCache terrainSources;
 std::shared_ptr<NorthlightGI::SceneSourceCache> decodedSources;
 // Worker-affine CPU acceleration stays on the worker. Type erasure avoids
 // linking the GI baker into standalone material-only consumers.
 std::shared_ptr<void> workerPreparation;
 std::shared_ptr<NorthlightAutoNormalCache> normalMaps;
 std::shared_ptr<NorthlightDisplacementStorage> displacement;
 struct MaterialTargets {
  IDirect3DTexture9* height=nullptr;IDirect3DSurface9* target=nullptr,*depth=nullptr;
  IDirect3DTexture9* occlusion=nullptr;IDirect3DSurface9* occlusionTarget=nullptr;
  ~MaterialTargets(){if(target)target->Release();if(height)height->Release();if(depth)depth->Release();if(occlusionTarget)occlusionTarget->Release();if(occlusion)occlusion->Release();}
 } materialTargets;
 explicit Scene(IDirect3DDevice9* d):device(d){}
 ~Scene(){for(auto& page:meshBuffers){if(page.vb)page.vb->Release();if(page.ib)page.ib->Release();}}
 Scene(const Scene&)=delete;Scene& operator=(const Scene&)=delete;
 bool attach(NorthlightDrawSnapshot::Frame& frame){if(!captureSources){captureSources=frame.sharedSources();return true;}return frame.joinSnapshotPool(captureSources);}
 void setFrame(uint64_t frame){native.setFrame(frame);}
};
inline std::shared_ptr<Scene> forDevice(IDirect3DDevice9* d,std::shared_ptr<Scene> scene){
 if(scene&&scene->device!=d)throw std::invalid_argument("GPU scene belongs to another device");
 return scene?std::move(scene):std::make_shared<Scene>(d);
}
}
