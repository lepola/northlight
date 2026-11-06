#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib timing
"""0.3.137 fast ready bounds: bit-identity with the 0.3.136 skin evaluator and
enclosure of the real shader emulation over randomized palettes/weights/views."""
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
fixture = literal(HERE / 'test_replay_skin_safety.py', 'harness').split('int main(')[0]
fixture = fixture.replace('static Status finish(', '[[maybe_unused]] static Status finish(').replace('static Mesh geometry(', '[[maybe_unused]] static Mesh geometry(').replace('static void palette(', '[[maybe_unused]] static void palette(')
# Bit-identity and enclosure do not depend on scheduling: a frozen clock keeps the wall-clock Heavy/Build budgets from ever expiring, so the ASan build under CPU load cannot starve phase().
frozen_clock = r'''
#include <chrono>
struct FrozenClock {
    using duration=std::chrono::nanoseconds;using rep=duration::rep;using period=duration::period;
    using time_point=std::chrono::time_point<FrozenClock,duration>;static constexpr bool is_steady=true;
    static time_point now()noexcept{return time_point(duration(1000000));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK FrozenClock
'''
harness = frozen_clock + fixture + r'''
using Stage=EnvelopeCache::Stage;using Kind=EnvelopeCache::Kind;using Info=EnvelopeCache::WorkInfo;
/* Verbatim 0.3.136 SkinEnvelope::evaluate, as the bit-identity reference. */
static bool legacyEvaluate(const SkinEnvelope& skin,const float* constants,const float* inverse,Bounds& output,bool(*shouldStop)(void*)=nullptr,void* stopContext=nullptr){
    output={};if(!constants||!inverse)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverse[i])||(inverse[i]!=0&&std::fabs(inverse[i])<std::numeric_limits<float>::min()))return false;
    if(inverse[3]!=0||inverse[7]!=0||inverse[11]!=0||inverse[15]!=1)return false;
    if(!std::isfinite(skin.sumLow)||!std::isfinite(skin.sumHigh)||skin.sumLow<0||skin.sumHigh<skin.sumLow||skin.sumHigh>16)return false;
    double low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY},magnitude[3]={};
    double largest[3][4]={};bool any=false;unsigned checkedBones=0;
    for(unsigned bone=0;bone<75;++bone){const auto& b=skin.bones[bone];if(!b.used)continue;
        if((checkedBones++&7u)==0&&shouldStop&&shouldStop(stopContext))return false;any=true;
        for(unsigned axis=0;axis<3;++axis){const float* row=constants+4*(31+3*bone+axis);double lo=row[3],hi=row[3];
            for(unsigned j=0;j<4;++j){if(!std::isfinite(row[j])||(row[j]!=0&&std::fabs(row[j])<std::numeric_limits<float>::min()))return false;largest[axis][j]=std::max(largest[axis][j],std::fabs(double(row[j])));}
            for(unsigned j=0;j<3;++j){double x=double(row[j])*b.low[j],y=double(row[j])*b.high[j];lo+=std::min(x,y);hi+=std::max(x,y);}
            low[axis]=std::min(low[axis],lo);high[axis]=std::max(high[axis],hi);
        }
    }
    if(!any)return false;detail::Vector view={};view[3]={1,0};
    for(unsigned axis=0;axis<3;++axis){
        // w_s >= 0 proves convex-hull enclosure after division by S=sum(w).
        // S is retained as an interval, not assumed to equal one. S=0 is safe.
        const double values[]={low[axis]*skin.sumLow,low[axis]*skin.sumHigh,high[axis]*skin.sumLow,high[axis]*skin.sumHigh};
        double lo=*std::min_element(values,values+4),hi=*std::max_element(values,values+4);
        double positionScale=1;for(unsigned j=0;j<4;++j){
            if(largest[axis][j]*skin.sumHigh>double(std::numeric_limits<float>::max())/32)return false;
            magnitude[axis]+=largest[axis][j]*skin.maxPosition[j];positionScale+=skin.maxPosition[j];
        }magnitude[axis]*=skin.sumHigh;
        // A term traverses <=8 float roundings (4-term MUL/MAD row blend,
        // followed by DP4). 64*epsilon exceeds gamma_16 with ample room for
        // dot reassociation, host-double summation and FTZ at each step.
        // The absolute-product sum bounds cancellation independently of pose.
        const double error=64*std::numeric_limits<float>::epsilon()*magnitude[axis]+1024*std::numeric_limits<float>::min()*positionScale;
        if(!std::isfinite(magnitude[axis])||magnitude[axis]>double(std::numeric_limits<float>::max())/32)return false;
        lo-=error;hi+=error;const float mid=float((lo+hi)*.5);
        view[axis]={mid,std::max(double(mid)-lo,hi-double(mid))};
    }
    for(unsigned axis=0;axis<3;++axis){detail::Value world={inverse[12+axis],0};double scale=std::fabs(world.value);
        for(unsigned j=0;j<3;++j){auto term=detail::mul(view[j],{inverse[4*j+axis],0});scale+=std::fabs(term.value)+term.error;world=detail::add(world,term);}
        world.error+=detail::roundError(scale)*8+.001;
        if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)+world.error>1000000)return false;
        output.low[axis]=std::nextafter(float(double(world.value)-world.error),-std::numeric_limits<float>::infinity());
        output.high[axis]=std::nextafter(float(double(world.value)+world.error),std::numeric_limits<float>::infinity());
    }output.valid=true;return true;
}
static bool same(const Bounds& a,const Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
static Mesh randomMesh(std::mt19937& rng,unsigned vertices,unsigned bones,bool wild){
    Mesh m;m.vertexCount=vertices;m.primitiveCount=vertices/3;m.topology=D3DPT_TRIANGLELIST;m.indexed=false;
    m.streams[0].stride=32;m.streams[0].bytes.resize(size_t(vertices)*32);std::uniform_real_distribution<float> u(-1,1),w(0,1);
    const unsigned first=rng()%(76-bones);
    for(unsigned n=0;n<vertices;++n){float v[7]={u(rng)*(wild?40.f:2.f),u(rng)*(wild?40.f:2.f),u(rng)*(wild?40.f:2.f),w(rng),w(rng),w(rng),w(rng)};
        const unsigned mode=rng()%5;float sum=v[3]+v[4]+v[5]+v[6];
        if(mode==0)for(unsigned k=3;k<7;++k)v[k]/=sum;            /* normalized */
        else if(mode==1){v[4]=v[5]=v[6]=0;v[3]=1;}                   /* rigid single bone */
        else if(mode==2)for(unsigned k=3;k<7;++k)v[k]*=.5f;         /* sum < 1 */
        else if(mode==3)for(unsigned k=3;k<7;++k)v[k]=v[k]*2/sum;   /* sum = 2 */
        else v[3+rng()%4]=0;                                       /* zero slot */
        auto* t=m.streams[0].bytes.data()+n*32;std::memcpy(t,v,28);
        for(unsigned k=0;k<4;++k)t[28+k]=(unsigned char)(first+rng()%bones);}
    return m;
}
static void randomPalette(std::mt19937& rng,float* c,bool wild){
    std::uniform_real_distribution<float> u(-1,1);for(unsigned i=0;i<1024;++i)c[i]=u(rng)*100;
    for(unsigned b=0;b<75;++b){const float s=wild?std::ldexp(1.f,int(rng()%24)-12):1+u(rng)*.3f;const float a=u(rng)*3.2f,e=u(rng)*3.2f;
        const float r[3][3]={{std::cos(a)*s,-std::sin(a)*std::cos(e)*s,std::sin(a)*std::sin(e)*s},{std::sin(a)*s,std::cos(a)*std::cos(e)*s*(wild&&(b&1)?-1.f:1.f),-std::cos(a)*std::sin(e)*s},{0,std::sin(e)*s*(wild?u(rng)*3:1.f),std::cos(e)*s}};
        for(unsigned i=0;i<3;++i){float* row=c+4*(31+3*b+i);for(unsigned j=0;j<3;++j)row[j]=r[i][j]+(wild?u(rng)*.2f:0.f);row[3]=u(rng)*(wild?5000.f:30.f);}}
}
static void randomView(std::mt19937& rng,float* inverse){
    std::uniform_real_distribution<float> u(-1,1);const float a=u(rng)*3.2f,e=u(rng)*1.5f;std::fill(inverse,inverse+16,0.f);
    const float r[3][3]={{std::cos(a),std::sin(a),0},{-std::sin(a)*std::cos(e),std::cos(a)*std::cos(e),std::sin(e)},{std::sin(a)*std::sin(e),-std::cos(a)*std::sin(e),std::cos(e)}};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)inverse[4*i+j]=r[i][j];
    inverse[12]=u(rng)*12000;inverse[13]=u(rng)*12000;inverse[14]=u(rng)*300;inverse[15]=1;
}
static void matrixEdges(){
    // Deliberately unaligned palettes and views, every SIMD lane and both signs.
    alignas(16) float storage[1025],view[17]={};float* c=storage+1;float* inverse=view+1;inverse[0]=inverse[5]=inverse[10]=inverse[15]=1;
    const uint32_t patterns[]={0,1,0x007fffff,0x00800000,0x00800001,0x3f800000,0x7f7ffffe,0x7f7fffff,0x7f800000,0x7f800001,0x7fc12345};
    size_t checks=0;for(unsigned count:{1u,4u,33u,65u,75u}){
        std::fill(c,c+1024,0.f);
        SkinEnvelope skin;for(unsigned n=0;n<count;++n){Four in[16]={};in[0]={float(int(n%7)-3),-1,2,1};in[1]={.1f,.2f,.3f,.4f};const float bone=float((n*37)%75);in[2]={bone,bone,bone,bone};assert(skin.add(in));}
        for(unsigned bone=0;bone<75;++bone){float* row=c+4*(31+3*bone);std::fill(row,row+12,0.f);row[0]=row[5]=row[10]=1;row[3]=float(bone)*.1f;}
        auto equal=[&]{Bounds a,b;const bool x=legacyEvaluate(skin,c,inverse,a),y=skin.evaluate(c,inverse,b);
            assert(x==y&&a.valid==b.valid&&!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12));++checks;};
        equal();for(unsigned bone:{0u,1u,31u,32u,63u,64u,74u})for(unsigned lane=0;lane<12;++lane)for(auto bits:patterns)for(unsigned sign=0;sign<2;++sign){
            float& coefficient=c[4*(31+3*bone)+lane];const float saved=coefficient;const uint32_t u=bits|(sign<<31);std::memcpy(&coefficient,&u,4);equal();coefficient=saved;}
        // Equal signed zeros must preserve std::min/max's first-operand rule.
        for(unsigned n=0;n<skin.usedBones;++n)for(unsigned axis=0;axis<3;++axis){skin.bones[skin.order[n]].low[axis]=-0.f;skin.bones[skin.order[n]].high[axis]=0.f;}equal();
        struct Stop {unsigned calls=0,limit=0;};auto stop=[](void* p){auto& s=*static_cast<Stop*>(p);return ++s.calls==s.limit;};
        for(unsigned limit:{0u,1u,2u}){Stop state{0,limit};Bounds b;const bool valid=skin.evaluate(c,inverse,b,stop,&state);const unsigned calls=(count-1)/32;
            assert(state.calls==std::min(calls,limit?limit:calls));assert(valid==(!limit||limit>calls));if(!valid)assert(!b.valid&&b.low[0]==0&&b.high[0]==0);}
    }
    std::printf("SKIN_MATRIX_EDGES_PASS checks=%zu all_row_lanes signs normal_subnormal_edges nonfinite unused_bones unaligned signed_zero stop_cadence failure_output\n",checks);
}
/* Warm through the production phases, then the fast cheap turn and the Timer
   heavy turn must return identical bits for the same pose/camera. */
static size_t fastTaken=0;
static Bounds phase(EnvelopeCache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& mesh,const float* c,const float* inverse,Stage stage,Info& info){
    for(unsigned attempt=0;attempt<20000;++attempt){cache.beginFrame(false);Budget b;b.maxVertices=stage==Stage::Build?2048:0;b.maxOperations=262144;Bounds out;info={};
        const auto s=cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,b,out,stage,&info);fastTaken+=cache.stats().fastValid;
        if(s==Status::Valid||s==Status::Unsupported||s==Status::Invalid)return out;
        if(info.kind==Kind::Cold){Budget build;build.maxVertices=2048;cache.calculatePrepared(prepared,*mesh,mesh,c,inverse,build,out,Stage::Build);}
    }assert(false);return {};
}
int main(int argc,char** argv){
    assert(argc==2);auto p=load(argv[1]);assert(SkinEnvelope::supports(p));auto prepared=EnvelopeCache::prepareProgram(p,declaration,4);assert(prepared);
    matrixEdges();
    auto palettePrg=p;palettePrg.definitions.push_back({40,{1,0,0,900}});auto paletteDef=EnvelopeCache::prepareProgram(palettePrg,declaration,4);assert(paletteDef);
    std::mt19937 rng(137);float c[1024],inverse[16];size_t meshes=0,poses=0,vertices=0,fast=0,slow=0,rejected=0;EnvelopeCache cache;
    for(unsigned m=0;m<160;++m){const bool wild=m%4==3;auto mesh=std::make_shared<const Mesh>(randomMesh(rng,30+rng()%900,1+rng()%75,wild));++meshes;
        SkinEnvelope skin;for(unsigned n=0;n<mesh->vertexCount;++n){Four in[16]={};for(unsigned k=0;k<3;++k)assert(decodeElement(mesh->streams[0].bytes.data()+n*32+declaration[k].Offset,declaration[k].Type,in[k]));assert(skin.add(in));}
        for(unsigned pose=0;pose<12;++pose){randomPalette(rng,c,wild);randomView(rng,inverse);++poses;
            if(pose==11){c[4*(31+3*(rng()%75))+rng()%4]=std::numeric_limits<float>::denorm_min();}
            if(pose==10){c[4*(31+3*(rng()%75))+rng()%4]=NAN;}
            Bounds reference,current;const bool r=legacyEvaluate(skin,c,inverse,reference),n=skin.evaluate(c,inverse,current);
            assert(r==n&&same(reference,current));
            const size_t before=fastTaken;Info cheapInfo,heavyInfo;const auto a=phase(cache,*prepared,mesh,c,inverse,Stage::Cheap,cheapInfo);const auto b=phase(cache,*prepared,mesh,c,inverse,Stage::Heavy,heavyInfo);
            assert(same(a,b)&&same(a,reference));assert(fastTaken==before+size_t(a.valid)); /* cheap: fast lane; heavy: Timer lane */
            if(a.valid){++fast;vertices+=enclosed(p,*mesh,c,inverse,a);}else ++rejected;
            /* A DEF inside the palette keeps the copying Timer path, with its DEF. */
            const size_t mid=fastTaken;Info di;const auto d=phase(cache,*paletteDef,mesh,c,inverse,Stage::Cheap,di);assert(fastTaken==mid);if(d.valid){++slow;enclosed(palettePrg,*mesh,c,inverse,d);}
        }
    }
    const auto& stats=cache.stats();(void)stats;
    std::printf("PASS fast ready bounds: %zu meshes x %zu poses, %zu valid (bit-identical to 0.3.136 evaluator and Timer path), %zu rejected (NaN/denormal gates), %zu palette-DEF slow-path bounds, %zu shader-emulated vertices enclosed\n",meshes,poses,fast,rejected,slow,vertices);
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-fast-bounds-') as tmp:
    root = Path(tmp)
    (root / 'd3d9.h').write_text(stub)
    (root / 'test.cpp').write_text(harness)
    for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']):
        subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(root), *fp.test_include_flags(), str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
        subprocess.run([str(root / 'test'), str(client_fixtures.four_bone_vs3())], check=True)
