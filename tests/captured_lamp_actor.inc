// Included in the existing production-cube test friend. Only controlled actor
// poses are synthesized; the lamp mesh, placement and light stay unchanged.
static void housingActor(IDirect3DDevice9* d,const std::string& root,const std::string& output,bool soft,bool omitActor=false){
 std::vector<NorthlightLocalLights::Light> lights;assert(NorthlightLocalLights::readFile(root+"/sources.fgl",lights)&&lights.size()==1);const auto l=lights[0];
 const auto placement=readBytes(root+"/actor-foot.f32");assert(placement.size()==12);float foot[3];std::memcpy(foot,placement.data(),12);
 WorldRenderer w(d);load(w,root,false,{l.position[0],l.position[1],l.position[2]});w.quality.softShadows=soft;
 assert(NorthlightWmoLampShadow::parts(l,w.batches).size()==1);const auto housing=w.batches;
 const float camera[]={l.position[0]+.5f,l.position[1],foot[2]+9};std::memcpy(w.context.camera,camera,12);std::memcpy(w.context.inverseView+12,camera,12);w.projection[0]=w.projection[1]=1.4f;w.projection[2]=-1;
 const auto bytes=readBytes(output+"/character.bin");unsigned h[5];assert(bytes.size()>=20);std::memcpy(h,bytes.data(),20);assert(h[0]==0x31414c4e&&bytes.size()==20+size_t(h[1])*48+size_t(h[2])*4);
 const auto code=readBytes(output+"/character-vs.bin");std::vector<uint32_t> words(code.size()/4);std::memcpy(words.data(),code.data(),code.size());
 auto program=std::make_shared<NorthlightActorDeformation::Program>();assert(NorthlightActorDeformation::compile(words.data(),words.size(),*program));
 auto actor=std::make_unique<WorldRenderer::Replay>();actor->constantGroup=1;must(d->CreateVertexShader((DWORD*)words.data(),&actor->originalShader));w.actorPrograms[actor->originalShader]=program;
 std::vector<uint32_t> patched;assert(NorthlightShadowShader::patch(words.data(),words.size(),patched));must(d->CreateVertexShader((DWORD*)patched.data(),&actor->shader));
 const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};must(d->CreateVertexDeclaration(elements,&actor->decl));
 actor->count=h[2]/3;actor->vertices=h[1];actor->type=D3DPT_TRIANGLELIST;actor->indexed=true;actor->snapshot.indexed=true;actor->snapshot.primitiveCount=actor->count;actor->snapshot.vertexCount=h[1];actor->snapshot.indices.resize(h[2]);std::memcpy(actor->snapshot.indices.data(),bytes.data()+20+size_t(h[1])*48,size_t(h[2])*4);
 auto& stream=actor->snapshot.streams[0];stream.stride=48;stream.bytes.assign(bytes.begin()+20,bytes.begin()+20+size_t(h[1])*48);actor->stride[0]=48;
 void* upload=nullptr;must(d->CreateVertexBuffer(UINT(stream.bytes.size()),0,0,D3DPOOL_MANAGED,&actor->stream[0],nullptr));must(actor->stream[0]->Lock(0,0,&upload,0));std::memcpy(upload,stream.bytes.data(),stream.bytes.size());must(actor->stream[0]->Unlock());
 must(d->CreateIndexBuffer(h[2]*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&actor->index,nullptr));must(actor->index->Lock(0,0,&upload,0));std::memcpy(upload,actor->snapshot.indices.data(),size_t(h[2])*4);must(actor->index->Unlock());
 actor->constantUsage=NorthlightShaderConstants::analyze(words.data(),words.size(),2);actor->constantStorage[8]=actor->constantStorage[13]=actor->constantStorage[18]=actor->constantStorage[23]=1;
 float low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};for(unsigned n=0;n<h[1];++n){float p[3];std::memcpy(p,stream.bytes.data()+n*48,12);for(unsigned k=0;k<3;++k){low[k]=std::min(low[k],p[k]);high[k]=std::max(high[k],p[k]);}}
 actor->shared=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(actor->snapshot));
 constexpr unsigned Size=512,Full=Size*2;
 Fgr normal;normal.w=normal.h=Size;normal.format=D3DFMT_A32B32G32R32F;normal.stride=16;normal.bytes.resize(Size*Size*16);for(unsigned i=0;i<Size*Size;++i){const float n[]={0,0,1,1};std::memcpy(normal.bytes.data()+i*16,n,16);}drop(w.normalBuffer);w.normalBuffer=texture(d,normal);
 Fgr depth;depth.w=depth.h=Full;depth.format=D3DFMT_R32F;depth.stride=4;depth.bytes.resize(Full*Full*4);const float z=64.f/(64.f-.1f)*(1.f-.1f/9.f);for(unsigned i=0;i<Full*Full;++i)std::memcpy(depth.bytes.data()+i*4,&z,4);auto* depths=texture(d,depth);
 IDirect3DSurface9* target=nullptr;must(d->CreateRenderTarget(Size,Size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
 must(d->SetRenderTarget(0,w.baselineSurface));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
 const auto render=[&](const char* name){const auto map=cube(w,l,!omitActor);must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));states(d,false);must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));w.pointSlot().selectionGain=1;
  must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));must(d->BeginScene());assert(w.renderPointLighting(depths,nullptr,Full,Full,.1f,64,0,1,1,0,1,0));must(d->EndScene());
  const auto image=readSurface(d,target,16);save(image,output+"/"+name+".fgr");std::vector<float> energy(Size*Size);for(unsigned i=0;i<energy.size();++i){float p[4];std::memcpy(p,image.bytes.data()+i*16,16);energy[i]=-p[0]-p[1]-p[2];assert(std::isfinite(energy[i])&&energy[i]>=-1e-6f);}return std::make_pair(map,energy);};
 const auto housingOnly=render("housing-only");
 const auto pose=[&](float shift){auto& p=*w.replays.back();for(unsigned bone=0;bone<h[3];++bone){float* c=p.constantStorage+4*(31+3*bone);std::fill(c,c+12,0.f);c[0]=c[5]=c[10]=.9f;c[3]=foot[0]-camera[0]-(low[0]+high[0])*.45f+shift;c[7]=foot[1]-camera[1]-(low[1]+high[1])*.45f;c[11]=foot[2]-camera[2]-low[2]*.9f;}
  p.pointBounds={};w.captureMode=WorldRenderer::CaptureFresh;w.shadowSelectionDone=true;for(unsigned n=0;n<8&&!p.pointBounds.valid;++n){w.replayBoundsKick();w.replayBoundsJoin();}assert(p.pointBounds.valid);};
 w.replays.push_back(std::move(actor));pose(0);const auto combined=render("housing-actor");assert(w.pointReplayCount==unsigned(!omitActor));
 w.batches.clear();const auto actorOnly=render("actor-only");w.batches=housing;pose(2);const auto shifted=render("housing-shifted-actor");
 unsigned nearCube=0,nearPixels=0,actorPixels=0,shiftCube=0,shiftPixels=0;for(size_t i=0;i<housingOnly.first.size();++i){nearCube+=combined.first[i]+1e-5f<housingOnly.first[i];shiftCube+=shifted.first[i]+1e-5f<housingOnly.first[i];}
 for(size_t i=0;i<actorOnly.second.size();++i){actorPixels+=actorOnly.second[i]>1e-5f;nearPixels+=combined.second[i]>housingOnly.second[i]+1e-5f;shiftPixels+=shifted.second[i]>housingOnly.second[i]+1e-5f;}
 assert(!nearCube&&!nearPixels&&actorPixels>100&&shiftCube>20&&shiftPixels>20);
 assert(cube(w,l,false)==housingOnly.first);w.replays.clear();const auto removed=render("actor-removed");assert(removed==housingOnly);
 std::printf("CAPTURED_HOUSING_ACTOR_PASS soft=%u humanTriangles=%u bones=%u actorOnlyPixels=%u nearAddedCubeTexels=%u nearAddedPixels=%u shiftedAddedCubeTexels=%u shiftedAddedPixels=%u actorOffExact=1 removalExact=1 historicalActorAdmissionProven=0\n",unsigned(soft),h[2]/3,h[3],actorPixels,nearCube,nearPixels,shiftCube,shiftPixels);std::fflush(stdout);
 for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);depths->Release();target->Release();
}
