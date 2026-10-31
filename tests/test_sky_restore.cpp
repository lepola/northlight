#include "sky_restore.h"
#include <cassert>
int main(){
    using namespace SkyRestore;
    const Draw b{320,0x35545844,512,512,5,6,baseVS,basePS,0xddb1ff594d51e1aaull,0x1234,true};
    const Draw c{768,0x35545844,512,512,5,6,cloudVS,cloudPS,0x44e88f50406cbb61ull,0x1234,true};
    State s;s.begin(1);assert(!s.before(c));
    s.after(b,false);assert(!s.before(c));s.after(b,true);
    for(unsigned field=0;field<11;++field){
        auto other=c;
        switch(field){
        case 0:other.target++;break;case 1:other.ps++;break;case 2:other.vs++;break;
        case 3:other.hash++;break;case 4:other.count++;break;case 5:other.fmt++;break;
        case 6:other.w++;break;case 7:other.h++;break;case 8:other.src++;break;
        case 9:other.dst++;break;case 10:other.valid=false;break;
        }
        assert(!s.before(other));
    }
    s.targetSet(b.target);assert(s.before(c));assert(!s.before(c));
    s.invalidate();s.after(b,true);assert(!s.before(c)); // no second insertion in the same frame
    s.begin(2);assert(!s.before(c));s.after(b,true);s.invalidate();assert(!s.before(c)); // Clear(TARGET)
    s.after(b,true);s.targetSet(b.target+1);s.targetSet(b.target);assert(!s.before(c));
    s.after(b,true);s.colorWrite(b.target+1);assert(s.before(c)); // unrelated copy is harmless
    s.begin(3);s.after(b,true);s.colorWrite(b.target);assert(!s.before(c));
    s.after(b,true);assert(s.before(c));
    s=State{};assert(!s.before(c)); // device reset
}
