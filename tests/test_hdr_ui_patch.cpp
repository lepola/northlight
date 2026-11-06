#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include "hdr_ui_patch.h"

using NorthlightHdrUi::Word;
using Words=std::vector<Word>;
static Word dst(unsigned type,unsigned index,unsigned mask=15){return NorthlightHdrUi::reg(0x80000000u|(mask<<16),type,index);}
static Word src(unsigned type,unsigned index){return NorthlightHdrUi::reg(0x80e40000u,type,index);}
static void mov(Words& code,Word to,Word from){code.insert(code.end(),{0x02000001u,to,from});}
int main(){
    unsigned cases=0;
    for(Word version:{0xffff0200u,0xffff0300u}){
        Words transfer{version};mov(transfer,dst(8,0),src(1,0));transfer.push_back(65535);
        for(unsigned outputs=0;outputs<8;++outputs){
            Words native{version};mov(native,dst(8,0),src(2,0));
            // Distinct partial writes exercise preservation of masks/modifiers,
            // with auxiliary sources using the original scratch registers.
            for(unsigned n=1;n<4;++n)if(outputs&(1u<<(n-1))){
                mov(native,dst(0,n),src(2,n));
                mov(native,dst(8,n,7)|0x00100000u,src(0,n));
                mov(native,dst(8,n,8),src(2,n));
            }
            native.push_back(65535);
            for(unsigned linear=0;linear<4;++linear){
                Words patched;NorthlightHdrUi::Info info;
                const bool accepted=NorthlightHdrUi::patch(native.data(),native.size(),transfer.data(),transfer.size(),80,false,patched,&info,false,linear);
                const bool collision=linear&&(outputs&(1u<<(linear-1)));
                assert(accepted==!collision);
                if(!accepted){assert(patched.empty());continue;}
                std::vector<NorthlightHdrUi::Instruction> instructions;unsigned slots=0;
                assert(NorthlightHdrUi::parse(patched.data(),patched.size(),instructions,slots));
                Words auxiliary;
                for(const auto& instruction:instructions)if(instruction.op==1){
                    const auto to=patched[instruction.at+1];
                    if(NorthlightHdrUi::type(to)==8&&NorthlightHdrUi::index(to)&&NorthlightHdrUi::index(to)!=linear)
                        auxiliary.insert(auxiliary.end(),patched.begin()+instruction.at,patched.begin()+instruction.at+instruction.count);
                }
                Words expected;
                for(unsigned n=1;n<4;++n)if(outputs&(1u<<(n-1))){
                    mov(expected,dst(8,n,7)|0x00100000u,src(0,n));mov(expected,dst(8,n,8),src(2,n));
                }
                assert(auxiliary==expected);++cases;
            }
            // Depth writes and incomplete primary output remain unsupported.
            Words bad=native;bad.insert(bad.end()-1,{0x02000001u,dst(9,0,1),src(2,0)});
            Words output;
            assert(!NorthlightHdrUi::patch(bad.data(),bad.size(),transfer.data(),transfer.size(),80,false,output));
            bad=native;bad[2]=dst(8,0,7);
            assert(!NorthlightHdrUi::patch(bad.data(),bad.size(),transfer.data(),transfer.size(),80,false,output));
        }
    }
    std::printf("HDR_UI_PATCH_PASS accepted=%u auxiliary_outputs_exact=1 collision_depth_incomplete_rejected=1\n",cases);
    return 0;
}
