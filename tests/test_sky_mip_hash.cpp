#include "sky_mip_hash.h"
#include "atlas_upload_proof.h"
#include <cassert>
#include <cstring>
int main(){using namespace SkyMipHash;
 unsigned char b[24]={1,2,3,4,5,6,7,8,255,255,255,255,9,10,11,12,13,14,15,16,255,255,255,255};
 int locks=0,unlocks=0,pitch=12;bool lockOk=true,unlockOk=true;const void* data=b;
 auto lock=[&](const void*& p,int& n,long& hr){++locks;p=data;n=pitch;hr=lockOk?0:-1;return lockOk;};
 auto unlock=[&](long& hr){++unlocks;hr=unlockOk?0:-1;return unlockOk;};Budget budget;
 auto run=[&](bool managed,unsigned fmt,unsigned w,unsigned h){return capture(managed,fmt,w,h,budget,lock,unlock,NorthlightAtlasProof::hashRows);};
 auto r=run(true,21,2,2);assert(r.status==Status::ok&&r.bytes==16&&locks==1&&unlocks==1);
 unsigned char tight[16];std::memcpy(tight,b,8);std::memcpy(tight+8,b+12,8);assert(r.hash==NorthlightAtlasProof::hashRows(tight,8,2,8));
 assert(run(false,21,2,2).status==Status::unsupported);assert(run(true,1,2,2).status==Status::unsupported);assert(run(true,21,0,2).status==Status::unsupported);assert(locks==1);
 pitch=7;assert(run(true,21,2,2).status==Status::invalid_lock&&unlocks==2);pitch=12;data=nullptr;assert(run(true,21,2,2).status==Status::invalid_lock&&unlocks==3);
 data=b;lockOk=false;assert(run(true,21,2,2).status==Status::lock_failed&&unlocks==3);assert(budget.count==4);lockOk=true;unlockOk=false;budget=Budget{};assert(run(true,21,2,2).status==Status::unlock_failed&&unlocks==4);
 Layout x;assert(layout(0x31545844,5,7,x)&&x.rowBytes==16&&x.rows==2&&x.bytes==32);assert(layout(0x35545844,5,7,x)&&x.rowBytes==32&&x.rows==2&&x.bytes==64);
 budget.count=MaxTextures;assert(run(true,21,2,2).status==Status::overflow);budget.count=0;budget.bytes=MaxBytes-15;assert(run(true,21,2,2).status==Status::overflow);
}
