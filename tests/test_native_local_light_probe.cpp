#include "native_local_light_probe.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
    NorthlightNativeLocalLight::Probe probe;NorthlightNativeLocalLight::Sample out;
    float rows[256][4]={};bool known[256]={},written[256]={};
    rows[17][0]=.6f;rows[17][1]=.3929412f;rows[17][2]=.0682353f;rows[17][3]=1;
    for(unsigned i=17;i<28;++i)known[i]=true;
    assert(!probe.sample(false,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(!probe.sample(true,true,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(!probe.sample(true,false,false,true,0,1,rows,known,written,0x7ff,true,out));
    assert(!probe.sample(true,false,true,false,0,1,rows,known,written,0x7ff,true,out));
    assert(!probe.sample(true,false,true,true,0,1,rows,known,nullptr,0x7ff,true,out));
    assert(probe.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(out.known==0x7ff&&out.fromGet==0x7ff&&!out.fromWrite&&out.finite==0x7ff&&out.read==0x7ff&&out.exactReads&&out.rows[0][0]==.6f);
    assert(!probe.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    known[18]=false;rows[19][0]=std::numeric_limits<float>::quiet_NaN();
    assert(probe.sample(true,false,true,true,0,1,rows,known,written,0,false,out));
    assert(!(out.known&2)&&!(out.finite&4)&&out.rows[1][0]==0&&!out.exactReads);
    for(unsigned i=0;i<6;++i)assert(probe.sample(true,false,true,true,0,100+i,rows,known,written,0,false,out));
    assert(!probe.sample(true,false,true,true,0,500,rows,known,written,0,false,out));
    assert(probe.sample(true,false,true,true,127,1,rows,known,written,0,false,out));
    unsigned remaining=0;
    for(unsigned frame=128;frame<400;++frame)remaining+=probe.sample(true,false,true,true,frame,1,rows,known,written,0,false,out);
    assert(remaining==119&&probe.full());
    NorthlightNativeLocalLight::Probe provenance;
    std::memset(known,0,sizeof known);std::memset(written,0,sizeof written);
    // Unknown bank storage may contain old nonzero values; it is never evidence.
    assert(provenance.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(!out.known&&!out.fromGet&&!out.fromWrite&&!out.finite&&out.rows[0][0]==0);
    written[17]=true;
    assert(provenance.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(out.known==1&&!out.fromGet&&out.fromWrite==1&&out.finite==1&&out.rows[0][0]==.6f);
    known[17]=true;known[18]=true;written[19]=true;
    assert(provenance.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(out.known==7&&out.fromGet==3&&out.fromWrite==5&&out.finite==3);
    // Provenance changes are retained even when all row values are identical.
    known[18]=false;written[18]=true;
    assert(provenance.sample(true,false,true,true,0,1,rows,known,written,0x7ff,true,out));
    assert(out.known==7&&out.fromGet==1&&out.fromWrite==7&&out.finite==3);
    std::puts("PASS native local-light probe: getter/write provenance, exact/unknown reads, no fabricated rows, inactive gates, dedup, per-frame/session bounds");
}
