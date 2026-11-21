#pragma once
#include "reflection_distance.h"
#include <array>
#include <algorithm>
#include <utility>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace water::world
{
// Stable archive/source window keys. The controller retains compatible active
// scenery and finishes useful pending work across nearby key changes; these
// bounds still limit the receiving-water coverage prepared for each scene.
struct OwnedWorldRegion
{
    int32_t map=-1,x=0,y=0;
    // WaterReach: receiving water always prepared. WaterFarReach: further water, nearest first, while the scene's water budget
    // lasts. Stopping at WaterReach left a stream's far end flat sky behind a straight line across the world, with its nearer
    // reaches mirroring the scenery; an ocean fills the budget, as it must not fail the scene.
    static constexpr float Step=128, WaterReach=160, ShortWaterFarReach=480, WaterFarReach=528, NearReach=224, MiddleReach=640, SourceReach=1536;
    static constexpr float ExtendedWaterReach=816, ExtendedSourceReach=1792;
    static constexpr unsigned MaxTiles=49, MaxExtendedTiles=64;
    // A followed region is kept while the camera stays within this of its centre: a quarter cell past its edge, well
    // inside every reach its scene is prepared for.
    static constexpr float Hold=Step*.75f;
    static const char* Directory(int32_t map)
    {
        switch(map){case 0:return "azeroth";case 1:return "kalimdor";
            case 530:return "expansion01";case 571:return "northrend";default:return nullptr;}
    }
    static OwnedWorldRegion For(int32_t map,const float* p)
    {
        if(!Directory(map)||!p||!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])||
            std::abs(p[0])>17066||std::abs(p[1])>17066)return {};
        return {map,int32_t(std::floor(p[0]/Step)),int32_t(std::floor(p[1]/Step))};
    }
    // The region a view follows: its camera's cell, unless the camera is still within Hold of the region it follows.
    // Without the margin a camera at a cell edge (swimming at a corner of four cells at Crystal Lake, 2026-11-08)
    // flipped between cells every few yards, and each flip prepared and uploaded a whole new scene.
    static OwnedWorldRegion Follow(const OwnedWorldRegion& current,int32_t map,const float* p)
    {return current.map==map&&current.Contains(p,Hold)?current:For(map,p);}
    bool Valid()const{return Directory(map)&&x>=-134&&x<=133&&y>=-134&&y<=133;}
    float Centre(unsigned axis)const{return (float(axis?y:x)+.5f)*Step;}
    bool Contains(const float* p,float reach)const
    {return Valid()&&p&&std::abs(p[0]-Centre(0))<=reach&&std::abs(p[1]-Centre(1))<=reach;}
    bool WithinRadius(const float* p,float radius)const
    {
        if(!Valid()||!p||!std::isfinite(p[0])||!std::isfinite(p[1]))return false;
        const double x=double(p[0])-Centre(0),y=double(p[1])-Centre(1);
        return x*x+y*y<=double(radius)*radius;
    }
    bool Overlaps(const std::array<float,3>& low,const std::array<float,3>& high,float reach=SourceReach)const
    {
        if(!Valid())return false;
        for(unsigned i=0;i<2;++i)if(!std::isfinite(low[i])||!std::isfinite(high[i])||
            low[i]>high[i]||high[i]<Centre(i)-reach||low[i]>Centre(i)+reach)return false;
        return true;
    }
    std::vector<std::string> Tiles(float reach=SourceReach)const
    {
        std::vector<std::string> result;if(!Valid())return result;
        constexpr double origin=17066.666666666668,tile=1600.0/3;
        const auto index=[&](unsigned axis,double offset){return int(std::floor((origin-Centre(axis)-offset)/tile));};
        std::vector<std::pair<double,std::string>> ordered;
        for(int ty=index(0,reach);ty<=index(0,-reach);++ty)
            for(int tx=index(1,reach);tx<=index(1,-reach);++tx)
                if(tx>=0&&tx<64&&ty>=0&&ty<64){const std::string mapname=Directory(map);
                    const auto distance=std::max(std::abs(origin-(ty+.5)*tile-Centre(0)),std::abs(origin-(tx+.5)*tile-Centre(1)));
                    ordered.push_back({distance,"world\\maps\\"+mapname+"\\"+mapname+"_"+std::to_string(tx)+"_"+std::to_string(ty)+".adt"});}
        std::sort(ordered.begin(),ordered.end());
        for(auto& entry:ordered)result.push_back(std::move(entry.second));
        return result;
    }
    bool operator==(const OwnedWorldRegion& b)const{return map==b.map&&x==b.x&&y==b.y;}
};
}
