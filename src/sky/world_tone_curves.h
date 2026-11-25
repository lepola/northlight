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
inline Curve blend(const Curve& a,const Curve& b,float weight);
enum class TimeStop : unsigned { Day=0,Dusk=1,Night=2 };
constexpr unsigned StopCount=3;
constexpr float DuskNightWeight=.84375f; // nightFactor at the authored 19:30 dusk endpoint
struct TimeWeights {float day=1,dusk=0,night=0;};
inline TimeWeights timeWeights(float nightFactor,bool evening){
    if(!std::isfinite(nightFactor))return {};
    const float n=std::clamp(nightFactor,0.f,1.f);
    if(!evening)return {1-n,0,n};
    if(n<=DuskNightWeight)return {1-n/DuskNightWeight,n/DuskNightWeight,0};
    const float after=(n-DuskNightWeight)/(1-DuskNightWeight);
    return {0,1-after,after};
}
enum class HueBand : unsigned { Red=0,Orange=1,Yellow=2,Green=3,Blue=4 };
constexpr unsigned HueBandCount=5;
enum class SelectiveProperty : unsigned { HueShift=0,Saturation=1,Lightness=2 };
inline const char* timeName(TimeStop time){
    static constexpr const char* names[]={"day","dusk","night"};
    const auto i=static_cast<unsigned>(time);return i<StopCount?names[i]:"day";
}
inline const char* bandName(HueBand band){
    static constexpr const char* names[]={"red","orange","yellow","green","blue"};
    const auto i=static_cast<unsigned>(band);return i<HueBandCount?names[i]:"red";
}
inline const char* propertyName(SelectiveProperty property){
    static constexpr const char* names[]={"hue_shift","saturation","lightness"};
    const auto i=static_cast<unsigned>(property);return i<3?names[i]:"hue_shift";
}
struct Grade {
    Channels curves{};
    float vibrance=0;
    std::array<float,HueBandCount> hueShift{},saturation{},lightness{};
};
using Timeline=std::array<Grade,StopCount>;
inline Grade blend(const Grade& a,const Grade& b,float weight){
    const float t=std::clamp(weight,0.f,1.f);Grade out;
    for(unsigned c=0;c<4;++c)out.curves[c]=blend(a.curves[c],b.curves[c],t);
    auto mix=[t](float x,float y){return x+(y-x)*t;};
    out.vibrance=mix(a.vibrance,b.vibrance);
    for(unsigned i=0;i<HueBandCount;++i){
        out.hueShift[i]=mix(a.hueShift[i],b.hueShift[i]);
        out.saturation[i]=mix(a.saturation[i],b.saturation[i]);
        out.lightness[i]=mix(a.lightness[i],b.lightness[i]);
    }
    return out;
}
inline Grade legacyDusk(const Grade& day,const Grade& night){return blend(day,night,DuskNightWeight);}
inline Timeline legacyTimeline(const Regional& curves,float dayVibrance,float nightVibrance){
    Timeline timeline{};timeline[0].curves=curves[0];timeline[0].vibrance=dayVibrance;
    timeline[2].curves=curves[1];timeline[2].vibrance=nightVibrance;
    timeline[1]=legacyDusk(timeline[0],timeline[2]);return timeline;
}
inline Grade sample(const Timeline& timeline,const TimeWeights& weights){
    Grade out;const float total=weights.day+weights.dusk+weights.night;
    if(!(total>0)||!std::isfinite(total))return out;
    auto mix=[&](float a,float b,float c){return (a*weights.day+b*weights.dusk+c*weights.night)/total;};
    for(unsigned ch=0;ch<4;++ch){
        const Curve& a=timeline[0].curves[ch];const Curve& b=timeline[1].curves[ch];const Curve& c=timeline[2].curves[ch];
        out.curves[ch].lowInput=mix(a.lowInput,b.lowInput,c.lowInput);
        out.curves[ch].lowBend=mix(a.lowBend,b.lowBend,c.lowBend);
        for(unsigned i=0;i<3;++i)out.curves[ch].levels[i]=mix(a.levels[i],b.levels[i],c.levels[i]);
    }
    out.vibrance=mix(timeline[0].vibrance,timeline[1].vibrance,timeline[2].vibrance);
    for(unsigned i=0;i<HueBandCount;++i){
        out.hueShift[i]=mix(timeline[0].hueShift[i],timeline[1].hueShift[i],timeline[2].hueShift[i]);
        out.saturation[i]=mix(timeline[0].saturation[i],timeline[1].saturation[i],timeline[2].saturation[i]);
        out.lightness[i]=mix(timeline[0].lightness[i],timeline[1].lightness[i],timeline[2].lightness[i]);
    }
    return out;
}
inline Grade sample(const Timeline& timeline,float nightFactor,bool evening){return sample(timeline,timeWeights(nightFactor,evening));}
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
enum class GradeField : unsigned { Curve=0,Vibrance=1,HueShift=2,Saturation=3,Lightness=4 };
struct GradeBinding {
    TimeStop time=TimeStop::Day;
    GradeField field=GradeField::Curve;
    unsigned channel=0,param=0,band=0;
};
inline std::string gradeKey(TimeStop time,unsigned channel,unsigned param){
    static constexpr const char* channels[]={"","red_","green_","blue_"};
    static constexpr const char* params[]={"low_input","low_bend","shadow_level","midtone_level","highlight_level"};
    const unsigned c=channel<4?channel:0,p=param<5?param:0;
    return std::string(timeName(time))+"_"+channels[c]+params[p];
}
inline std::string vibranceKey(TimeStop time){return std::string(timeName(time))+"_vibrance";}
inline std::string selectiveKey(TimeStop time,HueBand band,SelectiveProperty property){
    return std::string(timeName(time))+"_"+bandName(band)+"_"+propertyName(property);
}
inline const std::map<std::string,GradeBinding>& gradeBindings(){
    static const auto fields=[] {
        std::map<std::string,GradeBinding> result;
        for(unsigned t=0;t<StopCount;++t){
            const auto time=static_cast<TimeStop>(t);
            for(unsigned c=0;c<4;++c)for(unsigned p=0;p<5;++p)
                result.emplace(gradeKey(time,c,p),GradeBinding{time,GradeField::Curve,c,p,0});
            result.emplace(vibranceKey(time),GradeBinding{time,GradeField::Vibrance,0,0,0});
            for(unsigned b=0;b<HueBandCount;++b)for(unsigned p=0;p<3;++p){
                const auto band=static_cast<HueBand>(b);
                const auto property=static_cast<SelectiveProperty>(p);
                const auto field=p==0?GradeField::HueShift:p==1?GradeField::Saturation:GradeField::Lightness;
                result.emplace(selectiveKey(time,band,property),GradeBinding{time,field,0,0,b});
            }
        }
        return result;
    }();
    return fields;
}
inline float* parameter(Grade& grade,const GradeBinding& binding,float& lo,float& hi){
    const auto time=static_cast<unsigned>(binding.time);
    if(time>=StopCount)return nullptr;
    switch(binding.field){
        case GradeField::Curve:
            if(binding.channel>=4)return nullptr;
            return parameter(grade.curves[binding.channel],binding.param,lo,hi);
        case GradeField::Vibrance:lo=0;hi=1;return &grade.vibrance;
        case GradeField::HueShift:if(binding.band>=HueBandCount)return nullptr;lo=-30;hi=30;return &grade.hueShift[binding.band];
        case GradeField::Saturation:if(binding.band>=HueBandCount)return nullptr;lo=-1;hi=1;return &grade.saturation[binding.band];
        case GradeField::Lightness:if(binding.band>=HueBandCount)return nullptr;lo=-.25f;hi=.25f;return &grade.lightness[binding.band];
    }
    return nullptr;
}
// Quantise the key below an 8-bit display step. A steady camera or a nearly
// settled region transition reuses the previous texture without locking it.
struct TableKey {
    std::array<std::array<uint16_t,5>,4> points{};
    uint16_t strength=0;
    bool operator==(const TableKey& b)const{return strength==b.strength&&points==b.points;}
};
inline TableKey tableKey(const Regional& curves,float night,float strength,bool includeBrightness=false){
    TableKey k;
    if(strength<=0)return k;
    bool advanced=false;
    auto encode=[](float v){return uint16_t(std::lround(v*4096));};
    for(unsigned i=0;i<4;++i){
        const Curve c=blend(curves[0][i],curves[1][i],night);
        auto& p=k.points[i];
        p={encode(c.lowInput),encode(c.lowBend),encode(c.levels[0]),encode(c.levels[1]),encode(c.levels[2])};
        advanced|=p[1]!=2048 || ((i>0||includeBrightness)&&(p[2]!=1024||p[3]!=2048||p[4]!=3072));
    }
    k.strength=advanced?encode(strength):0;
    return k;
}
inline TableKey tableKey(const Grade& grade,float strength,bool includeBrightness=true){
    Regional curves{{grade.curves,grade.curves}};
    return tableKey(curves,0.f,strength,includeBrightness);
}
// Bake at full strength once per device. Each channel moves away from the
// same luminance; limit all three together to preserve hue at the gamut edge.
constexpr unsigned CubeSize=32;
inline std::array<float,3> vibrantColour(float r,float g,float b){
    const float peak=std::max({r,g,b}),low=std::min({r,g,b});
    if(peak==low)return {r,g,b};
    const float luma=.2126f*r+.7152f*g+.0722f*b;
    const float weight=std::clamp((.90f-(peak-low)/peak)/.70f,0.f,1.f);
    const float gain=std::min({1.2f*weight*weight*(3-2*weight),
                               (1-peak)/(peak-luma),low/(luma-low)});
    return {r+(r-luma)*gain,g+(g-luma)*gain,b+(b-luma)*gain};
}
// Values are frozen before baking so a stable key can be shared with a worker.
// Quantisation is 1/4096 except hue (1/256 degree); the texture itself stores
// normalized RGBA16 values. Strength zero canonicalizes to an identity key.
struct BakeKey {
    std::array<std::array<uint16_t,5>,4> curves{};
    uint16_t strength=0,vibrance=0;
    std::array<int16_t,HueBandCount> hueShift{},saturation{},lightness{};
    bool operator==(const BakeKey& b)const{
        return curves==b.curves&&strength==b.strength&&vibrance==b.vibrance&&
               hueShift==b.hueShift&&saturation==b.saturation&&lightness==b.lightness;
    }
    bool operator!=(const BakeKey& b)const{return !(*this==b);}
    uint64_t stableHash()const;
};
inline uint64_t stableHash(const BakeKey& key){
    uint64_t hash=14695981039346656037ull;
    auto byte=[&](uint8_t value){hash^=value;hash*=1099511628211ull;};
    auto word=[&](uint16_t value){byte(uint8_t(value));byte(uint8_t(value>>8));};
    for(const auto& channel:key.curves)for(auto value:channel)word(value);
    word(key.strength);word(key.vibrance);
    for(auto value:key.hueShift)word(uint16_t(value));
    for(auto value:key.saturation)word(uint16_t(value));
    for(auto value:key.lightness)word(uint16_t(value));
    return hash;
}
inline uint64_t BakeKey::stableHash()const{return NorthlightTone::stableHash(*this);}
inline BakeKey makeBakeKey(const Grade& grade,float strength){
    BakeKey key;
    if(!std::isfinite(strength)||strength<=0)return key;
    auto encode=[](float value){
        if(!std::isfinite(value))return uint16_t(0);
        return uint16_t(std::lround(std::clamp(value,0.f,1.f)*4096.f));
    };
    auto encodeSigned=[](float value,float minimum,float maximum,float scale){
        if(!std::isfinite(value))return int16_t(0);
        return int16_t(std::lround(std::clamp(value,minimum,maximum)*scale));
    };
    key.strength=encode(strength);
    key.vibrance=encode(grade.vibrance);
    for(unsigned ch=0;ch<4;++ch){
        const auto& curve=grade.curves[ch];auto& p=key.curves[ch];
        p={encode(curve.lowInput),encode(curve.lowBend),encode(curve.levels[0]),encode(curve.levels[1]),encode(curve.levels[2])};
    }
    bool advanced=key.vibrance!=0;
    for(unsigned ch=0;ch<4;++ch){
        const auto& p=key.curves[ch];
        advanced|=p[1]!=2048||p[2]!=1024||p[3]!=2048||p[4]!=3072;
    }
    for(unsigned i=0;i<HueBandCount;++i){
        key.hueShift[i]=encodeSigned(grade.hueShift[i],-30.f,30.f,256.f);
        key.saturation[i]=encodeSigned(grade.saturation[i],-1.f,1.f,4096.f);
        key.lightness[i]=encodeSigned(grade.lightness[i],-.25f,.25f,4096.f);
        advanced|=key.hueShift[i]!=0||key.saturation[i]!=0||key.lightness[i]!=0;
    }
    if(!advanced||key.strength==0)return BakeKey{};
    return key;
}
inline float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3.f-2.f*x);}
inline float wrapHue(float degrees){
    float result=std::fmod(degrees,360.f);if(result<0)result+=360.f;return result;
}
inline void hueWeights(float hue,std::array<float,HueBandCount>& weights){
    static constexpr float centers[HueBandCount]={0.f,30.f,60.f,140.f,240.f};
    weights.fill(0);hue=wrapHue(hue);
    for(unsigned i=0;i<HueBandCount;++i){
        const unsigned next=(i+1)%HueBandCount;
        const float start=centers[i],end=next?centers[next]:360.f;
        if(hue>=start&&(hue<end||(next==0&&hue<=end))){
            const float t=(hue-start)/(end-start);
            weights[i]=1-t;weights[next]=t;return;
        }
    }
    weights[0]=1;
}
inline std::array<float,3> rgbToHsl(const std::array<float,3>& rgb){
    const float maximum=std::max({rgb[0],rgb[1],rgb[2]}),minimum=std::min({rgb[0],rgb[1],rgb[2]});
    const float delta=maximum-minimum,light=(maximum+minimum)*.5f;
    if(delta<1e-7f)return {0,0,light};
    float hue;
    if(maximum==rgb[0])hue=60.f*std::fmod((rgb[1]-rgb[2])/delta,6.f);
    else if(maximum==rgb[1])hue=60.f*((rgb[2]-rgb[0])/delta+2.f);
    else hue=60.f*((rgb[0]-rgb[1])/delta+4.f);
    const float saturation=delta/(1.f-std::fabs(2.f*light-1.f));
    return {wrapHue(hue),saturation,light};
}
inline std::array<float,3> hslToRgb(float hue,float saturation,float lightness){
    hue=wrapHue(hue);saturation=std::clamp(saturation,0.f,1.f);lightness=std::clamp(lightness,0.f,1.f);
    const float chroma=(1.f-std::fabs(2.f*lightness-1.f))*saturation;
    const float x=chroma*(1.f-std::fabs(std::fmod(hue/60.f,2.f)-1.f));
    const float m=lightness-chroma*.5f;
    std::array<float,3> value{};
    if(hue<60){value={chroma,x,0};}else if(hue<120){value={x,chroma,0};}
    else if(hue<180){value={0,chroma,x};}else if(hue<240){value={0,x,chroma};}
    else if(hue<300){value={x,0,chroma};}else{value={chroma,0,x};}
    for(float& channel:value)channel=std::clamp(channel+m,0.f,1.f);
    return value;
}
inline Curve curveFromKey(const BakeKey& key,unsigned channel){
    const auto& p=key.curves[channel];
    return {p[0]/4096.f,p[1]/4096.f,{p[2]/4096.f,p[3]/4096.f,p[4]/4096.f}};
}
inline std::array<float,3> transform(const BakeKey& key,const std::array<float,3>& input){
    if(!key.strength)return input;
    const float strength=key.strength/4096.f;
    std::array<float,3> color={std::clamp(input[0],0.f,1.f),std::clamp(input[1],0.f,1.f),std::clamp(input[2],0.f,1.f)};
    const float luminance=std::clamp(.2126f*color[0]+.7152f*color[1]+.0722f*color[2],0.f,1.f);
    const Curve brightnessCurve=curveFromKey(key,0);
    const float mappedLuminance=luminance+(sample(brightnessCurve,luminance)-luminance)*strength;
    const float change=mappedLuminance-luminance;
    const float peak=std::max({color[0],color[1],color[2]});
    const float gain=std::min(change/std::max(luminance,.00001f),std::clamp(1.f-peak,0.f,1.f)/std::max(peak,.00001f));
    for(float& channel:color)channel=std::clamp(channel*(1.f+gain),0.f,1.f);
    for(unsigned c=0;c<3;++c){
        const Curve channelCurve=curveFromKey(key,c+1);
        color[c]=std::clamp(color[c]+(sample(channelCurve,color[c])-color[c])*strength,0.f,1.f);
    }
    const float vibrance=(key.vibrance/4096.f)*strength;
    if(vibrance>0){
        const auto vivid=vibrantColour(color[0],color[1],color[2]);
        for(unsigned c=0;c<3;++c)color[c]+=vibrance*(vivid[c]-color[c]);
    }
    std::array<float,HueBandCount> weights{};
    auto hsl=rgbToHsl(color);hueWeights(hsl[0],weights);
    const float chroma=smooth(hsl[1]/.15f);
    float hueShift=0,saturation=0,lightness=0;
    for(unsigned i=0;i<HueBandCount;++i){
        const float weight=weights[i]*chroma;
        hueShift+=weight*(key.hueShift[i]/256.f);
        saturation+=weight*(key.saturation[i]/4096.f);
        lightness+=weight*(key.lightness[i]/4096.f);
    }
    if(hueShift!=0||saturation!=0||lightness!=0)
        color=hslToRgb(hsl[0]+hueShift*strength,hsl[1]+saturation*strength,hsl[2]+lightness*strength);
    return color;
}
inline std::array<float,3> transform(const BakeKey& key,float r,float g,float b){return transform(key,{r,g,b});}
using BakeKeys=std::array<BakeKey,StopCount>;
inline BakeKeys makeBakeKeys(const Timeline& timeline,float strength){
    BakeKeys keys{};
    for(unsigned i=0;i<StopCount;++i)keys[i]=makeBakeKey(timeline[i],strength);
    return keys;
}
inline std::array<float,3> mixLutSamples(const std::array<std::array<float,3>,StopCount>& mapped,const TimeWeights& weights){
    const float total=weights.day+weights.dusk+weights.night;
    if(!(total>0)||!std::isfinite(total))return mapped[0];
    std::array<float,3> out{};
    const float w[]={weights.day/total,weights.dusk/total,weights.night/total};
    for(unsigned c=0;c<3;++c)for(unsigned t=0;t<StopCount;++t)out[c]+=mapped[t][c]*w[t];
    return out;
}
inline std::array<float,3> transform(const BakeKeys& keys,const TimeWeights& weights,const std::array<float,3>& input){
    std::array<std::array<float,3>,StopCount> mapped{};
    const float w[]={weights.day,weights.dusk,weights.night};
    for(unsigned t=0;t<StopCount;++t)if(w[t]>0)mapped[t]=transform(keys[t],input);
    return mixLutSamples(mapped,weights);
}
inline std::array<float,3> transform(const BakeKeys& keys,float nightFactor,bool evening,const std::array<float,3>& input){
    return transform(keys,timeWeights(nightFactor,evening),input);
}
template<typename Transform>
inline void bakeColourVolume(void* pixels,unsigned rowPitch,unsigned slicePitch,Transform transform){
    const float step=1.f/(CubeSize-1);
    for(unsigned b=0;b<CubeSize;++b)for(unsigned g=0;g<CubeSize;++g){
        auto* row=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(pixels)+b*slicePitch+g*rowPitch);
        for(unsigned r=0;r<CubeSize;++r){
            const auto color=transform(r*step,g*step,b*step);
            for(unsigned c=0;c<3;++c)row[4*r+c]=uint16_t(std::lround(std::clamp(color[c],0.f,1.f)*65535));
            row[4*r+3]=65535;
        }
    }
}
inline void bakeColourVolume(void* pixels,unsigned rowPitch,unsigned slicePitch,const BakeKey& key){
    const float step=1.f/(CubeSize-1);
    for(unsigned b=0;b<CubeSize;++b)for(unsigned g=0;g<CubeSize;++g){
        auto* row=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(pixels)+b*slicePitch+g*rowPitch);
        for(unsigned r=0;r<CubeSize;++r){
            const auto color=transform(key,{r*step,g*step,b*step});
            for(unsigned c=0;c<3;++c)row[4*r+c]=uint16_t(std::lround(std::clamp(color[c],0.f,1.f)*65535.f));
            row[4*r+3]=65535;
        }
    }
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
