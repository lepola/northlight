#include "patch_terrain_shadow.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace NorthlightTerrainShadow;
// Token builders for ps_2_0/ps_3_0 bytecode (D3D9 shader token format).
static Word instr(unsigned op,unsigned operands){return op|(operands<<24);}
static Word reg(unsigned type,unsigned index){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|index;}
static Word dst(unsigned type,unsigned index,unsigned mask=15){return reg(type,index)|(mask<<16);}
static Word src(unsigned type,unsigned index,unsigned swizzle=0xe4){return reg(type,index)|(swizzle<<16);}
static Word f(float v){Word w;std::memcpy(&w,&v,4);return w;}
enum {Temp=0,Input=1,Const=2,Sampler=10,ColorOut=8};
enum {XXXX=0x00,YYYY=0x55,ZZZZ=0xaa,WWWW=0xff,XYZW=0xe4};
static void def(std::vector<Word>& p,unsigned c,float x,float y,float z,float w){p.insert(p.end(),{instr(81,5),reg(Const,c),f(x),f(y),f(z),f(w)});}
static void dclTexcoord(std::vector<Word>& p,unsigned index,unsigned reg_){p.insert(p.end(),{instr(31,2),0x80000000u|5u|(index<<16),dst(Input,reg_,3)});}
static void dclSampler(std::vector<Word>& p,unsigned s){p.insert(p.end(),{instr(31,2),0x80000000u|(2u<<27),dst(Sampler,s)});}
static void texld(std::vector<Word>& p,unsigned r,unsigned v,unsigned s){p.insert(p.end(),{instr(66,3),dst(Temp,r),src(Input,v),src(Sampler,s)});}

// Terrain1-like: S = r4.w straight from the packed alpha map sampler.
static std::vector<Word> terrain1(bool oneLiteral=true,bool predicated=false,Word version=0xffff0300){
    std::vector<Word> p{version};
    def(p,0,.3f,.7f,oneLiteral?1.f:.5f,0);
    dclTexcoord(p,0,0);dclTexcoord(p,6,6);dclSampler(p,0);dclSampler(p,4);
    p.insert(p.end(),{instr(1,2),dst(Temp,5,1),src(Temp,4,WWWW)});          // mov r5.x, r4.w  (earlier meaning of r4.w)
    texld(p,0,0,0);texld(p,4,6,4);
    p.insert(p.end(),{instr(5,3),dst(Temp,1,7),src(Temp,4,WWWW),src(Temp,0)}); // mul r1.xyz, r4.w, r0  (specular*shadow)
    p.insert(p.end(),{instr(4,4)|(predicated?0x10000000u:0),dst(Temp,0,8),src(Temp,4,WWWW),src(Const,0,XXXX),src(Const,0,YYYY)}); // mad r0.w, r4.w, 0.3, 0.7
    p.insert(p.end(),{instr(5,3),dst(Temp,0,7),src(Temp,0),src(Temp,0,WWWW)}); // mul r0.xyz, r0, r0.w
    p.insert(p.end(),{instr(1,2),dst(Temp,4,8),src(Temp,1,XXXX)});          // mov r4.w, r1.x   (register reuse after)
    p.insert(p.end(),{instr(2,3),dst(Temp,0,7),src(Temp,0),src(Temp,4,WWWW)}); // add r0.xyz, r0, r4.w  (must stay)
    p.insert(p.end(),{instr(1,2),dst(ColorOut,0),src(Temp,0)});
    p.push_back(0xffff);return p;
}
// Terrain3-like: S = r3.x = min(dynamic, baked) or baked, chosen by if/else.
static std::vector<Word> terrain3(){
    std::vector<Word> p{0xffff0300};
    def(p,0,1.f,0,0,0);def(p,2,0,0,.3f,.7f);
    dclTexcoord(p,6,6);dclSampler(p,4);dclSampler(p,5);
    texld(p,1,6,4);texld(p,2,6,5);
    p.insert(p.end(),{instr(40,1),0x80000000u|(0xeu<<28)});                 // if b0
    p.insert(p.end(),{instr(10,3),dst(Temp,3,1),src(Temp,2,XXXX),src(Temp,1,WWWW)}); // min r3.x, r2.x, r1.w
    p.push_back(instr(42,0));                                               // else
    p.insert(p.end(),{instr(1,2),dst(Temp,3,1),src(Temp,1,WWWW)});          // mov r3.x, r1.w
    p.push_back(instr(43,0));                                               // endif
    p.insert(p.end(),{instr(5,3),dst(Temp,4,7),src(Temp,3,XXXX),src(Temp,1)}); // mul r4.xyz, r3.x, r1
    p.insert(p.end(),{instr(4,4),dst(Temp,1,1),src(Temp,3,XXXX),src(Const,2,ZZZZ),src(Const,2,WWWW)}); // mad r1.x, r3.x, 0.3, 0.7
    p.insert(p.end(),{instr(5,3),dst(Temp,0,7),src(Temp,4),src(Temp,1,XXXX)});
    p.insert(p.end(),{instr(1,2),dst(ColorOut,0),src(Temp,0)});
    p.push_back(0xffff);return p;
}
static unsigned differences(const std::vector<Word>& a,const std::vector<Word>& b){
    assert(a.size()==b.size());unsigned n=0;for(size_t i=0;i<a.size();++i)n+=a[i]!=b[i];return n;
}
int main(int argc,char** argv){
    if(argc>1){
        for(int i=1;i<argc;++i){
            std::ifstream in(argv[i],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
            std::vector<Word> code(bytes.size()/4);std::memcpy(code.data(),bytes.data(),code.size()*4);
            std::vector<Word> out;PatchInfo info;
            if(patch(code.data(),code.size(),out,&info)){
                unsigned constant=0;std::vector<Word> gradual;
                if(!weighted(code.data(),code.size(),gradual,constant)){
                    std::fprintf(stderr,"WEIGHTED_REFUSED %s major=%u instructions=%u\n",argv[i],info.major,info.instructions);
                    return 2;
                }
                std::ofstream w(std::string(argv[i])+".weighted",std::ios::binary);w.write((const char*)gradual.data(),gradual.size()*4);
                std::printf("TERRAIN_SHADOW %u %u.%u r%u.%u one=c%u.%u %s\n",info.replaced,info.major,info.minor,info.shadowRegister,info.shadowComponent,unsigned(info.oneConstant),info.oneComponent,argv[i]);
                std::ofstream o(std::string(argv[i])+".patched",std::ios::binary);o.write((const char*)out.data(),out.size()*4);
            } else std::printf("TERRAIN_SHADOW -1 %u.%u - - %s\n",(code.empty()?0u:(code[0]>>8)&255),(code.empty()?0u:code[0]&255),argv[i]);
        }
        return 0;
    }
    std::vector<Word> out;PatchInfo info;
    auto t1=terrain1();assert(patch(t1.data(),t1.size(),out,&info));
    assert(info.replaced==2&&info.shadowRegister==4&&info.shadowComponent==3&&info.oneConstant==0&&info.oneComponent==2);
    assert(out.size()==t1.size()&&differences(t1,out)==2&&out[0]==t1[0]&&out.back()==0xffff);
    const Word one=src(Const,0,ZZZZ);
    // Only the specular multiply and the diffuse scale read the literal now;
    // the earlier r4.w meaning and the reuse after the scale are untouched.
    unsigned literalReads=0;for(Word w:out)literalReads+=w==one;assert(literalReads==2);
    for(size_t i=0;i<t1.size();++i)if(t1[i]!=out[i]){assert(t1[i]==src(Temp,4,WWWW)&&out[i]==one);}
    // Fail closed.
    auto noOne=terrain1(false);assert(patch(noOne.data(),noOne.size(),out,&info)&&info.oneConstant==-1&&info.replaced==2); // folded: add+nop, mov+nop
    auto predicated=terrain1(true,true);assert(!patch(predicated.data(),predicated.size(),out,&info));
    auto ps14=terrain1(true,false,0xffff0104);assert(!patch(ps14.data(),ps14.size(),out,&info));
    auto ps20=terrain1(true,false,0xffff0200);assert(patch(ps20.data(),ps20.size(),out,&info)&&info.major==2&&info.replaced==2);
    auto truncated=terrain1();truncated.pop_back();assert(!patch(truncated.data(),truncated.size(),out,&info));
    auto noShadow=terrain1();for(auto& w:noShadow)if(w==src(Const,0,XXXX))w=src(Const,0,ZZZZ);assert(!patch(noShadow.data(),noShadow.size(),out,&info));
    std::vector<Word> empty{0xffff0300,0xffff};assert(!patch(empty.data(),empty.size(),out,&info));
    // Both branch-free and Terrain3's combined branch result support a
    // runtime handoff weight. Insertion must occur outside the if/else.
    {unsigned constant=0;auto t=terrain1();assert(weighted(t.data(),t.size(),out,constant));
     t=terrain3();assert(weighted(t.data(),t.size(),out,constant));}
    // Terrain3: replacement follows the combined register through if/else.
    auto t3=terrain3();assert(patch(t3.data(),t3.size(),out,&info));
    assert(info.replaced==2&&info.shadowRegister==3&&info.shadowComponent==0&&info.oneConstant==0&&info.oneComponent==0);
    // The scale reads c2 and the literal lives in c0: one c# per instruction,
    // so the scale becomes add(0.3,0.7)+nop while the specular reads c0.x.
    {size_t mulAt=0;while(!(t3[mulAt]==instr(5,3)&&t3[mulAt+1]==dst(Temp,4,7)))++mulAt;
     assert(out[mulAt+2]==src(Const,0,XXXX)&&out[mulAt+3]==t3[mulAt+3]);
     size_t madAt=mulAt+4;assert(t3[madAt]==instr(4,4)&&out[madAt]==instr(2,3)&&out[madAt+1]==t3[madAt+1]&&out[madAt+2]==src(Const,2,ZZZZ)&&out[madAt+3]==src(Const,2,WWWW)&&out[madAt+4]==0);
     assert(differences(t3,out)==5);}
    // The instruction that ends S's lifetime still reads S before writing it:
    // "mul r1.xyz, r1.x, r1.yzww" after "mad r1.y, r1.x, 0.3, 0.7" (Terrain3).
    {std::vector<Word> p{0xffff0300};def(p,0,1.f,0,0,0);def(p,2,0,0,.3f,.7f);dclTexcoord(p,6,6);dclSampler(p,4);
     texld(p,1,6,4);
     p.insert(p.end(),{instr(4,4),dst(Temp,1,2),src(Temp,1,XXXX),src(Const,2,ZZZZ),src(Const,2,WWWW)}); // mad r1.y, r1.x, 0.3, 0.7 (two c# → add+nop)
     p.insert(p.end(),{instr(5,3),dst(Temp,0,7),src(Temp,0),src(Temp,1,YYYY)});
     p.insert(p.end(),{instr(5,3),dst(Temp,1,7),src(Temp,1,XXXX),src(Temp,1,0xf9)});   // mul r1.xyz, r1.x, r1.yzww (writes r1.x)
     p.insert(p.end(),{instr(1,2),dst(ColorOut,0),src(Temp,0)});p.push_back(0xffff);
     assert(patch(p.data(),p.size(),out,&info)&&info.replaced==2&&out.size()==p.size());
     size_t madAt=0;while(p[madAt]!=instr(4,4))++madAt;assert(out[madAt]==(2u|(3u<<24))&&out[madAt+1]==dst(Temp,1,2)&&out[madAt+2]==src(Const,2,ZZZZ)&&out[madAt+3]==src(Const,2,WWWW)&&out[madAt+4]==0);
     size_t mulAt=madAt+5+4;assert(out[mulAt]==p[mulAt]&&out[mulAt+2]==src(Const,0,XXXX)&&out[mulAt+3]==p[mulAt+3]);}
    // In-place scale "mad r0.w, r0.w, 0.3, 0.7": later reads see the scale, not S.
    {std::vector<Word> p{0xffff0300};def(p,0,.3f,.7f,1.f,0);dclTexcoord(p,6,6);dclSampler(p,4);
     texld(p,0,6,4);
     p.insert(p.end(),{instr(4,4),dst(Temp,0,8),src(Temp,0,WWWW),src(Const,0,XXXX),src(Const,0,YYYY)});
     p.insert(p.end(),{instr(5,3),dst(Temp,0,7),src(Temp,0),src(Temp,0,WWWW)});
     p.insert(p.end(),{instr(1,2),dst(ColorOut,0),src(Temp,0)});p.push_back(0xffff);
     assert(patch(p.data(),p.size(),out,&info)&&info.replaced==1&&differences(p,out)==1);}
    // A specular multiply with a second constant and no literal available fails closed... unless foldable: mul → mov+nop.
    {std::vector<Word> p{0xffff0300};def(p,2,0,0,.3f,.7f);dclTexcoord(p,6,6);dclSampler(p,4);
     texld(p,1,6,4);
     p.insert(p.end(),{instr(5,3),dst(Temp,4,7),src(Temp,1,WWWW),src(Temp,0)});                    // mul r4.xyz, r1.w, r0 → mov r4.xyz, r0 + nop
     p.insert(p.end(),{instr(4,4),dst(Temp,1,1),src(Temp,1,WWWW),src(Const,2,ZZZZ),src(Const,2,WWWW)}); // → add + nop
     p.insert(p.end(),{instr(1,2),dst(ColorOut,0),src(Temp,4)});p.push_back(0xffff);
     assert(patch(p.data(),p.size(),out,&info)&&info.replaced==2&&info.oneConstant==-1);
     size_t mulAt=0;while(p[mulAt]!=instr(5,3))++mulAt;assert(out[mulAt]==(1u|(2u<<24))&&out[mulAt+1]==dst(Temp,4,7)&&out[mulAt+2]==src(Temp,0)&&out[mulAt+3]==0);
     p[mulAt]=instr(2,3);assert(!patch(p.data(),p.size(),out,&info)&&out.empty());}                  // add with S and no literal: not foldable
    // Negated/abs S reads are refused.
    {auto p=terrain1();unsigned seen=0;for(auto& w:p)if(w==src(Temp,4,WWWW)&&++seen==2){w|=1u<<24;break;}assert(seen==2&&!patch(p.data(),p.size(),out,&info)&&out.empty());}
    std::puts("PASS terrain shadow literal replaced in specular and diffuse scale only; earlier/later register meanings kept");
    std::puts("PASS terminating-write source read, in-place scale, single-constant folding (add/mov + nop), modifier refusal");
    std::puts("PASS fail-closed on missing 1.0 literal, predication, ps_1_x, truncation, missing 0.3/0.7 scale; ps_2_0 accepted");
    std::puts("PASS combined baked/dynamic shadow register through if/else (Terrain3 shape)");
    return 0;
}
