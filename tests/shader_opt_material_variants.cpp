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
                assert(b.normalOnlyCapture(domain)==((domain==1||domain==3)&&mode!=2));
                b.pomControl[0]=0;assert(b.normalOnlyCapture(domain)==((domain==1||domain==3)&&mode!=2));
            }
            // Each static cache owns its shader references independently.
            for(unsigned i=0;i<8;++i){b.shaders[i]=b.sharedShaders[i]=b.normalShaders[i]=&d.ps;for(unsigned n=0;n<3;++n)d.ps.AddRef();}
            assert(d.ps.refs==25);
        }assert(d.ps.refs==1);
    }
};
int main(){
    ShaderOptMaterialVariantTestAccess::selectionAndCleanup();
    std::puts("MATERIAL_NORMAL_VARIANTS_PASS terrain_grass_normal mode2_wmo_m2_generic separate_static_shader_refs cleanup");return 0;
}
