#define main prior_material_bindings_main
#include "test_material_batch_bindings.cpp"
#undef main
struct DepthDevice:MaterialBindingDevice {
    float depth=1;unsigned index=0;std::vector<bool> passed;
    HRESULT STDMETHODCALLTYPE Clear(DWORD,const D3DRECT*,DWORD flags,D3DCOLOR,float z,DWORD)override {if(flags&D3DCLEAR_ZBUFFER)depth=z;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE,UINT,const void*,UINT)override {
        const float z[]={.4f,.8f,.3f,.3f};assert(index<4);
        const auto op=state.render[D3DRS_ZFUNC];bool pass=op==D3DCMP_EQUAL?z[index]==depth:op==D3DCMP_LESS?z[index]<depth:op==D3DCMP_LESSEQUAL?z[index]<=depth:false;
        passed.push_back(pass);if(pass&&state.render[D3DRS_ZWRITEENABLE])depth=z[index];++index;return record(true);
    }
};
void MaterialCaptureStateTestAccess::privateDepth(){
 for(DWORD op:{DWORD(D3DCMP_LESS),DWORD(D3DCMP_LESSEQUAL),DWORD(D3DCMP_EQUAL),DWORD(D3DCMP_GREATER),DWORD(D3DCMP_GREATEREQUAL),DWORD(D3DCMP_NOTEQUAL),DWORD(D3DCMP_NEVER),DWORD(D3DCMP_ALWAYS)}){
  assert(NorthlightAutoBump::privateDepthSupported(op)==(op==D3DCMP_LESS||op==D3DCMP_LESSEQUAL||op==D3DCMP_EQUAL));
  if(!NorthlightAutoBump::privateDepthSupported(op))continue;
  MaterialBindingDevice d;d.state.render[D3DRS_ZFUNC]=op;
  {NorthlightAutoBump::ReplayState saved(&d,&d.vs,d.state.viewport);saved.privateEqual=op==D3DCMP_EQUAL;assert(SUCCEEDED(saved.apply()));assert(d.state.render[D3DRS_ZFUNC]==(op==D3DCMP_EQUAL?DWORD(D3DCMP_LESSEQUAL):op));}
  assert(d.state.render[D3DRS_ZFUNC]==op);
 }
 {
  MaterialBindingDevice d;d.state.render[D3DRS_ZFUNC]=D3DCMP_EQUAL;
  {NorthlightAutoBump::ReplayState saved(&d,&d.vs,d.state.viewport);assert(SUCCEEDED(saved.apply()));assert(d.state.render[D3DRS_ZFUNC]==D3DCMP_EQUAL);}
  assert(d.state.render[D3DRS_ZFUNC]==D3DCMP_EQUAL);
 }
 {
  struct FailingDepth:MaterialBindingDevice {HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE k,DWORD v)override{if(k==D3DRS_ZFUNC&&v==D3DCMP_LESSEQUAL)return E_FAIL;return MaterialBindingDevice::SetRenderState(k,v);}} d;
  d.state.render[D3DRS_ZFUNC]=D3DCMP_EQUAL;
  {NorthlightAutoBump::ReplayState saved(&d,&d.vs,d.state.viewport);saved.privateEqual=true;assert(FAILED(saved.apply()));}
  assert(d.state.render[D3DRS_ZFUNC]==D3DCMP_EQUAL);
 }
 DepthDevice d;d.state.render[D3DRS_ZFUNC]=D3DCMP_EQUAL;const auto before=d.state;
 {
  NorthlightAutoBump b(&d);b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();
  auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);
  const DWORD funcs[]={D3DCMP_EQUAL,D3DCMP_LESS,D3DCMP_LESSEQUAL,D3DCMP_EQUAL};
  for(auto func:funcs){auto p=std::make_unique<NorthlightAutoBump::Deferred>();p->mesh=mesh;p->tag=19;p->vertex=&d.vs;p->vertex->AddRef();p->pixel=&d.ps;p->pixel->AddRef();p->declaration=&d.decl;p->declaration->AddRef();p->vertexCount=0;p->vertexFloatOnly=p->pixelFloatOnly=true;p->viewport={0,0,64,64,0,1};std::fill(p->states,p->states+14,0);p->states[0]=TRUE;p->states[4]=15;std::fill(p->raster,p->raster+NorthlightAutoBump::DeferredRasterCount,0);p->raster[1]=TRUE;p->raster[2]=func;b.deferred.push_back(std::move(p));}
  assert(b.finishCaptures());assert((d.passed==std::vector<bool>{true,false,true,true}));assert(d.state==before);
 }
 assert(d.state==before);std::puts("PRIVATE_DEPTH_PASS equal_first=1 behind_rejected=1 nearer=1 equal_repeated=1 state_restored=1");
}
int main(){MaterialCaptureStateTestAccess::privateDepth();}
