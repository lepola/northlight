#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""Independent exact-four-bone hull safety tests; native compiler only."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

stub = literal(HERE / 'test_terrain_snapshot.py', 'stub')
harness = r'''
#include "replay_bounds.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace NorthlightReplayBounds;
using namespace NorthlightActorDeformation;
using Mesh=NorthlightDrawSnapshot::Mesh;
static Program load(const char* path){
    FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);long bytes=std::ftell(f);std::rewind(f);
    std::vector<Word> words(size_t(bytes)/4);assert(std::fread(words.data(),1,size_t(bytes),f)==size_t(bytes));std::fclose(f);
    Program p;assert(compile(words.data(),words.size(),p));return p;
}
static D3DVERTEXELEMENT9 declaration[]={{0,0,2,0,0,0},{0,12,3,0,1,0},{0,28,5,0,2,0},{0xff,0,17,0,0,0}};
static Mesh geometry(unsigned vertices){
    Mesh m;m.vertexCount=vertices;m.primitiveCount=vertices/3;m.topology=D3DPT_TRIANGLELIST;m.indexed=false;
    m.streams[0].stride=32;m.streams[0].bytes.resize(size_t(vertices)*32);std::mt19937 rng(14392);
    for(unsigned n=0;n<vertices;++n){
        const float v[]={std::sin(float(n))*.7f,std::cos(float(n))*.9f,float(int(n%13)-6)*.2f,
            float(rng()%100)/80,float(rng()%100)/130,float(rng()%100)/170,float(rng()%100)/200};
        auto* target=m.streams[0].bytes.data()+n*32;std::memcpy(target,v,sizeof v);
        // First two indices encode n uniquely for n<5625, exceeding256 tuples.
        target[28]=n%75;target[29]=(n/75)%75;target[30]=(n*7)%75;target[31]=(n*13+7)%75;
    }return m;
}
static void palette(float* c,unsigned pose){
    std::fill(c,c+1024,0.f);
    for(unsigned b=0;b<75;++b){const unsigned row=31+3*b;const float a=float(b+pose)*.027f;
        c[4*row]=std::cos(a);c[4*row+1]=std::sin(a);c[4*row+3]=float(b)*.31f;
        c[4*(row+1)]=-std::sin(a);c[4*(row+1)+1]=std::cos(a);c[4*(row+1)+3]=float(b)*-.23f;
        c[4*(row+2)+2]=1;c[4*(row+2)+3]=float(pose)*.1f;
    }
}
static Status finish(EnvelopeCache& cache,const Program& p,const std::shared_ptr<const Mesh>& mesh,const float* c,const float* inverse,Bounds& bounds){
    for(unsigned attempt=0;attempt<10000;++attempt){cache.beginFrame();Budget budget;budget.maxVertices=512;
        auto status=cache.calculate(p,*mesh,mesh,declaration,4,c,inverse,budget,bounds);
        assert(budget.vertices<=512&&budget.operations<=budget.maxOperations);
        if(status!=Status::Budget)return status;
    }assert(false);return Status::Budget;
}
static size_t enclosed(const Program& p,const Mesh& mesh,const float* c,const float* inverse,const Bounds& bounds){
    assert(bounds.valid);std::vector<Position> positions;assert(worldPositions(p,mesh,declaration,4,c,inverse,positions));
    for(const auto& v:positions){const float actual[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(actual[k]>=bounds.low[k]&&actual[k]<=bounds.high[k]);}
    return positions.size();
}
int main(int argc,char** argv){
    assert(argc==2);auto p=load(argv[1]);std::swap(p.operations[0].source[0],p.operations[0].source[1]);buildKernel(p);assert(SkinEnvelope::supports(p));float c[1024],inverse[16]={};
    for(unsigned n=0;n<4;++n)inverse[5*n]=1;inverse[12]=10000;inverse[13]=-900;
    auto mesh=std::make_shared<const Mesh>(geometry(1200));EnvelopeCache cache;Bounds bounds;size_t vertices=0;
    for(unsigned pose=0;pose<20;++pose){palette(c,pose);const float a=pose*.013f;inverse[0]=inverse[5]=std::cos(a);inverse[1]=std::sin(a);inverse[4]=-std::sin(a);
        assert(finish(cache,p,mesh,c,inverse,bounds)==Status::Valid);vertices+=enclosed(p,*mesh,c,inverse,bounds);
    }
    for(const auto& op:p.operations)for(unsigned field=0;field<8;++field){auto broken=p;auto& b=broken.operations[&op-p.operations.data()];
        if(field==0)b.code^=1;else if(field==1)b.destination^=0x10000;else if(field%2==0)b.source[(field-2)/2].token^=1;else b.source[(field-3)/2].address^=1;
        assert(!SkinEnvelope::supports(broken));}
    for(unsigned pose=20;pose<40;++pose){palette(c,pose);assert(finish(cache,p,mesh,c,inverse,bounds)==Status::Valid);cache.beginFrame();Budget quick;quick.maxVertices=0;WorkInfo info;
        const auto status=cache.calculate(p,*mesh,mesh,declaration,4,c,inverse,quick,bounds,EnvelopeCache::Stage::Cheap,&info);
        assert(info.skin&&quick.vertices==0&&quick.operations<=1275);if(status==Status::Valid){assert(info.kind==WorkKind::Cheap);vertices+=enclosed(p,*mesh,c,inverse,bounds);}}
    // A local DEF can override a bone-palette row even with the same18 ops.
    // GPU API constants alone cannot supply the effective palette.
    auto defined=p;defined.definitions.push_back({31,{1,0,0,2000}});
    assert(finish(cache,defined,mesh,c,inverse,bounds)==Status::Valid);vertices+=enclosed(defined,*mesh,c,inverse,bounds);
    auto zero=geometry(1200);for(unsigned n=0;n<1200;++n)std::memset(zero.streams[0].bytes.data()+n*32+12,0,16);
    auto zeroOwner=std::make_shared<const Mesh>(zero);
    assert(finish(cache,p,zeroOwner,c,inverse,bounds)==Status::Valid);vertices+=enclosed(p,*zeroOwner,c,inverse,bounds);

    Four inputs[16]={};inputs[0]={1,2,3,1};inputs[1]={.1f,.2f,.3f,.4f};inputs[2]={0,1,2,3};SkinEnvelope skin;
    assert(skin.add(inputs));inputs[1][0]=-.01f;SkinEnvelope negative;assert(!negative.add(inputs));
    inputs[1][0]=std::numeric_limits<float>::denorm_min();SkinEnvelope denormalWeight;assert(!denormalWeight.add(inputs));
    inputs[1][0]=.1f;inputs[0][0]=std::numeric_limits<float>::denorm_min();SkinEnvelope denormalPosition;assert(!denormalPosition.add(inputs));
    inputs[0][0]=1;inputs[2][0]=.5f;SkinEnvelope fractionalIndex;assert(!fractionalIndex.add(inputs));
    inputs[2][0]=75;SkinEnvelope outsidePalette;assert(!outsidePalette.add(inputs));
    inputs[2][0]=0;inputs[0][3]=1.00001f;SkinEnvelope homogeneous;assert(!homogeneous.add(inputs));
    auto implicit=p;implicit.operations[2].source[0].token^=0x00110000;assert(!SkinEnvelope::supports(implicit));

    float identity[16]={};for(unsigned n=0;n<4;++n)identity[5*n]=1;
    // Normal*normal can underflow in blended matrix rows, then a huge position
    // amplifies the lost value. Native non-FTZ and GPU-FTZ answers BOTH belong
    // in the conservative bound (or the specialization must fail open).
    inputs[0]={1e38f,1e38f,1e38f,1};inputs[1]={1e-20f,0,0,0};inputs[2]={0,0,0,0};SkinEnvelope ftz;assert(ftz.add(inputs));
    std::fill(c,c+1024,0.f);for(unsigned n=0;n<3;++n)c[4*(31+n)+n]=1e-20f;
    if(ftz.evaluate(c,identity,bounds))for(unsigned n=0;n<3;++n){const double exact=double(1e-20f)*double(1e-20f)*double(1e38f);assert(bounds.low[n]<=0&&bounds.high[n]>=0);assert(bounds.low[n]<=exact&&bounds.high[n]>=exact);}
    // Source subnormals must not silently turn into exact palette values.
    c[4*31]=std::numeric_limits<float>::denorm_min();assert(!ftz.evaluate(c,identity,bounds));
    // Cancellation/zero positions cannot disguise overflowing blend stages.
    inputs[0]={0,0,0,1};inputs[1]={1,1,1,1};SkinEnvelope overflow;assert(overflow.add(inputs));
    std::fill(c,c+1024,0.f);c[4*31]=std::numeric_limits<float>::max();assert(!overflow.evaluate(c,identity,bounds));
    std::printf("PASS %zu independently deformed vertices enclosed with1200 distinct tuples, changing palettes/cameras, nonunit weight sums, zero weights and DEF override; FTZ/amplification/overflow/template gates safe\n",vertices);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-skin-safety-') as tmp:
    root = Path(tmp)
    (root / 'd3d9.h').write_text(stub)
    (root / 'test.cpp').write_text(harness)
    original = harness.replace('std::swap(p.operations[0].source[0],p.operations[0].source[1]);buildKernel(p);', '')
    for program, fixture in ((original,client_fixtures.four_bone_vs3()),(harness,client_fixtures.four_bone_vs3()),(original,client_fixtures.four_bone_body_vs3())):
      (root / 'test.cpp').write_text(program)
      for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']):
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(root), *fp.test_include_flags(), str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test'), str(fixture)], check=True)
