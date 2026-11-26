#include "world_tone_curves.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace NorthlightTone;

static_assert(BakeMathSchemaVersion==3);

static bool near(float a,float b,float epsilon=1e-5f){return std::abs(a-b)<=epsilon;}
static float luminance(const std::array<float,3>& rgb){return .2126f*rgb[0]+.7152f*rgb[1]+.0722f*rgb[2];}
static float distance(const std::array<float,3>& a,const std::array<float,3>& b){
    return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]));
}
static std::array<float,3> sampleVolume(const std::vector<uint16_t>& volume,const std::array<float,3>& rgb){
    const float p[3]={std::clamp(rgb[0],0.f,1.f)*(CubeSize-1),std::clamp(rgb[1],0.f,1.f)*(CubeSize-1),std::clamp(rgb[2],0.f,1.f)*(CubeSize-1)};
    const unsigned low[3]={unsigned(std::floor(p[0])),unsigned(std::floor(p[1])),unsigned(std::floor(p[2]))};
    const unsigned high[3]={std::min(low[0]+1,CubeSize-1),std::min(low[1]+1,CubeSize-1),std::min(low[2]+1,CubeSize-1)};
    const float f[3]={p[0]-low[0],p[1]-low[1],p[2]-low[2]};
    std::array<float,3> out{};
    for(unsigned mask=0;mask<8;++mask){
        const unsigned r=mask&1?high[0]:low[0],g=mask&2?high[1]:low[1],b=mask&4?high[2]:low[2];
        const float weight=(mask&1?f[0]:1.f-f[0])*(mask&2?f[1]:1.f-f[1])*(mask&4?f[2]:1.f-f[2]);
        const unsigned offset=((b*CubeSize+g)*CubeSize+r)*4;
        for(unsigned channel=0;channel<3;++channel)out[channel]+=volume[offset+channel]*(weight/65535.f);
    }
    return out;
}

int main(){
    assert(StopCount==3&&HueBandCount==7);
    assert(std::string(bandName(HueBand::Cyan))=="cyan"&&std::string(bandName(HueBand::Magenta))=="magenta");
    assert(std::string(timeName(TimeStop::Dusk))=="dusk");
    assert(std::string(bandName(HueBand::Yellow))=="yellow");
    assert(std::string(propertyName(SelectiveProperty::HueShift))=="hue_shift");
    assert(gradeKey(TimeStop::Dusk,0,2)=="dusk_shadow_level");
    assert(vibranceKey(TimeStop::Night)=="night_vibrance");
    assert(selectiveKey(TimeStop::Dusk,HueBand::Orange,SelectiveProperty::Saturation)=="dusk_orange_saturation");
    assert(gradeBindings().size()==3*(4*5+4+1+3*HueBandCount));
    assert(pointsKey(TimeStop::Day,0)=="day_curve"&&pointsKey(TimeStop::Night,3)=="night_blue_curve");

    {
        const auto day=timeWeights(0,false),night=timeWeights(1,false),dawn=timeWeights(.5f,false);
        assert(day.day==1&&day.dusk==0&&day.night==0);
        assert(night.day==0&&night.dusk==0&&night.night==1);
        assert(near(dawn.day,.5f)&&dawn.dusk==0&&near(dawn.night,.5f));
        const auto start=timeWeights(0,true),anchor=timeWeights(DuskNightWeight,true),end=timeWeights(1,true);
        assert(start.day==1&&start.dusk==0&&start.night==0);
        assert(anchor.day==0&&anchor.dusk==1&&anchor.night==0);
        assert(end.day==0&&end.dusk==0&&end.night==1);
        const auto before=timeWeights((DuskNightWeight)*.5f,true);
        const auto after=timeWeights((DuskNightWeight+1)*.5f,true);
        assert(near(before.day,.5f)&&near(before.dusk,.5f)&&before.night==0);
        assert(after.day==0&&near(after.dusk,.5f)&&near(after.night,.5f));
    }
    {   // The grade's clock: Night until dawn starts, Dusk at sunrise and from 19:30 through the sunset, never three looks at once.
        auto is=[](TimeWeights w,float day,float dusk,float night){return near(w.day,day)&&near(w.dusk,dusk)&&near(w.night,night);};
        for(float hour:{0.f,1.f,4.49f,21.25f,23.99f})assert(is(clockWeights(hour),0,0,1));
        for(float hour:{8.f,12.f,18.f})assert(is(clockWeights(hour),1,0,0));
        for(float hour:{6.f,19.5f,20.f,20.5f})assert(is(clockWeights(hour),0,1,0));
        assert(is(clockWeights(5.25f),0,.5f,.5f)&&is(clockWeights(7.f),.5f,.5f,0)&&is(clockWeights(20.875f),0,.5f,.5f));
        // 18:00-19:30 is the earlier evening curve.
        for(float hour:{18.25f,18.5f,19.f,19.25f}){
            const auto old=timeWeights(NorthlightTone::smooth((hour-18)*.5f),true);assert(is(clockWeights(hour),old.day,old.dusk,old.night));
        }
        for(float hour=0;hour<24;hour+=1/64.f){
            const auto w=clockWeights(hour);
            assert(near(w.day+w.dusk+w.night,1)&&w.day>=0&&w.dusk>=0&&w.night>=0&&(w.day==0||w.night==0));
        }
        for(float hour:{-1.f,24.f,std::nanf("")})assert(is(clockWeights(hour),1,0,0));
    }
    {
        Grade day,night;
        day.curves[1].levels[1]=.61f;day.vibrance=.14f;day.hueShift[0]=8;day.saturation[1]=.2f;
        night.curves[1].levels[1]=.39f;night.vibrance=.08f;night.hueShift[0]=-4;night.saturation[1]=-.1f;
        const Grade dusk=legacyDusk(day,night);
        assert(near(dusk.curves[1].levels[1],day.curves[1].levels[1]+(night.curves[1].levels[1]-day.curves[1].levels[1])*DuskNightWeight));
        Timeline timeline{day,dusk,night};
        for(unsigned step=0;step<=100;++step){
            const float n=step/100.f;
            const auto evening=sample(timeline,n,true),legacy=blend(day,night,n);
            const auto dawn=sample(timeline,n,false),direct=blend(day,night,n);
            assert(near(evening.curves[1].levels[1],legacy.curves[1].levels[1]));
            assert(near(evening.vibrance,legacy.vibrance));
            assert(near(evening.hueShift[0],legacy.hueShift[0]));
            assert(near(dawn.curves[1].levels[1],direct.curves[1].levels[1]));
            assert(near(dawn.vibrance,direct.vibrance)&&near(dawn.saturation[1],direct.saturation[1]));
        }
        assert(near(sample(timeline,DuskNightWeight,true).vibrance,dusk.vibrance));
        Timeline authored=timeline;authored[1].vibrance=.99f;
        assert(near(sample(authored,DuskNightWeight,true).vibrance,.99f));
        assert(near(sample(authored,DuskNightWeight,false).vibrance,blend(day,night,DuskNightWeight).vibrance));
        assert(!near(sample(authored,DuskNightWeight,false).vibrance,.99f));
        const auto converted=legacyTimeline({day.curves,night.curves},day.vibrance,night.vibrance);
        assert(near(converted[1].vibrance,dusk.vibrance)&&near(converted[1].curves[1].levels[1],dusk.curves[1].levels[1]));
    }
    {
        Grade grade;const auto identity=makeBakeKey(grade,.8f);
        assert(identity.strength==0&&identity.stableHash()==stableHash(identity));
        const std::array<std::array<float,3>,3> samples={{{0,0,0},{.12f,.4f,.8f},{1,1,1}}};
        for(const auto& rgb:samples)
            assert(transform(identity,rgb)==rgb);
        float lo=0,hi=0;
        const auto binding=gradeBindings().at("dusk_yellow_saturation");
        float* value=parameter(grade,binding,lo,hi);
        assert(value&&near(lo,-1)&&near(hi,1));*value=.25f;
        assert(near(grade.saturation[static_cast<unsigned>(HueBand::Yellow)],.25f));
        assert(!parameter(grade,GradeBinding{static_cast<TimeStop>(99),GradeField::Vibrance},lo,hi));
    }
    {
        Grade warm;warm.hueShift[static_cast<unsigned>(HueBand::Orange)]=12;
        warm.saturation[static_cast<unsigned>(HueBand::Yellow)]=.12f;
        warm.lightness[static_cast<unsigned>(HueBand::Orange)]=.05f;
        const auto key=makeBakeKey(warm,1);
        const auto gold=hslToRgb(45,.72f,.55f),graded=transform(key,gold),goldHsl=rgbToHsl(graded);
        assert(distance(gold,graded)>.01f);
        assert(goldHsl[0]>45&&goldHsl[1]>.72f&&goldHsl[2]>.55f);
        const auto pink=hslToRgb(330,.65f,.55f),cyan=hslToRgb(190,.65f,.55f);
        assert(distance(pink,transform(key,pink))<1e-6f);
        assert(distance(cyan,transform(key,cyan))<1e-6f);
        const auto warmBefore=luminance(gold),warmAfter=luminance(graded);
        assert(warmAfter>warmBefore);
        std::cout<<"gold45="<<graded[0]<<','<<graded[1]<<','<<graded[2]
                 <<" pink330="<<pink[0]<<','<<pink[1]<<','<<pink[2]
                 <<" cyan190="<<cyan[0]<<','<<cyan[1]<<','<<cyan[2]<<'\n';
        std::array<float,HueBandCount> weights{};
        hueWeights(45,weights);assert(near(weights[1],.5f)&&near(weights[2],.5f));
        // Pink sits between magenta and red, turquoise between cyan and blue: each is reached by its two neighbours only.
        hueWeights(330,weights);assert(near(weights[6],.5f)&&near(weights[0],.5f)&&weights[1]==0&&weights[2]==0&&weights[5]==0);
        hueWeights(190,weights);assert(near(weights[4],5.f/6.f)&&near(weights[5],1.f/6.f)&&weights[3]==0&&weights[1]==0&&weights[2]==0);
        hueWeights(120,weights);assert(near(weights[3],1.f)&&weights[2]==0&&weights[4]==0);
        float total=0;hueWeights(275,weights);for(float w:weights)total+=w;assert(near(total,1.f)&&near(weights[5],5.f/12.f)&&near(weights[6],7.f/12.f));
    }
    {
        assert(selectiveRgbGate(1.f/31.f)==0);
        assert(near(selectiveRgbGate(2.f/31.f),.5f));
        assert(selectiveRgbGate(3.f/31.f)==1);
        Grade intense;
        intense.hueShift={15,-16,17,-18,19};
        intense.saturation={.12f,-.24f,.36f,-.48f,.6f};
        intense.lightness={.129f,-.129f,.129f,-.129f,.129f};
        const auto key=makeBakeKey(intense,1);
        constexpr unsigned n=CubeSize,rowPitch=n*4*sizeof(uint16_t),slicePitch=n*rowPitch;
        std::vector<uint16_t> volume(n*n*n*4);
        bakeColourVolume(volume.data(),rowPitch,slicePitch,key);
        float maximumNeutralSpread=0;
        for(unsigned value=0;value<256;++value){
            const float grey=value/255.f;
            const auto output=sampleVolume(volume,{grey,grey,grey});
            const auto bounds=std::minmax_element(output.begin(),output.end());
            maximumNeutralSpread=std::max(maximumNeutralSpread,(*bounds.second-*bounds.first)*255.f);
        }
        assert(maximumNeutralSpread<=1.1f);

        Grade channelCurves=intense;
        channelCurves.curves[1].levels[1]=.61f;
        channelCurves.curves[2].levels[1]=.47f;
        auto curveOnly=channelCurves;
        curveOnly.hueShift.fill(0);curveOnly.saturation.fill(0);curveOnly.lightness.fill(0);
        const auto withSelective=makeBakeKey(channelCurves,1),withoutSelective=makeBakeKey(curveOnly,1);
        std::vector<uint16_t> gradedVolume(n*n*n*4),curveVolume(n*n*n*4);
        bakeColourVolume(gradedVolume.data(),rowPitch,slicePitch,withSelective);
        bakeColourVolume(curveVolume.data(),rowPitch,slicePitch,withoutSelective);
        float maximumCurveDifference=0;
        for(unsigned value=0;value<256;++value){
            const float grey=value/255.f;
            const auto actual=sampleVolume(gradedVolume,{grey,grey,grey});
            const auto expected=sampleVolume(curveVolume,{grey,grey,grey});
            for(unsigned channel=0;channel<3;++channel)
                maximumCurveDifference=std::max(maximumCurveDifference,std::abs(actual[channel]-expected[channel])*255.f);
        }
        assert(maximumCurveDifference<=1.1f);
        const auto vivid=hslToRgb(45,.72f,.55f);
        assert(selectiveRgbGate(*std::max_element(vivid.begin(),vivid.end())-*std::min_element(vivid.begin(),vivid.end()))==1);
        assert(distance(transform(withSelective,vivid),transform(withoutSelective,vivid))>.01f);
        std::cout<<"neutral LUT ramp spread="<<maximumNeutralSpread<<" curve preservation="<<maximumCurveDifference<<"\n";
    }
    {
        Grade curve;curve.curves[1].levels[1]=.6f;
        const auto curveKey=makeBakeKey(curve,1);
        assert(curveKey.strength&&transform(curveKey,.5f,0,0)[0]>.5f);
        const auto fallback=tableKey(curve,1);
        assert(fallback.strength);
        std::array<uint16_t,256*4> pixels{};bake(fallback,pixels.data());
        assert(pixels[0]==0&&pixels[255*4]==65535&&pixels[128*4]>128*257);

        Grade vivid;vivid.vibrance=.8f;
        const auto vividKey=makeBakeKey(vivid,1);
        const std::array<float,3> muted={.55f,.42f,.32f};
        const auto rich=transform(vividKey,muted);
        assert(rich[0]-rich[2]>muted[0]-muted[2]);
        assert(std::abs(luminance(rich)-luminance(muted))<.02f);
    }
    {
        Grade grade;grade.hueShift[static_cast<unsigned>(HueBand::Orange)]=7.25f;
        grade.saturation[static_cast<unsigned>(HueBand::Yellow)]=.12f;
        const auto key=makeBakeKey(grade,.73f);
        assert(key==makeBakeKey(grade,.73f)&&key.stableHash()==stableHash(key));
        Grade quantized=grade;quantized.hueShift[static_cast<unsigned>(HueBand::Orange)]+=0.0001f;
        assert(key==makeBakeKey(quantized,.73f));
        quantized.hueShift[static_cast<unsigned>(HueBand::Orange)]+=.01f;
        assert(key!=makeBakeKey(quantized,.73f));

        constexpr unsigned n=CubeSize,rowPitch=n*4*sizeof(uint16_t),slicePitch=n*rowPitch;
        std::vector<uint16_t> voxels(n*n*n*4,0xaaaa);
        bakeColourVolume(voxels.data(),rowPitch,slicePitch,key);
        const unsigned r=12,g=17,b=23;
        const auto expected=transform(key,{r/float(n-1),g/float(n-1),b/float(n-1)});
        const auto offset=((b*n+g)*n+r)*4;
        for(unsigned c=0;c<3;++c)assert(std::abs(int(voxels[offset+c])-int(std::lround(expected[c]*65535.f)))<=1);
        assert(voxels[offset+3]==65535&&voxels[3]==65535);
    }
    {
        Timeline timeline{};
        timeline[0].curves[1].levels[1]=.64f;timeline[0].vibrance=.12f;
        timeline[1].curves[1].levels[1]=.55f;timeline[1].hueShift[1]=8;
        timeline[2].curves[1].levels[1]=.40f;timeline[2].vibrance=.2f;
        const auto keys=makeBakeKeys(timeline,.8f);
        assert(keys==makeBakeKeys(timeline,.8f));
        const std::array<float,3> input={.78f,.54f,.27f};
        const auto day=transform(keys[0],input),dusk=transform(keys[1],input),night=transform(keys[2],input);
        assert(transform(keys,timeWeights(0,false),input)==day);
        assert(transform(keys,timeWeights(DuskNightWeight,true),input)==dusk);
        assert(transform(keys,timeWeights(1,false),input)==night);
        const auto weights=timeWeights(DuskNightWeight*.5f,true);
        const std::array<std::array<float,3>,3> mapped={day,dusk,night};
        assert(transform(keys,weights,input)==mixLutSamples(mapped,weights));
        auto guarded=keys;guarded[1]=BakeKey{};guarded[1].strength=4096;
        const auto dayOnly=transform(guarded,timeWeights(0,false),input);
        assert(dayOnly==day&&std::all_of(dayOnly.begin(),dayOnly.end(),[](float v){return std::isfinite(v);}));
    }
    {   // The five levels of an older look give the curve they always gave.
        auto before=[](const Curve& c,float x){
            const float points[]={0,c.lowInput,.25f,.5f,.75f,1};
            const float values[]={0,lowOutput(c),c.levels[0],c.levels[1],c.levels[2],1};
            float slope=values[1]/points[1],value=x*slope;constexpr float r=2.f/255.f;
            for(unsigned i=1;i<5;++i){
                const float next=(values[i+1]-values[i])/(points[i+1]-points[i]);
                const float d=x-points[i],k=std::clamp(d+r,0.f,2*r);
                value+=(next-slope)*(std::max(0.f,d-r)+k*k/(4*r));slope=next;
            }
            return value;
        };
        Curve levels;levels.lowInput=.07f;levels.lowBend=.8f;levels.levels={.31f,.46f,.80f};
        assert(!levels.free()&&pointCount(levels)==4);
        for(unsigned i=0;i<=1000;++i)assert(near(sample(levels,i/1000.f),before(levels,i/1000.f),4e-6f));
        // The same curve written as points is the same curve and the same table.
        Curve written;Point points[4];for(unsigned i=0;i<4;++i)points[i]=point(levels,i);
        setPoints(written,points,4);
        assert(written.free()&&curveKey(written)==curveKey(levels));
        for(unsigned i=0;i<=100;++i)assert(sample(written,i/100.f)==sample(levels,i/100.f));
    }
    {   // No points: a straight line, and no table at all however it is written.
        Curve straight;assert(!parseCurve("straight",straight.points)&&straight.free()&&pointCount(straight)==0);
        for(unsigned i=0;i<=100;++i)assert(near(sample(straight,i/100.f),i/100.f,1e-6f));
        Curve onLine;assert(!parseCurve("0.3,0.3 0.6,0.6",onLine.points));
        assert(curveKey(straight)==CurveKey{}&&curveKey(onLine)==CurveKey{}&&curveKey(Curve{})==CurveKey{});
        Grade grade;grade.curves[0]=onLine;assert(!makeBakeKey(grade,1).strength&&!tableKey(grade,1).strength);
        Curve bent;assert(!parseCurve("0.3,0.3,2,-",bent.points)&&curveKey(bent)!=CurveKey{});
    }
    {   // One point without handles: straight lines to it from black and on to white, the corner rounded.
        Curve one;assert(!parseCurve("0.5,0.6",one.points));
        assert(near(sample(one,.25f),.3f)&&near(sample(one,.75f),.8f)&&sample(one,.5f)<.6f&&sample(one,.5f)>.59f);
        assert(near(sample(one,0),0,1e-4f)&&near(sample(one,1),1,2e-3f));
        // Handles bend the curve on each side and it still passes through the point.
        Curve handled;assert(!parseCurve("0.5,0.6,0,0",handled.points));
        const Shape s=shape(handled);
        assert(near(float(exact(s,.5)),.6f)&&exact(s,.45)>.58&&exact(s,.55)<.61);   // flat through the point
        assert(exact(s,.25)>.3&&exact(s,.75)<.8);                                    // rising sooner before it, later after
        Curve oneSide;assert(!parseCurve("0.5,0.6,-,0",oneSide.points));
        const Shape o=shape(oneSide);
        assert(near(float(exact(o,.25)),.3f)&&exact(o,.55)<.61);                     // the left side is still straight
        for(unsigned i=1;i<=200;++i)assert(sample(handled,i/200.f)>=sample(handled,(i-1)/200.f));
        // A handle may point down: the curve dips after the point and still reaches white.
        Curve dip;assert(!parseCurve("0.5,0.6,-,-2",dip.points));
        assert(exact(shape(dip),.55)<.6&&near(float(exact(shape(dip),1)),1));
    }
    {   // The text of a curve: read, written back the same, and refused when it cannot be a curve.
        Curve c;const std::string text="0.1,0.08 0.25,0.3,1.25,- 0.7,0.75,-0.5,2";
        assert(!parseCurve(text,c.points)&&pointCount(c)==3&&curveText(c.points.data())==text);
        assert(point(c,1).in==1.25f&&point(c,1).out==StraightHandle&&point(c,2).out==2);
        assert(curveText(Curve{}.points.data())=="levels");
        Curve levels;assert(!parseCurve("levels",levels.points)&&!levels.free());
        auto refused=[](const char* text){Curve c;const auto before=c.points;return parseCurve(text,c.points)&&c.points==before;};
        assert(refused("")&&refused("0.5")&&refused("0.5,0.5,1")&&refused("0.6,0.5 0.4,0.5")&&refused("0.5,0.5 0.5,0.6"));
        assert(refused("0,0.5")&&refused("1,0.5")&&refused("0.5,1.2")&&refused("0.5,0.5,9,1")&&refused("0.5,0.5,-9,1")&&refused("-,0.5")&&refused("0.5,x")&&refused("straight 0.5,0.5"));
        std::string many;for(unsigned i=0;i<MaxCurvePoints+1;++i)many+=std::to_string(.05f+.08f*i)+",0.5 ";
        assert(refused(many.c_str()));
        many.clear();for(unsigned i=0;i<MaxCurvePoints;++i)many+=std::to_string(.05f+.08f*i)+",0.5,1.234,5.678 ";
        Curve full;assert(!parseCurve(many,full.points)&&pointCount(full)==MaxCurvePoints&&curveText(full.points.data()).size()<512);
    }
    {   // Part way between two curves is part way between their heights everywhere, whatever their points.
        Curve a,b,levels;levels.levels={.3f,.55f,.7f};
        assert(!parseCurve("0.2,0.3,0.5,2 0.6,0.5",a.points)&&!parseCurve("0.35,0.2 0.6,0.7,0,0 0.9,0.95",b.points));
        for(const auto& pair:{std::pair<Curve,Curve>{a,b},{a,levels},{levels,b}}){
            const Curve half=blend(pair.first,pair.second,.25f);
            assert(half.free());
            const Shape sa=shape(pair.first),sb=shape(pair.second),sh=shape(half);
            for(unsigned i=0;i<=400;++i){
                const double x=i/400.,inside=x>=sh.knots[1].x&&x<=sh.knots[sh.count-2].x;
                assert(std::abs(exact(sh,x)-(exact(sa,x)*.75+exact(sb,x)*.25))<(inside?2e-6:.01));
            }
        }
        assert(blend(a,b,0).points==a.points&&blend(a,b,1).points==b.points);
        // Too many points between them to keep each: sampled at even steps, still close.
        Curve many,other;std::string first,second;
        for(unsigned i=0;i<8;++i){first+=std::to_string(.06f+.11f*i)+","+std::to_string(.1f+.1f*i)+" ";second+=std::to_string(.11f+.11f*i)+","+std::to_string(.05f+.11f*i)+" ";}
        assert(!parseCurve(first,many.points)&&!parseCurve(second,other.points));
        const Curve mixed=blend(many,other,.5f);assert(pointCount(mixed)==MaxCurvePoints);
        for(unsigned i=0;i<=100;++i)assert(std::abs(sample(mixed,i/100.f)-.5f*(sample(many,i/100.f)+sample(other,i/100.f)))<.02f);
        // Daylight, Dusk and Night curves with free points blend through the day like the levels do.
        Timeline timeline{};timeline[0].curves[0]=a;timeline[1].curves[0]=b;timeline[2].curves[0]=levels;
        const auto dusk=sample(timeline,TimeWeights{0,1,0}),between=sample(timeline,TimeWeights{0,.5f,.5f});
        assert(dusk.curves[0].points==b.points);
        assert(near(sample(between.curves[0],.4f),.5f*(sample(b,.4f)+sample(levels,.4f)),1e-5f));
    }
    {   // Editing: points go in in order, stay between their neighbours, and come out again.
        PointList list=pointsOf(Curve{});assert(list.count==0);   // an untouched curve starts with no points
        Curve older;older.levels[1]=.6f;assert(pointsOf(older).count==4&&pointsOf(older).points[2].y==.6f);   // an older look's levels arrive as four
        assert(addPoint(list,.5f,.6f)==0&&addPoint(list,.2f,.1f)==0&&addPoint(list,.8f,.9f)==2&&list.count==3);
        PointList tight;assert(addPoint(tight,.5f,.5f)==0&&addPoint(tight,.515f,.5f)==1);
        assert(addPoint(tight,.507f,.5f)==-1&&tight.count==2);                    // no room between two close points
        movePoint(list,1,.05f,1.5f);assert(near(list.points[1].x,.2f+EditGap)&&list.points[1].y==1);
        movePoint(list,1,.95f,-1);assert(near(list.points[1].x,.8f-EditGap)&&list.points[1].y==0);
        setHandle(list,1,true,2,false);assert(list.points[1].out==2&&!hasHandle(list.points[1].in));
        setHandle(list,1,false,-20,true);assert(list.points[1].in==-MaxHandleSlope&&list.points[1].out==-MaxHandleSlope);
        assert(handleReach(list,0,false)>=.05f&&handleReach(list,2,true)<=.12f);
        removePoint(list,0);assert(list.count==2&&near(list.points[1].x,.8f));
        // What the editor writes is a curve a look file accepts, and reads back as the same points.
        Curve written=curveOf(list),read;assert(!parseCurve(curveText(written.points.data()),read.points));
        assert(pointCount(read)==2&&near(point(read,0).x,point(written,0).x,1e-4f)&&point(read,0).in==-MaxHandleSlope);
        PointList full;for(unsigned i=0;i<MaxCurvePoints;++i)assert(addPoint(full,.05f+.09f*i,.5f)==int(i));
        assert(addPoint(full,.97f,.5f)==-1);
    }
    {   // A curve with points makes a table, and the table follows the curve.
        Grade grade;assert(!parseCurve("0.5,0.7,1,1",grade.curves[1].points));
        const auto key=makeBakeKey(grade,1);assert(key.strength&&key==makeBakeKey(grade,1));
        Grade other=grade;assert(!parseCurve("0.5,0.7,1,1.5",other.curves[1].points));
        assert(key!=makeBakeKey(other,1)&&key.stableHash()!=makeBakeKey(other,1).stableHash());
        assert(near(transform(key,.5f,0,0)[0],sample(grade.curves[1],.5f),1e-3f)&&transform(key,.5f,0,0)[0]>.69f);
        std::array<uint16_t,256*4> pixels{};bake(tableKey(grade,1),pixels.data());
        assert(pixels[0]==0&&std::abs(int(pixels[128*4])-int(std::lround(sample(grade.curves[1],128/255.f)*65535)))<40);
    }
    std::cout<<"world tone curves: timeline, compatibility, selective colour, key and LUT checks passed\n";
}
