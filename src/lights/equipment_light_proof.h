#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// Called only for exact known native Model/WMO shader identities. Prove the
// register layout from the complete straight-line program, rather than a
// compiler-specific instruction order. No shader execution or GPU readback.
namespace NorthlightEquipmentLightProof {
inline unsigned kind(std::uint32_t t){return ((t>>28)&7)|((t>>8)&24);}
inline unsigned index(std::uint32_t t){return t&2047;}
struct Key {unsigned op,a,b;bool operator==(const Key& o)const{return op==o.op&&a==o.a&&b==o.b;}};
struct Hash {size_t operator()(const Key& k)const{return (size_t(k.a)*16777619u+k.b)*16777619u+k.op;}};
struct Node {Key key{};unsigned attenuation=0;std::array<unsigned,3> color{};};
struct Expressions {
    enum {Constant=1,Literal,Atom,Add,Mul,Neg,Reciprocal,InvRoot,Minimum,Maximum,Other};
    std::vector<Node> nodes{{}};std::unordered_map<Key,unsigned,Hash> intern;
    unsigned expected[4]{};
    unsigned make(unsigned op,unsigned a,unsigned b=0){
        if((op==Add||op==Mul||op==Minimum||op==Maximum)&&a>b)std::swap(a,b);
        Key key{op,a,b};const auto old=intern.find(key);if(old!=intern.end())return old->second;
        Node n;n.key=key;
        if(op>=Add){if(a<nodes.size()){n.attenuation|=nodes[a].attenuation;for(unsigned k=0;k<3;++k)n.color[k]|=nodes[a].color[k];}
            if(b<nodes.size()){n.attenuation|=nodes[b].attenuation;for(unsigned k=0;k<3;++k)n.color[k]|=nodes[b].color[k];}}
        if(op==Reciprocal)for(unsigned i=0;i<4;++i)if(expected[i]&&a==expected[i])n.attenuation|=1u<<i;
        if(op==Mul){const auto color=[&](unsigned c,unsigned factor){if(c>=nodes.size()||factor>=nodes.size())return;
            const auto& key=nodes[c].key;if(key.op!=Constant||key.a/4<17||key.a/4>20||key.a%4>=3)return;
            const unsigned i=key.a/4-17;if(nodes[factor].attenuation&(1u<<i))n.color[key.a%4]|=1u<<i;};color(a,b);color(b,a);}
        const unsigned id=unsigned(nodes.size());nodes.push_back(n);intern.emplace(key,id);return id;
    }
    unsigned constant(unsigned r,unsigned c){return make(Constant,4*r+c);}
    unsigned dot(const std::array<unsigned,4>& a,const std::array<unsigned,4>& b,unsigned n){
        unsigned sum=make(Mul,a[0],b[0]);for(unsigned k=1;k<n;++k)sum=make(Add,sum,make(Mul,a[k],b[k]));return sum;}
    void projection(const std::array<unsigned,4>& p){
        for(unsigned i=0;i<4;++i){std::array<unsigned,4> delta{};for(unsigned k=0;k<3;++k)delta[k]=make(Add,make(Neg,p[k]),constant(21+i,k));
            const unsigned squared=dot(delta,delta,3),distance=make(Mul,squared,make(InvRoot,squared));
            expected[i]=make(Add,constant(25,i),make(Add,make(Mul,constant(26,i),distance),make(Mul,constant(27,i),squared)));}
    }
};
inline int operands(unsigned op){switch(op){
    case 1:case 6:case 7:case 14:case 15:case 16:case 19:case 35:case 36:case 46:return 1;
    case 2:case 3:case 5:case 8:case 9:case 10:case 11:case 12:case 13:case 17:case 32:case 33:return 2;
    case 4:case 18:return 3;default:return -1;}}
inline unsigned analyze(const std::uint32_t* w,std::size_t n){
    if(!w||n<2||(w[0]!=0xfffe0200u&&w[0]!=0xfffe0300u)||n>4096)return 0;
    const unsigned major=(w[0]>>8)&255;Expressions e;
    std::array<unsigned,4> temp[32]{},address{},output[16]{},definitions[256]{};bool defined[256]={};
    int color=major==2?0:-1,position=major==2?0:-1;unsigned serial=0;bool projected=false;
    auto read=[&](std::uint32_t token,std::uint32_t relative,std::array<unsigned,4>& value){
        const unsigned type=kind(token),r=index(token),modifier=(token>>24)&15;
        if((type==0&&r>=32)||(type==1&&r>=16)||(type==2&&r>=256)||(type==3&&r)||type>3||modifier>1)return false;
        for(unsigned k=0;k<4;++k){const unsigned c=(token>>(16+2*k))&3;unsigned id=0;
            if(type==0)id=temp[r][c];else if(type==3)id=address[c];else if(type==1)id=e.make(Expressions::Atom,4*r+c);
            else if(token&0x2000u){if(major==3&&kind(relative)!=3)return false;id=e.make(Expressions::Atom,1024+4*r+c,address[(relative>>16)&3]);}
            else id=defined[r]?definitions[r][c]:e.constant(r,c);
            if(!id)id=e.make(Expressions::Atom,4096+serial*4+k);
            value[k]=modifier?e.make(Expressions::Neg,id):id;}
        return true;
    };
    for(std::size_t p=1;p<n;){const auto token=w[p];const unsigned op=token&65535;
        if(op==65535){if(p+1!=n||color<0||color>=16||!projected)return 0;
            const unsigned mask=e.nodes[output[color][0]].color[0]&e.nodes[output[color][1]].color[1]&e.nodes[output[color][2]].color[2];
            if(!mask||(mask&(mask+1)))return 0;unsigned count=0;for(unsigned m=mask;m;m>>=1)++count;return count;}
        if(op==65534){const auto count=(token>>16)&32767;if(count>=n-p)return 0;p+=count+1;continue;}
        const unsigned count=(token>>24)&15;if(count>=n-p||(token&0xf0000000u))return 0;
        const auto* a=w+p+1;p+=count+1;++serial;
        if(op==31&&count==2){if(major==3&&kind(a[1])==6){if((a[0]&0xf000fu)==10)color=int(index(a[1]));if((a[0]&0xf000fu)==0)position=int(index(a[1]));}continue;}
        if(op==81&&count==5&&kind(a[0])==2&&index(a[0])<256){const unsigned r=index(a[0]);defined[r]=true;for(unsigned k=0;k<4;++k)definitions[r][k]=e.make(Expressions::Literal,a[k+1]);continue;}
        const int inputs=operands(op);if(inputs<0||!count)return 0;
        std::array<unsigned,4> v[3]{};unsigned cursor=1;
        for(int i=0;i<inputs;++i){if(cursor>=count)return 0;const auto source=a[cursor++];std::uint32_t relative=0;
            if(source&0x2000u){if(kind(source)!=2)return 0;if(cursor>=count)return 0;relative=a[cursor++];}
            if(!read(source,relative,v[i]))return 0;}
        if(cursor!=count)return 0;
        const unsigned type=kind(a[0]),r=index(a[0]),mask=(a[0]>>16)&15;
        if((type==0&&r>=32)||(type==3&&r)||((type==4||type==5||type==6)&&r>=16)||!(type==0||type==3||type==4||type==5||type==6)||(a[0]&0x0f002000u))return 0;
        if(op==9&&type==(major==3?6u:4u)&&int(r)==position&&mask==1){
            // c2 is the first row of the main-view projection, as validated
            // again at capture time. Its other input is the actual view point.
            bool first=true,second=true;for(unsigned k=0;k<4;++k){first&=v[0][k]==e.constant(2,k);second&=v[1][k]==e.constant(2,k);}
            if(first||second){e.projection(first?v[1]:v[0]);projected=true;}
        }
        std::array<unsigned,4> result{};
        for(unsigned k=0;k<4;++k){switch(op){
            case 1:case 46:result[k]=v[0][k];break;
            case 2:result[k]=e.make(Expressions::Add,v[0][k],v[1][k]);break;
            case 3:result[k]=e.make(Expressions::Add,v[0][k],e.make(Expressions::Neg,v[1][k]));break;
            case 4:result[k]=e.make(Expressions::Add,e.make(Expressions::Mul,v[0][k],v[1][k]),v[2][k]);break;
            case 5:result[k]=e.make(Expressions::Mul,v[0][k],v[1][k]);break;
            case 6:result[k]=e.make(Expressions::Reciprocal,v[0][0]);break;
            case 7:result[k]=e.make(Expressions::InvRoot,v[0][0]);break;
            case 8:case 9:result[k]=e.dot(v[0],v[1],op==8?3:4);break;
            case 10:case 11:result[k]=e.make(op==10?Expressions::Minimum:Expressions::Maximum,v[0][k],v[1][k]);break;
            default: // Other native arithmetic is opaque, but retains proved
                // light contributions. It cannot create a new light proof.
                result[k]=e.make(Expressions::Other,v[0][k],v[1][k]);result[k]=e.make(Expressions::Other,result[k],v[2][k]);break;}
            if(a[0]&0x100000u)result[k]=e.make(Expressions::Other,result[k]);}
        std::array<unsigned,4> ignored{};auto* dest=type==0?&temp[r]:type==3?&address:type==(major==3?6u:5u)?&output[r]:&ignored;for(unsigned k=0;k<4;++k)if(mask&(1u<<k))(*dest)[k]=result[k];
        if(e.nodes.size()>32768)return 0;
    }
    return 0;
}
} // namespace NorthlightEquipmentLightProof
