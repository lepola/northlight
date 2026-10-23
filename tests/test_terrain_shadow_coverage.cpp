#include "patch_terrain_shadow_coverage.h"
#include "world_math.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace NorthlightTerrainCoverage;
using V=std::array<float,4>;
using NorthlightGI::Vec3;
static Word op(unsigned o,unsigned n){return o|(n<<24);}
static void def(std::vector<Word>& w,unsigned c,V v){w.insert(w.end(),{op(81,5),dst(2,c)});for(auto x:v)w.push_back(bits(x));}
static std::vector<Word> ps(bool view){
    std::vector<Word> w{0xffff0300};def(w,0,{.3f,.7f,1,0});
    w.insert(w.end(),{op(31,2),0x8000000au,dst(1,0)});
    if(view)w.insert(w.end(),{op(31,2),0x80050005u,dst(1,1,7)});
    w.insert(w.end(),{op(1,2),dst(0,0),src(1,0),
        op(4,4),dst(0,1,1),src(0,0,0xff),src(2,0,0x00),src(2,0,0x55),
        op(5,3),dst(0,1,7),src(1,0),src(0,1,0x00),
        op(1,2),dst(8,0,7),src(0,1),65535});return w;
}
static std::vector<Word> vs(bool view){
    std::vector<Word> w{0xfffe0300};def(w,8,{1,.5f,0,0});
    w.insert(w.end(),{op(31,2),0x80000000u,dst(1,0),op(31,2),0x80000000u,dst(6,0)});
    if(view)w.insert(w.end(),{op(31,2),0x80050005u,dst(6,1,7)});
    w.insert(w.end(),{op(5,3),dst(0,0),src(2,1),src(1,0,0x55),
        op(4,4),dst(0,0),src(1,0,0x00),src(2,0),src(0,0),
        op(4,4),dst(0,0),src(1,0,0xaa),src(2,2),src(0,0),
        op(2,3),dst(0,0),src(0,0),src(2,3),op(1,2),dst(6,0),src(0,0)});
    if(view)w.insert(w.end(),{op(1,2),dst(0,0,8),src(2,8,0x55),op(1,2),dst(6,1,7),src(0,0)});
    w.push_back(65535);return w;
}
// Execute the actual generated arithmetic, including swizzles, masks and
// modifiers. This catches bytecode that assembles but computes the wrong fade.
struct VM{
    V temp[32]={},constant[256]={},input[10]={},output[12]={};
    V read(Word t){V v{};const V* p=nullptr;unsigned k=sourceType(t),i=t&2047;
        if(k==0)p=&temp[i];else if(k==2)p=&constant[i];else if(k==1)p=&input[i];else assert(false);
        for(unsigned j=0;j<4;++j){v[j]=(*p)[(t>>(16+j*2))&3];if((t>>24&15)==1)v[j]=-v[j];else assert((t>>24&15)==0);}return v;}
    void run(const std::vector<Word>& w){
        for(std::size_t i=1;i<w.size();){unsigned code=w[i]&65535,n=w[i]>>24&15;if(code==65535)break;
            if(code==31){i+=n+1;continue;}
            if(code==81){for(unsigned j=0;j<4;++j)constant[w[i+1]&2047][j]=NorthlightTerrainShadow::wordFloat(w[i+2+j]);i+=n+1;continue;}
            V a=read(w[i+2]),b{},c{},r{};if(n>2)b=read(w[i+3]);if(n>3)c=read(w[i+4]);
            for(unsigned j=0;j<4;++j)switch(code){
            case 1:r[j]=a[j];break;case 2:r[j]=a[j]+b[j];break;case 3:r[j]=a[j]-b[j];break;
            case 4:r[j]=a[j]*b[j]+c[j];break;case 5:r[j]=a[j]*b[j];break;
            case 9:r[j]=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];break;
            case 10:r[j]=std::min(a[j],b[j]);break;case 11:r[j]=std::max(a[j],b[j]);break;
            case 18:r[j]=a[j]*b[j]+(1-a[j])*c[j];break;case 35:r[j]=std::fabs(a[j]);break;
            case 88:r[j]=a[j]>=0?b[j]:c[j];break;default:assert(false);}
            Word d=w[i+1];auto& v=sourceType(d)==0?temp[d&2047]:output[d&2047];
            for(unsigned j=0;j<4;++j)if(d>>(16+j)&1)v[j]=(d&0x100000)?std::max(0.f,std::min(1.f,r[j])):r[j];i+=n+1;
        }
    }
};
static float coverage(V q){
    float u=q[0]*.5f+.5f+.5f/1024,v=.5f-q[1]*.5f+.5f/1024;
    if(u<.003f||u>.997f||v<.003f||v>.997f||q[2]<=0||q[2]>=1)return 0;
    return std::max(0.f,std::min(1.f,(1-std::max(std::fabs(q[0]),std::fabs(q[1])))/.12f));
}
int main(){
    std::mt19937 gen(8471);std::uniform_real_distribution<float> sample(-1.3f,1.3f),unit(0,1);
    unsigned cases=0;
    for(bool hasView:{false,true})for(unsigned sources:{1u,2u}){
        auto original=ps(hasView);std::vector<Word> patched;PixelInfo info;
        assert(pixel(original.data(),original.size(),patched,info,sources));
        auto vertexOriginal=vs(hasView);std::vector<Word> vertexPatched;
        assert(vertex(vertexOriginal.data(),vertexOriginal.size(),vertexPatched));
        VM v;v.input[0]={10,20,30,1};for(unsigned i=0;i<4;++i)v.constant[i][i]=1;v.run(vertexPatched);
        assert((v.output[1]==V{10,20,30,1})); // existing VS overwrote view temporary.w with .5
        for(unsigned i=0;i<30000;++i){
            V q0={sample(gen),sample(gen),sample(gen),1},q1={sample(gen),sample(gen),sample(gen),1};
            if(i%3==0)q1={0,0,.5f,1}; // either active source covering the receiver removes the authored shadow
            float shadow=unit(gen),rgb=unit(gen);VM machine;machine.input[0]={rgb,rgb,rgb,shadow};machine.input[1]={0,0,0,1};
            for(unsigned s=0;s<sources;++s){V q=s?q1:q0;for(unsigned j=0;j<3;++j)machine.constant[info.constants+s*3+j][3]=q[j];}
            machine.run(patched);float weight=sources==1?coverage(q0):std::max(coverage(q0),coverage(q1));
            float expected=rgb*(.7f+.3f*(shadow+(1-shadow)*weight));
            assert(std::fabs(machine.output[0][0]-expected)<2e-6f);++cases;
        }
        // Unsupported prefixes, occupied view outputs and legacy programs are unchanged.
        auto bad=vertexOriginal;for(auto& t:bad)if(t==src(2,1))t=src(2,2);
        assert(!vertex(bad.data(),bad.size(),vertexPatched)&&vertexPatched.empty());
        auto legacy=original;legacy[0]=0xffff0200;assert(!pixel(legacy.data(),legacy.size(),patched,info)&&patched.empty());
    }
    // A camera rotation/translation must cancel: the same receiver retains the
    // same coverage in world space, without reading a previous screen buffer.
    Vec3 pivot(5833,496,667),sun=NorthlightGI::normalized(Vec3(.651f,.651f,.39f));float m[16];
    NorthlightWorldMath::shadowMatrix(pivot,sun,192,m);
    for(unsigned i=0;i<1000;++i){
        float angle=sample(gen)*3,c=std::cos(angle),s=std::sin(angle);
        float inv[]={c,s,0,0,-s,c,0,0,0,0,1,0,5800+sample(gen)*10,500+sample(gen)*10,650,1},rows[16];
        NorthlightWorldMath::replayProjection(inv,m,rows);V view={sample(gen)*300,sample(gen)*300,sample(gen)*300,1},world{},q{},expected{};
        for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)world[j]+=view[k]*inv[k*4+j];
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<4;++k){q[j]+=view[k]*rows[j*4+k];expected[j]+=world[k]*m[k*4+j];}
        assert(std::fabs(coverage(q)-coverage(expected))<5e-5f);++cases;
    }
    std::printf("PASS %u generated-bytecode/camera cases, existing/new view varying, legacy/prefix refusal\n",cases);
}
