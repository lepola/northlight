#include "packet_batch_fixture.h"
#include "northlight_mem.h"
#include <chrono>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
struct AdmissionDevice:PacketDevice{int fail=-1;unsigned calls=0;HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{++calls;if(int(key)==fail)return E_FAIL;return PacketDevice::GetRenderState(key,out);}};
static unsigned run=0,scenario=0;
void MaterialCaptureStateTestAccess::admissionBatch(){
 for(unsigned exclusive=0;exclusive<2;++exclusive)for(unsigned test=0;test<9;++test){if(run&&(exclusive!=0||test!=scenario))continue;AdmissionDevice d;DeviceMirror mirror;ExtensionDevice ext(&d,&mirror);NorthlightAutoBump b(&ext);if(exclusive)mirror.gate.exclusive.store(true);b.setRasterStateSource(&ext,[](void* c,const D3DRENDERSTATETYPE* k,unsigned n,DWORD* out){return static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(c))->knownRenderStates(k,n,out);});
  NorthlightAutoBump::ReplayState initial(&ext,&d.vs,d.state.viewport);for(unsigned i=0;i<14;++i)d.state.render[unsigned(initial.states[i].key)]=100+i;for(unsigned i=0;i<14;++i)if(i!=0&&i!=4){DWORD value;assert(SUCCEEDED(ext.GetRenderState(initial.states[i].key,&value)));}
  const auto answerBefore=mirror.answered;d.calls=0;if(test==1)mirror.invalidate();if(test==2)mirror.recording=true;if(test==3)mirror.rawDepth=1;if(test==4){mirror.renderStateKnown[initial.states[7].key]=false;d.fail=initial.states[7].key;}if(test==5)mirror.renderStateKnown[initial.states[7].key]=false;if(test==6){mirror.renderStateKnown[initial.states[7].key]=false;d.state.render[unsigned(initial.states[7].key)]=333;}if(test==7){AdmissionDevice other;b.setRasterStateSource(&other,[](void*,const D3DRENDERSTATETYPE*,unsigned,DWORD*){assert(false);return true;});assert(!b.rasterReader);}if(test==8)assert(SUCCEEDED(ext.SetRenderState(initial.states[7].key,333)));
  unsigned repeats=run?2000:1;for(unsigned iteration=0;iteration<repeats;++iteration){if(run&&test==5)mirror.renderStateKnown[initial.states[7].key]=false;NorthlightAutoBump::ReplayState saved(&ext,&d.vs,d.state.viewport);saved.states[0].value=1;saved.states[4].value=15;
#if defined(ADMISSION_BASELINE)
  bool ok=true;for(unsigned i=0;i<saved.stateCount;++i){if(i==0||i==4)continue;auto& state=saved.states[i];if(FAILED(ext.GetRenderState(state.key,&state.value))){ok=false;break;}}
#else
  const bool ok=b.readAdmissionStates(saved);
#endif
  if(test==4){assert(!ok);assert(saved.states[8].value==0);}else{assert(ok&&saved.states[0].value==1&&saved.states[4].value==15);for(unsigned i=0;i<14;++i)if(i!=0&&i!=4)assert(saved.states[i].value==d.state.render[unsigned(saved.states[i].key)]);}}
  if(!run){if(test==0||test==7)assert(d.calls==0&&mirror.answered-answerBefore==12);if(test==1||test==2||test==3)assert(d.calls==12);if(test==4||test==5||test==6)assert(d.calls==1);}
 }
 if(!run)for(unsigned mode=0;mode<2;++mode)for(unsigned test=0;test<6;++test){
  struct Capture:CaptureBackend{unsigned targetQueries=0;unsigned mode=0;HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index,IDirect3DSurface9** out)override{if(!index)++targetQueries;return CaptureBackend::GetRenderTarget(index,out);}
   HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{if(mode==3&&key==D3DRS_FOGENABLE)return E_FAIL;if(mode==1&&key==D3DRS_ALPHATESTENABLE){*out=1;return D3D_OK;}if(mode==2&&key==D3DRS_ALPHABLENDENABLE){*out=1;return D3D_OK;}return CaptureBackend::GetRenderState(key,out);}}d;
  d.mode=test;DeviceMirror mirror;ExtensionDevice ext(&d,&mirror);NorthlightAutoBump b(&ext);setup(b,d,1);if(mode)b.setRasterStateSource(&ext,[](void* c,const D3DRENDERSTATETYPE* k,unsigned n,DWORD* out){return static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(c))->knownRenderStates(k,n,out);});
  if(test==4)mirror.recording=true;if(test==5)mirror.rawDepth=1;
  if(mode&&test==0){NorthlightAutoBump::ReplayState warm(&ext,&d.vs,D3DVIEWPORT9{});for(unsigned i=0;i<14;++i)if(i!=0&&i!=4){DWORD value;assert(SUCCEEDED(ext.GetRenderState(warm.states[i].key,&value)));}}
  const auto before=mirror.answered;unsigned draws=0;b.capture(&d.vs,[&]{++draws;return D3D_OK;});assert(!draws);assert(d.pixelQueries==1);assert(d.targetQueries==(test==0||test==4||test==5?1u:0u));assert(b.invisibleRejected==0&&b.rangeRejected==0);
  if(mode&&test==0)assert(mirror.answered-before>=12);}
}
int main(int argc,char**argv){if(argc>1){run=1;scenario=std::atoi(argv[1]);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<80;++i)MaterialCaptureStateTestAccess::admissionBatch();std::printf("%.3f\n",std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}else{MaterialCaptureStateTestAccess::admissionBatch();puts("MATERIAL_ADMISSION_BATCH_PASS exact state values, all-or-nothing fallback, invalidation, recording, raw, injected failure, changed state, gate modes");}}
