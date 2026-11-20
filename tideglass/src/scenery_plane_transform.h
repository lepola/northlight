#pragma once
#include <cmath>
namespace reflection {
// Camera-relative world -> mirrored clip coordinates. With a supplied unit world
// normal, distance is the plane's signed offset from the camera (d - n.camera).
inline bool WaterMirrorClip(const float* view,const float* camera,float water,
        const float* projection,double (&matrix)[4][4],const float* normal=nullptr,float distance=0){
    if(normal){
        double length=0;for(int c=0;c<3;++c){if(!std::isfinite(normal[c]))return false;length+=double(normal[c])*normal[c];}
        if(std::fabs(length-1)>.001||!std::isfinite(distance))return false;
    }
    for(int c=0;c<4;++c){
        for(int r=0;r<3;++r){matrix[r][c]=0;
            for(int k=0;k<3;++k){
                double value=(r==2?-1:1)*double(view[r*4+k]);
                if(normal){value=view[r*4+k];for(int s=0;s<3;++s)value-=2*double(normal[r])*normal[s]*view[s*4+k];}
                matrix[r][c]+=value*projection[k*4+c];
            }
        }
        matrix[3][c]=projection[12+c]-2*(double(water)-camera[2])*matrix[2][c];
        if(normal){
            matrix[3][c]=projection[12+c];
            for(int r=0;r<3;++r)for(int k=0;k<3;++k)
                matrix[3][c]+=2*double(distance)*normal[r]*view[r*4+k]*projection[k*4+c];
        }
    }
    for(const auto& row:matrix)for(double v:row)if(!std::isfinite(v))return false;
    return true;
}
}
