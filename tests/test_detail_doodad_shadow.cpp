#include "detail_doodad_shadow.h"
#include "actor_deformation.h"
#include "replay_bounds.h"
#include "shader_tags.h"
#include "m2_shader_families.h"
#include <cassert>
#include <cstdio>
#include <limits>
using Word=std::uint32_t;
// Test adapter: route one scalar output to UV0.xy, allowing the existing
// sliced native VS evaluator to execute the exact native fade arithmetic.
static NorthlightActorDeformation::Program scalarProgram(const std::vector<Word>& source,unsigned color,unsigned channel){
    using namespace NorthlightShadowShader;auto code=source;const unsigned major=(source[0]>>8)&255;
    unsigned output=color,type=5;
    if(major==3){type=6;bool found=false;
        for(size_t at=1;at<code.size();){const unsigned op=code[at]&65535,n=(code[at]>>24)&15;if(op==65535)break;
            if(op==65534){at+=1+((code[at]>>16)&32767);continue;}
            if(op==31&&n==2&&regType(code[at+2])==6){if((code[at+1]&15)==5&&((code[at+1]>>16)&15)==0)code[at+1]=0x80060005;
                if((code[at+1]&15)==10&&((code[at+1]>>16)&15)==color){output=regIndex(code[at+2]);found=true;}}
            at+=n+1;}
        assert(found);
    }
    for(size_t at=1;at<code.size();){const unsigned op=code[at]&65535,n=(code[at]>>24)&15;if(op==65535)break;
        if(op==65534){at+=1+((code[at]>>16)&32767);continue;}
        if(op!=31&&op!=81&&n){Word& destination=code[at+1];const unsigned dt=regType(destination),index=regIndex(destination),mask=(destination>>16)&15;
            if(dt==type&&index==output&&(mask&(1u<<channel))){assert(mask==(1u<<channel));destination=replaceReg((destination&~Word(15u<<16))|(3u<<16),6,major==3?11:0);}
            else if(major==2&&dt==6&&index==0)destination=replaceReg(destination,6,6);
        }at+=n+1;}
    if(major==3)code.insert(code.begin()+1,{0x0200001f,0x80000005,0xe003000b});
    NorthlightActorDeformation::Program program;assert(NorthlightActorDeformation::compile(code.data(),code.size(),program,true));return program;
}
static void nativeFade(const std::vector<Word>& native){
    const auto original=native;std::vector<Word> shadow;
    assert(NorthlightDetailShadow::shadowFade(native,shadow)&&native==original&&shadow.size()==native.size());
    unsigned changed=0;for(unsigned i=0;i<native.size();++i)if(shadow[i]!=native[i]){++changed;const unsigned mask=(native[i]>>16)&15;assert(mask==1||mask==8);}
    assert(changed==2);
    std::vector<Word> replay;assert(NorthlightShadowShader::patch(shadow.data(),shadow.size(),replay));
    const auto source=scalarProgram(native,1,0),result=scalarProgram(replay,0,3);
    for(float rawFade:{-.25f,0.f,.25f,.5f,1.f,1.25f})for(float lighting:{0.f,.37f,1.f})for(float slope:{.125f,-.5f})for(unsigned projection=0;projection<4;++projection){
        const float fade=std::max(0.f,std::min(1.f,rawFade));
        float constants[1024]={};constants[0]=constants[5]=constants[10]=constants[15]=1;
        constants[9*4]=slope;constants[9*4+1]=rawFade-slope*4;
        // Replay changes c4..7 to the light projection. The c9 fade must
        // continue using the native preprojection view Z from c0..3.
        for(unsigned j=0;j<16;++j)constants[16+j]=projection==0?float(j%5==0):projection==1?float(j+1)*.37f:projection==2?-float(j+1)*13.f:float(j%3==0)*.003f;
        NorthlightActorDeformation::Four inputs[16]={},before,after;inputs[0]={2,3,4,1};inputs[2]={1,1,1,lighting};
        assert(NorthlightActorDeformation::evaluate(source,inputs,constants,before)&&NorthlightActorDeformation::evaluate(result,inputs,constants,after));
        assert(before[0]==fade&&after[0]==fade);
        for(float texture:{0.f,.25f,.5f,1.f})for(float cutoff:{0.f,.25f,.5f,1.f}){
            const float nativeAlpha=texture*before[0],shadowAlpha=texture*after[0];assert(nativeAlpha==shadowAlpha&&((nativeAlpha>=cutoff)==(shadowAlpha>=cutoff)));}
    }
    std::vector<Word> sentinel{123};auto unknown=native;unknown[unknown.size()-2]^=1;
    assert(!NorthlightDetailShadow::shadowFade(unknown,sentinel)&&sentinel==std::vector<Word>{123});
}
// Route clip output through COLOR0 for the standalone branch's older decoder API.
static NorthlightActorDeformation::Program clipProgram(std::vector<Word> code){
    using namespace NorthlightShadowShader;const unsigned major=(code[0]>>8)&255;
    for(size_t at=1;at<code.size();){const unsigned op=code[at]&65535;
        if(op==65535)break;
        const unsigned n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        if(op==31&&major==3&&regType(code[at+2])==6){
            const unsigned usage=code[at+1]&15,index=(code[at+1]>>16)&15;
            if(usage==10&&index==0)code[at+1]=0x80060005;
            else if(usage==5&&index==7)code[at+1]=0x8000000a;
        }else if(major==2&&op!=31&&op!=81&&op!=65534&&n){
            Word& dst=code[at+1];
            if(regType(dst)==5&&regIndex(dst)==0)dst=replaceReg(dst,5,1);
            else if(regType(dst)==6&&regIndex(dst)==7)dst=replaceReg(dst,5,0);
        }
        at+=n+1;
    }
    NorthlightActorDeformation::Program p;assert(NorthlightActorDeformation::compile(code.data(),code.size(),p,true,2,10));return p;
}
static void nativeWorldProjection(const std::vector<Word>& native){
    std::vector<Word> world,patched;
    assert(NorthlightDetailShadow::shadowVertex(native,world)&&world.size()+1==native.size());
    assert(NorthlightShadowShader::patch(world.data(),world.size(),patched));
    const auto position=clipProgram(patched);
    const auto fadeBefore=scalarProgram(native,1,0),fadeAfter=scalarProgram(patched,0,3);
    for(unsigned projection=0;projection<4;++projection){
        NorthlightActorDeformation::Four reference;
        for(unsigned camera=0;camera<41;++camera){
            const float angle=(float(camera)-20)*.01f;
            float constants[1024]={};
            constants[0]=std::cos(angle);constants[1]=std::sin(angle);constants[4]=-std::sin(angle);constants[5]=std::cos(angle);
            constants[10]=1;constants[12]=-114.584f;constants[13]=-5210.417f;constants[14]=-20+camera*.5f;constants[15]=1;
            for(unsigned j=0;j<16;++j)constants[16+j]=projection==0?float(j%5==0):float(int(j%5)-2)*(.01f+projection*.007f);
            constants[36]=-.2f;constants[37]=1;
            NorthlightActorDeformation::Four inputs[16]={},clip,before,after;
            inputs[0]={114.584f,5210.417f,23.091f,1};inputs[2]={1,1,1,.37f};
            assert(NorthlightActorDeformation::evaluate(position,inputs,constants,clip));
            if(camera==0)reference=clip;else assert(std::memcmp(&reference,&clip,sizeof clip)==0);
            assert(NorthlightActorDeformation::evaluate(fadeBefore,inputs,constants,before));
            assert(NorthlightActorDeformation::evaluate(fadeAfter,inputs,constants,after)&&before[0]==after[0]);
        }
    }
    auto unknown=native;unknown.back()^=1;std::vector<Word> sentinel{123};
    assert(!NorthlightDetailShadow::shadowVertex(unknown,sentinel)&&sentinel==std::vector<Word>{123});
}
static void policy(){
    using namespace NorthlightDetailShadow;
    assert(shader(0x7f2707e33c6c97beULL)&&shader(0xc8e41181431a84c7ULL));
    assert(!shader(0)&&!shader(kTerrainVS[0]));
    assert(pixelShader(0x4c4e8c78e53fb551ULL)&&!pixelShader(0)&&!pixelShader(kTerrainVS[0]));
    for(unsigned source=0;source<18;++source)for(unsigned destination=0;destination<18;++destination)for(unsigned operation=0;operation<6;++operation)
        assert(blendedMaterial(true,source,destination,operation)==(source==5&&destination==6&&operation==1));
    assert(!blendedMaterial(false,5,6,1));
    Budget b;assert(!b.reserve(0,0)&&!b.reserve(Budget::MaxVertices+1,1));
    assert(b.reserve(8,256)&&b.take(8,256));
    assert(!b.reserve(8,Budget::MaxBytes)&&b.attempts==1&&b.readVertices==8);
    for(unsigned i=1;i<Budget::MaxDraws;++i)assert(b.reserve(8,256));
    assert(!b.reserve(8,256));
    b={};assert(!b.reserve(Budget::MaxDrawVertices+1,1));
    for(unsigned i=0;i<Budget::MaxVertices/Budget::MaxDrawVertices;++i){assert(b.reserve(8192,512*1024));assert(b.take(8192,512*1024));}
    assert(!b.room(1)&&!b.take(1,1));
    // Recorded field: dozens of native batches, not one eight-thousand-vertex draw.
    // Reordering a full field must not choose a different first handful of casters.
    for(unsigned phase=0;phase<64;++phase){Budget field;std::uint64_t accepted=0;
        for(unsigned i=0;i<64;++i){const unsigned id=(i*17+phase)%64,vertices=512+id%4*256;const size_t bytes=size_t(vertices)*44;
            assert(field.reserve(vertices,bytes)&&field.take(vertices,bytes));accepted|=1ull<<id;}
        assert(accepted==~std::uint64_t(0)&&field.draws==64&&field.bytes<=Budget::MaxBytes);}
    b={};assert(b.reserve(1,Budget::MaxBytes)&&!b.room(1)&&!b.reserve(1,1));
    float camera[]={0,0,0};NorthlightReplayBounds::Bounds bounds;bounds.valid=true;
    bounds.low[0]=64;bounds.high[0]=65;assert(!NorthlightReplayBounds::outsideSphere(bounds,camera,64));
    bounds.low[0]=64.01f;assert(NorthlightReplayBounds::outsideSphere(bounds,camera,64));
    ReceiverCensus census;census.observe(false,true,true,false,true,false,true);
    census.observe(true,true,false,true,true,true,true);census.observe(false,false,false,false,false,false,false);
    assert(census.before==2&&census.after==1&&census.depthWrites==1&&census.blended==1&&census.alphaCut==2&&census.alreadyResolved==1&&census.worldDepth==2&&census.stateFailures==1);
}
static std::vector<Word> synthetic(){
    // Affine column model-view followed by the native DetailDoodad column projection.
    return {0xfffe0300,0x0200001f,0x80000000,0x900f0000,
        0x0200001f,0x80000000,0xe00f0000,
        0x03000005,0x800f0000,0xa0e40001,0x90550000,
        0x04000004,0x800f0000,0x90000000,0xa0e40000,0x80e40000,
        0x04000004,0x800f0000,0x90aa0000,0xa0e40002,0x80e40000,
        0x03000002,0x800f0000,0x80e40000,0xa0e40003,
        0x03000005,0x800f0001,0x80550000,0xa0e40005,
        0x04000004,0x800f0001,0x80000000,0xa0e40004,0x80e40001,
        0x04000004,0x800f0001,0x80aa0000,0xa0e40006,0x80e40001,
        0x04000004,0xe00f0000,0x80ff0000,0xa0e40007,0x80e40001,0xffff};
}
static void positions(const NorthlightActorDeformation::Program& program){
    using namespace NorthlightActorDeformation;
    assert(!program.skinned&&program.inputs.size()==1&&program.operations.size()==4);
    float constants[1024]={};constants[0]=constants[5]=constants[10]=constants[15]=1;
    constants[12]=10;constants[13]=20;constants[14]=30;
    Four input[16]={},position;input[0]={2,3,4,1};
    assert(evaluate(program,input,constants,position)&&position==Four({12,23,34,1}));
    NorthlightDrawSnapshot::Mesh mesh;mesh.topology=D3DPT_TRIANGLELIST;mesh.vertexCount=3;mesh.primitiveCount=1;mesh.indexed=true;mesh.indices={0,1,2};
    mesh.streams[0].stride=36;mesh.streams[0].bytes.resize(3*36);
    const float points[3][3]={{2,3,4},{-8,7,20},{32,-3,-4}};
    for(unsigned i=0;i<3;++i)std::memcpy(mesh.streams[0].bytes.data()+36*i,points[i],12);
    D3DVERTEXELEMENT9 declaration[]={{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,4,0,10,0},{0,28,1,0,5,0},{0xff,0,17,0,0,0}};
    float inverse[16]={};for(unsigned i=0;i<4;++i)inverse[5*i]=1;
    inverse[12]=1000;inverse[13]=-500;inverse[14]=200;
    NorthlightReplayBounds::Budget budget;NorthlightReplayBounds::Bounds original;
    assert(NorthlightReplayBounds::calculate(program,mesh,declaration,5,constants,inverse,budget,original)==NorthlightReplayBounds::Status::Valid);
    assert(original.low[0]<=1002&&original.high[0]>=1042&&original.high[2]>=250);
    NorthlightReplayBounds::Bounds box;assert(NorthlightDetailShadow::bounds(program,mesh,declaration,5,constants,inverse,box));
    for(unsigned axis=0;axis<3;++axis){assert(box.low[axis]<=original.low[axis]&&box.high[axis]>=original.high[axis]);}
    // Rotation of the camera leaves the absolute submitted bounds unchanged.
    constants[0]=constants[5]=0;constants[1]=-1;constants[4]=1;constants[12]=20;constants[13]=-10;
    inverse[0]=inverse[5]=0;inverse[1]=1;inverse[4]=-1;
    budget.reset();NorthlightReplayBounds::Bounds rotated;
    assert(NorthlightReplayBounds::calculate(program,mesh,declaration,5,constants,inverse,budget,rotated)==NorthlightReplayBounds::Status::Valid);
    for(unsigned i=0;i<3;++i){assert(std::fabs(original.low[i]-rotated.low[i])<.01f);assert(std::fabs(original.high[i]-rotated.high[i])<.01f);}
}
static void enclosure(const NorthlightActorDeformation::Program& program){
    std::uint32_t state=0x2911;
    const auto sample=[&](){state^=state<<13;state^=state>>17;state^=state<<5;return (float(state%200001)-100000)*.001f;};
    NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=99;mesh.streams[0].stride=36;mesh.streams[0].bytes.resize(99*36);
    D3DVERTEXELEMENT9 declaration[]={{0,0,2,0,0,0},{0xff,0,17,0,0,0}};
    for(unsigned run=0;run<200;++run){
        float constants[1024]={},inverse[16]={};
        for(unsigned axis=0;axis<3;++axis){for(unsigned k=0;k<3;++k){constants[4*k+axis]=sample()*.01f;inverse[4*k+axis]=sample()*.01f;}
            constants[12+axis]=sample()*10;inverse[12+axis]=sample()*100;}
        constants[15]=inverse[15]=1;
        for(unsigned i=0;i<99;++i){const float xyz[]={sample(),sample(),sample()};std::memcpy(mesh.streams[0].bytes.data()+i*36,xyz,12);}
        NorthlightReplayBounds::Bounds bounds;assert(NorthlightDetailShadow::bounds(program,mesh,declaration,2,constants,inverse,bounds));
        for(unsigned i=0;i<99;++i){float xyz[3];std::memcpy(xyz,mesh.streams[0].bytes.data()+i*36,12);
            for(bool fused:{false,true}){
                float view[3],world[3];const auto mad=[&](float a,float b,float c){return fused?std::fma(a,b,c):a*b+c;};
                for(unsigned axis=0;axis<3;++axis){float v=constants[4+axis]*xyz[1];v=mad(xyz[0],constants[axis],v);v=mad(xyz[2],constants[8+axis],v);view[axis]=v+constants[12+axis];}
                for(unsigned axis=0;axis<3;++axis){float v=inverse[12+axis];for(unsigned k=0;k<3;++k)v=mad(view[k],inverse[4*k+axis],v);world[axis]=v;
                    assert(bounds.low[axis]<=world[axis]&&bounds.high[axis]>=world[axis]);}
            }
        }
    }
    mesh.streams[0].bytes[2]=0xff;const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(mesh.streams[0].bytes.data(),&nan,4);
    float constants[1024]={},inverse[16]={};NorthlightReplayBounds::Bounds bounds;
    assert(!NorthlightDetailShadow::bounds(program,mesh,declaration,2,constants,inverse,bounds)&&!bounds.valid);
}
int main(int argc,char** argv){
    policy();auto code=synthetic();NorthlightActorDeformation::Program program;
    assert(NorthlightActorDeformation::compile(code.data(),code.size(),program,false,1));positions(program);enclosure(program);
    // Validate the worst admitted vertex count without measuring execution time.
    {NorthlightDrawSnapshot::Mesh mesh;mesh.topology=D3DPT_TRIANGLELIST;mesh.vertexCount=8190;mesh.primitiveCount=2730;mesh.indexed=false;mesh.streams[0].stride=36;mesh.streams[0].bytes.resize(8190*36);
     for(unsigned i=0;i<8190;++i){float xyz[]={float(i%31),float(i%13),float(i%7)};std::memcpy(mesh.streams[0].bytes.data()+36*i,xyz,12);}
     D3DVERTEXELEMENT9 declaration[]={{0,0,2,0,0,0},{0xff,0,17,0,0,0}};
     float constants[1024]={},inverse[16]={};constants[0]=constants[5]=constants[10]=constants[15]=1;for(unsigned i=0;i<4;++i)inverse[5*i]=1;
     for(unsigned pass=0;pass<128;++pass){NorthlightReplayBounds::Bounds bounds;assert(NorthlightDetailShadow::bounds(program,mesh,declaration,2,constants,inverse,bounds));}
     }
    assert(!NorthlightActorDeformation::compile(code.data(),code.size(),program));
    for(unsigned i=27;i<code.size()-1;++i){auto bad=code;bad[i]^=1;assert(!NorthlightActorDeformation::compile(bad.data(),bad.size(),program,false,1));}
    assert(!NorthlightActorDeformation::compile(code.data(),code.size(),program,false,3));
    for(int i=1;i<argc;++i){
        FILE* file=std::fopen(argv[i],"rb");assert(file);std::fseek(file,0,SEEK_END);const auto size=std::ftell(file);std::rewind(file);
        assert(size>0&&size%4==0);std::vector<Word> native(size/4);assert(std::fread(native.data(),1,size,file)==size_t(size));std::fclose(file);
        assert(NorthlightDetailShadow::shader(NorthlightShaderTags::fnv1a(native.data(),size)));
        assert(m2ShaderFamily(NorthlightShaderTags::fnv1a(native.data(),size))==NorthlightM2Family::kNone);
        nativeFade(native);nativeWorldProjection(native);
        NorthlightShadowShader::PatchInfo info;std::vector<Word> patched;
        std::vector<Word> shadow;assert(NorthlightDetailShadow::shadowFade(native,shadow));
        assert(NorthlightShadowShader::patch(shadow.data(),shadow.size(),patched,&info)&&info.texcoord0XY&&info.color0W);
        assert(NorthlightActorDeformation::compile(native.data(),native.size(),program,false,1));positions(program);
        NorthlightActorDeformation::Program uv;assert(NorthlightActorDeformation::compile(native.data(),native.size(),uv,true));
        float constants[1024]={};NorthlightActorDeformation::Four inputs[16]={},result;inputs[3]={.125f,.875f,0,1};
        assert(NorthlightActorDeformation::evaluate(uv,inputs,constants,result)&&result[0]==.125f&&result[1]==.875f);
    }
    std::printf("PASS grass policy, column-projection negatives, full mesh bounds/camera invariance, %d native shader/UV/alpha replay permutations\n",argc-1);
}
