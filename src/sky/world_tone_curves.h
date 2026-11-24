#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

namespace NorthlightTone {
struct Curve {
    float lowInput=.10f,lowBend=.5f;
    std::array<float,3> levels{.25f,.5f,.75f};
};
using Channels=std::array<Curve,4>; // brightness, red, green, blue
using Regional=std::array<Channels,2>; // daylight, night
inline Curve blend(const Curve& a,const Curve& b,float weight){
    auto mix=[weight](float x,float y){return x+(y-x)*weight;};
    Curve c;
    c.lowInput=mix(a.lowInput,b.lowInput);
    c.lowBend=mix(a.lowBend,b.lowBend);
    for(unsigned i=0;i<3;++i)c.levels[i]=mix(a.levels[i],b.levels[i]);
    return c;
}
inline float lowOutput(const Curve& c){
    const float neutral=c.lowInput*4*c.levels[0];
    const float offset=2*c.lowBend-1;
    return neutral+offset*(offset<0?neutral:c.levels[0]-neutral);
}
inline float bendForOutput(const Curve& c,float y){
    const float neutral=c.lowInput*4*c.levels[0];
    const float span=y<neutral?neutral:c.levels[0]-neutral;
    return std::clamp(.5f+.5f*(y-neutral)/span,0.f,1.f);
}
inline float sample(const Curve& c,float x){
    const float points[]={0,c.lowInput,.25f,.5f,.75f,1};
    const float values[]={0,lowOutput(c),c.levels[0],c.levels[1],c.levels[2],1};
    float slope=values[1]/points[1];
    float value=x*slope;
    constexpr float r=2.f/255.f;
    for(unsigned i=1;i<5;++i){
        const float next=(values[i+1]-values[i])/(points[i+1]-points[i]);
        const float d=x-points[i],k=std::clamp(d+r,0.f,2*r);
        value+=(next-slope)*(std::max(0.f,d-r)+k*k/(4*r));
        slope=next;
    }
    return value;
}
enum Parameter { LowInput,LowBend,Shadow,Midtone,Highlight };
struct Binding { unsigned time,channel,param; };
inline std::string key(unsigned time,unsigned channel,unsigned param){
    static constexpr const char* channels[]={"","red_","green_","blue_"};
    static constexpr const char* params[]={"low_input","low_bend","shadow_level","midtone_level","highlight_level"};
    return std::string(time?"night_":"day_")+channels[channel]+params[param];
}
inline const auto& bindings(){
    static const auto fields=[]{
        std::map<std::string,Binding> result;
        for(unsigned t=0;t<2;++t)for(unsigned c=0;c<4;++c)for(unsigned p=0;p<5;++p)
            result.emplace(key(t,c,p),Binding{t,c,p});
        return result;
    }();
    return fields;
}
inline float* parameter(Curve& c,unsigned p,float& lo,float& hi){
    if(p==LowInput){lo=.04f;hi=.20f;return &c.lowInput;}
    if(p==LowBend){lo=0;hi=1;return &c.lowBend;}
    const unsigned i=p-Shadow;
    lo=.25f*(i+1)-.12f;hi=.25f*(i+1)+.12f;
    return &c.levels[i];
}
// Quantise the key below an 8-bit display step. A steady camera or a nearly
// settled region transition reuses the previous texture without locking it.
struct TableKey {
    std::array<std::array<uint16_t,5>,4> points{};
    uint16_t strength=0;
    bool operator==(const TableKey& b)const{return strength==b.strength&&points==b.points;}
};
inline TableKey tableKey(const Regional& curves,float night,float strength){
    TableKey k;
    if(strength<=0)return k;
    bool advanced=false;
    auto encode=[](float v){return uint16_t(std::lround(v*4096));};
    for(unsigned i=0;i<4;++i){
        const Curve c=blend(curves[0][i],curves[1][i],night);
        auto& p=k.points[i];
        p={encode(c.lowInput),encode(c.lowBend),encode(c.levels[0]),encode(c.levels[1]),encode(c.levels[2])};
        advanced|=p[1]!=2048 || (i>0&&(p[2]!=1024||p[3]!=2048||p[4]!=3072));
    }
    k.strength=advanced?encode(strength):0;
    return k;
}
inline void bake(const TableKey& k,uint16_t* rgba){
    for(unsigned i=0;i<4;++i){
        const auto& p=k.points[i];
        Curve c{p[0]/4096.f,p[1]/4096.f,{p[2]/4096.f,p[3]/4096.f,p[4]/4096.f}};
        for(unsigned x=0;x<256;++x){
            const float in=x/255.f;
            const float out=in+(sample(c,in)-in)*(k.strength/4096.f);
            rgba[x*4+(i+3)%4]=uint16_t(std::lround(std::clamp(out,0.f,1.f)*65535.f));
        }
    }
}
}
