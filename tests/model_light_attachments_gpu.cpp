#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
struct ModelLightAttachmentsTest {
 static void run(IDirect3DDevice9* d){
  WorldRenderer w(d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map="moving-lamp";
  std::vector<uint32_t> words={0xfffe0300};
  auto op=[&](unsigned code,std::initializer_list<uint32_t> args){words.push_back(code|(unsigned(args.size())<<24));words.insert(words.end(),args);};
  op(81,{0xa00f0000,0x40400000,0x3f800000,0,0});
  op(31,{0x80000000,0x900f0000});op(31,{0x80000002,0x900f0001});op(31,{0x80000005,0x900f0003});
  op(31,{0x80000000,0xe00f0000});op(31,{0x80000005,0xe0030001});
  op(1,{0x80080000,0xa0550000});op(5,{0x80010001,0x90000001,0xa0000000});op(46,{0xb0010000,0x80000001});
  for(unsigned row=0;row<3;++row)op(9,{0x80000000u|(1u<<(16+row)),0xa0e4201fu+row,0xb0000000,0x90e40000});
  for(unsigned row=0;row<4;++row)op(9,{0xe0000000u|(1u<<(16+row)),0xa0e40002u+row,0x80e40000});
  op(1,{0xe0030001,0x90e40003});words.push_back(0xffff);
  auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(words.data(),words.size(),*program));assert(NorthlightRigidGeometry::oneBoneTemplate(*program));
  std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(words.data(),words.size(),patched));
  IDirect3DVertexShader9* original=nullptr;WorldRenderer::CaptureShader metadata;
  must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(words.data()),&original));
  must(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&metadata.replacement));
  metadata.program=program;metadata.skinned=true;metadata.projectionKind=2;metadata.usage=NorthlightShaderConstants::analyze(words.data(),words.size(),2);
  w.captureShaders.emplace(original,metadata);w.actorPrograms.emplace(original,program);
  D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  IDirect3DVertexDeclaration9* declaration=nullptr;must(d->CreateVertexDeclaration(elements,&declaration));must(d->SetVertexDeclaration(declaration));
  struct Vertex {float x,y,z;uint32_t bone;float u,v;};std::vector<Vertex> vertices;
  for(unsigned i=0;i<99;++i)for(auto v:{Vertex{0,0,0,1,0,0},Vertex{1,0,0,1,1,0},Vertex{0,1,0,1,0,1}})vertices.push_back(v);
  NorthlightLightAttachments::Attachment attachment;attachment.sourceId=42;attachment.bone=1;attachment.reference=0;attachment.witness={0,0,0,1,0,0,0,1,0};attachment.point[2]=2;
  attachment.placement[0]=attachment.placement[4]=attachment.placement[8]=1;attachment.placement[9]=10;attachment.placement[10]=20;attachment.placement[11]=30;
  w.active->lightAttachments=std::make_shared<const NorthlightLightAttachments::Records>(NorthlightLightAttachments::Records{attachment});
  w.active->localLights.push_back({{10,20,32},{.2f,.5f,1},1,12,42,3,NorthlightLocalLights::CastShadow});
  for(unsigned i=0;i<16;++i)w.context.inverseView[i]=i%5==0?1.f:0.f;w.projection[0]=w.projection[1]=w.projection[2]=1;
  float constants[1024]{};constants[8]=constants[13]=constants[18]=constants[22]=1;
  for(unsigned bone=0;bone<2;++bone){float* r=constants+4*(31+3*bone);r[0]=r[5]=r[10]=1;r[3]=10;r[7]=20;r[11]=30;}
  must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
  must(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP));must(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP));must(d->SetTexture(0,nullptr));
  w.quality.actorShadows=0;w.quality.gi=0;w.captureMode=WorldRenderer::CaptureFresh;w.minSkinnedShadowTriangles=100;
  assert(w.replaySnapshots.configureBudget(32*1048576,16*1048576));
  std::vector<NorthlightLocalLights::Light> moved;
  for(float x:{11.f,12.f}){constants[4*34+3]=x;must(d->SetVertexShaderConstantF(0,constants,256));
   w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
   assert(!w.replays.empty()&&w.replays.back()->count==99&&w.replays.back()->shadowSelected&&!w.replays.back()->shadowSmall);
   const auto& lights=w.lightAttachments.apply(w.active->localLights,moved);assert(lights[0].sourceId==42&&lights[0].position[0]==x&&lights[0].position[2]==32);
  }
  // The same shader can draw a multi-bone character section: keep its threshold.
  vertices.back().bone=0;const auto retained=w.replays.size();
  w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
  assert(w.replays.size()==retained);
  w.minSkinnedShadowTriangles=99;
  w.captureModel(D3DPT_TRIANGLELIST,0,0,unsigned(vertices.size()),0,99,false,original,false,nullptr,D3DFMT_INDEX16,vertices.data(),sizeof(Vertex));
  assert(w.replays.size()==retained+1);
  original->Release();declaration->Release();std::puts("POINT_MOVING_LIGHT_CAPTURE_PASS triangles=99 minimum=100 poses=2 sameSource=1");
 }
};
int main(int argc,char** argv){assert(argc==3);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightPointCoverage";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d));ModelLightAttachmentsTest::run(d);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);}
