#include "sky_composition.h"
#include "sky_material_catalog.h"
#include "sky_alpha_patch.h"
#include <cassert>
#include <cmath>
#include <iterator>
int main(){
    using namespace NorthlightSkyComposition;
    unsigned backgrounds=0,foregrounds=0;
    for(const auto& m:Materials){assert(materialRole(m.hash,m.format,m.width,m.height,m.primitives)==m.role);
        backgrounds+=m.role==Role::Background;foregrounds+=m.role==Role::Foreground;}
    assert(backgrounds&&foregrounds);assert(materialRole(0,0,0,0,0)==Role::Unknown);
    for(float cloud:{0.f,.25f,1.f})for(float transition:{0.f,.5f,1.f}){
        Sample s{{.2f,.3f,.4f},1};const float fog[]={.7f,.6f,.5f},next[]={.4f,.3f,.2f};
        over(s,Role::Foreground,fog,cloud);over(s,Role::Background,next,transition);
        const float surface=.45f,halo=.12f;
        for(unsigned k=0;k<3;++k){
            const float initial=.2f+.1f*k;
            const float bare=(initial*(1-cloud)+fog[k]*cloud)*(1-transition)+next[k]*transition;
            const auto body=[&](float b){return (b+surface)*(1-halo)+halo;};
            const float expected=(body(initial)*(1-cloud)+fog[k]*cloud)*(1-transition)+body(next[k])*transition;
            assert(std::fabs(bare+contribution(surface,halo,s.background[k],s.transmission)-expected)<1e-6f);
        }
    }
    const std::uint32_t code[]={0xffff0200,0x0200001f,0x80000000,0x900f0000,0x02000001,0x800f0800,0x90e40000,0xffff};
    for(unsigned function=0;function<=8;++function){std::vector<std::uint32_t> patched;unsigned control=99;
        assert(patchOutput(code,std::size(code),function,patched,control));
        std::vector<NorthlightHdrUi::Instruction> ops;unsigned slots=0;
        assert(NorthlightHdrUi::parse(patched.data(),patched.size(),ops,slots));assert(control<32);}
    std::vector<std::uint32_t> patched;unsigned control=0;assert(!patchOutput(code,3,8,patched,control));
}
