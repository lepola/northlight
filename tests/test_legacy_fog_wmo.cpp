#include "legacy_fog.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace NorthlightLegacyFog;
using V=std::vector<uint32_t>;
static int proof(const V& v){return ps3FogColorRegister(v.data(),v.size());}
static V with(V v,size_t at,V add){v.insert(v.begin()+at,add.begin(),add.end());return v;}
static V set(V v,size_t at,uint32_t word){v[at]=word;return v;}

int main(){
    // Real MapObj PS3 token sequences (patch-2 archive): the colour is copied to a temp first (mov rK.xyz, c2 ; mad ..., -rK).
    const V formA={0xffff0300,0x05000051,0xa00f0000,0x40000000,0x00000000,0x00000000,0x00000000,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x90030001,0x0200001f,0x8000000b,0x90010002,0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0x90e40001,0xa0e40800,0x03000005,0x80080800,0x80ff0000,0x90ff0000,0x03000005,0x80070000,0x80e40000,0x90e40000,0x02000001,0x80070001,0xa0e40002,0x04000004,0x80070000,0x80e40000,0xa0000000,0x81e40001,0x04000004,0x80070800,0x90000002,0x80e40000,0xa0e40002,0x0000ffff};
    const V formATail={0xffff0300,0x05000051,0xa00f0000,0x40000000,0x00000000,0x00000000,0x00000000,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x90030001,0x0200001f,0x8000000b,0x90010002,0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0x90e40001,0xa0e40800,0x03000005,0x80070000,0x80e40000,0x90e40000,0x02000001,0x80070001,0xa0e40002,0x04000004,0x80070000,0x80e40000,0xa0000000,0x81e40001,0x04000004,0x80070800,0x90000002,0x80e40000,0xa0e40002,0x02000001,0x80080800,0x90ff0000,0x0000ffff};
    const V formB={0xffff0300,0x05000051,0xa00f0000,0x40000000,0x00000000,0x00000000,0x00000000,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x90030001,0x0200001f,0x8000000b,0x90010002,0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0x90e40001,0xa0e40800,0x04000004,0x800f0001,0x90ff0000,0x80ff0000,0xa1ff0002,0x03000005,0x800f0000,0x80930000,0x90930000,0x01000041,0x800f0001,0x02000001,0x80070001,0xa0e40002,0x04000004,0x800e0000,0x80e40000,0xa0000000,0x81900001,0x02000001,0x80080800,0x80000000,0x04000004,0x80070800,0x90000002,0x80f90000,0xa0e40002,0x0000ffff};
    const V formC={0xffff0300,0x05000051,0xa00f0000,0x40000000,0x00000000,0x00000000,0x00000000,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x8001000a,0x90080001,0x0200001f,0x80000005,0x90030002,0x0200001f,0x80010005,0x90030003,0x0200001f,0x8000000b,0x90010004,0x0200001f,0x90000000,0xa00f0800,0x0200001f,0x90000000,0xa00f0801,0x03000042,0x800f0000,0x90e40002,0xa0e40800,0x03000042,0x800f0001,0x90e40003,0xa0e40801,0x04000012,0x800f0002,0x90ff0001,0x80930000,0x80930001,0x04000004,0x800f0000,0x90ff0000,0x80000002,0xa1ff0002,0x03000005,0x800f0001,0x80e40002,0x90930000,0x01000041,0x800f0000,0x02000001,0x80070000,0xa0e40002,0x04000004,0x80070000,0x80f90001,0xa0000000,0x81e40000,0x02000001,0x80080800,0x80000001,0x04000004,0x80070800,0x90000004,0x80e40000,0xa0e40002,0x0000ffff};
    assert(proof(formA)==2&&proof(formATail)==2&&proof(formB)==2&&proof(formC)==2);
    // Original common-2 copies use c16 (same idiom): rename the constant in the mov and in the fog mad.
    assert(proof(set(set(formA,33,0xa0e40010),43,0xa0e40010))==16);
    assert(proof(set(set(formB,36,0xa0e40010),49,0xa0e40010))==16);
    // Control flow before the epilogue is allowed (it clears the tracking); the copy has to follow it.
    assert(proof(with(formA,19,{0x01000028,0xb0e40000,0x0000002b}))==2);
    // Negative mutations of form A: all fail closed.
    assert(proof(set(formA,43,0xa0e40003))==-1);                    // fog mad colour differs from the copied one
    assert(proof(set(formA,33,0xa0e40003))==-1);                    // mov copies another constant
    assert(proof(set(formA,33,0xa0a50002))==-1);                    // mov swizzle .yyz
    assert(proof(set(formA,38,0x80e40001))==-1);                    // pre-mad does not negate
    assert(proof(set(formA,41,0x90550002))==-1);                    // fog .y
    assert(proof(set(formA,42,0x80e40001))==-1);                    // fog mad reads another temp
    assert(proof(set(formA,35,0x80170000))==-1);                    // _sat on the pre-mad destination
    assert(proof(with(formA,34,{0x02000001,0x80020001,0xa0000000}))==-1); // mov r1.y, c0.x between the copy and the use
    assert(proof(with(formA,44,{0x02000001,0x80070800,0xa0e40000}))==-1); // mov oC0.xyz after the epilogue
    assert(proof(with(formATail,44,{0x02000001,0x80070800,0xa0e40000}))==-1);
    assert(proof(with(formA,1,{0x05000051,0xa00f0002,0,0,0,0}))==-1);    // local def of c2
    assert(proof(with(formA,19,{0x0000001c}))==-1);                      // ret
    assert(proof(with(formA,19,{0x0000002b}))==-1);                      // endif without if
    assert(proof(with(formA,19,{0x01000019,0x10000000}))==-1);           // call
    assert(proof(with(formA,19,{0x01000028,0xb0e40000}))==-1);           // unterminated if
    assert(proof(set(formA,15,0x90010003))==-1);                         // dcl_fog v3 but the fog mad reads v2
    assert(proof(set(set(formA,15,0x90010003),41,0x90000003))==2);       // the fog input register number is read, not assumed
    assert(proof(V(formA.begin(),formA.end()-1))==-1&&proof(V{})==-1);
    // A temp holding the colour is no proof when it is overwritten before the pre-mad.
    assert(proof(with(formA,34,{0x03000005,0x80070001,0x80e40000,0x90e40000}))==-1);
    assert(proof(with(formA,44,{0x05000051,0xa00f0002,0,0,0,0}))==-1);   // def of the colour constant after the epilogue
    assert(proof(with(formATail,44,{0x05000051,0xa00f0002,0,0,0,0}))==-1);
    assert(proof(with(formATail,43,{0x05000051,0xa00f0002,0,0,0,0}))==-1);
    std::puts("legacy fog wmo ok");return 0;
}
