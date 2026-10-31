#define main preceding_native_scene_main
#include "shared_gpu_residency_gpu.cpp"
#undef main
#include "scene_native_store.h"
static Result peerRun(IDirect3DDevice9* d,const std::vector<Geometry>& meshes,unsigned samples,bool capture){
 using namespace NorthlightReplayGPU;Cache material;BatchedCache shadow;material.setLimit(32u<<20);createBudgetMs()=1e6;
 const auto materialSource=residentSource(material),shadowSource=residentSource(shadow);Result r;Targets targets(d,samples);
 std::vector<Binding> bindings(meshes.size());std::vector<bool> ready(meshes.size());
 for(unsigned frame=0;frame<(capture?12u:48u);++frame){
  auto start=std::chrono::steady_clock::now();material.beginFrame();shadow.beginFrame();
  for(unsigned pass=0;pass<2;++pass){
   if(pass){for(size_t i=0;i<meshes.size();++i){auto& b=bindings[i];for(auto& p:b.vb)drop(p);drop(b.ib);b.at={};
      ready[i]=materialSource.borrow(meshes[i].mesh,b.vb,b.ib,b.at)||shadow.bind(d,meshes[i].mesh,b.vb,b.ib,[](size_t){return true;},b.at);}
    shadow.commitPending();}
   targets.bind();ok(d->BeginScene());const float angle=float(frame)*.47f;const float rows[4][4]={{std::cos(angle),0,std::sin(angle),0},{0,1,0,0},{-std::sin(angle),0,std::cos(angle),0},{.08f*std::sin(angle*2),float(pass),0,0}};
   ok(d->SetVertexShaderConstantF(1,rows[0],4));Binding single;
   for(size_t i=0;i<meshes.size();++i){auto& b=pass?bindings[i]:single;const auto& g=meshes[i];bool resident=false;
    if(pass){if(!ready[i]&&shadow.resident(g.mesh.get()))ready[i]=shadow.bind(d,g.mesh,b.vb,b.ib,[](size_t){return true;},b.at);resident=ready[i];}
    else{b.at={};resident=shadowSource.borrow(g.mesh,b.vb,b.ib,b.at)||material.bind(d,g.mesh,b.vb,b.ib,[](size_t){return true;});}
    ok(d->SetVertexShaderConstantF(0,g.center,1));
    if(resident){ok(d->SetStreamSource(0,b.vb[0],0,48));ok(d->SetIndices(b.ib));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,INT(b.at.vertexBase),0,g.mesh->vertexCount,b.at.indexStart,g.mesh->primitiveCount));}
    else{++r.fallback;ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,g.mesh->vertexCount,g.mesh->primitiveCount,g.mesh->indices.data(),D3DFMT_INDEX32,g.mesh->streams[0].bytes.data(),48));}
   }
   ok(d->EndScene());if(capture){r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();targets.pixels(r.pixels);start=std::chrono::steady_clock::now();}
  }
  if(!capture)r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  r.uploads+=material.uploaded()+shadow.uploaded();r.peak=std::max(r.peak,material.bytes()+shadow.bytes());
  for(auto& b:bindings){for(auto& p:b.vb)drop(p);drop(b.ib);}ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));
  if(frame%11==10){material.trimTo(0);shadow.trimTo(0);}
 }
 return r;
}
static Result canonicalRun(IDirect3DDevice9* d,const std::vector<Geometry>& meshes,unsigned samples,bool capture){
 using namespace NorthlightGpuScene;NativeStore scene;NativeClient material(scene,Consumer::Material),shadow(scene,Consumer::Shadow);
 NorthlightReplayGPU::createBudgetMs()=1e6;Result r;Targets targets(d,samples);
 std::vector<Binding> bindings(meshes.size());std::vector<bool> ready(meshes.size());
 for(unsigned frame=0;frame<(capture?12u:48u);++frame){
  auto start=std::chrono::steady_clock::now();scene.setFrame(frame+1);
  for(unsigned pass=0;pass<2;++pass){auto& cache=pass?shadow:material;
   for(size_t i=0;i<meshes.size();++i){auto& b=bindings[i];for(auto& p:b.vb)drop(p);drop(b.ib);b.at={};ready[i]=cache.bind(d,meshes[i].mesh,b.vb,b.ib,[](size_t){return true;},b.at);}
   cache.commitPending();targets.bind();ok(d->BeginScene());
   const float angle=float(frame)*.47f;const float rows[4][4]={{std::cos(angle),0,std::sin(angle),0},{0,1,0,0},{-std::sin(angle),0,std::cos(angle),0},{.08f*std::sin(angle*2),float(pass),0,0}};
   ok(d->SetVertexShaderConstantF(1,rows[0],4));
   for(size_t i=0;i<meshes.size();++i){const auto& g=meshes[i];auto& b=bindings[i];if(!ready[i]&&cache.resident(g.mesh))ready[i]=cache.bind(d,g.mesh,b.vb,b.ib,[](size_t){return true;},b.at);
    ok(d->SetVertexShaderConstantF(0,g.center,1));
    if(ready[i]){ok(d->SetStreamSource(0,b.vb[0],0,48));ok(d->SetIndices(b.ib));ok(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,INT(b.at.vertexBase),0,g.mesh->vertexCount,b.at.indexStart,g.mesh->primitiveCount));}
    else{++r.fallback;ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,g.mesh->vertexCount,g.mesh->primitiveCount,g.mesh->indices.data(),D3DFMT_INDEX32,g.mesh->streams[0].bytes.data(),48));}
   }
   ok(d->EndScene());if(capture){r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();targets.pixels(r.pixels);start=std::chrono::steady_clock::now();}
  }
  if(!capture)r.cpuMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  r.uploads+=material.uploaded()+shadow.uploaded();r.peak=std::max(r.peak,material.bytes()+shadow.bytes());
  assert(material.chargedBytes()<=material.limit()&&shadow.chargedBytes()<=shadow.limit());
  for(auto& b:bindings){for(auto& p:b.vb)drop(p);drop(b.ib);}ok(d->SetStreamSource(0,nullptr,0,0));ok(d->SetIndices(nullptr));
  if(frame%11==10){material.trimTo(0);shadow.trimTo(0);}
 }
 scene.clearAll();return r;
}
int main(int argc,char** argv){
 assert(argc>=4);const std::string folder=argv[2];auto meshes=load(argv[3]);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCanonicalScene";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=96;pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;auto bytes=read(folder+"vs.bin");ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(bytes.data()),&vs));bytes=read(folder+"ps.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps));
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
 IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));
 for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
 if(argc==5){const bool canonical=std::string(argv[4])=="canonical";auto r=canonical?canonicalRun(d,meshes,0,false):peerRun(d,meshes,0,false);
  std::printf("CANONICAL_GPU_TIMING canonical=%u cpuMs=%.6f uploads=%zu peak=%zu fallback=%zu\n",canonical,r.cpuMs,r.uploads,r.peak,r.fallback);
 }else for(unsigned samples:{0u,4u,8u}){DWORD quality=0;ok(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_TYPE(samples),&quality));auto old=peerRun(d,meshes,samples,true),current=canonicalRun(d,meshes,samples,true);
  assert(old.pixels==current.pixels&&current.uploads<=old.uploads&&current.fallback<=old.fallback);
  std::printf("CANONICAL_GPU_EXACT_PASS msaa=%u pixels=%zu uploads=%zu->%zu peak=%zu->%zu fallback=%zu->%zu\n",samples,current.pixels.size(),old.uploads,current.uploads,old.peak,current.peak,old.fallback,current.fallback);
 }
 ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetVertexDeclaration(nullptr));drop(vs);drop(ps);drop(decl);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);std::puts("CANONICAL_GPU_NATIVE_PASS");return 0;
}
