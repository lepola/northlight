#include "local_light_selection.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
using namespace NorthlightLocalLightSelection;
template<unsigned N> unsigned check(const Selection& s){unsigned cases=0;for(unsigned first=0;first<Slots+N;++first){const auto old=s.batch<N>(first);const auto view=s.directBatch<N>(first);assert(old.count==view.count);assert(!std::memcmp(old.position.data(),view.position,N*sizeof(Constant)));assert(!std::memcmp(old.color.data(),view.color,N*sizeof(Constant)));if(view.count==N)assert(view.position==s.position.data()+first&&view.color==s.color.data()+first);else assert(view.position==view.paddedPosition.data()&&view.color==view.paddedColor.data());++cases;}return cases;}
int main(){std::mt19937 random(4631);Selection s;unsigned cases=0;for(unsigned count=0;count<=Slots;++count){s.count=count;for(unsigned i=0;i<Slots;++i)for(unsigned k=0;k<4;++k){auto bits=random();std::memcpy(&s.position[i][k],&bits,4);bits=random();std::memcpy(&s.color[i][k],&bits,4);}cases+=check<1>(s)+check<8>(s)+check<16>(s);}std::printf("LIGHT_DIRECT_CONSTANTS_PASS cases=%u exactPayloads=1 fullCopies=0 partialZeroed=1 timings=0\n",cases);}
