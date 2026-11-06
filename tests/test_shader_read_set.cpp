// Task 34: the shader read-set scanner (shader_read_set.h) on hand-assembled token streams.
#include "shader_read_set.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace NorthlightShaderReads;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"FAIL %s:%d %s\n",__FILE__,__LINE__,#c);std::abort();}}while(0)
using W=std::vector<uint32_t>;
enum{CONST=2,SAMPLER=10,INT=7,BOOLC=14,TEMP=0,INPUT=1,ADDR=3,RASTOUT=4,LOOPR=15,CONST2=11};
// a register token: type, number, relative flag
static uint32_t reg(unsigned type,unsigned num,bool rel=false,unsigned swz=0xE4){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|num|(rel?0x2000u:0)|(swz<<16);}
static uint32_t dst(unsigned type,unsigned num){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|num|(0xFu<<16);}
// SM2+ instruction: opcode token with the length, then operand tokens
static void ins(W& w,unsigned op,std::initializer_list<uint32_t> args,bool predicated=false){
    w.push_back(op|(unsigned(args.size())<<24)|(predicated?0x10000000u:0));for(uint32_t a:args)w.push_back(a);
}
static W shader(uint32_t version,const W& body){W w{version};w.insert(w.end(),body.begin(),body.end());w.push_back(0xffff);return w;}
static ReadSet scanW(const W& w){return scan(w.data(),w.size());}
static bool f(const ReadSet& r,unsigned i){return r.f[i>>6]>>(i&63)&1;}
static unsigned fcount(const ReadSet& r){unsigned n=0;for(auto w:r.f)n+=unsigned(__builtin_popcountll(w));return n;}
static const uint32_t VS2=0xfffe0200,VS3=0xfffe0300,VS1=0xfffe0101,PS2=0xffff0200,PS3=0xffff0300,PS11=0xffff0101;

static void testVertexBasics(){
    W b;
    ins(b,31,{0x80000000u,dst(INPUT,0)});                                     // dcl_position v0
    ins(b,81,{dst(CONST,5),0x3f800000u,0,0,0});                               // def c5, 1,0,0,0
    ins(b,1,{dst(TEMP,0),reg(CONST,3)});                                       // mov r0, c3
    ins(b,2,{dst(TEMP,0),reg(TEMP,0),reg(CONST,5)});                           // add r0, r0, c5 (defined: not a device read)
    ins(b,20,{dst(RASTOUT,0),reg(TEMP,0),reg(CONST,8)});                       // m4x4 oPos, r0, c8 -> c8..c11
    ins(b,21,{dst(TEMP,1),reg(TEMP,0),reg(CONST,20)});                         // m4x3 -> c20..c22
    ins(b,22,{dst(TEMP,1),reg(TEMP,0),reg(CONST,30)});                         // m3x4 -> c30..c33
    ins(b,23,{dst(TEMP,1),reg(TEMP,0),reg(CONST,40)});                         // m3x3 -> c40..c42
    ins(b,24,{dst(TEMP,1),reg(TEMP,0),reg(CONST,50)});                         // m3x2 -> c50..c51
    const ReadSet r=scanW(shader(VS2,b));
    CHECK(r.exact&&r.vertex&&!r.i&&!r.b&&!r.samplers);
    const unsigned want[]={3,8,9,10,11,20,21,22,30,31,32,33,40,41,42,50,51};
    CHECK(fcount(r)==sizeof want/sizeof want[0]);for(unsigned i:want)CHECK(f(r,i));CHECK(!f(r,5));
}
static void testRelative(){
    {   W b;ins(b,1,{dst(TEMP,0),reg(CONST,31,true),reg(ADDR,0)});            // mov r0, c[a0.x+31]: c31 .. c255 (the bytecode does not bound it)
        const ReadSet r=scanW(shader(VS2,b));CHECK(r.exact&&!f(r,30)&&f(r,31)&&f(r,255)&&fcount(r)==225);}
    {   W b;b.push_back(1|(0u<<24));b.pop_back();                              // vs_1_1: c[a0.x+7] has no address token
        b.push_back(1);b.push_back(dst(TEMP,0));b.push_back(reg(CONST,7,true));
        const ReadSet r=scanW(shader(VS1,b));CHECK(r.exact&&!f(r,6)&&f(r,7)&&f(r,255));}
    {   W b;ins(b,1,{dst(TEMP,0),reg(CONST,5,true),reg(ADDR,0)});            // ps_2_0 / ps_3_0: relative up to the end of the pixel bank
        const ReadSet r=scanW(shader(PS3,b));CHECK(r.exact&&f(r,5)&&f(r,223)&&!f(r,224)&&fcount(r)==219);}
    {   W b;ins(b,1,{dst(TEMP,0),reg(INT,2,true),reg(ADDR,0)});               // a relative int register: not understood
        CHECK(!scanW(shader(VS3,b)).exact);}
    {   W b;ins(b,1,{dst(TEMP,0),reg(BOOLC,2,true),reg(ADDR,0)});CHECK(!scanW(shader(VS3,b)).exact);}
}
static void testPixel(){
    W b;
    ins(b,31,{0x90000000u,dst(SAMPLER,0)});ins(b,31,{0x90000000u,dst(SAMPLER,4)});ins(b,31,{0x90000000u,dst(SAMPLER,7)});
    ins(b,66,{dst(TEMP,0),reg(INPUT,0),reg(SAMPLER,0)});                       // texld r0, t0, s0
    ins(b,5,{dst(TEMP,0),reg(TEMP,0),reg(CONST,2)});
    ins(b,5,{dst(TEMP,0),reg(TEMP,0),reg(CONST,26)});
    const ReadSet r=scanW(shader(PS2,b));
    CHECK(r.exact&&!r.vertex&&r.samplers==(1u|1u<<4|1u<<7)&&fcount(r)==2&&f(r,2)&&f(r,26)&&!r.i&&!r.b);
    CHECK(r.sampledSamplers==1u); // unused declarations never fetch textures
    W c;ins(c,66,{dst(TEMP,0),reg(INPUT,0),reg(SAMPLER,9)});                   // a sampler used without dcl still counts
    CHECK(scanW(shader(PS2,c)).samplers==1u<<9);
    CHECK(scanW(shader(PS2,c)).sampledSamplers==1u<<9);
    W declaration;ins(declaration,31,{0x90000000u,dst(SAMPLER,3)});
    const auto unused=scanW(shader(PS3,declaration));CHECK(unused.exact&&unused.samplers==(1u<<3)&&unused.sampledSamplers==0);
}
static void testIntBoolAndControlFlow(){
    W b;
    ins(b,48,{dst(INT,2),1,2,3,4});ins(b,47,{dst(BOOLC,1),1});                // defi i2, defb b1: shader-owned
    ins(b,27,{reg(LOOPR,0),reg(INT,0)});                                       // loop aL, i0
    ins(b,1,{dst(TEMP,0),reg(CONST,60,true),reg(LOOPR,0)});                    // mov r0, c[aL+60]
    ins(b,29,{});                                                              // endloop
    ins(b,40,{reg(BOOLC,3)});ins(b,1,{dst(TEMP,0),reg(CONST,4)});ins(b,43,{}); // if b3 ... endif: every operand counts, branches included
    ins(b,38,{reg(INT,2)});ins(b,39,{});                                       // rep i2 (defined: not a read)
    const ReadSet r=scanW(shader(VS3,b));
    CHECK(r.exact&&r.i==1&&r.b==(1u<<3)&&f(r,4)&&f(r,60)&&f(r,255)&&!f(r,59));
}
static void testPredicatedAndComments(){
    W b;
    b.push_back(0xfffe|(4u<<16));b.push_back(0x42415443);b.push_back(0x80000000u|CONST);b.push_back(0x0001ffffu);b.push_back(0x00000051u|(5u<<24)); // a comment (CTAB) holding things that look like instructions
    ins(b,1,{dst(TEMP,0),reg(0,0),reg(CONST,9)},true);                         // (p0) mov r0, c9: the predicate token follows the destination
    ins(b,1,{dst(TEMP,0),reg(CONST,10)},false);
    const ReadSet r=scanW(shader(VS2,b));
    CHECK(r.exact&&fcount(r)==2&&f(r,9)&&f(r,10));
    W t;ins(t,2,{dst(TEMP,0),reg(TEMP,1),reg(TEMP,2)});CHECK(scanW(shader(VS2,t)).exact&&fcount(scanW(shader(VS2,t)))==0);
}
static void testPredicatedPlacement(){
    { W b;ins(b,2,{dst(TEMP,0),reg(0,0),reg(TEMP,1),reg(CONST,5)},true);        // vs_3_0 (p0) add r0, r1, c5: c5 is the last source, not the predicate
      const ReadSet r=scanW(shader(VS3,b));CHECK(r.exact&&fcount(r)==1&&f(r,5)); }
    { W b;ins(b,2,{dst(TEMP,0),reg(0,0),reg(CONST,7,true),reg(ADDR,0),reg(CONST,5)},true); // (p0) add r0, c[a0.x+7], c5
      const ReadSet r=scanW(shader(VS3,b));CHECK(r.exact&&f(r,5)&&!f(r,6)&&f(r,7)&&f(r,255)); }
    { W b;ins(b,40,{reg(BOOLC,0)},true);CHECK(!scanW(shader(VS3,b)).exact); }  // predicated without a destination: cannot be placed
    { W b;ins(b,2,{dst(TEMP,0)},true);CHECK(!scanW(shader(VS3,b)).exact); } // truncated: no predicate token after the destination
}
static void testFallbacks(){
    { W b;ins(b,55,{dst(TEMP,0)});CHECK(!scanW(shader(VS2,b)).exact); }       // unknown opcode
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST2,1)});CHECK(!scanW(shader(VS3,b)).exact); } // extended constant bank
    { W b;ins(b,1,{dst(CONST,0),reg(TEMP,1)});CHECK(!scanW(shader(VS2,b)).exact); }  // a constant as destination
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,224)});CHECK(!scanW(shader(PS2,b)).exact);CHECK(scanW(shader(VS2,b)).exact); } // c224 is outside the pixel bank
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,256)});CHECK(!scanW(shader(VS2,b)).exact); }
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,1)});W w=shader(PS11,b);CHECK(!scanW(w).exact); } // ps_1_x
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,1)});W w=shader(VS2,b);w.pop_back();CHECK(!scanW(w).exact); }  // no END
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,1)});W w=shader(VS2,b);w[1]=1|(9u<<24);CHECK(!scanW(w).exact); } // length beyond the stream
    { W b;ins(b,1,{dst(TEMP,0),0x00000001u});CHECK(!scanW(shader(VS2,b)).exact); }    // operand token without the high bit
    CHECK(!scan(nullptr,0).exact);{const uint32_t v[2]={0x12345678u,0xffff};CHECK(!scan(v,2).exact);}
    { W b;ins(b,1,{dst(TEMP,0),reg(CONST,1)});W w=shader(0xfffe0400u,b);CHECK(!scanW(w).exact); } // vs_4_0 is not a D3D9 shader
    { W b;b.push_back(81);b.push_back(dst(CONST,3));b.push_back(0);b.push_back(0);b.push_back(0);b.push_back(0); // vs_1_1 def, mov, a vs_1_1 instruction the table does not know
      W w=shader(VS1,b);CHECK(scanW(w).exact);W u=shader(VS1,W{80u,dst(TEMP,0)});CHECK(!scanW(u).exact); }
}
int main(){
    testVertexBasics();testRelative();testPixel();testIntBoolAndControlFlow();testPredicatedAndComments();testPredicatedPlacement();testFallbacks();
    std::puts("shader read set: 7 tests passed");return 0;
}
