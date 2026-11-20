#pragma once
#include "scenery_water_surface.h"
#include <atomic>
#include <limits>
#include <vector>

namespace water
{
// Connected wet quads, not the nearest unrelated water at each dry object.
// Built on the preparation worker; immutable bucket lookups are cheap on frames.
class OwnedWaterReceivers
{
    struct Bucket { int x,y; unsigned surface; };
    std::vector<unsigned> m_body;
    std::vector<Bucket> m_buckets;
    static constexpr float Step=16, EdgeTolerance=.02f, HeightTolerance=.1f;
    static int Cell(double x){return int(std::floor(x/Step));}
    static bool Less(const Bucket& a,const Bucket& b){return a.x!=b.x?a.x<b.x:a.y<b.y;}
    static double Height(const reflection::WaterSurface& s,double x,double y)
    {return s.anchor[2]-(s.normal[0]*(x-s.anchor[0])+s.normal[1]*(y-s.anchor[1]))/s.normal[2];}
    enum class EdgeConnection { None, Separated, Joined };
    // A fitted plane need not pass through a warped quad's corners. Connectivity
    // belongs to its authored edges: otherwise one continuous liquid becomes
    // many independent reflection views. Intersect collinear edge segments so
    // a subdivided edge or rotated building quad obeys the same rule.
    static EdgeConnection SharedEdge(const reflection::WaterSurface& a,const reflection::WaterSurface& b)
    {
        if(!a.cornersKnown || !b.cornersKnown)return EdgeConnection::None;
        constexpr unsigned edges[][2]={{0,1},{1,3},{3,2},{2,0}};
        auto result=EdgeConnection::None;
        for(const auto& ea:edges)for(const auto& eb:edges){
            const auto& p=a.corners[ea[0]];const auto& q=a.corners[ea[1]];
            const auto& r=b.corners[eb[0]];const auto& s=b.corners[eb[1]];
            const double dx=double(q[0])-p[0],dy=double(q[1])-p[1],length2=dx*dx+dy*dy;
            const double bx=double(s[0])-r[0],by=double(s[1])-r[1],other2=bx*bx+by*by;
            if(length2<=EdgeTolerance*EdgeTolerance || other2<=EdgeTolerance*EdgeTolerance)continue;
            const double cross0=dx*(double(r[1])-p[1])-dy*(double(r[0])-p[0]);
            const double cross1=dx*(double(s[1])-p[1])-dy*(double(s[0])-p[0]);
            if(std::max(cross0*cross0,cross1*cross1)>EdgeTolerance*EdgeTolerance*length2)continue;
            const double t0=((double(r[0])-p[0])*dx+(double(r[1])-p[1])*dy)/length2;
            const double t1=((double(s[0])-p[0])*dx+(double(s[1])-p[1])*dy)/length2;
            const double low=std::max(0.,std::min(t0,t1)),high=std::min(1.,std::max(t0,t1));
            if(high<=low || (high-low)*(high-low)*length2<=EdgeTolerance*EdgeTolerance)continue;
            result=EdgeConnection::Separated;
            bool joined=true;
            for(double t:{low,high}){
                const double u=((p[0]+t*dx-r[0])*bx+(p[1]+t*dy-r[1])*by)/other2;
                const double z=p[2]+t*(double(q[2])-p[2]),other=r[2]+u*(double(s[2])-r[2]);
                joined=joined && std::abs(z-other)<=HeightTolerance;
            }
            if(joined)return EdgeConnection::Joined;
        }
        return result;
    }
    static bool Joined(const reflection::WaterSurface& a,const reflection::WaterSurface& b)
    {
        const double x0=std::max(a.bounds[0],b.bounds[0]),x1=std::min(a.bounds[3],b.bounds[3]);
        const double y0=std::max(a.bounds[1],b.bounds[1]),y1=std::min(a.bounds[4],b.bounds[4]);
        if(x0>x1+EdgeTolerance || y0>y1+EdgeTolerance ||
           (x1-x0<EdgeTolerance && y1-y0<EdgeTolerance))return false;
        const auto edge=SharedEdge(a,b);
        if(edge!=EdgeConnection::None)return edge==EdgeConnection::Joined;
        // Overlapping coplanar water and legacy inputs without corners retain
        // the plane test. A known separated edge must never take this fallback.
        for(double x:{x0,x1})for(double y:{y0,y1})
            if(std::abs(Height(a,x,y)-Height(b,x,y))>HeightTolerance)return false;
        return true;
    }
public:
    static constexpr unsigned Missing=std::numeric_limits<unsigned>::max();
    std::size_t Bytes()const{return m_body.capacity()*sizeof(unsigned)+m_buckets.capacity()*sizeof(Bucket);}
    unsigned Body(unsigned surface)const{return surface<m_body.size()?m_body[surface]:Missing;}
    bool Empty()const{return m_body.empty();}
    bool Build(const std::vector<reflection::WaterSurface>& surfaces,const std::atomic<bool>& cancelled)
    {
        m_body.clear();m_buckets.clear();
        if(surfaces.empty()||surfaces.size()>8192)return false;
        std::size_t count=0;
        for(const auto& s:surfaces){
            if(!std::isfinite(s.normal[2])||s.normal[2]<.25)return false;
            for(unsigned c=0;c<3;++c)if(!std::isfinite(s.normal[c])||!std::isfinite(s.anchor[c])||!std::isfinite(s.bounds[c])||
                !std::isfinite(s.bounds[c+3])||s.bounds[c]>s.bounds[c+3]||std::abs(s.bounds[c])>50000||std::abs(s.bounds[c+3])>50000)return false;
            const int w=Cell(s.bounds[3])-Cell(s.bounds[0])+1,h=Cell(s.bounds[4])-Cell(s.bounds[1])+1;
            if(w>8||h>8)return false;
            count+=unsigned(w*h);
        }
        m_body.resize(surfaces.size());m_buckets.reserve(count);
        for(unsigned i=0;i<surfaces.size();++i){m_body[i]=i;const auto& s=surfaces[i];
            for(int x=Cell(s.bounds[0]);x<=Cell(s.bounds[3]);++x)for(int y=Cell(s.bounds[1]);y<=Cell(s.bounds[4]);++y)m_buckets.push_back({x,y,i});}
        std::sort(m_buckets.begin(),m_buckets.end(),Less);
        const auto root=[&](unsigned i){while(m_body[i]!=i){m_body[i]=m_body[m_body[i]];i=m_body[i];}return i;};
        for(unsigned i=0;i<surfaces.size();++i){
            if(cancelled.load())return false;
            const auto& s=surfaces[i];
            for(int x=Cell(s.bounds[0]-EdgeTolerance);x<=Cell(s.bounds[3]+EdgeTolerance);++x)
                for(int y=Cell(s.bounds[1]-EdgeTolerance);y<=Cell(s.bounds[4]+EdgeTolerance);++y){
                    auto p=std::lower_bound(m_buckets.begin(),m_buckets.end(),Bucket{x,y,0},Less);
                    for(;p!=m_buckets.end()&&p->x==x&&p->y==y;++p)if(p->surface<i && Joined(s,surfaces[p->surface])){
                        const auto a=root(i),b=root(p->surface);m_body[std::max(a,b)]=std::min(a,b);}
                }
        }
        for(unsigned i=0;i<m_body.size();++i)m_body[i]=root(i);
        return true;
    }
    unsigned Select(const std::vector<reflection::WaterSurface>& surfaces,const float* point,bool containing=false)const
    {
        unsigned best=Missing;double distance=std::numeric_limits<double>::infinity(),vertical=distance;
        const auto consider=[&](unsigned i){const auto& s=surfaces[i];
            const double x=std::clamp(double(point[0]),double(s.bounds[0]),double(s.bounds[3]));
            const double y=std::clamp(double(point[1]),double(s.bounds[1]),double(s.bounds[4]));
            const double d=(x-point[0])*(x-point[0])+(y-point[1])*(y-point[1]);
            const double z=std::abs(Height(s,x,y)-point[2]);
            if((!containing||d<.0001)&&(d<distance||(d==distance&&(z<vertical||(z==vertical&&i<best))))){best=i;distance=d;vertical=z;}
        };
        if(m_body.size()!=surfaces.size()||!point)return Missing;
        for(unsigned c=0;c<3;++c)if(!std::isfinite(point[c]))return Missing;
        if(containing){const int x=Cell(point[0]),y=Cell(point[1]);
            auto p=std::lower_bound(m_buckets.begin(),m_buckets.end(),Bucket{x,y,0},Less);
            for(;p!=m_buckets.end()&&p->x==x&&p->y==y;++p)consider(p->surface);
        }else for(unsigned i=0;i<surfaces.size();++i)consider(i);
        return Body(best);
    }
    // Only a single horizontal connected receiver may be continued beyond the
    // sampled water window. Sloped rivers still require their local field.
    bool HorizontalPlane(const std::vector<reflection::WaterSurface>& surfaces,unsigned body,float& height)const
    {
        if(body==Missing || surfaces.size()!=m_body.size())return false;
        bool found=false;double low=0,high=0;
        for(unsigned i=0;i<surfaces.size();++i)if(Body(i)==body){
            const auto& s=surfaces[i];
            if(std::abs(s.normal[0])>1e-6 || std::abs(s.normal[1])>1e-6)return false;
            if(!found){low=high=s.anchor[2];found=true;}
            low=std::min(low,s.anchor[2]);high=std::max(high,s.anchor[2]);
            if(high-low>.02)return false;
        }
        if(found)height=float((low+high)*.5);
        return found;
    }
    bool HeightAt(const std::vector<reflection::WaterSurface>& surfaces,unsigned body,float x,float y,float& height)const
    {
        double best=std::numeric_limits<double>::infinity();unsigned chosen=Missing;
        for(unsigned i=0;i<surfaces.size();++i)if(Body(i)==body){const auto& s=surfaces[i];
            const double px=std::clamp(double(x),double(s.bounds[0]),double(s.bounds[3]));
            const double py=std::clamp(double(y),double(s.bounds[1]),double(s.bounds[4]));
            const double d=(px-x)*(px-x)+(py-y)*(py-y);
            if(d<best){best=d;chosen=i;height=float(Height(s,px,py));}
        }
        return chosen!=Missing && std::isfinite(height);
    }

};
}
