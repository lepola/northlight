#include "dither_noise.h"
#include <cassert>
#include <cstdio>

int main()
{
    const auto tile=NorthlightDitherNoise::offsets();
    assert(tile.size()==64*64);
    double mean=0;
    for(float offset:tile){assert(std::isfinite(offset)&&std::abs(offset)<.5f/255);mean+=offset;}
    assert(std::abs(mean)<1e-9);
    // A final pass with no lighting change must not invent texture on its input.
    for(unsigned value=0;value<256;++value)for(float offset:tile)
        assert(std::floor(double(value)+double(offset)*255+.5)==value);
    // Smooth gradients retain their fractional mean rather than rounding into bands.
    for(unsigned step=1;step<128;++step){
        const double value=53+double(step)/128;double rounded=0;
        for(float offset:tile)rounded+=std::floor(value+double(offset)*255+.5);
        assert(std::abs(rounded/tile.size()-value)<1.0/tile.size());
    }
    assert(tile==NorthlightDitherNoise::offsets()); // Reset/recreation keeps the pattern stable.
    std::puts("PASS: exact colours stay exact; fractional gradients round without bias; reset is deterministic");
}
