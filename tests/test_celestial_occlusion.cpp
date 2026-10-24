#include "celestial_disc.h"
#include <cassert>
#include <iostream>
using namespace NorthlightCelestialDisc;
int main(int argc,char** argv){
    assert(argc==2);
    SkyBand band;
    const float worldMax=.94f;
    assert(band.observe(.9990234375f,1,worldMax));
    const float depth=band.depth(worldMax);
    assert(depth>.9999f&&depth<1);
    // The former normalized world-depth test classified distant sky geometry
    // as clear sky. Fallback, early repair and halo now share the raw test.
    for(float mountain:{.940001f,.98f,.999f,depth-4.f/16777215}){
        assert((mountain/worldMax)>=1.f);
        assert(!repairVisible(mountain,depth));
    }
    assert(repairVisible(depth,depth)&&repairVisible(1,depth));
    assert(!repairVisible(.5f,depth));
    assert(!band.observe(NAN,1,worldMax));
    assert(band.depth(worldMax)==depth); // bad observation does not erase known band
    SkyBand initial;assert(initial.depth(worldMax)>worldMax);
    assert(!repairVisible(.999f,initial.depth(worldMax)));
    Texture moon;assert(loadTexture(argv[1],moon));const auto original=moon.rgba;
    softenMoonRim(moon);unsigned softened=0,opaque=0;
    for(std::size_t i=0;i<moon.rgba.size();i+=4){
        for(int c=0;c<3;++c)assert(moon.rgba[i+c]==original[i+c]);
        assert(moon.rgba[i+3]<=original[i+3]);
        if(original[i+3]==0)assert(moon.rgba[i+3]==0);
        softened+=moon.rgba[i+3]<original[i+3];opaque+=moon.rgba[i+3]==255;
    }
    assert(softened>500&&opaque>20000);
    // A pixel far inside the disc keeps both its opacity and crater data.
    const auto center=(moon.height/2*moon.width+moon.width/2)*4;
    for(int c=0;c<4;++c)assert(moon.rgba[center+c]==original[center+c]);
    std::cout<<"PASS sky-band mountain/fallback/halo policy; moon rim "<<softened<<" softened pixels, "<<opaque<<" opaque interior pixels, RGB unchanged\n";
}
