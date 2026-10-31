#include "translucent_depth.h"
#include "grass_spell_classifier.h" // exact pure classification section from spell_layer.h
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct DepthSequence {
    NorthlightTranslucentDepth::Frame early;
    bool captured=false;unsigned current=100,snapshot=0,undos=0;
    void draw(bool skinned,bool blended,bool zwrite,unsigned depth){
        const auto z=[&]{return zwrite;};const auto blend=[&]{return blended;};
        const auto src=[] {return 5u;};const auto dst=[] {return 6u;};const auto rgb=[] {return 15u;};
        if(captured&&early.earlyCaptured&&NorthlightTranslucentDepth::isUndo(false,z,blend,src,dst,rgb)){
            captured=false;early.undo();++undos;}
        if(!captured&&early.armed()&&NorthlightTranslucentDepth::shouldResolve(false,z,blend,src,dst,rgb,[&]{return skinned;})&&early.attempt()){
            snapshot=current;captured=true;early.success();}
        if(zwrite&&depth<current)current=depth;
    }
    void composite(){if(!captured){snapshot=current;captured=true;}}
};
int main(){
    using namespace NorthlightSpellLayer;
    DrawFacts grass;grass.family=Family::None;grass.fixedFunction=false;grass.skinned=false;
    grass.worldDomain=grass.zEnable=grass.rt0IsTarget=grass.dsIsWorldDepth=true;
    grass.srcBlend=kSrcAlpha;grass.destBlend=kInvSrcAlpha;
    for(bool pending:{false,true})for(bool zwrite:{false,true})for(bool blend:{false,true}){
        grass.pending=pending;grass.zWrite=zwrite;grass.blend=blend;
        const auto result=classify(grass,15);
        assert(result.kind!=Kind::Defer); // native DetailDoodad is never moved behind WorldComposite
        if(zwrite)assert(result.kind==(blend?Kind::TranslucentZ:Kind::Opaque));
        else assert(result.kind==Kind::Hazard&&result.reason==Reason::Order);
    }
    {DepthSequence s;s.draw(false,false,true,20);s.composite();assert(s.snapshot==20);}
    {DepthSequence s;s.draw(false,false,true,20);s.draw(true,true,true,10);s.composite();assert(s.snapshot==20);}
    {DepthSequence s;s.draw(true,true,true,70);s.draw(false,false,true,20);s.composite();assert(s.snapshot==20&&s.undos==1);}
    // Conditional limitation, NOT evidence that the client issues this order:
    // blended Z-writing grass after an early actor resolve does not undo it.
    {DepthSequence s;s.draw(true,true,true,70);s.draw(false,true,true,20);s.composite();assert(s.snapshot==100&&s.current==20&&s.undos==0);}
    // No-Z grass contributes colour, but the shared composite has only the
    // background's position; BLS cannot establish whether this path is used.
    {DepthSequence s;s.draw(false,true,false,20);s.composite();assert(s.snapshot==100);}
    std::puts("PASS production spell classifier/depth predicates: native grass never deferred; opaque Z receiver included/re-resolved; conditional blended/no-Z limitations explicit");
}
