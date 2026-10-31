#include "native_shadow_observation.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace NorthlightNativeShadow;
// Public synthetic affine shader, assembled independently from register roles.
static Word destination(unsigned type,unsigned index,unsigned mask=15){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|index|(mask<<16);}
static Word source(unsigned type,unsigned index,unsigned swizzle=0xe4){return destination(type,index,0)|(swizzle<<16);}
static std::vector<Word> rigid(unsigned model){
    std::vector<Word> words{model==1?0xfffe0101u:model==2?0xfffe0200u:0xfffe0300u};
    auto emit=[&](unsigned op,std::initializer_list<Word> args){words.push_back(op|(model==1?0:unsigned(args.size())<<24));words.insert(words.end(),args);};
    emit(81,{destination(2,0),0x3f800000u,0,0,0});
    emit(31,{0x80000000u,destination(1,0)});emit(31,{0x80000005u,destination(1,1)});
    if(model==3){emit(31,{0x80000000u,destination(6,0)});emit(31,{0x80000005u,destination(6,1,3)});emit(31,{0x80010005u,destination(6,2,7)});}
    emit(1,{destination(0,0,8),source(2,0,0)});
    for(unsigned row=0;row<3;++row)emit(9,{destination(0,0,1u<<row),source(2,31+row),source(1,0)});
    for(unsigned row=0;row<4;++row)emit(9,{destination(model==3?6:4,0,1u<<row),source(2,2+row),source(0,0)});
    emit(1,{destination(6,model==3?2:1,7),source(0,0)});
    emit(9,{destination(6,model==3?1:0,1),source(2,6),source(1,1)});
    emit(9,{destination(6,model==3?1:0,2),source(2,7),source(1,1)});words.push_back(0xffff);return words;
}
static Proof proof(unsigned model,Skin skin){return {model,skin,skin!=Skin::Rigid,skin!=Skin::Rigid,{Range{2,6},Range{skin==Skin::Rigid?31u:14u,3}}};}
static Word word(const unsigned char* bytes){return Word(bytes[0])|(Word(bytes[1])<<8)|(Word(bytes[2])<<16)|(Word(bytes[3])<<24);}
static void controls(){
    for(unsigned model=1;model<=3;++model){const auto words=rigid(model);Proof p;
        assert(analyze(words.data(),words.size(),p)&&valid(p)&&p.major==model&&p.skin==Skin::Rigid&&!p.affine&&!p.relativePalette);
        for(size_t end=0;end<words.size();++end){Proof rejected=p;assert(!analyze(words.data(),end,rejected)&&!valid(rejected));}
        auto extra=words;extra.push_back(0);assert(!analyze(extra.data(),extra.size(),p));
        auto comment=words;comment.insert(comment.begin()+1,{0x0003fffeu,0xffffu,0x3148534eu,0xdeadbeefu});assert(analyze(comment.data(),comment.size(),p));
        // A register changed in the projection, palette, UV or output is not
        // interchangeable with an observed native domain. Never loosen gates.
        for(size_t i=2;i+1<words.size();++i){auto changed=words;changed[i]^=1;assert(!analyze(changed.data(),changed.size(),p));}
    }
    Proof p;assert(!analyze(nullptr,100,p));std::vector<Word> oversized(513);assert(!analyze(oversized.data(),oversized.size(),p));
    auto malformed=rigid(3);malformed.insert(malformed.begin()+1,0x7ffffffeu);assert(!analyze(malformed.data(),malformed.size(),p));
    float constants[256][4];bool known[256];for(unsigned i=0;i<256;++i){known[i]=true;for(unsigned c=0;c<4;++c)constants[i][c]=float(i*4+c)+.25f;}
    for(Skin skin:{Skin::Rigid,Skin::Single,Skin::Weighted}){
        const auto meta=proof(3,skin);auto s=sample(meta,constants,known,true,123,456,UINT64_C(0x0123456789abcdef));assert(s.knownRows==511&&s.finiteRows==511);
        assert(s.rows[0][0]==8.25f&&s.rows[5][3]==31.25f&&s.rows[6][0]==float((skin==Skin::Rigid?31:14)*4)+.25f);
        std::array<unsigned char,SerializedBytes+8> bytes;bytes.fill(0xa5);size_t n=999;
        assert(serialize(s,bytes.data(),SerializedBytes,n)&&n==SerializedBytes&&word(bytes.data())==0x3148534e&&word(bytes.data()+4)==SerializedBytes);
        assert(word(bytes.data()+8)==123&&word(bytes.data()+12)==456&&word(bytes.data()+16)==0x89abcdefu&&word(bytes.data()+20)==0x01234567u);
        assert(word(bytes.data()+28)==(2u|(6u<<16))&&word(bytes.data()+32)==((skin==Skin::Rigid?31u:14u)|(3u<<16)));
        assert(word(bytes.data()+36)==0x01ff01ffu);for(size_t i=SerializedBytes;i<bytes.size();++i)assert(bytes[i]==0xa5);
        assert(!serialize(s,bytes.data(),SerializedBytes-1,n)&&n==0);assert(!serialize(s,nullptr,SerializedBytes,n)&&n==0);
        known[3]=false;auto unknown=sample(meta,constants,known,true,0,0,0);assert(unknown.knownRows==509&&unknown.rows[1][0]==0);known[3]=true;
        constants[2][0]=NAN;auto bad=sample(meta,constants,known,true,0,0,0);assert(bad.knownRows==511&&bad.finiteRows==510);constants[2][0]=8.25f;
        assert(!sample(meta,constants,known,false,0,0,0).knownRows&&!sample(meta,nullptr,known,true,0,0,0).knownRows);
        s.knownRows|=1024;assert(!serialize(s,bytes.data(),bytes.size(),n)&&n==0);
    }
    Limiter limiter;const auto meta=proof(3,Skin::Single);
    assert(!limiter.take(false,true,0,meta)&&!limiter.take(true,false,0,meta)&&!limiter.total());
    assert(limiter.take(true,true,0,meta)&&!limiter.take(true,true,119,meta)&&limiter.take(true,true,120,meta));
    assert(limiter.take(true,true,120,proof(3,Skin::Rigid))&&!limiter.take(true,true,120,proof(3,Skin::Weighted)));
    for(unsigned frame=240;frame<30000;++frame)for(unsigned model=1;model<=3;++model)for(Skin skin:{Skin::Rigid,Skin::Single,Skin::Weighted})limiter.take(true,true,frame,proof(model,skin));
    assert(limiter.total()==Limiter::SessionLimit&&!limiter.take(true,true,30001,meta));
    Limiter wrap;assert(wrap.take(true,true,UINT32_MAX-59,meta)&&!wrap.take(true,true,59,meta)&&wrap.take(true,true,60,meta));
    std::puts("PASS native shadow structural rejection, exact matrix ranges/raw serialization/unknown provenance, finite masks and bounded profile/frame/session observation");
}
static void corpus(const char* path){
    std::ifstream stream(path,std::ios::binary);Word magic=0,count=0;assert(stream.read(reinterpret_cast<char*>(&magic),4)&&magic==0x3143534e&&stream.read(reinterpret_cast<char*>(&count),4)&&count==18);
    unsigned accepted=0,rejected=0;bool seen[9]={};
    for(unsigned i=0;i<count;++i){Word expect[4];assert(stream.read(reinterpret_cast<char*>(expect),sizeof expect));assert(expect[3]>=2&&expect[3]<=512);std::vector<Word> words(expect[3]);assert(stream.read(reinterpret_cast<char*>(words.data()),words.size()*4));Proof p;
        const bool ok=analyze(words.data(),words.size(),p);
        if(ok!=bool(expect[0]))std::fprintf(stderr,"Corpus record %u: expected=%u model=%u skin=%u words=%u actual=%u\n",i,expect[0],expect[1],expect[2],expect[3],unsigned(ok));
        assert(ok==bool(expect[0]));
        if(ok){assert(p.major==expect[1]&&unsigned(p.skin)==expect[2]&&valid(p));seen[(p.major-1)*3+unsigned(p.skin)-1]=true;++accepted;
            for(size_t cut=0;cut<words.size();++cut){Proof unknown;assert(!analyze(words.data(),cut,unknown));}
            for(size_t token=1;token+1<words.size();++token){auto changed=words;changed[token]^=1;Proof unknown;assert(!analyze(changed.data(),changed.size(),unknown));}
            if(p.major>=2&&p.skin!=Skin::Rigid){
                bool checked=false;
                for(size_t token=1;token+3<words.size();++token){
                    if(words[token]!=0x03000005u)continue;
                    auto changed=words;std::swap(changed[token+2],changed[token+3]);Proof unknown;
                    assert(!analyze(changed.data(),changed.size(),unknown));checked=true;break;
                }
                assert(checked);
            }
        }else ++rejected;
    }assert(stream.peek()==EOF&&accepted==9&&rejected==9);for(bool value:seen)assert(value);
    std::puts("PASS private original MPQ corpus: all nine structural profiles accepted, all nine main-view Diffuse comparisons rejected, truncations rejected; no bytecode committed");
}
int main(int argc,char** argv){assert(argc==1||argc==2);controls();if(argc==2)corpus(argv[1]);}
