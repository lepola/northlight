#include "celestial_disc.h"
#include <cassert>
#include <iostream>
using namespace NorthlightCelestialDisc;
int main(int argc,char** argv){
    assert(argc==2);Texture moon;assert(loadTexture(argv[1],moon));auto before=moon;
    softenMoonSurface(moon);double edgesBefore=0,edgesAfter=0,meanBefore=0,meanAfter=0;unsigned count=0;
    for(unsigned y=0;y<moon.height;++y)for(unsigned x=0;x<moon.width;++x){
        auto i=(std::size_t(y)*moon.width+x)*4;assert(moon.rgba[i+3]==before.rgba[i+3]);
        if(!before.rgba[i+3])for(unsigned c=0;c<3;++c)assert(moon.rgba[i+c]==before.rgba[i+c]);
        if(before.rgba[i+3]!=255)continue;
        ++count;meanBefore+=before.rgba[i];meanAfter+=moon.rgba[i];
        if(x+1<moon.width&&before.rgba[i+7]==255){
            edgesBefore+=std::abs(int(before.rgba[i])-int(before.rgba[i+4]));
            edgesAfter+=std::abs(int(moon.rgba[i])-int(moon.rgba[i+4]));
        }
    }
    assert(count>20000&&edgesAfter<edgesBefore*.8&&edgesAfter>edgesBefore*.2);
    assert(std::abs(meanBefore-meanAfter)/count<3);
    Texture flat;flat.width=flat.height=16;flat.rgba.resize(16*16*4);
    for(unsigned i=0;i<flat.rgba.size();i+=4){flat.rgba[i]=140;flat.rgba[i+1]=180;flat.rgba[i+2]=220;flat.rgba[i+3]=255;}
    // Exterior black must not bleed into a constant-color silhouette.
    for(unsigned i=0;i<16*4;i+=4)for(unsigned c=0;c<4;++c)flat.rgba[i+c]=0;
    auto original=flat.rgba;softenMoonSurface(flat);assert(flat.rgba==original);
    std::cout<<"PASS lunar surface: alpha/transparent exterior unchanged, uniform edge has no dark fringe, detail ratio="<<edgesAfter/edgesBefore<<", mean shift="<<(meanAfter-meanBefore)/count<<" /255\n";
}
