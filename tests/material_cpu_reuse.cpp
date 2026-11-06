#include "packet_batch_fixture.h"
#include "northlight_mem.h"
#include <chrono>
extern "C" int memcmp(const void*a,const void*b,size_t n){return NorthlightMem::compare(a,b,n);}
static bool bench=false;
static volatile unsigned sink=0;
template<class F>static double timed(F f){const auto start=std::chrono::steady_clock::now();f();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
static void controlChecks(){
    using namespace NorthlightWaterShaderPatch;
    for(unsigned native:{0u,7u,100u,128u,183u,200u})for(bool receiver:{false,true}){
        std::vector<Word> code={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x05000051,dst(2,192),0,0,0,0,0x03000005,dst(8,0),src(1,0),src(2,native),65535},patched;
        unsigned control=0;assert(NorthlightNativeMaterialCapture::patch(code.data(),code.size(),patched,control,true,false,receiver));
        const unsigned count=receiver?1:9;assert(control+count<=192&&!(native>=control&&native<control+count));
        const unsigned actual=std::max(native+1,control+count)-std::min(native,control);
        for(unsigned base=0;base<=192-count;++base)if(!(native>=base&&native<base+count))assert(actual<=std::max(native+1,base+count)-std::min(native,base));
    }
    puts("CPU_CONTROL_SPAN_PASS compact banks native_constants_and_DEF_preserved receiver_and_material");
}
void DisplacementStateTestAccess::cpuReuse(){
    using G=NorthlightAutoDisplacement;
    // The captured admission must preserve every opaque, cutout and terrain case.
    struct AdmissionBackend:LimitBackend {unsigned states=0;G::Admission admission;
        HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE key,DWORD* out)override{++states;*out=key==D3DRS_ZENABLE?admission.z:key==D3DRS_ZWRITEENABLE?admission.write:key==D3DRS_ALPHABLENDENABLE?admission.blend:key==D3DRS_ALPHATESTENABLE?admission.alpha:key==D3DRS_SRCBLEND?admission.src:key==D3DRS_DESTBLEND?admission.dst:admission.func;return D3D_OK;}
        HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT,float*,UINT)override{return E_FAIL;}
    } admissionDevice;
    G admitted(&admissionDevice);rangeProgram(admitted,&admissionDevice.vs,true);admissionDevice.vs.AddRef();admitted.shaders[&admissionDevice.vs].patched=&admissionDevice.vs;G::Args empty{};
    unsigned combinations=0;
    for(unsigned domain=1;domain<=4;++domain)for(unsigned material=1;material<=2;++material)
    for(DWORD z:{0u,1u,2u})for(DWORD write:{0u,1u})for(DWORD blend:{0u,1u})for(DWORD alpha:{0u,1u})
    for(DWORD src:{DWORD(D3DBLEND_ONE),DWORD(D3DBLEND_SRCALPHA)})for(DWORD dst:{DWORD(D3DBLEND_ZERO),DWORD(D3DBLEND_INVSRCALPHA)})for(DWORD func:{DWORD(D3DCMP_GREATER),DWORD(D3DCMP_GREATEREQUAL),DWORD(D3DCMP_EQUAL)}){
        G::Admission a{z,write,blend,alpha,src,dst,func};const bool cutout=blend&&write&&alpha&&src==D3DBLEND_SRCALPHA&&dst==D3DBLEND_INVSRCALPHA&&(func==D3DCMP_GREATER||func==D3DCMP_GREATEREQUAL);
        assert(a.allows(domain,material)==!(!z||(!write&&domain!=1)||(blend&&!cutout&&!(domain==1&&material==2))));++combinations;
        admissionDevice.admission=a;admissionDevice.states=0;auto rejected=admitted.stateRejected;assert(!admitted.prepare(&admissionDevice.vs,domain,material,empty));const auto delta=admitted.stateRejected-rejected;assert(admissionDevice.states==(blend&&write?7u:3u));
        admissionDevice.states=0;rejected=admitted.stateRejected;assert(!admitted.prepare(&admissionDevice.vs,domain,material,empty,&a));assert(admitted.stateRejected-rejected==delta&&!admissionDevice.states);
    }
    if(bench){admissionDevice.admission={1,1,1,1,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DCMP_GREATER};for(unsigned pass=0;pass<7;++pass){double ms[2];for(unsigned j=0;j<2;++j){const unsigned mode=(j+pass)%2;ms[mode]=timed([&]{for(unsigned i=0;i<100000;++i)assert(!admitted.prepare(&admissionDevice.vs,4,1,empty,mode?&admissionDevice.admission:nullptr));})/100000*502;}printf("CPU_GEOMETRY_ADMISSION oldMs=%.6f newMs=%.6f\n",ms[0],ms[1]);}}
    // A refused geometry build still hands off the native immutable source.
    LimitBackend capture;G geometry(&capture);rangeProgram(geometry,&capture.vs,true);geometry.materials[&capture.vs].id=1;capture.vs.AddRef();geometry.shaders[&capture.vs].patched=&capture.vs;geometry.buildsThisFrame=2;
    float vertices[]={1,1,1,0,2,1,1,0,1,2,1,0};G::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};G::CaptureSource source;
    G::Admission state{1,1,0};assert(!geometry.prepare(&capture.vs,4,1,args,&state,&source));assert(source.mesh&&source.declaration==&capture.decl);const auto old=source.mesh;
    vertices[0]=8;geometry.endFrame();geometry.buildsThisFrame=2;assert(!geometry.prepare(&capture.vs,4,1,args,&state,&source));assert(source.mesh&&source.mesh!=old);float x;std::memcpy(&x,source.mesh->streams[0].bytes.data(),4);assert(x==8);std::memcpy(&x,old->streams[0].bytes.data(),4);assert(x==1);
    state.z=0;assert(!geometry.prepare(&capture.vs,4,1,args,&state,&source));assert(!source.mesh&&!source.declaration&&!geometry.prepared()&&geometry.nearestPreparedDepth()==0);
    printf("CPU_DISPLACEMENT_PASS admissionCases=%u source_lifetime rejection_reset\n",combinations);
}
void MaterialCaptureStateTestAccess::cpuReuse(){
    using B=NorthlightAutoBump;
    PacketDevice d;DeviceMirror mirror;ExtensionDevice ext(&d,&mirror);B b(&ext);b.profileTag=19;
    b.setRasterStateSource(&ext,[](void* c,const D3DRENDERSTATETYPE* keys,unsigned count,DWORD* out){return static_cast<ExtensionDevice*>(static_cast<IDirect3DDevice9*>(c))->knownRenderStates(keys,count,out);});
    const D3DRENDERSTATETYPE keys[]={D3DRS_ZENABLE,D3DRS_ZFUNC,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_STENCILENABLE};DWORD values[5];
    for(unsigned i=0;i<5;++i){d.state.render[keys[i]]=20+i;assert(SUCCEEDED(ext.GetRenderState(keys[i],values+i)));}assert(b.readStates(keys,values));for(unsigned i=0;i<5;++i)assert(values[i]==20+i);
    MirrorGuard outer(mirror.gate);
    if(bench)for(unsigned pass=0;pass<7;++pass){double ms[2];for(unsigned j=0;j<2;++j){const unsigned mode=(j+pass)%2;ms[mode]=timed([&]{for(unsigned i=0;i<100000;++i){if(mode)assert(b.readStates(keys,values));else for(unsigned k=0;k<5;++k)assert(SUCCEEDED(ext.GetRenderState(keys[k],values+k)));sink+=values[0];}})/100000*562;}printf("CPU_ADMISSION oldMs=%.6f newMs=%.6f\n",ms[0],ms[1]);}
    B::Vertex vertex;vertex.kind=4;vertex.patched=&d.vs;vertex.floatOnly=true;vertex.constantFirst=0;vertex.constantCount=4;
    B::NativeCapture model;model.shader=&d.ps;model.first=model.constant=20;model.last=24;model.pixelFloatOnly=true;float vertices[]={1,1,1,0,2,1,1,0,1,2,1,0},constants[20]={};
    NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,3,0,1,false},nullptr,D3DFMT_UNKNOWN,vertices,16};NorthlightAutoNormalCache::Lease normal;
    NorthlightAutoDisplacement::CaptureSource source;NorthlightDrawSnapshot::Mesh mesh;assert(b.deferredSnapshots.readUP(&d.decl,args.draw,nullptr,args.format,vertices,16,mesh));source.mesh=b.deferredSnapshots.retain(std::move(mesh));source.declaration=&d.decl;d.decl.AddRef();
    auto record=[&](bool reuse){B::ReplayState saved(&ext,&d.vs,d.state.viewport);return b.recordCapture(args,normal,false,saved,model,vertex,constants,D3DTADDRESS_WRAP,D3DTADDRESS_WRAP,false,reuse?&source:nullptr);};
    assert(record(false));const auto reference=b.deferred.back()->mesh;assert(record(true));assert(b.deferred.back()->mesh==source.mesh);assert(reference->streams[0].bytes==source.mesh->streams[0].bytes);b.clearDeferred();
    for(unsigned cycle=0;cycle<4;++cycle){for(unsigned i=0;i<286;++i){vertex.floatOnly=model.pixelFloatOnly=(i%2==0);vertex.constantFirst=i%16;vertex.constantCount=4+i%8;d.state.vf.fill(float(i+cycle));assert(record(true));const auto& packet=*b.deferred.back();assert(packet.vertexFirst==vertex.constantFirst&&packet.vertexCount==vertex.constantCount);}
        b.clearDeferred();assert(b.sparePackets.size()==286);for(auto& packet:b.sparePackets){assert(!packet->mesh&&!packet->normal&&!packet->declaration&&!packet->vertex&&!packet->pixel&&!packet->gpuIndex&&packet->textures.empty());for(auto vb:packet->gpuVertex)assert(!vb);}
    }
    vertex.floatOnly=model.pixelFloatOnly=true;vertex.constantFirst=0;vertex.constantCount=4;
    if(bench)for(unsigned candidate=0;candidate<2;++candidate)for(unsigned pass=0;pass<7;++pass){double ms[2];size_t allocations[2];for(unsigned j=0;j<2;++j){const unsigned mode=(j+pass)%2;const auto before=allocationCount;
        ms[mode]=timed([&]{for(unsigned frame=0;frame<120;++frame){if(candidate==0&&!mode)b.sparePackets.clear();for(unsigned i=0;i<286;++i)assert(record(candidate==0||mode));b.clearDeferred();if(candidate==1&&!mode)b.deferredSnapshots.clearFrame();}})/120;allocations[mode]=allocationCount-before;}
        printf("CPU_%s oldMs=%.6f newMs=%.6f oldAlloc=%zu newAlloc=%zu\n",candidate?"SNAPSHOT":"PACKETS",ms[0],ms[1],allocations[0],allocations[1]);}
    // No allocation or retained COM resources during clear; trim and pressure release CPU storage.
    assert(record(true));failAllocationAfter=0;b.clearDeferred();assert(failAllocationAfter==0);failAllocationAfter=-1;
    b.trimMemory();assert(b.sparePackets.empty());assert(record(true));b.clearDeferred();b.setMemoryPressure(true);assert(b.sparePackets.empty());assert(record(true));b.clearDeferred();assert(b.sparePackets.empty());b.setMemoryPressure(false);assert(record(true));b.clearDeferred();b.reset();assert(b.sparePackets.empty());
    puts("CPU_PACKETS_PASS recycled_constants resource_release bounded_pool trim pressure snapshot_equivalence");
}
int main(int argc,char**){bench=argc>1;controlChecks();DisplacementStateTestAccess::cpuReuse();MaterialCaptureStateTestAccess::cpuReuse();puts("MATERIAL_CPU_REUSE_PASS");}
