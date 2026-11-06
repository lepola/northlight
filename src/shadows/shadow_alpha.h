#pragma once
#include <cstddef>
#include <cstdint>

// Some patched clients implement alpha testing in the pixel shader and leave
// ALPHATESTENABLE off. Prove the mask feeding TEXKILL instead of guessing from
// a texture name. Unknown discard programs must never become opaque replays.
namespace NorthlightShadowAlpha {
enum Mode : unsigned { Texture=0, TextureColor=1, Color=2 };
struct Info { bool discard=false,known=false;Mode mode=Texture;unsigned ref=0,component=0; };
inline unsigned type(uint32_t t){return ((t>>28)&7)|((t>>8)&24);}
struct Value {
    enum Kind { Unknown,VertexAlpha,TexAlpha,Product,Constant,TestColor,TestProduct } kind=Unknown;
    unsigned reg=0,component=0;bool negative=false;
};
inline bool same(Value a,Value b){return a.kind==b.kind&&a.reg==b.reg&&a.component==b.component&&a.negative==b.negative;}
inline Value multiply(Value a,Value b){
    if(!a.negative&&!b.negative&&((a.kind==Value::VertexAlpha&&b.kind==Value::TexAlpha)||(b.kind==Value::VertexAlpha&&a.kind==Value::TexAlpha)))return {Value::Product};
    return {};
}
inline Value add(Value a,Value b){
    if(a.kind==Value::Constant&&a.negative&&!b.negative){Value t=a;a=b;b=t;}
    if(!a.negative&&b.kind==Value::Constant&&b.negative&&(a.kind==Value::VertexAlpha||a.kind==Value::Product))
        return {a.kind==Value::Product?Value::TestProduct:Value::TestColor,b.reg,b.component};
    return {};
}
inline Info analyze(const uint32_t* w,size_t n){
    Info result;if(!w||n<2||(w[0]!=0xffff0200u&&w[0]!=0xffff0300u))return result;
    const unsigned major=(w[0]>>8)&255;Value temps[32][4]={};bool defined[224]={};
    int color=major==2?0:-1,uv=major==2?0:-1;unsigned kills=0;bool flow=false;
    auto source=[&](uint32_t t,unsigned c)->Value{
        if(t&0x2000)return {};
        const unsigned component=(t>>(16+2*c))&3,index=t&2047,kind=type(t),modifier=(t>>24)&15;
        Value v;
        if(kind==0&&index<32)v=temps[index][component];
        else if(kind==2&&index<224&&!defined[index])v={Value::Constant,index,component};
        else if(component==3&&((major==3&&kind==1&&int(index)==color)||(major==2&&kind==1&&index==0)))v={Value::VertexAlpha};
        if(modifier==1)v.negative=!v.negative;else if(modifier)return {};
        return v;
    };
    for(size_t p=1;p<n;){
        const uint32_t ins=w[p];unsigned op=ins&65535;
        if(op==65535){if(p+1!=n||kills!=1)result.known=false;return result;}
        if(op==65534){size_t count=(ins>>16)&32767;if(count>=n-p)return {};p+=count+1;continue;}
        const unsigned count=(ins>>24)&15;if(count>=n-p)return {};
        const auto* a=w+p+1;p+=count+1;
        if(ins&0xf0000000u)flow=true;
        if(op==31&&count==2){
            if(major==3&&type(a[1])==1){if((a[0]&15)==10&&((a[0]>>16)&15)==0)color=int(a[1]&2047);
                if((a[0]&15)==5&&((a[0]>>16)&15)==0)uv=int(a[1]&2047);}
            continue;
        }
        if(op==81&&count==5){if(type(a[0])==2&&(a[0]&2047)<224)defined[a[0]&2047]=true;continue;}
        if(op==65){
            result.discard=true;++kills;result.known=false;
            if(count!=1||flow||kills!=1)continue;
            const unsigned k=type(a[0]),r=a[0]&2047;
            if(k!=0||r>=32)continue;
            const Value v=temps[r][0];
            if(!same(v,temps[r][1])||!same(v,temps[r][2])||v.negative)continue;
            if(v.kind!=Value::TestProduct&&v.kind!=Value::TestColor)continue;
            result.known=true;result.mode=v.kind==Value::TestProduct?TextureColor:Color;result.ref=v.reg;result.component=v.component;continue;
        }
        if((op>=25&&op<=30)||(op>=38&&op<=45)||op==96)flow=true;
        if(!count)continue;
        const unsigned reg=a[0]&2047;if(type(a[0])!=0||reg>=32)continue;
        Value values[4]={};
        for(unsigned c=0;c<4;++c){
            if(op==1&&count==2)values[c]=source(a[1],c);
            else if(op==2&&count==3)values[c]=add(source(a[1],c),source(a[2],c));
            else if(op==3&&count==3){Value b=source(a[2],c);b.negative=!b.negative;values[c]=add(source(a[1],c),b);}
            else if(op==5&&count==3)values[c]=multiply(source(a[1],c),source(a[2],c));
            else if(op==4&&count==4)values[c]=add(multiply(source(a[1],c),source(a[2],c)),source(a[3],c));
            else if(op==66&&!(ins&0x00ff0000u)&&count==3&&c==3&&type(a[2])==10&&(a[2]&2047)==0&&
                    type(a[1])==(major==3?1u:3u)&&int(a[1]&2047)==uv&&!(a[1]&0x0f002000u))values[c]={Value::TexAlpha};
        }
        for(unsigned c=0;c<4;++c)if(a[0]&(1u<<(16+c)))temps[reg][c]=(a[0]&0x0ff00000u)?Value{}:values[c];
    }
    result.known=false;return result;
}
}
