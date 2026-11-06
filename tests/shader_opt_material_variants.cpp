// Reuse the existing device mocks; this test creates no graphics device.
#define main displacement_state_reuse_main
#include "displacement_state_reuse.cpp"
#undef main

struct ShaderOptMaterialVariantTestAccess {
    static void selectionAndCleanup(){
        CaptureBackend d;{
            NorthlightAutoBump b(&d);
            for(unsigned domain=1;domain<=4;++domain)for(unsigned mode:{1u,2u,3u}){
                b.modes[domain-1]=mode;
                assert(b.normalOnlyCapture(domain)==((domain==1||domain==3||domain==4)&&mode!=2));
                b.pomControl[0]=0;assert(b.normalOnlyCapture(domain)==((domain==1||domain==3||domain==4)&&mode!=2));
            }
            // Each static cache owns its shader references independently.
            for(unsigned i=0;i<8;++i){b.shaders[i]=b.sharedShaders[i]=b.normalShaders[i]=&d.ps;for(unsigned n=0;n<3;++n)d.ps.AddRef();}
            assert(d.ps.refs==25);
        }assert(d.ps.refs==1);
    }
    static void nativeVariants(){
        struct Source:CaptureShader<IDirect3DPixelShader9> {
            HRESULT STDMETHODCALLTYPE GetFunction(void* out,UINT* bytes)override{
                using namespace NorthlightWaterShaderPatch;
                const DWORD code[]={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x02000001,dst(8,0),src(2,0),65535};
                ++reads;if(out){assert(*bytes>=sizeof code);std::memcpy(out,code,sizeof code);}*bytes=sizeof code;return D3D_OK;
            }
        } source;
        struct Device:CaptureBackend {
            bool fail=false;std::vector<DWORD> code;
            HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* words,IDirect3DPixelShader9** out)override{
                code.clear();for(auto* p=words;;++p){code.push_back(*p);if(*p==65535)break;}
                *out=fail?nullptr:&ps;if(*out)ps.AddRef();return fail?E_FAIL:D3D_OK;
            }
        } d;
        {
            NorthlightAutoBump b(&d);
            for(bool alpha:{false,true}){
                auto* generic=b.modelCapture(&source,alpha);assert(generic);const auto genericCode=d.code;
                auto* normal=b.modelCapture(&source,alpha,false,true);assert(normal&&normal!=generic&&normal->deferredSafe);
                assert(d.code.size()<genericCode.size());const auto reads=source.reads;
                assert(b.modelCapture(&source,alpha,false,true)==normal&&source.reads==reads);
                assert(b.modelCapture(&source,alpha)==generic&&source.reads==reads);
                auto* shared=b.modelCapture(&source,alpha,true);assert(shared&&shared!=normal);
                assert(b.modelCapture(&source,alpha,true,true)==shared);
            }
            assert(source.refs==7&&d.ps.refs==7);
        }
        assert(source.refs==1&&d.ps.refs==1);
        {
            NorthlightAutoBump b(&d);d.fail=true;
            assert(!b.modelCapture(&source,true,false,true));const auto reads=source.reads;d.fail=false;
            assert(!b.modelCapture(&source,true,false,true)&&source.reads==reads);
            assert(b.modelCapture(&source,true)); // Specialized failure retains the generic fallback.
        }
        assert(source.refs==1&&d.ps.refs==1);
    }
};
int main(){
    ShaderOptMaterialVariantTestAccess::selectionAndCleanup();
    ShaderOptMaterialVariantTestAccess::nativeVariants();
    std::puts("MATERIAL_NORMAL_VARIANTS_PASS terrain_grass_m2_normal mode2_wmo_generic separate_static_shader_refs cleanup");return 0;
}
