#include "epoch_guard_fixture.h"
#include "northlight_mem.h"
#include <type_traits>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
using namespace NorthlightConstantEpoch;
static_assert(!std::is_constructible<TrustedSource,void*,Reader>::value,"only the mirror may certify a source");
static_assert(!std::is_constructible<TrustedSource,void*,Reader,KnownSpan>::value,"only the mirror may certify a span");
constexpr size_t priorSize=(offsetof(Stamp,valid)+1+alignof(Stamp)-1)/alignof(Stamp)*alignof(Stamp);
static_assert(sizeof(Stamp)==priorSize,"the certificate must fit the original padding");
struct Forged {ExtensionDevice* ext;unsigned calls=0;bool mutate=false;};
static bool forged(void* context,Stamp& out){auto& x=*static_cast<Forged*>(context);++x.calls;const bool ok=x.ext->constantStamp(out);out.held=true;if(x.mutate){x.mutate=false;const float v[4]={42,0,0,0};assert(SUCCEEDED(x.ext->SetVertexShaderConstantF(0,v,1)));}return ok;}
void DisplacementStateTestAccess::guardedChecks(){for(unsigned mode=0;mode<2;++mode){Backend backend;DeviceMirror mirror;if(mode)mirror.gate.exclusive.store(true);ExtensionDevice ext(&backend,&mirror);NorthlightAutoDisplacement g(&ext);Stamp stamp;stamp.requestHeld=true;assert(ext.constantStamp(stamp)&&!stamp.held);{MirrorGuard held(mirror.gate,MirrorSite::Device);stamp.requestHeld=true;assert(ext.constantStamp(stamp)&&stamp.held);}
 float span[4]={-7,-7,-7,-7};Stamp spanStamp;spanStamp.reset=987;assert(!ext.knownVertexConstantSpan(0,1,span,spanStamp)&&span[0]==-7&&spanStamp.reset==987);
 {MirrorGuard held(mirror.gate,MirrorSite::Device);float fetched[4];assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,fetched,1)));assert(ext.knownVertexConstantSpan(0,1,span,spanStamp)&&span[0]==backend.bank[0]&&spanStamp.valid);mirror.vsFloatKnown[0]=false;span[0]=-7;spanStamp.reset=987;assert(!ext.knownVertexConstantSpan(0,1,span,spanStamp)&&span[0]==-7&&spanStamp.reset==987);mirror.recording=true;assert(!ext.knownVertexConstantSpan(0,1,span,spanStamp));mirror.recording=false;mirror.rawDepth=1;assert(!ext.knownVertexConstantSpan(0,1,span,spanStamp));mirror.rawDepth=0;}
 g.setTrustedConstantEpochSource(ext.trustedConstantSource());auto read=[&]{return DisplacementStateTestAccess::read(g);};
 {MirrorGuard held(mirror.gate,MirrorSite::Device);assert(read()&&certified(g));assert(read()&&certified(g));float value[4]={11,12,13,14};assert(SUCCEEDED(ext.SetVertexShaderConstantF(64,value,1)));assert(read()&&values(g)[64*4]==11&&certified(g));
  mirror.invalidate();backend.mutate=&ext;assert(read()&&!certified(g));assert(read()&&certified(g)&&values(g)[64*4]==21);}
 {MirrorGuard held(mirror.gate,MirrorSite::Device);float value[4]={31,32,33,34};assert(SUCCEEDED(ext.SetVertexShaderConstantF(64,value,1)));mirror.vsFloatKnown[64]=false;backend.mutate=&ext;assert(read()&&!certified(g));assert(read()&&certified(g)&&values(g)[64*4]==21);
  assert(SUCCEEDED(ext.SetVertexShaderConstantF(64,value,1)));mirror.vsFloatKnown[64]=false;backend.fail=true;assert(!read()&&!certified(g));backend.fail=false;assert(read()&&certified(g)&&values(g)[64*4]==31);}
 {ExtensionDevice::RawScope raw(ext);assert(read()&&!certified(g));}assert(read()&&certified(g));mirror.recording=true;mirror.invalidate();assert(read()&&!certified(g));mirror.recording=false;mirror.invalidate();assert(read()&&certified(g));
 Forged f{&ext};g.setConstantEpochSource(&f,forged);assert(read()&&certified(g));f.calls=0;f.mutate=true;{MirrorGuard held(mirror.gate,MirrorSite::Device);assert(read()&&f.calls==2&&!certified(g));}assert(read()&&certified(g)&&values(g)[0]==42);
 float v[4]={9,0,0,0};assert(SUCCEEDED(ext.SetVertexShaderConstantF(0,v,1)));backend.fail=true;mirror.invalidate();assert(!read()&&!certified(g));backend.fail=false;assert(read()&&certified(g)&&values(g)[0]==9);g.reset();assert(read()&&certified(g));
 g.setTrustedConstantEpochSource(ext.trustedConstantSource());{MirrorGuard held(mirror.gate,MirrorSite::Device);float a[4]={5,6,7,8},b[4]={15,16,17,18};assert(SUCCEEDED(ext.SetVertexShaderConstantF(20,a,1)));assert(DisplacementStateTestAccess::read(g,false)&&values(g)[20*4]==5);assert(SUCCEEDED(ext.SetVertexShaderConstantF(0,b,1)));assert(DisplacementStateTestAccess::read(g,false)&&values(g)[20*4]==5);assert(SUCCEEDED(ext.SetVertexShaderConstantF(21,b,1)));assert(DisplacementStateTestAccess::read(g,false)&&values(g)[21*4]==15);}
 Backend otherBackend;DeviceMirror otherMirror;ExtensionDevice other(&otherBackend,&otherMirror);g.setTrustedConstantEpochSource(other.trustedConstantSource());backend.bank[0]=23;mirror.invalidate();assert(read()&&!certified(g)&&values(g)[0]==23);
 std::printf("MATERIAL_EPOCH_GUARD_PASS stamp_size=%zu held unlocked trusted generic_forged reentry mutation raw recording failure reset unrelated_write current_range mismatched_device\n",sizeof(Stamp));}}
int main(){DisplacementStateTestAccess::guardedChecks();}
