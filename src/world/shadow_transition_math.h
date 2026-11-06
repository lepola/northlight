#pragma once
#include "world_math.h"

namespace ShadowTransitionMath {
// Row-vector old WORKING NDC -> world -> current NDC. radius is the old
// 1024-map radius (48/192), not the margin cache's larger 60/240 extent.
inline void reproject(const NorthlightWorldMath::ShadowFrame& old,const float current[16],float radius,float output[16])noexcept{
    const double span=NorthlightWorldMath::ShadowDepthSpan;
    const double depthOrigin=double(old.cz)+.5*span;
    const double inverse[4][4]={
        {double(old.x.x)*radius,double(old.x.y)*radius,double(old.x.z)*radius,0},
        {double(old.y.x)*radius,double(old.y.y)*radius,double(old.y.z)*radius,0},
        {-double(old.z.x)*span,-double(old.z.y)*span,-double(old.z.z)*span,0},
        {double(old.x.x)*old.cx+double(old.y.x)*old.cy+double(old.z.x)*depthOrigin,
         double(old.x.y)*old.cx+double(old.y.y)*old.cy+double(old.z.y)*depthOrigin,
         double(old.x.z)*old.cx+double(old.y.z)*old.cy+double(old.z.z)*depthOrigin,1}
    };
    float result[16];
    for(int row=0;row<4;++row)for(int col=0;col<4;++col){
        double value=0;for(int k=0;k<4;++k)value+=inverse[row][k]*current[k*4+col];
        result[row*4+col]=float(value);
    }
    std::memcpy(output,result,sizeof result); // current and output may alias
}
}
