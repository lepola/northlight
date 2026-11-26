#pragma once
#include "frame/frame_inputs.h"
#include "reflection/owned_receiver_views.h"
#include "frame/liquid_draw_coverage.h"
#include "math/projection.h"
#include "math/matrix.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace water
{
// A flat receiver's inverse view depth is affine in screen coordinates. Prepare
// its gradient and the fully-fogged half-space once, including the largest wave
// lookup displacement. Each box then needs two extrema rather than eleven
// projections. This applies only to water using the game's distance fog.
class ReceiverFogVolume
{
public:
    ReceiverFogVolume(const float* camera,const double (&clip)[4][4],const D3DVIEWPORT9& viewport,
                      float height,float fogEnd,float warp):m_height(height)
    {
        const double above=double(camera[2])-height;
        if (!(above>.05) || !(fogEnd>.001) || !viewport.Width || !viewport.Height || clip[3][3]!=0) return;
        const double a=clip[0][1]*clip[1][3]-clip[0][3]*clip[1][1];
        const double b=clip[0][3]*clip[1][0]-clip[0][0]*clip[1][3];
        const double c=clip[0][0]*clip[1][1]-clip[0][1]*clip[1][0];
        const double denominator=a*(clip[3][0]-above*clip[2][0])+b*(clip[3][1]-above*clip[2][1])+
                                 c*(clip[3][3]-above*clip[2][3]);
        if (!std::isfinite(denominator) || std::abs(denominator)<1e-12) return;
        const double gx=2*a/(denominator*viewport.Width),gy=-2*b/(denominator*viewport.Height);
        const double margin=double(warp)*viewport.Width+2;
        const double inverse=1.0/fogEnd-margin*std::hypot(gx,gy);
        if (!(inverse>0) || !std::isfinite(inverse)) return;
        for (unsigned axis=0;axis<3;++axis) {
            m_camera[axis]=camera[axis];m_depth[axis]=clip[axis][3];
            m_fog[axis]=-inverse*above*clip[axis][3]-(axis==2?1:0);
            if (!std::isfinite(m_fog[axis]) || !std::isfinite(m_camera[axis])) return;
        }
        m_valid=true;
    }

    bool Contains(const std::array<float,6>& bounds) const
    {
        if (!m_valid) return false;
        auto box=bounds;
        for (float value:box) if (!std::isfinite(value)) return false;
        // Geometry below the water cannot contribute an above-water reflection.
        box[2]=std::min(box[2],m_height);box[5]=std::min(box[5],m_height);
        double nearestDepth=0,farthestFog=0;
        for (unsigned axis=0;axis<3;++axis) {
            if (box[axis]>box[axis+3]) return false;
            nearestDepth+=(double(box[axis+(m_depth[axis]<0?3:0)])-m_camera[axis])*m_depth[axis];
            farthestFog+=(double(box[axis+(m_fog[axis]>=0?3:0)])-m_camera[axis])*m_fog[axis];
        }
        // Near-plane crossings and contact remain visible conservatively.
        return nearestDepth>1e-3 && farthestFog < -1e-9;
    }

private:
    std::array<double,3> m_camera{},m_depth{},m_fog{};
    float m_height=0;
    bool m_valid=false;
};

// The visible part of a box has vertices at its surviving corners and where
// its edges cross the near plane. Project those points before rounding to pixels.
inline bool ReceiverScreenArea(const FrameInputs& frame,const std::array<float,6>& bounds,float padding,RECT& area)
{
    float projection[16];if(!math::ClientToDeviceProjection(frame.glProjection,projection))return false;
    const auto& v=frame.viewport;
    area={LONG(v.X),LONG(v.Y),LONG(v.X+v.Width),LONG(v.Y+v.Height)};
    double low[2]={1e30,1e30},high[2]={-1e30,-1e30};unsigned common=63;bool crossing=false;
    double corners[8][4]={};
    const auto include=[&](const double* clip){
        for(unsigned c=0;c<2;++c){
            const double ndc=clip[c]/clip[3];
            low[c]=std::min(low[c],ndc);high[c]=std::max(high[c],ndc);
        }
    };
    for(unsigned corner=0;corner<8;++corner){
        double view[4]={0,0,0,1};
        auto& clip=corners[corner];
        for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)
            view[c]+=(bounds[r+((corner&(1u<<r))?3:0)]-frame.cameraPosition[r])*frame.cameraRelativeView[r*4+c];
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)clip[c]+=view[r]*projection[r*4+c];
        for(double x:clip)if(!std::isfinite(x))return false;
        common&=(clip[0]<-clip[3]?1u:0)|(clip[0]>clip[3]?2u:0)|(clip[1]<-clip[3]?4u:0)|
            (clip[1]>clip[3]?8u:0)|(clip[2]<0?16u:0)|(clip[2]>clip[3]?32u:0);
        if(clip[2]<0){crossing=true;continue;}
        if(!(clip[3]>0))return true;
        include(clip);
    }
    if(common)return false;
    if(crossing){
        for(unsigned i=0;i<8;++i){
            const auto& a=corners[i];
            for(unsigned axis:{1u,2u,4u}){
                if(i&axis)continue;
                const auto& b=corners[i|axis];
                if((a[2]<0)==(b[2]<0))continue;
                const double t=a[2]/(a[2]-b[2]);
                const double cut[4]={a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1]),0,a[3]+t*(b[3]-a[3])};
                if(!(cut[3]>0))return true;
                include(cut);
            }
        }
    }
    // Near-camera bounds can project beyond LONG even at valid world
    // coordinates. Intersect in floating point before integer conversion.
    area.left=LONG(std::clamp(std::floor(v.X+(low[0]+1)*.5*v.Width-padding),double(v.X),double(v.X+v.Width)));
    area.right=LONG(std::clamp(std::ceil(v.X+(high[0]+1)*.5*v.Width+padding),double(v.X),double(v.X+v.Width)));
    area.top=LONG(std::clamp(std::floor(v.Y+(1-high[1])*.5*v.Height-padding),double(v.Y),double(v.Y+v.Height)));
    area.bottom=LONG(std::clamp(std::ceil(v.Y+(1-low[1])*.5*v.Height+padding),double(v.Y),double(v.Y+v.Height)));
    return area.left<area.right && area.top<area.bottom;
}
// Known current draw observations restrict the receiving area independently of
// imported water extent. Finite views also restrict observations to their source
// footprint; a different pool at the same height cannot activate them. A sloped native draw that spans this height remains a
// conservative candidate; the per-pixel receiver/height test keeps ownership.
inline bool ObservedReceiverScreenArea(const FrameInputs& frame,const LiquidDrawCoverage& coverage,
                                       const std::array<float,2>& heights,RECT& area,
                                       const std::array<float,6>* source=nullptr)
{
    bool visible=false;
    for(const auto& bounds:coverage.Draws()){
        if(bounds[5]<heights[0] || bounds[2]>heights[1])continue;
        if(source && (bounds[3]<(*source)[0]-.02f || bounds[0]>(*source)[3]+.02f ||
                      bounds[4]<(*source)[1]-.02f || bounds[1]>(*source)[4]+.02f))continue;
        RECT draw{};if(!ReceiverScreenArea(frame,bounds,1,draw))continue;
        if(!visible)area=draw;
        else {area.left=std::min(area.left,draw.left);area.top=std::min(area.top,draw.top);
              area.right=std::max(area.right,draw.right);area.bottom=std::max(area.bottom,draw.bottom);}
        visible=true;
    }
    return visible;
}

// Project the intersection of a horizontal height interval and the view frustum.
// Flat water can continue outside imported quads; restricting that continuation
// to its actual screen area avoids rendering the whole viewport for every level.
// When supplied, exclude the completely imported XY rectangle (low xy, high xy):
// the caller separately unions known wet quads, so no foreground is added for a
// distant continuation. At most 8 corners + 24 edge/height intersections exist.
inline bool ReceiverHeightScreenArea(const FrameInputs& frame,const std::array<float,2>& heights,RECT& area,
                                     const std::array<float,4>* complete=nullptr)
{
    float projection[16],inverse[16];
    if(!(heights[0]<heights[1]) || !math::ClientToDeviceProjection(frame.glProjection,projection) ||
       !math::Invert4x4(frame.cameraRelativeView,inverse))return false;
    const double nearPlane=-double(projection[14])/projection[10],farPlane=-double(projection[14])/(projection[10]-1);
    if(!(nearPlane>0 && farPlane>nearPlane) || !std::isfinite(farPlane))return false;
    struct Corner {double x,y,z,height;};Corner corners[8];
    struct Point {double x,y,z,world[2];};std::array<Point,32> points{};unsigned count=0;
    double low[2]={1,1},high[2]={-1,-1};bool found=false;
    const auto project=[&](double x,double y,double z){
        const double ndc[]={projection[0]*x/z+projection[8],projection[5]*y/z+projection[9]};
        for(unsigned c=0;c<2;++c){low[c]=std::min(low[c],ndc[c]);high[c]=std::max(high[c],ndc[c]);}found=true;
    };
    const auto add=[&](double x,double y,double z){
        Point point{x,y,z,{}};
        for(unsigned c=0;c<2;++c)point.world[c]=frame.cameraPosition[c]+x*inverse[c]+y*inverse[4+c]+z*inverse[8+c];
        points[count++]=point;
    };
    for(unsigned i=0;i<8;++i){
        const double z=i&4?farPlane:nearPlane;
        const double x=((i&1?1.:-1.)-projection[8])*z/projection[0];
        const double y=((i&2?1.:-1.)-projection[9])*z/projection[5];
        const double height=frame.cameraPosition[2]+x*inverse[2]+y*inverse[6]+z*inverse[10];
        if(!std::isfinite(height))return false;
        corners[i]={x,y,z,height};
        if(height>=heights[0] && height<=heights[1])add(x,y,z);
    }
    constexpr unsigned edges[][2]={{0,1},{0,2},{1,3},{2,3},{4,5},{4,6},{5,7},{6,7},{0,4},{1,5},{2,6},{3,7}};
    for(const auto& edge:edges){
        const auto& a=corners[edge[0]];const auto& b=corners[edge[1]];
        if(a.height==b.height)continue;
        for(float height:heights){
            const double t=(height-a.height)/(b.height-a.height);
            if(t>=0 && t<=1)add(a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),a.z+t*(b.z-a.z));
        }
    }
    for(unsigned i=0;i<count;++i){
        const auto& p=points[i];
        if(!complete || p.world[0]<=(*complete)[0] || p.world[1]<=(*complete)[1] ||
           p.world[0]>=(*complete)[2] || p.world[1]>=(*complete)[3])project(p.x,p.y,p.z);
    }
    if(complete){
        // The clipped frustum/slab is convex. Intersect its vertex chords with
        // each edge of the fully imported XY rectangle. This includes every
        // real edge intersection; extra interior chords cannot enlarge the
        // clipped volume. No hull sorting or unbounded world box is needed.
        for(unsigned a=0;a<count;++a)for(unsigned b=0;b<a;++b)for(unsigned edge=0;edge<4;++edge){
            const auto& p=points[a];const auto& q=points[b];const unsigned axis=edge%2;
            const double delta=q.world[axis]-p.world[axis];
            if(std::abs(delta)<1e-10)continue;
            const double t=((*complete)[edge]-p.world[axis])/delta;
            if(t>=0 && t<=1)project(p.x+t*(q.x-p.x),p.y+t*(q.y-p.y),p.z+t*(q.z-p.z));
        }
    }
    if(!found)return false;
    const auto& v=frame.viewport;
    area.left=LONG(std::clamp(std::floor(v.X+(low[0]+1)*.5*v.Width-1),double(v.X),double(v.X+v.Width)));
    area.right=LONG(std::clamp(std::ceil(v.X+(high[0]+1)*.5*v.Width+1),double(v.X),double(v.X+v.Width)));
    area.top=LONG(std::clamp(std::floor(v.Y+(1-high[1])*.5*v.Height-1),double(v.Y),double(v.Y+v.Height)));
    area.bottom=LONG(std::clamp(std::ceil(v.Y+(1-low[1])*.5*v.Height+1),double(v.Y),double(v.Y+v.Height)));
    return area.left<area.right && area.top<area.bottom;
}

// A group's bounding box can span valleys and the camera even when its actual
// wet quads occupy only a few distant pixels. Union the visible quads instead.
inline bool ReceiverViewScreenArea(const FrameInputs& frame,const OwnedReceiverView& view,
                                  const std::vector<reflection::WaterSurface>& surfaces,RECT& area,float worldMargin=0)
{
    const auto expanded=[&](std::array<float,6> bounds){
        for(unsigned c=0;c<3;++c){bounds[c]-=worldMargin;bounds[c+3]+=worldMargin;}return bounds;
    };
    RECT broad{};if(!ReceiverScreenArea(frame,expanded(view.bounds),1,broad))return false;
    bool found=false;
    const auto contains=[](const RECT& outer,const RECT& inner){
        return outer.left<=inner.left && outer.top<=inner.top && outer.right>=inner.right && outer.bottom>=inner.bottom;
    };
    const auto add=[&](unsigned index){
        RECT tile{};if(!ReceiverScreenArea(frame,expanded(surfaces[index].bounds),1,tile))return;
        if(!found)area=tile;
        else {area.left=std::min(area.left,tile.left);area.top=std::min(area.top,tile.top);
              area.right=std::max(area.right,tile.right);area.bottom=std::max(area.bottom,tile.bottom);}
        found=true;
    };
    // Once a projected group is contained by the accumulated union, none of its
    // quads can enlarge it. Whole-view coverage likewise ends traversal early.
    if(view.surfaceGroups.empty()){
        for(unsigned index:view.surfaces){add(index);if(found && contains(area,broad))break;}
    }else for(const auto& group:view.surfaceGroups){
        RECT groupArea{};
        if(!ReceiverScreenArea(frame,expanded(group.bounds),1,groupArea) || (found && contains(area,groupArea)))continue;
        for(unsigned i=group.first;i<group.first+group.count;++i){
            add(view.surfaces[i]);if(found && contains(area,groupArea))break;
        }
        if(found && contains(area,broad))break;
    }
    return found;
}

}
