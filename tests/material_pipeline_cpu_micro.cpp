// Focused CPU regressions for the material optimization batch; never starts a graphics device.
#define main unused_material_binding_main
#include "test_material_batch_bindings.cpp"
#undef main
#define CURRENT 1
#include "material_input_poison.h"



static void borrowedProfile(){LimitTexture source;auto& cache=NorthlightMaterialTextures::cache();NorthlightMaterialTextures::enabled=true;std::vector<unsigned char> bytes(128*128*4);for(unsigned i=0;i<bytes.size();++i)bytes[i]=(i*31+i/7)%256;cache.upload(&source,&source,128,128,0,D3DFMT_A8R8G8B8,bytes.data(),bytes.size(),128*4,true,true);
 unsigned cases=0;for(unsigned polarity=0;polarity<3;++polarity)for(unsigned details:{0u,75u,100u})for(unsigned balance:{0u,50u,100u}){NorthlightMaterialHeight::Image image;assert(NorthlightMaterialHeight::read(&source,image));const auto expected=NorthlightMaterialHeight::profile(image,polarity,details,balance);failAllocationAfter=0;const auto actual=NorthlightMaterialHeight::textureProfile(&source,polarity,details,balance);assert(failAllocationAfter==0);failAllocationAfter=-1;assert(!std::memcmp(expected.data(),actual.data(),16));++cases;}
 cache.invalidate(&source);const auto fallback=NorthlightMaterialHeight::textureProfile(&source,0,75,50);assert(fallback==NorthlightMaterialHeight::profile({},0,75,50));assert(source.refs==1);cache.clear();NorthlightMaterialTextures::enabled=false;printf("BORROWED_PROFILE_MINI_PASS cases=%u exactFloat4=1 copiedBytesBefore=65536 copiedBytesAfter=0 vectorAllocationBefore=1 vectorAllocationAfter=0 fallback=1 sourceUnchanged=1\n",cases);
}


namespace Reference {using namespace NorthlightMaterialHeight;
inline bool read(IDirect3DTexture9* source,Image& out){
    out={};if(!source)return false;NorthlightParticleTextures::View uploaded;
    if(NorthlightMaterialTextures::cache().lookup(source,uploaded)){out.width=uploaded.width;out.height=uploaded.height;out.rgba.assign(uploaded.rgba,uploaded.rgba+size_t(uploaded.width)*uploaded.height*4);means(out);return true;}
    D3DSURFACE_DESC desc{};UINT level=0;
    for(;level<source->GetLevelCount();++level){if(FAILED(source->GetLevelDesc(level,&desc)))return false;if(desc.Width<=128&&desc.Height<=128)break;}
    if(level==source->GetLevelCount()||!desc.Width||!desc.Height||(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL|D3DUSAGE_DYNAMIC)))return false;
    using F=NorthlightActorTexture::Format;F format;
    switch(desc.Format){case D3DFMT_A8R8G8B8:format=F::BGRA8;break;case D3DFMT_X8R8G8B8:format=F::BGRX8;break;case D3DFMT_DXT1:format=F::BC1;break;case D3DFMT_DXT3:format=F::BC2;break;case D3DFMT_DXT5:format=F::BC3;break;case D3DFMT_R5G6B5:format=F::RGB565;break;default:return false;}
    const auto pitch=NorthlightActorTexture::rowBytes(desc.Width,format);const auto rows=NorthlightActorTexture::rowCount(desc.Height,format);std::vector<unsigned char> bytes(pitch*rows);
    D3DLOCKED_RECT lock{};if(FAILED(source->LockRect(level,&lock,nullptr,D3DLOCK_READONLY)))return false;
    const bool valid=lock.pBits&&lock.Pitch>=INT(pitch);if(valid)for(unsigned i=0;i<rows;++i)std::memcpy(bytes.data()+i*pitch,static_cast<unsigned char*>(lock.pBits)+size_t(i)*lock.Pitch,pitch);const auto hr=source->UnlockRect(level);
    if(!valid||FAILED(hr)||!NorthlightActorTexture::decode(bytes.data(),bytes.size(),desc.Width,desc.Height,pitch,format,out.rgba))return false;
    out.width=desc.Width;out.height=desc.Height;means(out);return true;
}
}

struct MipTexture:LimitTexture {unsigned queries=0,descs=0,locked=999,levelCount=8;bool bad=false;std::vector<unsigned char> bytes=std::vector<unsigned char>(128*128*4,37);
 DWORD STDMETHODCALLTYPE GetLevelCount()override{++queries;return levelCount;}
 HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level,D3DSURFACE_DESC* out)override{++descs;if(bad&&level==1)return E_FAIL;*out={D3DFMT_A8R8G8B8,D3DRTYPE_SURFACE,0,D3DPOOL_MANAGED,D3DMULTISAMPLE_NONE,0,512u>>level,512u>>level};return D3D_OK;}
 HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* out,const RECT*,DWORD)override{locked=level;out->Pitch=128*4;out->pBits=bytes.data();return D3D_OK;}HRESULT STDMETHODCALLTYPE UnlockRect(UINT)override{return D3D_OK;}};
static void heightMips(){unsigned reference=0,current=0;for(unsigned shape=0;shape<3;++shape){MipTexture a,b;a.bad=b.bad=shape==1;a.levelCount=b.levelCount=shape==2?2:8;NorthlightMaterialHeight::Image x,y;const bool old=Reference::read(&a,x),now=NorthlightMaterialHeight::read(&b,y);assert(old==now&&old==(shape==0));assert(a.locked==b.locked&&a.descs==b.descs&&x.width==y.width&&x.height==y.height&&x.rgba==y.rgba&&!std::memcmp(&x.mean,&y.mean,4)&&!std::memcmp(&x.accentMean,&y.accentMean,4));if(!shape){reference=a.queries;current=b.queries;assert(reference==4&&current==1&&a.locked==2);}}printf("HEIGHT_MIPS_MINI_PASS cases=3 referenceQueries=%u currentQueries=%u exactPixelsAndMeans=1 failedDescriptor=1 noSuitableMip=1\n",reference,current);}



static void modelStatistics(){LimitTexture source;auto& cache=NorthlightMaterialTextures::cache();NorthlightMaterialTextures::enabled=true;std::vector<unsigned char> bytes(128*128*4);for(unsigned i=0;i<bytes.size();++i)bytes[i]=(i*37+i/11)%256;cache.upload(&source,&source,128,128,0,D3DFMT_A8R8G8B8,bytes.data(),bytes.size(),128*4,true,true);
 NorthlightMaterialHeight::Image a,b;assert(NorthlightMaterialHeight::read(&source,a)&&NorthlightMaterialHeight::read(&source,b,false));assert(a.rgba==b.rgba&&a.width==b.width&&a.height==b.height);unsigned values=0;
 for(unsigned polarity=0;polarity<3;++polarity)for(unsigned y=0;y<a.height;++y)for(unsigned x=0;x<a.width;++x){float old=NorthlightMaterialHeight::modelValue(a,x,y,polarity),now=NorthlightMaterialHeight::modelValue(b,x,y,polarity);assert(!std::memcmp(&old,&now,4));++values;}
 std::vector<double> times[2];for(unsigned block=0;block<1;++block)for(unsigned turn=0;turn<2;++turn){unsigned mode=(block+turn)&1;auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<32;++i){NorthlightMaterialHeight::Image image;assert(NorthlightMaterialHeight::read(&source,image,mode==0));}double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/32;if(block>=0)times[mode].push_back(ms);}double med[2];for(unsigned mode=0;mode<2;++mode){std::sort(times[mode].begin(),times[mode].end());med[mode]=times[mode][times[mode].size()/2];}
 printf("MODEL_STATISTICS_MINI_PASS values=%u floatBitsExact=1 reference=%.9f current=%.9f ratio=%.5f\n",values,med[0],med[1],med[1]/med[0]);cache.clear();NorthlightMaterialTextures::enabled=false;}



static void modelField(){unsigned values=0;for(auto size:{std::array<unsigned,2>{1,1},std::array<unsigned,2>{1,63},std::array<unsigned,2>{63,1},std::array<unsigned,2>{13,47},std::array<unsigned,2>{32,32},std::array<unsigned,2>{127,93},std::array<unsigned,2>{128,128}}){NorthlightMaterialHeight::Image image;image.width=size[0];image.height=size[1];image.rgba.resize(size_t(size[0])*size[1]*4);for(unsigned i=0;i<image.rgba.size();++i)image.rgba[i]=(i*37+i/11)%256;
 for(unsigned polarity=0;polarity<3;++polarity){std::vector<float> result;NorthlightMaterialHeight::modelValues(image,polarity,result);for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){const auto expected=NorthlightMaterialHeight::modelValue(image,x,y,polarity);assert(!std::memcmp(&expected,&result[size_t(y)*image.width+x],4));++values;}
 failAllocationAfter=0;NorthlightMaterialHeight::modelValues(image,polarity,result);failAllocationAfter=-1;for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){const auto expected=NorthlightMaterialHeight::modelValue(image,x,y,polarity);assert(!std::memcmp(&expected,&result[size_t(y)*image.width+x],4));}}
}
 printf("MODEL_FIELD_MINI_PASS values=%u floatBitsExact=1 oddDimensions=1 clampedEdges=1 allocationFallback=1\n",values);}


namespace NorthlightMaterialHeight{
inline std::array<float,4> profileReference(IDirect3DTexture9* source,unsigned polarity,unsigned details,unsigned balance){
    Image image;NorthlightParticleTextures::View uploaded;
    if(NorthlightMaterialTextures::cache().lookup(source,uploaded)){image.width=uploaded.width;image.height=uploaded.height;means(image,uploaded.rgba,size_t(uploaded.width)*uploaded.height*4);}
    else read(source,image);
    return profile(image,polarity,details,balance);
}

}

static void selectiveStatistics(){using namespace NorthlightMaterialHeight;LimitTexture source;auto& cache=NorthlightMaterialTextures::cache();NorthlightMaterialTextures::enabled=true;std::vector<unsigned char> bytes(128*128*4);for(unsigned i=0;i<bytes.size();++i)bytes[i]=(i*37+i/11)%256;cache.upload(&source,&source,128,128,0,D3DFMT_A8R8G8B8,bytes.data(),bytes.size(),128*4,true,true);unsigned cases=0;
 for(unsigned p=0;p<3;++p)for(unsigned d:{0u,75u,100u})for(unsigned b:{0u,50u,100u}){const auto old=profileReference(&source,p,d,b),now=textureProfile(&source,p,d,b);assert(!std::memcmp(old.data(),now.data(),16));Image a,z;assert(read(&source,a)&&read(&source,z,true,p==0&&d));assert(profile(a,p,d,b)==profile(z,p,d,b));++cases;}
 std::vector<double> times[2];float sink=0;for(unsigned block=0;block<1;++block)for(unsigned turn=0;turn<2;++turn){unsigned mode=(block+turn)&1;auto begin=std::chrono::steady_clock::now();for(unsigned rep=0;rep<1;++rep)sink+=(mode?textureProfile(&source,1,75,50):profileReference(&source,1,75,50))[0];double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/64;if(block>=0)times[mode].push_back(ms);}assert(sink>0);double med[2];for(unsigned i=0;i<2;++i){std::sort(times[i].begin(),times[i].end());med[i]=times[i][times[i].size()/2];}printf("SELECTIVE_STATISTICS_MINI_PASS cases=%u profileBitsExact=1 reference=%.9f current=%.9f ratio=%.5f\n",cases,med[0],med[1],med[1]/med[0]);cache.clear();NorthlightMaterialTextures::enabled=false;}



static volatile float sink;
static void luminanceHeight(){using namespace NorthlightMaterialHeight;std::vector<std::array<float,3>> pixels;for(unsigned r=0;r<256;++r)for(unsigned g=0;g<256;++g)for(float b:{0.f,float(g)/255,1.f})pixels.push_back({float(r)/255,float(g)/255,b});unsigned values=0;
 for(float mean:{0.f,.125f,.5f,.875f,1.f})for(float polarity:{-1.f,1.f})for(float offset:{-.25f,0.f,.25f}){const std::array<float,4> profile{mean,0,polarity,offset};for(const auto& rgb:pixels){const float expected=value(rgb[0],rgb[1],rgb[2],profile),actual=valueWithoutAccent(rgb[0],rgb[1],rgb[2],profile);assert(!std::memcmp(&expected,&actual,4));++values;}}
 std::vector<double> times[2];const std::array<float,4> profile{.5f,0,1,0};for(unsigned block=0;block<1;++block)for(unsigned turn=0;turn<2;++turn){unsigned mode=(block+turn)&1;auto begin=std::chrono::steady_clock::now();float sum=0;for(unsigned rep=0;rep<1;++rep)for(const auto& rgb:pixels)sum+=mode?valueWithoutAccent(rgb[0],rgb[1],rgb[2],profile):value(rgb[0],rgb[1],rgb[2],profile);sink=sum;double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/4;if(block>=0)times[mode].push_back(ms);}double med[2];for(unsigned i=0;i<2;++i){std::sort(times[i].begin(),times[i].end());med[i]=times[i][times[i].size()/2];}printf("LUMINANCE_HEIGHT_MINI_PASS values=%u bitsExact=1 reference=%.9f current=%.9f ratio=%.5f\n",values,med[0],med[1],med[1]/med[0]);}


namespace NorthlightActorDeformation{
class FrozenCache {
    struct Entry {std::shared_ptr<const PreparedBindings> prepared;uint64_t key=0,touched=0;};
    std::array<Entry,64> entries_{};uint64_t stamp_=0;
public:
    uint64_t hits=0,preparations=0;
    std::shared_ptr<const PreparedBindings> acquire(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count){
        if(!elements||!count||count>MAXD3DDECLLENGTH+1)return {};
        uint64_t key=14695981039346656037ull;auto mix=[&](uint64_t n){key=(key^n)*1099511628211ull;};
        for(const auto& input:program.inputs){mix(input.reg);mix(input.usage);mix(input.index);}mix(program.inputs.size());
        for(size_t i=0;i<count;++i){const auto& e=elements[i];mix(e.Stream);mix(e.Offset);mix(e.Type);mix(e.Method);mix(e.Usage);mix(e.UsageIndex);}mix(count);for(const auto& stream:mesh.streams)mix(stream.stride);
        ++stamp_;Entry* oldest=&entries_[0];
        for(auto& entry:entries_){if(entry.touched<oldest->touched)oldest=&entry;
            if(entry.prepared&&entry.key==key&&entry.prepared->declarationMatches(elements,count)&&entry.prepared->validFor(program,mesh)){entry.touched=stamp_;++hits;return entry.prepared;}}
        auto prepared=std::make_shared<PreparedBindings>();if(!prepareBindings(program,mesh,elements,count,*prepared))return {};
        ++preparations;*oldest={prepared,key,stamp_};return prepared;
    }
};
}

static void poseBindings(){using namespace NorthlightActorDeformation;Program program;program.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDWEIGHT,0},{2,D3DDECLUSAGE_BLENDINDICES,0}};NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=8;mesh.streams[0].stride=2048;mesh.streams[0].bytes.resize(8*2048);D3DVERTEXELEMENT9 elements[4]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,1024,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,1040,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};PreparedBindingCache now;FrozenCache old;
 for(unsigned i=0;i<64;++i){elements[0].Offset=WORD(i*16);auto a=old.acquire(program,mesh,elements,4),b=now.acquire(program,mesh,elements,4);assert(a&&b&&a->declarationMatches(elements,4)&&b->declarationMatches(elements,4)&&a->validFor(program,mesh)&&b->validFor(program,mesh));}
 std::vector<double> times[2];for(unsigned block=0;block<1;++block)for(unsigned turn=0;turn<2;++turn){unsigned mode=(block+turn)&1;auto begin=std::chrono::steady_clock::now();for(unsigned i=0;i<8;++i){auto result=mode?now.acquire(program,mesh,elements,4):old.acquire(program,mesh,elements,4);assert(result);}double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/100000;if(block>=0)times[mode].push_back(ms);}
 elements[0].Offset=0;assert(old.acquire(program,mesh,elements,4)&&now.acquire(program,mesh,elements,4));elements[0].Usage=D3DDECLUSAGE_NORMAL;assert(!old.acquire(program,mesh,elements,4)&&!now.acquire(program,mesh,elements,4));elements[0].Usage=D3DDECLUSAGE_POSITION;mesh.streams[0].bytes.resize(4);assert(!old.acquire(program,mesh,elements,4)&&!now.acquire(program,mesh,elements,4));mesh.streams[0].bytes.resize(8*2048);program.inputs[0].reg=7;auto a=old.acquire(program,mesh,elements,4),b=now.acquire(program,mesh,elements,4);assert(a&&b&&a->bindings[0].input.reg==7&&b->bindings[0].input.reg==7);
 double med[2];for(unsigned i=0;i<2;++i){std::sort(times[i].begin(),times[i].end());med[i]=times[i][times[i].size()/2];}printf("POSE_BINDINGS_MINI_PASS entries=64 repeated=100000 reference=%.9f current=%.9f ratio=%.5f exactLayoutAndInputs=1 shortStreamRejected=1\n",med[0],med[1],med[1]/med[0]);}


namespace NorthlightActorDeformation{
inline bool worldPositionsPreparedReference(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* constants,const float* inverseView,std::vector<Position>& output){
    output.clear();if(!constants||!inverseView||!prepared.validFor(program,mesh))return false;
    const auto& bindings=prepared.bindings;
    std::vector<Position> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;Position out;
        out.x=v[0]*inverseView[0]+v[1]*inverseView[4]+v[2]*inverseView[8]+inverseView[12];
        out.y=v[0]*inverseView[1]+v[1]*inverseView[5]+v[2]*inverseView[9]+inverseView[13];
        out.z=v[0]*inverseView[2]+v[1]*inverseView[6]+v[2]*inverseView[10]+inverseView[14];
        if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||std::fabs(out.x)>1000000||std::fabs(out.y)>1000000||std::fabs(out.z)>1000000)return false;positions.push_back(out);}
    output=std::move(positions);return true;
}
inline bool textureUVsPreparedReference(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* constants,std::vector<std::array<float,2>>& output){
    output.clear();if(!program.textureCoordinates||!constants||!prepared.validFor(program,mesh))return false;
    const auto& bindings=prepared.bindings;
    std::vector<std::array<float,2>> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;positions.push_back({v[0],v[1]});}
    output=std::move(positions);return true;
}
// Original transient-call path: avoid adding cache metadata copies for callers
// that do not reuse prepared bindings. Math and validation remain unchanged.
inline bool worldPositionsReference(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,std::size_t elementCount,const float* constants,const float* inverseView,std::vector<Position>& output){
    output.clear();if(!constants||!inverseView||!elements||!elementCount||elementCount>MAXD3DDECLLENGTH+1||mesh.vertexCount>262144)return false;
    struct Binding{Input input;D3DVERTEXELEMENT9 element;};std::vector<Binding> bindings;
    for(auto input:program.inputs){bool found=false;for(std::size_t j=0;j<elementCount;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
        if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;auto& s=mesh.streams[e.Stream];unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
        if(!size||UINT(e.Offset)+size>s.stride||std::uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;bindings.push_back({input,e});found=true;}if(!found)return false;}
    std::vector<Position> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;Position out;
        out.x=v[0]*inverseView[0]+v[1]*inverseView[4]+v[2]*inverseView[8]+inverseView[12];
        out.y=v[0]*inverseView[1]+v[1]*inverseView[5]+v[2]*inverseView[9]+inverseView[13];
        out.z=v[0]*inverseView[2]+v[1]*inverseView[6]+v[2]*inverseView[10]+inverseView[14];
        if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||std::fabs(out.x)>1000000||std::fabs(out.y)>1000000||std::fabs(out.z)>1000000)return false;positions.push_back(out);}
    output=std::move(positions);return true;
}
inline bool textureUVsReference(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,std::size_t elementCount,const float* constants,std::vector<std::array<float,2>>& output){
    output.clear();if(!program.textureCoordinates||!constants||!elements||!elementCount||elementCount>MAXD3DDECLLENGTH+1||mesh.vertexCount>262144)return false;
    struct Binding{Input input;D3DVERTEXELEMENT9 element;};std::vector<Binding> bindings;
    for(auto input:program.inputs){bool found=false;for(std::size_t j=0;j<elementCount;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
        if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;auto& s=mesh.streams[e.Stream];unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
        if(!size||UINT(e.Offset)+size>s.stride||std::uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;bindings.push_back({input,e});found=true;}if(!found)return false;}
    std::vector<std::array<float,2>> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;positions.push_back({v[0],v[1]});}
    output=std::move(positions);return true;
}

}

static void deformationDispatch(){using namespace NorthlightActorDeformation;using namespace NorthlightWaterShaderPatch;Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};p.definitions={{0,{3,1,0,0}}};auto op=[&](unsigned code,Word destination,Word a,Word b=0,Word address=0){Operation o;o.code=code;o.destination=destination;o.source[0].token=a;o.source[0].address=address;o.source[1].token=b;p.operations.push_back(o);};op(1,dst(0,0,8),src(2,0,0x55));op(5,dst(0,1,1),src(2,0,0),src(1,1,0));op(46,dst(3,0,1),src(0,1,0));for(unsigned axis=0;axis<3;++axis)op(9,dst(0,0,1u<<axis),src(2,31+axis)|0x2000,src(1,0),src(3,0,0));buildKernel(p);assert(p.kernel.singleSkin());
 NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=4096;mesh.streams[0].stride=24;mesh.streams[0].bytes.resize(4096*24);for(unsigned i=0;i<4096;++i){float data[]={float(i%13)*.1f,float(i%19)*.1f,float(i%23)*.1f,0,float(i%7)*.01f,float(i%11)*.01f};auto* target=mesh.streams[0].bytes.data()+i*24;std::memcpy(target,data,24);target[12]=i%4;}
 D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};PreparedBindings prepared;assert(prepareBindings(p,mesh,elements,4,prepared));float bank[1024]={},inverse[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};for(unsigned b=0;b<4;++b)for(unsigned row=0;row<3;++row){bank[(31+3*b+row)*4+row]=1;bank[(31+3*b+row)*4+3]=float(b)*.01f;}
 unsigned compared=0;for(unsigned frame=0;frame<32;++frame){bank[31*4+3]=float(frame)*.17f;std::vector<Position> a,b,c,d;assert(worldPositionsPreparedReference(p,mesh,prepared,bank,inverse,a)&&worldPositionsPrepared(p,mesh,prepared,bank,inverse,b)&&worldPositionsReference(p,mesh,elements,4,bank,inverse,c)&&worldPositions(p,mesh,elements,4,bank,inverse,d));assert(a.size()==b.size()&&a.size()==c.size()&&a.size()==d.size()&&!std::memcmp(a.data(),b.data(),a.size()*sizeof(Position))&&!std::memcmp(a.data(),c.data(),a.size()*sizeof(Position))&&!std::memcmp(a.data(),d.data(),a.size()*sizeof(Position)));compared+=a.size();}

 auto safety=[&](Program current){PreparedBindings bindings;assert(prepareBindings(current,mesh,elements,4,bindings));std::vector<Position> expected,actual,poison;const bool x=worldPositionsPreparedReference(current,mesh,bindings,bank,inverse,expected),y=worldPositionsPrepared(current,mesh,bindings,bank,inverse,actual),z=worldPositionsPreparedPoison(current,mesh,bindings,bank,inverse,poison);assert(x==y&&x==z&&expected.size()==actual.size()&&expected.size()==poison.size());if(x)assert(!std::memcmp(expected.data(),actual.data(),expected.size()*sizeof(Position))&&!std::memcmp(expected.data(),poison.data(),expected.size()*sizeof(Position)));};
 safety(p);auto missing=p;missing.inputs.pop_back();buildKernel(missing);safety(missing);auto unknown=p;unknown.kernel={};safety(unknown);unknown=p;unknown.kernel.ops.pop_back();safety(unknown);
 Program uv;uv.major=3;uv.positionRegister=32;uv.textureCoordinates=true;uv.outputMask=3;uv.inputs={{0,D3DDECLUSAGE_TEXCOORD,0}};Operation copy;copy.code=1;copy.destination=dst(0,32,3);copy.source[0].token=src(1,0);uv.operations={copy};buildKernel(uv);PreparedBindings uvBindings;assert(prepareBindings(uv,mesh,elements,4,uvBindings));std::vector<std::array<float,2>> a,b,c,d;assert(textureUVsPreparedReference(uv,mesh,uvBindings,bank,a)&&textureUVsPrepared(uv,mesh,uvBindings,bank,b)&&textureUVsReference(uv,mesh,elements,4,bank,c)&&textureUVs(uv,mesh,elements,4,bank,d));assert(a==b&&a==c&&a==d);compared+=a.size();
 std::vector<double> times[2];for(unsigned block=0;block<1;++block)for(unsigned turn=0;turn<2;++turn){unsigned mode=(block+turn)&1;std::vector<Position> positions;auto begin=std::chrono::steady_clock::now();for(unsigned rep=0;rep<1;++rep)assert(mode?worldPositionsPrepared(p,mesh,prepared,bank,inverse,positions):worldPositionsPreparedReference(p,mesh,prepared,bank,inverse,positions));double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/16;if(block>=0)times[mode].push_back(ms);}double med[2];for(unsigned i=0;i<2;++i){std::sort(times[i].begin(),times[i].end());med[i]=times[i][times[i].size()/2];}printf("DEFORMATION_DISPATCH_MINI_PASS vertices=%u positionAndUVExact=1 preparedAndTransient=1 reference=%.9f current=%.9f ratio=%.5f\n",compared,med[0],med[1],med[1]/med[0]);}

struct PoolDevice:MaterialBindingDevice {unsigned creates=0,captures=0,live=0;bool failNext=false;
 struct Block:MaterialBindingDevice::Block {PoolDevice& owner;Block(PoolDevice& d):MaterialBindingDevice::Block(d),owner(d){++owner.live;}~Block(){--owner.live;} ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
 HRESULT STDMETHODCALLTYPE Capture()override{++owner.captures;if(owner.failNext){owner.failNext=false;return E_FAIL;}return MaterialBindingDevice::Block::Capture();}};
 HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out)override{++creates;if(failNext){failNext=false;*out=nullptr;return E_FAIL;}*out=new Block(*this);return D3D_OK;}};
void MaterialCaptureStateTestAccess::microPool(){PoolDevice d;const auto before=d.state;unsigned reusedCreates=0;{
 NorthlightAutoBump b(&d);b.width=b.heightPixels=64;b.target=&d.capture;b.target->AddRef();b.captureDepth=&d.privateDepth;b.captureDepth->AddRef();auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->vertexCount=3;mesh->primitiveCount=1;mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);
 auto queue=[&](){auto p=std::make_unique<NorthlightAutoBump::Deferred>();p->mesh=mesh;p->tag=19;p->vertex=&d.vs;p->vertex->AddRef();p->pixel=&d.ps;p->pixel->AddRef();p->declaration=&d.decl;p->declaration->AddRef();p->vertexCount=0;p->vertexFloatOnly=true;p->pixelFloatOnly=true;p->viewport={0,0,64,64,0,1};std::fill(p->states,p->states+14,0);p->states[4]=15;std::fill(p->raster,p->raster+NorthlightAutoBump::DeferredRasterCount,0);b.deferred.push_back(std::move(p));};
 for(unsigned i=0;i<30;++i){queue();assert(b.finishCaptures()&&d.state==before);}reusedCreates=d.creates;assert(reusedCreates==(CURRENT?1u:30u));assert(d.live==(CURRENT?1u:0u));b.endFrame();assert(!d.live);queue();assert(b.finishCaptures()&&d.creates==reusedCreates+1&&d.state==before);queue();d.failNext=true;assert(!b.finishCaptures()&&!d.failNext&&d.state==before);assert(!d.live);b.reset();assert(!d.live);
 }assert(d.state==before&&d.vs.refs==1&&d.ps.refs==1&&d.decl.refs==1&&d.capture.refs==1&&d.privateDepth.refs==1);printf("CAPTURE_STATE_POOL_MINI_PASS flushes=30 creates=%u captures=%u frameRelease=1 failedCapture=1 restored=1 refs=1\n",reusedCreates,d.captures);}

int main(){borrowedProfile();heightMips();modelStatistics();modelField();selectiveStatistics();luminanceHeight();poseBindings();deformationDispatch();MaterialCaptureStateTestAccess::microPool();std::puts("MATERIAL_PIPELINE_MICRO_PASS groups=9 graphicsDevice=0");}
