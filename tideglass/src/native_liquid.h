#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Fade: a liquid draw lying where an authored slime style fades out (Twilight Vale's lake, at the edge of Felwood's
// light). It is drawn as water and as slime, and the slime composite lays the slime over the water by how slime-styled
// each pixel's place is, so no line runs through the liquid.
enum class LiquidKind { Water, Magma, Slime, Unknown, Fade };

// Felwood is declared water. Preserve its intentionally slime-like outdoor styling, using authored whole-day deep colours,
// independent of underwater lighting, current time and mutable GPU ramp contents. Only vivid green counts: Felwood's
// corrupted water is green nearly six times its red and blue; the murky greens of the Wetlands sea, Sholazar and
// Zul'Gurub (green under twice their red or blue) are water (2026-11-09: the Wetlands sea turned slime past a line).
// The share rises from none at green three times the larger of red and blue to all at four times, so faint fringes of a
// corrupted light stay plain water and draw no slime (Twilight Vale's lake, 2.0 to 2.9; the pond above it, 3.4 to 5.8);
// the shaders' SlimeShare (slime_fade.hlsli) is the same.
constexpr double kSlimeRatioLow=3.0,kSlimeRatioHigh=4.0,kSlimeGreenLow=.10,kSlimeGreenHigh=.14;
inline double SmoothStep(double low,double high,double x){const double t=std::clamp((x-low)/(high-low),0.,1.);return t*t*(3-2*t);}
inline double SlimeShare(const double* c)
{
    const double ratio=std::min(c[1]/std::max(c[0],1e-3),c[1]/std::max(c[2],1e-3));
    return SmoothStep(kSlimeRatioLow,kSlimeRatioHigh,ratio)*SmoothStep(kSlimeGreenLow,kSlimeGreenHigh,c[1]);
}
inline LiquidKind StyledLiquid(const double* c)
{
    return SlimeShare(c)>=.5?LiquidKind::Slime:LiquidKind::Water;
}

// CPU values only. Captured on the game thread, copied with the frame, and
// compared with real bound textures on replay. Never call a captured address.
struct NativeLiquidFrame {
    static constexpr unsigned MaxTextures=4096;
    // Negative selectors never index the ocean/river arrays. The host passes the selector back with its draw.
    static constexpr int FelMagma = -2;
    struct Texture { uintptr_t identity; LiquidKind kind; bool fel = false; };
    bool known=false;
    LiquidKind camera=LiquidKind::Unknown;
    LiquidKind water[2]={LiquidKind::Unknown,LiquidKind::Unknown};
    float deep[2][3]={}; // the whole-day deep colours the ocean and river styles were judged from (trace only)
    std::vector<Texture> textures;
    // The ocean and river styles (0 and 1, as the client keeps its generated ramps and light bands: its sea draws use the
    // first ramp, its lakes and rivers the second, traced 2026-11-09) are regional: the game draws all ocean (all river)
    // liquid with one generated ramp, and
    // whether it is slime-styled depends on the lights where it lies. water[] is the style at the camera; StyleAt()
    // judges a liquid draw from the zone's own colours and the lights near the camera (Twilight Vale, 2026-11-09: judged
    // at the camera only, every surface in view took one style, so water and slime could not show together).
    // lights: at most MaxStyleLights, the nearest to the camera, farthest first: the order their colours are laid over
    // the zone's own. The order is the camera's, not each point's, so the slime composite's shaders (which take the
    // same lights in the same order) and the judgement of each draw agree.
    struct StyleLight {double centre[3],inner,outer;double colour[2][3];bool usable[2];};
    static constexpr unsigned MaxStyleLights=16;
    double base[2][3]={};bool baseKnown=false;
    std::vector<StyleLight> lights;
    uintptr_t ramp[2]={0,0}; // the generated ocean and river ramps (mapped identities)
    // The authored colour: the zone's own, then each light over it by how far in, at the distance distanceTo gives.
    template<class Distance> void BlendWith(unsigned kind,double* colour,Distance distanceTo)const{
        for(unsigned c=0;c<3;++c)colour[c]=base[kind][c];
        for(const auto& l:lights){if(!l.usable[kind])continue;
            const double weight=std::clamp((l.outer-distanceTo(l))/(l.outer-l.inner),0.,1.);
            for(unsigned c=0;c<3;++c)colour[c]+=(l.colour[kind][c]-colour[c])*weight;}
    }
    void BlendAt(const float* point,unsigned kind,double* colour)const{
        BlendWith(kind,colour,[&](const StyleLight& l){double d=0;for(unsigned c=0;c<3;++c)d+=std::pow(double(point[c])-l.centre[c],2);return std::sqrt(d);});
    }
    double ShareAt(const float* point,unsigned kind)const{double colour[3];BlendAt(point,kind,colour);return SlimeShare(colour);}
    // Conservative colour intervals over the whole draw. Moving all lights inward/outward together is not a bound:
    // overlapping lights can pull opposite ways and hide a transition between the sampled points.
    LiquidKind StyleAt(const float* bounds,unsigned kind)const{
        if(!baseKnown||kind>1||!bounds)return kind<2?water[kind]:LiquidKind::Unknown;
        for(unsigned c=0;c<3;++c)
            if(!std::isfinite(bounds[c])||!std::isfinite(bounds[c+3])||bounds[c]>bounds[c+3])return LiquidKind::Fade;
        // Bound the linear threshold tests themselves, preserving colour correlation. Bounding RGB independently
        // can turn two entirely-water colours with the same G/R ratio into a spurious fade (high G from one, low R
        // from the other). Each threshold remains linear under every light's lerp, including opposing lights.
        auto threshold=[](const double* colour,unsigned test){
            if(test%3==2)return colour[1]-(test<3?kSlimeGreenLow:kSlimeGreenHigh);
            return colour[1]-(test<3?kSlimeRatioLow:kSlimeRatioHigh)*colour[test%3==0?0:2];
        };
        double low[6],high[6];
        for(unsigned c=0;c<6;++c)low[c]=high[c]=threshold(base[kind],c);
        for(const auto& l:lights){
            if(!l.usable[kind])continue;
            double near2=0,far2=0;
            for(unsigned c=0;c<3;++c){
                const double nearest=std::clamp(l.centre[c],double(bounds[c]),double(bounds[c+3]))-l.centre[c];
                const double farthest=std::max(std::fabs(bounds[c]-l.centre[c]),std::fabs(bounds[c+3]-l.centre[c]));
                near2+=nearest*nearest;far2+=farthest*farthest;
            }
            const double w0=std::clamp((l.outer-std::sqrt(far2))/(l.outer-l.inner),0.,1.);
            const double w1=std::clamp((l.outer-std::sqrt(near2))/(l.outer-l.inner),0.,1.);
            for(unsigned c=0;c<6;++c){
                const double colour=threshold(l.colour[kind],c);
                const double a=low[c]+(colour-low[c])*w0,b=low[c]+(colour-low[c])*w1;
                const double d=high[c]+(colour-high[c])*w0,e=high[c]+(colour-high[c])*w1;
                low[c]=std::min(a,b);high[c]=std::max(d,e);
            }
        }
        // The green thresholds exceed ratio * the 1e-3 denominator floor used by SlimeShare.
        return high[0]<=0||high[1]<=0||high[2]<=0?LiquidKind::Water:
               low[3]>=0&&low[4]>=0&&low[5]>=0?LiquidKind::Slime:LiquidKind::Fade;
    }
    // The ramp a texture is, 0 ocean, 1 river, -1 neither.
    int Ramp(uintptr_t identity)const{for(int i=0;i<2;++i)if(ramp[i]&&ramp[i]==identity)return i;return -1;}
    bool Lists(uintptr_t identity) const {
        for(const auto& t:textures)if(t.identity==identity)return true;
        return false;
    }
    LiquidKind Find(uintptr_t identity, int* style = nullptr) const {
        LiquidKind result=LiquidKind::Unknown;bool found=false, fel=true;
        for(const auto& t:textures)if(t.identity==identity){
            if(found&&result!=t.kind)return LiquidKind::Unknown;
            result=t.kind;found=true;fel &= t.fel;
        }
        if(style && result==LiquidKind::Magma && fel)*style=FelMagma;
        return result;
    }
};

namespace water::client12340 {
template<class Read> bool LiquidSourceSupported(Read read){
    struct Proof {uintptr_t address;const unsigned char* bytes;unsigned size;};
    static const unsigned char slot[]={0x8b,0x44,0x81,0x1c};
    static const unsigned char band[]={0x8d,0x14,0xc0,0x8b,0x45,0x14,0x8d,0x4c,0x50,0xef};
    static const unsigned char ordinary[]={0x8b,0x46,0x44,0x5f,0x5b,0x5e,0x5d,0xc3};
    static const unsigned char binding[]={0x8b,0x53,0x38};
    static const unsigned char material[]={0x8b,0x7d,0x08,0x3b,0x3d,0x18,0x3b,0xd4,0x00};
    const Proof proofs[]={{0x7eb189,slot,sizeof slot},{0x7ebf52,band,sizeof band},
        {0x4b6d7a,ordinary,sizeof ordinary},{0x6a492e,binding,sizeof binding},{0x8a28f5,material,sizeof material}};
    for(const auto& p:proofs){unsigned char actual[16]={};if(!read(p.address,actual,p.size)||std::memcmp(actual,p.bytes,p.size))return false;}
    return true;
}
// Layouts verified against the native consumers in wow.exe 12340:
// 7eb180 (Light -> outdoor LightParams), 7ebf30/7eb210 (colour bands),
// 8a27c0/8a1d60 (liquid materials), 4b6cb0/6a492e (texture identity).
template<class Read> class LiquidSource {
    Read read;
    template<class T> bool Get(uintptr_t p,T& value)const{return p&&read(p,&value,sizeof(value));}
    bool Words(uintptr_t p,uint32_t* out,unsigned count)const{return p&&read(p,out,count*4);}
    uint32_t Record(uintptr_t table,uint32_t id,bool checkId=true,uint32_t maximumSpan=65536)const {
        uint32_t high=0,low=0,array=0,p=0,actual=0;
        if(!Get(table+12,high)||!Get(table+16,low)||high<low||high-low>maximumSpan||
           id<low||id>high||!Get(table+32,array)||!array||
           !Get(uintptr_t(array)+4*(id-low),p)||!p||
           (checkId&&(!Get(p,actual)||actual!=id)))return 0;
        return p;
    }
    bool Text(uint32_t p,const char* expected)const {
        if(!p)return false;
        for(unsigned i=0;i<80;++i){char c=0;if(!Get(uintptr_t(p)+i,c))return false;
            if(c>='A'&&c<='Z')c+=char('a'-'A');
            if(c!=expected[i])return false;
            if(!c)return true;
        }
        return false;
    }
    bool MeanBand(uint32_t param,unsigned channel,std::array<double,3>& colour)const {
        if(!param||param>65536)return false;
        const unsigned id=(param-1)*18+channel+1;
        const auto p=Record(0xaf49bc,id,false,18*65536);
        uint32_t b[34]={};unsigned char compressed=0;
        if(!p||!Get(0xc5dea0,compressed))return false;
        if(compressed){
            // Native 4cfbb0's byte-run encoding. Decode into bounded CPU
            // storage rather than invoking client code or reading past a row.
            auto* dst=reinterpret_cast<unsigned char*>(b);unsigned at=0,n=0;
            unsigned char previous=0,value=0;
            if(!Get(p+at++,previous))return false;
            dst[n++]=previous;
            while(n<sizeof b){
                if(at>sizeof(b)*2||!Get(p+at++,value))return false;
                dst[n++]=value;
                if(value==previous){unsigned char count=0;if(!Get(p+at++,count)||count>sizeof(b)-n)return false;
                    std::fill(dst+n,dst+n+count,value);n+=count;
                    if(n<sizeof b){if(!Get(p+at++,value))return false;dst[n++]=value;}}
                previous=value;
            }
        }else if(!Words(p,b,34))return false;
        if(b[0]!=id||!b[1]||b[1]>16)return false;
        colour={};
        const unsigned count=b[1];
        for(unsigned i=0;i<count;++i){
            const auto time=b[2+i];
            if(time>=2880||(i&&time<=b[1+i]))return false;
            const auto next=(i+1)%count;
            const double duration=next?double(b[2+next])-time:2880.+b[2]-time;
            for(unsigned c=0;c<3;++c){const unsigned shift=(2-c)*8;
                colour[c]+=duration*(((b[18+i]>>shift)&255)+((b[18+next]>>shift)&255))/(2.*2880.*255.);}
        }
        return true;
    }
    // The zone's own ocean/river colours and the lights within reach of the camera, then the style at the camera. Lights
    // farther than this from the camera cannot touch a surface in view.
    static constexpr double kStyleReach=2000.0;
    bool Styles(NativeLiquidFrame& out)const {
        uint32_t count=0,array=0;float position[3]={};
        if(!Get(0xd39008,count)||!count||count>2048||!Get(0xd3900c,array)||!array||
           !read(0xd38b24,position,sizeof position))return false;
        for(float v:position)if(!std::isfinite(v))return false;
        struct Candidate {
            float spatial[5];
            uint32_t profile;
            unsigned index;
            double distance;
            double outside;
        };
        std::vector<uint32_t> pointers(count);
        if(!Words(array,pointers.data(),count))return false;
        std::vector<Candidate> nearby;
        uint32_t base=0;
        for(unsigned i=0;i<count;++i){
            uint32_t row[15]={};
            if(!Words(pointers[i],row,15))return false;
            Candidate light{};
            std::memcpy(light.spatial,row+2,sizeof light.spatial);
            for(float v:light.spatial)if(!std::isfinite(v))return false;
            light.profile=row[7];
            if(!i){base=light.profile;continue;}
            const double inner=light.spatial[3],outer=light.spatial[4];
            if(inner<0||outer<=inner)continue;
            double squared=0;
            for(unsigned c=0;c<3;++c){
                const double delta=double(position[c])-light.spatial[c];
                squared+=delta*delta;
            }
            light.distance=std::sqrt(squared);
            if(light.distance>=outer+kStyleReach)continue;
            light.outside=light.distance-outer;
            light.index=i;
            nearby.push_back(light);
        }
        // The source order breaks ties at the cutoff. Equal centre distances
        // retain that first ranking when the lights are blended farthest first.
        const auto kept=std::min<size_t>(nearby.size(),NativeLiquidFrame::MaxStyleLights);
        std::partial_sort(nearby.begin(),nearby.begin()+kept,nearby.end(),[](const Candidate& a,const Candidate& b){
            return a.outside==b.outside ? a.index<b.index : a.outside<b.outside;
        });
        nearby.resize(kept);
        std::stable_sort(nearby.begin(),nearby.end(),[](const Candidate& a,const Candidate& b){
            return a.distance>b.distance;
        });

        // Several retained lights can reference the same authored colours.
        // Keep both successful and missing bands for this capture only.
        struct Palette {
            uint32_t profile;
            std::array<double,3> colour[2];
            bool usable[2];
        };
        std::vector<Palette> palettes;
        palettes.reserve(kept+1);
        auto paletteFor=[&](uint32_t profile) -> const Palette& {
            for(const auto& p:palettes)if(p.profile==profile)return p;
            Palette p{};
            p.profile=profile;
            for(unsigned kind=0;kind<2;++kind)
                p.usable[kind]=MeanBand(profile,15+kind*2,p.colour[kind]);
            palettes.push_back(p);
            return palettes.back();
        };
        const auto& zone=paletteFor(base);
        for(unsigned kind=0;kind<2;++kind){
            if(!zone.usable[kind])return false;
            std::copy(zone.colour[kind].begin(),zone.colour[kind].end(),out.base[kind]);
        }
        out.lights.reserve(kept);
        for(const auto& candidate:nearby){
            const auto& s=candidate.spatial;
            NativeLiquidFrame::StyleLight light{{s[0],s[1],s[2]},s[3],s[4],{},{false,false}};
            const auto& palette=paletteFor(candidate.profile);
            for(unsigned kind=0;kind<2;++kind){
                light.usable[kind]=palette.usable[kind];
                if(light.usable[kind])
                    std::copy(palette.colour[kind].begin(),palette.colour[kind].end(),light.colour[kind]);
            }
            out.lights.push_back(light);
        }
        out.baseKnown=true;
        // Classify authored styling by regional proximity, independently of the native atmospheric blend and its
        // underwater/weather choices. No coordinates, stock-zone list or GPU texture contents enter this.
        for(unsigned kind=0;kind<2;++kind){
            double colour[3];out.BlendAt(position,kind,colour);
            out.water[kind]=StyledLiquid(colour);
            for(unsigned c=0;c<3;++c)out.deep[kind][c]=float(colour[c]); // the trace's colours, as judged at the camera
        }
        return true;
    }
    LiquidKind Kind(uint32_t id,const LiquidKind* styles)const {
        uint32_t r[45]={};const auto p=Record(0xad4064,id);
        if(!p||!Words(p,r,45))return LiquidKind::Unknown;
        if(r[3]==2)return LiquidKind::Magma;
        if(r[3]==3)return LiquidKind::Slime;
        if(r[3]>1)return LiquidKind::Unknown;
        // Ocean is style 0, river 1 (the ramps' and bands' order; river liquid took the ocean's style before 2026-11-09).
        if(Text(r[16],"proceduraloceandepthtex"))return styles[0];
        if(Text(r[16],"proceduralriverdepthtex"))return styles[1];
        return LiquidKind::Water;
    }
    uintptr_t Texture(uint32_t handle)const {
        uint32_t words[24]={};if(!handle||!Words(handle,words,24))return 0;
        uint32_t gx=words[17],identity=0;
        if((words[10]&4)&&words[23]&&!Get(uintptr_t(words[23])+24,gx))return 0;
        return gx&&Get(uintptr_t(gx)+56,identity)?identity:0;
    }
public:
    explicit LiquidSource(Read reader):read(reader){}
    NativeLiquidFrame CaptureView(uint32_t camera)const {
        NativeLiquidFrame out;out.known=true;if(!Styles(out)){out.lights.clear();out.baseKnown=false;}
        if(camera)out.camera=Kind(camera,out.water);
        return out;
    }
    template<class Map> NativeLiquidFrame Capture(Map map)const {
        uint32_t camera=0;Get(0xcd8794,camera);
        auto out=CaptureView(camera);
        uint32_t count=0,array=0;
        if(!Get(0xd43b18,count)||count>65536||!Get(0xd43b1c,array)||!array)return out;
        std::vector<uint32_t> materials(count);
        if(count&&!Words(array,materials.data(),count))return out;
        bool overflow=false;
        auto add=[&](uint32_t handle,LiquidKind kind,bool fel=false){const auto identity=Texture(handle);
            if(identity){const auto raw=map(identity);if(raw){
                if(out.textures.size()==NativeLiquidFrame::MaxTextures)overflow=true;
                else out.textures.push_back({raw,kind,fel});}}};
        for(unsigned i=0;i<count;++i){
            const uint32_t material=materials[i];
            if(!material)continue;
            const auto kind=Kind(i,out.water);
            // Type 15 is Green Lava in the client's LiquidType.dbc; the camera still uses the magma family.
            const bool fel = i == 15 && kind == LiquidKind::Magma;
            // Authored colour animation, slot 0. Reflection cubes, secondary
            // normals and other shared textures are never material evidence.
            for(unsigned slot:{0u,6u}){
                uint32_t entries[4]={};if(!Words(uintptr_t(material)+0x37c+slot*16,entries,4)||entries[1]>64)continue;
                std::array<uint32_t,64> handles{};
                if(entries[1]&&Words(entries[2],handles.data(),entries[1]))
                    for(unsigned j=0;j<entries[1];++j)add(handles[j],kind,fel);
            }
        }
        // These generated ramps can change contents in-place underwater. Their
        // identities, not their pixels or dimensions, select the source style.
        for(unsigned i=0;i<2;++i){uint32_t h=0;if(Get(0xd43bd4+i*4,h)){add(h,out.water[i]);
            if(const auto identity=Texture(h))out.ramp[i]=map(identity);}}
        // A truncated list could miss a conflicting declaration for the same
        // texture. Leave native surfaces intact rather than publish that guess.
        if(overflow)out.textures.clear();
        return out;
    }
};
}
