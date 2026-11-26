#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace NorthlightTone {
// A curve is a straight line from black to white until points are placed on it. A point has a place and a slope on
// each side (its two handles); a side without a handle runs straight to the neighbouring point.
constexpr unsigned MaxCurvePoints=10;
constexpr unsigned CurveWords=1+4*MaxCurvePoints; // the number of points, then x, y, slope in and slope out of each
// A handle's slope may fall as well as rise. Any slope past the limit means the side has no handle.
constexpr float StraightHandle=99.f,MaxHandleSlope=8.f,MinPointGap=1.f/128.f;
inline bool hasHandle(float slope){return std::abs(slope)<=MaxHandleSlope;}
struct Curve {
    // The five levels of a look written before curves had free points. They describe the curve while points[0] is negative.
    float lowInput=.10f,lowBend=.5f;
    std::array<float,3> levels{.25f,.5f,.75f};
    std::array<float,CurveWords> points{-1.f};
    bool free()const{return points[0]>=0;}
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
// The grade's own clock, in game hours. It follows when the installed light tables turn (build_lighting.py: dawn from
// 04:30 to 06:00, sunset colour until 20:35, night at 21:15), not the 06-08 and 18-20 of nightFactor, which put the
// Night grade on a dawn-lit or sunset-lit scene. The Dusk look serves both ends of the day: Night to Dusk as dawn
// breaks and Dusk to Daylight by 08:00; Daylight to Dusk from 18:00, reached at 19:30 where Dusk looks are authored,
// held through the sunset, then Dusk to Night. An unknown clock is Daylight.
namespace GradeClock {constexpr float DawnStart=4.5f,Sunrise=6.f,DayStart=8.f,DayEnd=18.f,DuskAnchor=19.5f,SunsetEnd=20.5f,NightStart=21.25f;}
inline TimeWeights clockWeights(float hour){
    if(!std::isfinite(hour)||hour<0||hour>=24)return {};
    auto ramp=[hour](float from,float to){const float x=std::clamp((hour-from)/(to-from),0.f,1.f);return x*x*(3.f-2.f*x);};
    using namespace GradeClock;
    if(hour<DawnStart||hour>=NightStart)return {0,0,1};
    if(hour<Sunrise){const float dusk=ramp(DawnStart,Sunrise);return {0,dusk,1-dusk};}
    if(hour<DayStart){const float day=ramp(Sunrise,DayStart);return {day,1-day,0};}
    if(hour<DayEnd)return {1,0,0};
    if(hour<DuskAnchor){const float dusk=std::min(ramp(DayEnd,DayEnd+2)/DuskNightWeight,1.f);return {1-dusk,dusk,0};} // the earlier curve, so 18:00-19:30 is unchanged
    if(hour<SunsetEnd)return {0,1,0};
    const float night=ramp(SunsetEnd,NightStart);return {0,1-night,night};
}
// The colour ranges of a grade: the six primary and secondary hues, evenly spaced, so any part of the wheel can be
// adjusted without dragging a distant colour along, plus Orange, because the game's warm tones (sand, wood,
// torchlight, autumn leaves) sit between red and yellow and need telling apart. hueWeights() holds their centres.
enum class HueBand : unsigned { Red=0,Orange=1,Yellow=2,Green=3,Cyan=4,Blue=5,Magenta=6 };
constexpr unsigned HueBandCount=7;
enum class SelectiveProperty : unsigned { HueShift=0,Saturation=1,Lightness=2 };
inline const char* timeName(TimeStop time){
    static constexpr const char* names[]={"day","dusk","night"};
    const auto i=static_cast<unsigned>(time);return i<StopCount?names[i]:"day";
}
inline const char* bandName(HueBand band){
    static constexpr const char* names[]={"red","orange","yellow","green","cyan","blue","magenta"};
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
        if(a.free()||b.free()||c.free()){
            const float early=weights.day+weights.dusk;
            out.curves[ch]=blend(early>0?blend(a,b,weights.dusk/early):a,c,weights.night/total);
            continue;
        }
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
struct Point {float x=0,y=0,in=StraightHandle,out=StraightHandle;};
inline unsigned pointCount(const Curve& c){return c.free()?std::min(unsigned(c.points[0]+.5f),MaxCurvePoints):4u;}
// The five levels are four points: the low one, then shadows, midtones and highlights at each quarter.
inline Point point(const Curve& c,unsigned i){
    if(c.free())return {c.points[1+4*i],c.points[2+4*i],c.points[3+4*i],c.points[4+4*i]};
    return {i?.25f*i:c.lowInput,i?c.levels[i-1]:lowOutput(c),StraightHandle,StraightHandle};
}
inline void setPoints(Curve& c,const Point* points,unsigned count){
    c.points.fill(0);c.points[0]=float(count);
    for(unsigned i=0;i<count;++i){c.points[1+4*i]=points[i].x;c.points[2+4*i]=points[i].y;c.points[3+4*i]=points[i].in;c.points[4+4*i]=points[i].out;}
}
// The curve ready to evaluate: black, the points, white, each with the slope on both sides and the area under the
// curve up to it. Between two knots it is the cubic that leaves one at its slope and arrives at the other's; with
// both slopes equal to the straight line's it is that line.
struct Knot {double x=0,y=0,in=1,out=1,area=0;};
struct Shape {unsigned count=2;std::array<Knot,MaxCurvePoints+2> knots{};};
inline Shape shape(const Curve& c){
    Shape s;unsigned k=1;s.knots[0]={0,0,StraightHandle,StraightHandle,0};
    const unsigned n=pointCount(c);
    for(unsigned i=0;i<n;++i){
        const Point p=point(c,i);
        if(!(p.x>s.knots[k-1].x)||!(p.x<1)||!std::isfinite(p.y))continue; // a point out of order has no place on the curve
        s.knots[k++]={p.x,std::clamp(double(p.y),0.,1.),p.in,p.out,0};
    }
    s.knots[k]={1,1,StraightHandle,StraightHandle,0};s.count=k+1;
    double chord[MaxCurvePoints+1];
    for(unsigned i=0;i+1<s.count;++i)chord[i]=(s.knots[i+1].y-s.knots[i].y)/(s.knots[i+1].x-s.knots[i].x);
    for(unsigned i=0;i<s.count;++i){
        Knot& knot=s.knots[i];
        const double before=chord[i?i-1:0],after=chord[i+1<s.count?i:i-1];
        knot.in=hasHandle(float(knot.in))?knot.in:before;
        knot.out=hasHandle(float(knot.out))?knot.out:after;
    }
    for(unsigned i=1;i<s.count;++i){
        const Knot& a=s.knots[i-1];Knot& b=s.knots[i];const double h=b.x-a.x;
        b.area=a.area+h*(a.y+b.y)/2+h*h*(a.out-b.in)/12;
    }
    return s;
}
inline unsigned segmentAt(const Shape& s,double x){
    unsigned i=0;while(i+2<s.count&&x>s.knots[i+1].x)++i;return i;
}
// The curve as drawn, before its corners are rounded. Past black and white it carries on as its own mirror image,
// so rounding leaves black at black and white at white.
inline double exact(const Shape& s,double x){
    if(x<0)return -exact(s,-x);
    if(x>1)return 2-exact(s,2-x);
    const unsigned i=segmentAt(s,x);const Knot& a=s.knots[i];const Knot& b=s.knots[i+1];
    const double h=b.x-a.x,t=(x-a.x)/h,t2=t*t,t3=t2*t;
    return a.y*(2*t3-3*t2+1)+h*a.out*(t3-2*t2+t)+b.y*(3*t2-2*t3)+h*b.in*(t3-t2);
}
inline double area(const Shape& s,double x){
    if(x<0)return area(s,-x);
    if(x>1)return 2*(x-1)+area(s,2-x);
    const unsigned i=segmentAt(s,x);const Knot& a=s.knots[i];const Knot& b=s.knots[i+1];
    const double h=b.x-a.x,t=(x-a.x)/h,t2=t*t,t3=t2*t,t4=t3*t;
    return a.area+h*(a.y*(t-t3+.5*t4)+h*a.out*(.25*t4-2*t3/3+.5*t2)+b.y*(t3-.5*t4)+h*b.in*(.25*t4-t3/3));
}
// The value used for colour: the curve averaged over two display steps either side, which rounds each corner so a
// gradient crossing a point shows no edge. For straight segments this is the rounding the five levels always had.
inline float sample(const Shape& s,float x){
    constexpr double r=2./255.;
    return float((area(s,x+r)-area(s,x-r))/(2*r));
}
inline float sample(const Curve& c,float x){return sample(shape(c),x);}
// The height and the slope on each side at any place, for joining two curves.
inline Point at(const Shape& s,double x){
    for(unsigned i=0;i<s.count;++i)if(std::abs(s.knots[i].x-x)<1e-9)return {float(x),float(s.knots[i].y),float(s.knots[i].in),float(s.knots[i].out)};
    const unsigned i=segmentAt(s,x);const Knot& a=s.knots[i];const Knot& b=s.knots[i+1];
    const double h=b.x-a.x,t=(x-a.x)/h,t2=t*t;
    const double slope=(a.y*(6*t2-6*t)+h*a.out*(3*t2-4*t+1)+b.y*(6*t-6*t2)+h*b.in*(3*t2-2*t))/h;
    return {float(x),float(exact(s,x)),float(slope),float(slope)};
}
// Part of the way from one curve to another. Two curves of five levels mix level by level, as they always have. With
// free points the result has a point wherever either curve has one. That is exact between the first and the last
// point, and close before and after them, where black and white have no handle to carry a bend. When it needs more
// points than a curve holds, the mix is sampled at even steps instead.
inline Curve blend(const Curve& a,const Curve& b,float weight){
    auto mix=[weight](float x,float y){return x+(y-x)*weight;};
    Curve c;
    if(!a.free()&&!b.free()){
        c.lowInput=mix(a.lowInput,b.lowInput);
        c.lowBend=mix(a.lowBend,b.lowBend);
        for(unsigned i=0;i<3;++i)c.levels[i]=mix(a.levels[i],b.levels[i]);
        return c;
    }
    if(weight<=0)return a;
    if(weight>=1)return b;
    const Shape sa=shape(a),sb=shape(b);
    double xs[2*MaxCurvePoints];unsigned n=0,i=1,j=1;
    while(i+1<sa.count||j+1<sb.count){
        const double xa=i+1<sa.count?sa.knots[i].x:2,xb=j+1<sb.count?sb.knots[j].x:2,x=std::min(xa,xb);
        if(xa<=x+1e-9)++i;
        if(xb<=x+1e-9)++j;
        if(!n||x>xs[n-1]+MinPointGap*.5)xs[n++]=x;
    }
    if(n>MaxCurvePoints){n=MaxCurvePoints;for(unsigned k=0;k<n;++k)xs[k]=double(k+1)/(n+1);}
    Point points[MaxCurvePoints];
    for(unsigned k=0;k<n;++k){
        const Point pa=at(sa,xs[k]),pb=at(sb,xs[k]);
        points[k]={float(xs[k]),mix(pa.y,pb.y),mix(pa.in,pb.in),mix(pa.out,pb.out)};
    }
    setPoints(c,points,n);
    return c;
}
// A curve in a look file: "levels" (the five levels describe it), "straight" (no points), or its points in order,
// separated by spaces, each "x,y" or "x,y,in,out" where in and out are the slopes of its handles and "-" stands for
// a side without one. Returns why the text was refused, or nullptr.
inline const char* parseCurve(const std::string& text,std::array<float,CurveWords>& out){
    std::array<float,CurveWords> result{};result[0]=-1;
    std::string word;unsigned count=0;size_t at=0;float lastX=0;
    auto next=[&]{
        while(at<text.size()&&(text[at]==' '||text[at]=='\t'))++at;
        const size_t start=at;while(at<text.size()&&text[at]!=' '&&text[at]!='\t')++at;
        word=text.substr(start,at-start);return !word.empty();
    };
    if(!next())return "expected levels, straight or points";
    if(word=="levels"||word=="straight"){
        result[0]=word=="levels"?-1.f:0.f;
        if(next())return "too many values";
        out=result;return nullptr;
    }
    do{
        if(count==MaxCurvePoints)return "too many curve points";
        float v[4]={0,0,StraightHandle,StraightHandle};unsigned n=0;size_t p=0;
        while(p<=word.size()){
            const size_t comma=std::min(word.find(',',p),word.size());
            if(n==4)return "a curve point is x,y or x,y,in,out";
            char* end=nullptr;const std::string number=word.substr(p,comma-p);
            if(n>=2&&number=="-")v[n]=StraightHandle;
            else{
                v[n]=std::strtof(number.c_str(),&end);
                if(number.empty()||*end||!std::isfinite(v[n]))return "invalid numeric value";
                if(n>=2&&!hasHandle(v[n]))return "a handle's slope is from -8 to 8";
            }
            ++n;p=comma+1;
        }
        if(n!=2&&n!=4)return "a curve point is x,y or x,y,in,out";
        if(v[0]<lastX+MinPointGap*.999f||v[0]>1-MinPointGap*.999f)return "curve points must be in order, apart, and between black and white";
        if(v[1]<0||v[1]>1)return "a curve point's height is from 0 to 1";
        lastX=v[0];
        std::copy_n(v,4,result.begin()+1+4*count);++count;
    }while(next());
    result[0]=float(count);out=result;return nullptr;
}
inline std::string curveText(const float* points){
    if(points[0]<0)return "levels";
    const unsigned count=std::min(unsigned(points[0]+.5f),MaxCurvePoints);
    if(!count)return "straight";
    auto number=[](float value,int places){
        char buffer[32];std::snprintf(buffer,sizeof buffer,"%.*f",places,double(value));std::string s=buffer;
        while(s.back()=='0')s.pop_back();
        if(s.back()=='.')s.pop_back();
        return s=="-0"?std::string("0"):s;
    };
    std::string text;
    for(unsigned i=0;i<count;++i){
        const float* p=points+1+4*i;
        if(i)text+=' ';
        text+=number(p[0],4)+","+number(p[1],4);
        if(hasHandle(p[2])||hasHandle(p[3]))text+=","+(hasHandle(p[2])?number(p[2],3):std::string("-"))+","+(hasHandle(p[3])?number(p[3],3):std::string("-"));
    }
    return text;
}
// A curve's points while an editor changes them. The editor keeps neighbours a little further apart than a look
// file demands, so writing a place to four decimals cannot close the gap.
struct PointList {unsigned count=0;std::array<Point,MaxCurvePoints> points{};};
constexpr float EditGap=.01f;
// Five levels that leave the curve a straight line show no points, so an untouched curve starts empty.
inline PointList pointsOf(const Curve& c){
    PointList list;const unsigned n=pointCount(c);
    if(!c.free()){
        bool straight=true;
        for(unsigned i=0;i<n;++i)straight&=std::lround(point(c,i).x*4096)==std::lround(point(c,i).y*4096);
        if(straight)return list;
    }
    for(unsigned i=0;i<n;++i)list.points[list.count++]=point(c,i);
    return list;
}
inline Curve curveOf(const PointList& list){Curve c;setPoints(c,list.points.data(),list.count);return c;}
// Puts a point on the curve, in order. Returns its index, or -1 when the curve is full or there is no room there.
inline int addPoint(PointList& list,float x,float y){
    if(list.count>=MaxCurvePoints)return -1;
    unsigned at=0;while(at<list.count&&list.points[at].x<x)++at;
    const float before=at?list.points[at-1].x:0,after=at<list.count?list.points[at].x:1;
    if(after-before<2*EditGap)return -1;
    for(unsigned i=list.count;i>at;--i)list.points[i]=list.points[i-1];
    list.points[at]={std::clamp(x,before+EditGap,after-EditGap),std::clamp(y,0.f,1.f),StraightHandle,StraightHandle};
    ++list.count;return int(at);
}
// A point moves freely up and down, and sideways as far as its neighbours.
inline void movePoint(PointList& list,unsigned i,float x,float y){
    const float before=i?list.points[i-1].x:0,after=i+1<list.count?list.points[i+1].x:1;
    list.points[i].x=std::clamp(x,before+EditGap,after-EditGap);list.points[i].y=std::clamp(y,0.f,1.f);
}
inline void removePoint(PointList& list,unsigned i){
    for(unsigned k=i;k+1<list.count;++k)list.points[k]=list.points[k+1];
    --list.count;
}
// One handle, or both together so the curve passes smoothly through the point.
inline void setHandle(PointList& list,unsigned i,bool out,float slope,bool both){
    slope=std::clamp(slope,-MaxHandleSlope,MaxHandleSlope);
    if(both)list.points[i].in=list.points[i].out=slope;
    else (out?list.points[i].out:list.points[i].in)=slope;
}
// How far sideways a handle is drawn from its point: a third of the way to the neighbour, kept large enough to grab.
inline float handleReach(const PointList& list,unsigned i,bool out){
    const float neighbour=out?(i+1<list.count?list.points[i+1].x:1):(i?list.points[i-1].x:0);
    return std::clamp(std::abs(neighbour-list.points[i].x)/3,.05f,.12f);
}
enum Parameter { LowInput,LowBend,Shadow,Midtone,Highlight };
inline float* parameter(Curve& c,unsigned p,float& lo,float& hi){
    if(p==LowInput){lo=.04f;hi=.20f;return &c.lowInput;}
    if(p==LowBend){lo=0;hi=1;return &c.lowBend;}
    const unsigned i=p-Shadow;
    lo=.25f*(i+1)-.12f;hi=.25f*(i+1)+.12f;
    return &c.levels[i];
}
enum class GradeField : unsigned { Curve=0,Vibrance=1,HueShift=2,Saturation=3,Lightness=4,Points=5 };
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
// The key that holds a curve's points, e.g. day_curve or night_red_curve.
inline std::string pointsKey(TimeStop time,unsigned channel){
    static constexpr const char* channels[]={"","red_","green_","blue_"};
    return std::string(timeName(time))+"_"+channels[channel<4?channel:0]+"curve";
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
            for(unsigned c=0;c<4;++c)result.emplace(pointsKey(time,c),GradeBinding{time,GradeField::Points,c,0,0});
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
        case GradeField::Points:
            if(binding.channel>=4)return nullptr;
            lo=-MaxHandleSlope;hi=StraightHandle;return grade.curves[binding.channel].points.data();
        case GradeField::Vibrance:lo=0;hi=1;return &grade.vibrance;
        case GradeField::HueShift:if(binding.band>=HueBandCount)return nullptr;lo=-30;hi=30;return &grade.hueShift[binding.band];
        case GradeField::Saturation:if(binding.band>=HueBandCount)return nullptr;lo=-1;hi=1;return &grade.saturation[binding.band];
        case GradeField::Lightness:if(binding.band>=HueBandCount)return nullptr;lo=-.25f;hi=.25f;return &grade.lightness[binding.band];
    }
    return nullptr;
}
// A curve's points as 16-bit words, for the keys below: the count, then x, y and the two slopes of each. A curve
// that changes nothing is all zeros, however it was written.
using CurveKey=std::array<uint16_t,CurveWords>;
constexpr uint16_t StraightWord=0xffff;
inline CurveKey curveKey(const Curve& c){
    CurveKey k{};
    auto encode=[](float v){return std::isfinite(v)?uint16_t(std::lround(std::clamp(v,0.f,1.f)*4096)):uint16_t(0);};
    auto slope=[](float v){return hasHandle(v)?uint16_t(std::lround((v+MaxHandleSlope)*2048)):StraightWord;};
    const unsigned n=pointCount(c);bool changes=false;
    k[0]=uint16_t(n);
    for(unsigned i=0;i<n;++i){
        const Point p=point(c,i);uint16_t* w=k.data()+1+4*i;
        w[0]=encode(p.x);w[1]=encode(p.y);w[2]=slope(p.in);w[3]=slope(p.out);
        changes|=w[0]!=w[1]||w[2]!=StraightWord||w[3]!=StraightWord;
    }
    return changes?k:CurveKey{};
}
inline Curve curveFromKey(const CurveKey& k){
    Curve c;c.points.fill(0);
    const unsigned n=std::min(unsigned(k[0]),MaxCurvePoints);c.points[0]=float(n);
    for(unsigned i=0;i<n;++i){
        const uint16_t* w=k.data()+1+4*i;float* p=c.points.data()+1+4*i;
        p[0]=w[0]/4096.f;p[1]=w[1]/4096.f;
        for(unsigned s=2;s<4;++s)p[s]=w[s]==StraightWord?StraightHandle:w[s]/2048.f-MaxHandleSlope;
    }
    return c;
}
// Quantise the key below an 8-bit display step. A steady camera or a nearly
// settled region transition reuses the previous texture without locking it.
struct TableKey {
    std::array<CurveKey,4> points{};
    uint16_t strength=0;
    bool operator==(const TableKey& b)const{return strength==b.strength&&points==b.points;}
};
inline TableKey tableKey(const Grade& grade,float strength){
    TableKey k;
    if(strength<=0)return k;
    bool changes=false;
    for(unsigned i=0;i<4;++i){k.points[i]=curveKey(grade.curves[i]);changes|=k.points[i]!=CurveKey{};}
    if(!changes)return TableKey{};
    k.strength=uint16_t(std::lround(std::clamp(strength,0.f,1.f)*4096));
    return k;
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
    std::array<CurveKey,4> curves{};
    uint16_t strength=0,vibrance=0;
    std::array<int16_t,HueBandCount> hueShift{},saturation{},lightness{};
    bool operator==(const BakeKey& b)const{
        return curves==b.curves&&strength==b.strength&&vibrance==b.vibrance&&
               hueShift==b.hueShift&&saturation==b.saturation&&lightness==b.lightness;
    }
    bool operator!=(const BakeKey& b)const{return !(*this==b);}
    uint64_t stableHash()const;
};
// 2: seven colour ranges (cyan and magenta added, green moved from 140 to 120 degrees). 3: curves are free points
// with handles. Tables baked under an earlier schema are refused and baked again.
inline constexpr uint16_t BakeMathSchemaVersion=3;
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
    bool advanced=key.vibrance!=0;
    for(unsigned ch=0;ch<4;++ch){key.curves[ch]=curveKey(grade.curves[ch]);advanced|=key.curves[ch]!=CurveKey{};}
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
// Fade hue-selective work across neighbouring LUT voxels near the neutral axis.
// This is evaluated only while baking the volume, never in the runtime sampler.
inline float selectiveRgbGate(float rgbSpan){return smooth((rgbSpan-1.f/31.f)/(2.f/31.f));}
inline float wrapHue(float degrees){
    float result=std::fmod(degrees,360.f);if(result<0)result+=360.f;return result;
}
inline void hueWeights(float hue,std::array<float,HueBandCount>& weights){
    static constexpr float centers[HueBandCount]={0.f,30.f,60.f,120.f,180.f,240.f,300.f};
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
// The four curves of a key ready to evaluate; made once for a whole table.
using BakeShapes=std::array<Shape,4>;
inline BakeShapes shapes(const BakeKey& key){
    BakeShapes s;for(unsigned ch=0;ch<4;++ch)s[ch]=shape(curveFromKey(key.curves[ch]));return s;
}
inline std::array<float,3> transform(const BakeKey& key,const BakeShapes& curves,const std::array<float,3>& input){
    if(!key.strength)return input;
    const float strength=key.strength/4096.f;
    std::array<float,3> color={std::clamp(input[0],0.f,1.f),std::clamp(input[1],0.f,1.f),std::clamp(input[2],0.f,1.f)};
    const float inputSpan=std::max({color[0],color[1],color[2]})-std::min({color[0],color[1],color[2]});
    const float luminance=std::clamp(.2126f*color[0]+.7152f*color[1]+.0722f*color[2],0.f,1.f);
    const float mappedLuminance=luminance+(sample(curves[0],luminance)-luminance)*strength;
    const float change=mappedLuminance-luminance;
    const float peak=std::max({color[0],color[1],color[2]});
    const float gain=std::min(change/std::max(luminance,.00001f),std::clamp(1.f-peak,0.f,1.f)/std::max(peak,.00001f));
    for(float& channel:color)channel=std::clamp(channel*(1.f+gain),0.f,1.f);
    for(unsigned c=0;c<3;++c)color[c]=std::clamp(color[c]+(sample(curves[c+1],color[c])-color[c])*strength,0.f,1.f);
    const float vibrance=(key.vibrance/4096.f)*strength;
    if(vibrance>0){
        const auto vivid=vibrantColour(color[0],color[1],color[2]);
        for(unsigned c=0;c<3;++c)color[c]+=vibrance*(vivid[c]-color[c]);
    }
    std::array<float,HueBandCount> weights{};
    auto hsl=rgbToHsl(color);hueWeights(hsl[0],weights);
    const float chroma=smooth(hsl[1]/.15f)*selectiveRgbGate(inputSpan);
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
inline std::array<float,3> transform(const BakeKey& key,const std::array<float,3>& input){
    return key.strength?transform(key,shapes(key),input):input;
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
    const float step=1.f/(CubeSize-1);const BakeShapes curves=shapes(key);
    for(unsigned b=0;b<CubeSize;++b)for(unsigned g=0;g<CubeSize;++g){
        auto* row=reinterpret_cast<uint16_t*>(static_cast<uint8_t*>(pixels)+b*slicePitch+g*rowPitch);
        for(unsigned r=0;r<CubeSize;++r){
            const auto color=transform(key,curves,{r*step,g*step,b*step});
            for(unsigned c=0;c<3;++c)row[4*r+c]=uint16_t(std::lround(std::clamp(color[c],0.f,1.f)*65535.f));
            row[4*r+3]=65535;
        }
    }
}
inline void bake(const TableKey& k,uint16_t* rgba){
    for(unsigned i=0;i<4;++i){
        const Shape c=shape(curveFromKey(k.points[i]));
        for(unsigned x=0;x<256;++x){
            const float in=x/255.f;
            const float out=in+(sample(c,in)-in)*(k.strength/4096.f);
            rgba[x*4+(i+3)%4]=uint16_t(std::lround(std::clamp(out,0.f,1.f)*65535.f));
        }
    }
}
}
