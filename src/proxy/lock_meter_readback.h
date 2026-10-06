#pragma once
#include <d3d9.h>
#include "diagnostics_switch.h"
#include "lock_meter.h"
/* 0.3.192 (DXVK3): the read-back counters of the LOCK METER line. dynamic = DEFAULT|DYNAMIC (direct-mapped on DXVK 3.x:
   waits unless NOOVERWRITE), defaultStatic = other DEFAULT (BUFFER mode: Unlock still flushes), other = MANAGED etc.
   One GetDesc per lock, only with Diagnostics=1. */
namespace NorthlightLockMeter {
inline void readBack(IDirect3DVertexBuffer9* b,std::uint64_t bytes){
    ReadClass cls=ReadOther;if(b&&NorthlightDiagnostics::enabled()){D3DVERTEXBUFFER_DESC d={};if(!FAILED(b->GetDesc(&d)))cls=classify(d.Usage,unsigned(d.Pool));}
    readBack(cls,bytes);
}
inline void readBack(IDirect3DIndexBuffer9* b,std::uint64_t bytes){
    ReadClass cls=ReadOther;if(b&&NorthlightDiagnostics::enabled()){D3DINDEXBUFFER_DESC d={};if(!FAILED(b->GetDesc(&d)))cls=classify(d.Usage,unsigned(d.Pool));}
    readBack(cls,bytes);
}
}
