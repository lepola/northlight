#include "patch_shadow_shader.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightShadowShader;
static void test() {
    std::vector<Word> out;PatchInfo info;
    // VS1.1 has implicit instruction lengths and implicit a0.x addressing.
    const Word sm1[]={0xfffe0101,1,0xb0010000,0x90000001,
        1,0x800f0000,0xa0e4201f,1,0xc00f0000,0x80e40000,
        1,0xe0030000,0x90e40002,0xffff};
    assert(patch(sm1,14,out,&info));assert(info.major==1&&info.tempRegister==1&&info.texcoord0XY);
    assert(out.size()==20&&out[5]==0x800f0000&&out[6]==0xa0e4201f);
    assert(out[13]==1&&out[16]==1&&out[18]==0x80e40001);
    std::vector<Word> sm1bad(sm1,sm1+14);sm1bad.insert(sm1bad.end()-1,125,0);
    assert(!patch(sm1bad.data(),sm1bad.size(),out));
    const Word sm2[]={0xfffe0200,0x02000001,0xc00f0000,0xa0e40000,
        0x02000001,0xe0030000,0x90e40001,0xffff};
    assert(patch(sm2,8,out,&info));assert(info.major==2&&info.tempRegister==0);
    assert(info.texcoordRegister==7&&info.texcoord0XY&&info.positionWrites==1);
    assert(out.size()==14&&out[2]==0x800f0000&&out[out.size()-2]==0x80e40000);
    const Word sm3[]={0xfffe0300,
        0x0200001f,0x80000000,0xe00f0000,
        0x0200001f,0x80000005,0xe00f0001,
        0x02000001,0xe00f0000,0x90e40000,
        0x02000001,0xe0030001,0x90e40001,0xffff};
    assert(patch(sm3,14,out,&info));assert(info.major==3&&info.texcoordRegister==2);
    assert(info.texcoord0XY&&out.size()==23);
    assert(out[1]==0x0200001f&&out[2]==0x80070005&&out[3]==0xe00f0002);
    assert(!info.color0W);
    std::vector<Word> color(sm3,sm3+14);
    color.insert(color.begin()+1,{0x0200001f,0x8000000a,0xe00f0003});
    color.insert(color.end()-1,{0x02000001,0xe0080003,0x90ff0002});
    assert(patch(color.data(),color.size(),out,&info)&&info.color0W);
    color[color.size()-3]=0xe0030003;
    assert(patch(color.data(),color.size(),out,&info)&&!info.color0W);
    // Four separate position component writes, including relative bone input.
    std::vector<Word> partial={0xfffe0200,0x03000001,0x800f0000,0xa0e42014,0xb0000000};
    for(int i=0;i<4;++i) {
        const Word op[]={0x03000009,Word(0xc0000000|(1u<<(16+i))),Word(0xa0e40002+i),0x80e40000};
        partial.insert(partial.end(),op,op+4);
    }
    partial.push_back(0xffff);assert(patch(partial.data(),partial.size(),out,&info));
    assert(info.tempRegister==1&&info.positionWrites==4&&!info.texcoord0XY);
    for(int i=0;i<4;++i)assert(out[6+4*i]==Word(0x80000001|(1u<<(16+i))));
    // Reject preoccupied TEX7, unsupported flow, predication and truncation.
    std::vector<Word> bad(sm3,sm3+14);bad[5]=0x80070005;
    assert(!patch(bad.data(),bad.size(),out));
    bad.assign(sm2,sm2+8);bad[5]=0xe0030007;assert(!patch(bad.data(),bad.size(),out));
    bad.assign(sm2,sm2+8);bad[1]|=0x10000000;assert(!patch(bad.data(),bad.size(),out));
    assert(!patch(sm2,7,out));
    bad.assign(sm2,sm2+8);bad.insert(bad.end()-1,0x0000001c);assert(!patch(bad.data(),bad.size(),out));
    bad.assign(sm2,sm2+8);bad.insert(bad.end()-1,253,0);assert(!patch(bad.data(),bad.size(),out));
    // Reserve implicit matrix temporary operands r2..r5, not only explicit r2.
    bad={0xfffe0200,0x03000014,0x800f0000,0x90e40000,0x80e40002,
        0x02000001,0xc00f0000,0x80e40000,0xffff};
    assert(patch(bad.data(),bad.size(),out,&info));assert(info.tempRegister==1);
    // If all temp registers are in use there is no safe register to redirect to.
    bad.assign(sm2,sm2+7);
    for(unsigned i=0;i<12;++i){Word op[]={0x02000001,0x800f0000|i,0xa0e40000};bad.insert(bad.end(),op,op+3);}
    bad.push_back(0xffff);assert(!patch(bad.data(),bad.size(),out));
}
int main(int argc,char**argv) {
    test();std::puts("Shadow shader patch synthetic cases passed");
    if(argc>=2) {
        FILE* file=std::fopen(argv[1],"rb");assert(file);
        unsigned counts[4]={},patched[4]={},uv[4]={};Word bytes;
        while(std::fread(&bytes,4,1,file)==1) {
            assert(bytes%4==0&&bytes<1024*1024);
            std::vector<Word> code(bytes/4),result;
            assert(std::fread(code.data(),1,bytes,file)==bytes);
            unsigned major=code[0]>>8&255;assert(major<4);++counts[major];PatchInfo info;
            if(patch(code.data(),code.size(),result,&info)) {
                ++patched[major];if(info.texcoord0XY)++uv[major];
                assert(result.back()==0xffff);
                if(argc==3&&patched[major]<=2) {
                    char path[1024];std::snprintf(path,sizeof path,"%s_sm%u_%u.bin",argv[2],major,patched[major]);
                    FILE* output=std::fopen(path,"wb");assert(output);
                    assert(std::fwrite(result.data(),4,result.size(),output)==result.size());
                    assert(std::fclose(output)==0);
                }
            }
        }
        std::fclose(file);
        for(int i=1;i<=3;++i)std::printf("SM%d seen=%u patched=%u UV0compatible=%u\n",i,counts[i],patched[i],uv[i]);
    }
}
