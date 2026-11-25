#include "world_tone_curves.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace NorthlightTone;

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
    assert(StopCount==3&&HueBandCount==5);
    assert(std::string(timeName(TimeStop::Dusk))=="dusk");
    assert(std::string(bandName(HueBand::Yellow))=="yellow");
    assert(std::string(propertyName(SelectiveProperty::HueShift))=="hue_shift");
    assert(gradeKey(TimeStop::Dusk,0,2)=="dusk_shadow_level");
    assert(vibranceKey(TimeStop::Night)=="night_vibrance");
    assert(selectiveKey(TimeStop::Dusk,HueBand::Orange,SelectiveProperty::Saturation)=="dusk_orange_saturation");
    assert(gradeBindings().size()==108);

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
        hueWeights(330,weights);assert(near(weights[4],.25f)&&near(weights[0],.75f)&&weights[1]==0&&weights[2]==0);
        hueWeights(190,weights);assert(near(weights[3],.5f)&&near(weights[4],.5f)&&weights[1]==0&&weights[2]==0);
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
    std::cout<<"world tone curves: timeline, compatibility, selective colour, key and LUT checks passed\n";
}
