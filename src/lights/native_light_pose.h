#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace NorthlightNativeLights::SceneDetail {
using Value=std::array<float,4>;
using Matrix=std::array<float,16>;
inline Matrix identity(){Matrix m{};for(unsigned i=0;i<16;i+=5)m[i]=1;return m;}
inline bool affine(const Matrix& m){for(float v:m)if(!std::isfinite(v)||std::fabs(v)>=100000)return false;return std::fabs(m[3])<.001f&&std::fabs(m[7])<.001f&&std::fabs(m[11])<.001f&&std::fabs(m[15]-1)<.001f;}
inline Matrix multiply(const Matrix& a,const Matrix& b){Matrix c{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
inline Value transform(const float* p,const Matrix& m){Value q{};for(unsigned k=0;k<3;++k){q[k]=m[12+k];for(unsigned j=0;j<3;++j)q[k]+=p[j]*m[j*4+k];}return q;}
inline void translate(Matrix& m,const float* p){for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)m[12+k]+=p[j]*m[j*4+k];}
inline float rowLength(const Matrix& m,unsigned row){const unsigned i=row*4;return std::sqrt(m[i]*m[i]+m[i+1]*m[i+1]+m[i+2]*m[i+2]);}
inline void normalizeRow(Matrix& m,unsigned row){const float n=rowLength(m,row);if(n>1e-12f)for(unsigned j=0;j<3;++j)m[row*4+j]/=n;}
inline Value quaternionMix(Value a,Value b,float t,bool spherical){
    float dot=0;for(unsigned k=0;k<4;++k)dot+=a[k]*b[k];if(dot<0){for(float& v:b)v=-v;dot=-dot;}
    float x=1-t,y=t;if(spherical&&dot<.9995f){const float angle=std::acos(std::clamp(dot,-1.f,1.f)),den=std::sin(angle);if(den>1e-6f){x=std::sin((1-t)*angle)/den;y=std::sin(t*angle)/den;}}
    float length=0;for(unsigned k=0;k<4;++k){a[k]=a[k]*x+b[k]*y;length+=a[k]*a[k];}
    if(length>1e-12f){const float inverse=1/std::sqrt(length);for(float& v:a)v*=inverse;}else a={0,0,0,1};return a;
}
inline Matrix rotation(const Value& q){const float x=q[0],y=q[1],z=q[2],w=q[3];Matrix m=identity();
    m[0]=1-2*(y*y+z*z);m[1]=2*(x*y+z*w);m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w);m[5]=1-2*(x*x+z*z);m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w);m[9]=2*(y*z-x*w);m[10]=1-2*(x*x+y*y);return m;
}
inline Matrix suppressInherited(Matrix parent,const Matrix& world,const float* pivot,unsigned flags){
    if(!(flags&7))return parent;const auto position=transform(pivot,parent);
    for(unsigned row=0;row<3;++row){const float a=rowLength(parent,row),b=rowLength(world,row);switch(flags&6){
        case 2:if(a>1e-12f)for(unsigned k=0;k<3;++k)parent[row*4+k]*=b/a;break;
        case 4:for(unsigned k=0;k<3;++k)parent[row*4+k]=world[row*4+k]*(b*b<=.00001f?1:a/b);break;
        case 6:for(unsigned k=0;k<3;++k)parent[row*4+k]=world[row*4+k];break;}}
    for(unsigned k=0;k<3;++k){parent[12+k]=(flags&1)?world[12+k]:position[k];if(!(flags&1))for(unsigned j=0;j<3;++j)parent[12+k]-=pivot[j]*parent[j*4+k];}return parent;
}
inline Matrix billboard(Matrix bone,const Matrix& local,const Matrix& view,const Matrix& inverse,const float* pivot,unsigned flags){
    bone=multiply(bone,view);const auto position=transform(pivot,bone);float scale[3];for(unsigned k=0;k<3;++k)scale[k]=rowLength(bone,k);
    switch(flags&0x78){
        case 8:if(flags&0x280){for(unsigned k=0;k<3;++k){bone[k*4]=local[k*4+1];bone[k*4+1]=local[k*4+2];bone[k*4+2]=-local[k*4];normalizeRow(bone,k);}}
            else{bone[0]=0;bone[1]=0;bone[2]=-1;bone[4]=1;bone[5]=0;bone[6]=0;bone[8]=0;bone[9]=1;bone[10]=0;}break;
        case 16:normalizeRow(bone,0);bone[4]=bone[1];bone[5]=-bone[0];bone[6]=0;normalizeRow(bone,1);
            bone[8]=bone[2]*bone[5]-bone[6]*bone[1];bone[9]=bone[0]*bone[6]-bone[4]*bone[2];bone[10]=bone[4]*bone[1]-bone[0]*bone[5];break;
        case 32:normalizeRow(bone,1);bone[0]=-bone[5];bone[1]=bone[4];bone[2]=0;normalizeRow(bone,0);
            bone[8]=bone[2]*bone[5]-bone[6]*bone[1];bone[9]=bone[0]*bone[6]-bone[4]*bone[2];bone[10]=bone[4]*bone[1]-bone[0]*bone[5];break;
        case 64:normalizeRow(bone,2);bone[4]=bone[9];bone[5]=-bone[8];bone[6]=0;normalizeRow(bone,1);
            bone[0]=bone[9]*bone[6]-bone[10]*bone[5];bone[1]=bone[4]*bone[10]-bone[6]*bone[8];bone[2]=bone[5]*bone[8]-bone[4]*bone[9];break;
    }
    for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)bone[row*4+k]*=scale[row];
    for(unsigned k=0;k<3;++k){bone[12+k]=position[k];for(unsigned j=0;j<3;++j)bone[12+k]-=pivot[j]*bone[j*4+k];}bone[3]=bone[7]=bone[11]=0;bone[15]=1;return multiply(bone,inverse);
}
} // namespace NorthlightNativeLights::SceneDetail
