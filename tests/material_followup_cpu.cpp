#include "material_followup_fixture.h"
#include "fixtures/material_height_reference.h"
void MaterialCaptureStateTestAccess::textureStorage(){
 struct Frozen{IDirect3DBaseTexture9* texture=nullptr;unsigned stage=0;DWORD sampler[13]={};uint64_t bytes=0;~Frozen(){drop(texture);}};
 LimitTexture source;unsigned cases=0;
 for(unsigned count=1;count<=16;++count)for(unsigned reserve=0;reserve<2;++reserve){
  {std::vector<NorthlightAutoBump::DeferredTexture> entries;if(reserve){failAllocationAfter=1;entries.reserve(count);assert(failAllocationAfter==0);}
   for(unsigned i=0;i<count;++i){NorthlightAutoBump::DeferredTexture item;item.texture=&source;source.AddRef();item.stage=i;item.bytes=i*2048;for(unsigned j=0;j<13;++j)item.sampler[j]=i*100+j;entries.push_back(std::move(item));assert(!item.texture);}
   if(reserve){assert(failAllocationAfter==0);failAllocationAfter=-1;}
   assert(source.refs==count+1);for(unsigned i=0;i<count;++i){assert(entries[i].texture==&source&&entries[i].stage==i&&entries[i].bytes==i*2048);for(unsigned j=0;j<13;++j)assert(entries[i].sampler[j]==i*100+j);}}
  assert(source.refs==1);++cases;
 }
 std::vector<double> times[2];volatile unsigned observed=0;
 for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned k=0;k<2048;++k){
  if(mode){std::vector<NorthlightAutoBump::DeferredTexture> entries;entries.reserve(8);for(unsigned i=0;i<8;++i){NorthlightAutoBump::DeferredTexture item;item.texture=&source;source.AddRef();item.stage=i;entries.push_back(std::move(item));}observed=entries[7].stage;}
  else{std::vector<std::unique_ptr<Frozen>> entries;for(unsigned i=0;i<8;++i){auto item=std::make_unique<Frozen>();item->texture=&source;source.AddRef();item->stage=i;entries.push_back(std::move(item));}observed=entries[7]->stage;}
 }if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/2048);}
 for(auto& v:times)std::sort(v.begin(),v.end());assert(source.refs==1&&observed==7);printf("CAPTURE_TEXTURE_STORAGE_PASS cases=%u old=%.9f new=%.9f ratio=%.5f allocationsFor8Before=12 after=1 leasesExact=1 samplerBytesExact=1\n",cases,times[0][5],times[1][5],times[1][5]/times[0][5]);
}
void DisplacementStateTestAccess::borrowedHeight(){using namespace NorthlightMaterialHeight;NorthlightMaterialTextures::enabled=true;auto& cache=NorthlightMaterialTextures::cache();LimitTexture source;std::vector<unsigned char> bytes(128*128*4);for(unsigned i=0;i<bytes.size();++i)bytes[i]=(i*31+i/17)%256;cache.upload(&source,&source,128,128,0,21,bytes.data(),bytes.size(),512,true,true);unsigned cases=0;NorthlightAutoDisplacement old(nullptr),now(nullptr);
 auto reset=[](NorthlightAutoDisplacement& r){r.reset();r.readsThisFrame=0;};
 for(bool model:{false,true})for(unsigned polarity:{0u,1u,2u})for(unsigned details:{0u,75u,150u})for(unsigned balance:{0u,50u,100u}){reset(old);reset(now);old.polarity=now.polarity=polarity;old.heightDetails=now.heightDetails=details;old.heightBalance=now.heightBalance=balance;const auto* a=reference(old,&source,model);const auto* b=now.height(&source,model);assert(a&&b&&a->width==b->width&&a->height==b->height&&a->values==b->values);++cases;}
 old.polarity=now.polarity=0;old.heightDetails=now.heightDetails=75;std::vector<double> times[2];for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const auto mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<32;++i){auto& r=mode?now:old;reset(r);assert(mode?r.height(&source,false):reference(r,&source,false));}if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/32);}
 for(auto& v:times)std::sort(v.begin(),v.end());reset(old);reset(now);cache.invalidate(&source);assert(!reference(old,&source,true)&&!now.height(&source,true));reset(old);reset(now);cache.clear();assert(source.refs==1);printf("HEIGHT_BORROW_PASS cases=%u old=%.9f new=%.9f ratio=%.5f rgbaCopyBefore=65536 after=0 fallbackExact=1\n",cases,times[0][5],times[1][5],times[1][5]/times[0][5]);
}
static void profileStatistics(){using namespace NorthlightMaterialHeight;LimitTexture source;NorthlightMaterialTextures::enabled=true;auto& cache=NorthlightMaterialTextures::cache();std::vector<unsigned char> pixels(128*128*4);unsigned cases=0;
 for(unsigned revision=0;revision<8;++revision){for(unsigned i=0;i<pixels.size();++i)pixels[i]=(i*31+i/13+revision*11)%256;cache.upload(&source,&source,128,128,0,21,pixels.data(),pixels.size(),512,true,true);
  for(unsigned polarity:{0u,1u,2u})for(unsigned details:{0u,75u,150u})for(unsigned balance:{0u,50u,100u}){const auto old=FrozenHeight::textureProfile(&source,polarity,details,balance);for(unsigned hit=0;hit<3;++hit){const auto now=textureProfile(&source,polarity,details,balance);assert(!std::memcmp(old.data(),now.data(),16));}++cases;}
 }
 std::vector<double> times[2];volatile float sink=0;for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned k=0;k<64;++k)sink=(mode?textureProfile(&source,0,75,50):FrozenHeight::textureProfile(&source,0,75,50))[0];if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64);}
 for(auto& v:times)std::sort(v.begin(),v.end());cache.invalidate(&source);assert(textureProfile(&source,0,75,50)==FrozenHeight::textureProfile(&source,0,75,50));cache.clear();assert(source.refs==1&&sink>0);printf("PROFILE_STATISTICS_PASS cases=%u old=%.9f new=%.9f ratio=%.5f invalidationExact=1\n",cases,times[0][5],times[1][5],times[1][5]/times[0][5]);
}

struct Texture:LimitTexture{D3DFORMAT format;unsigned w,h,pitch,unlocks=0;bool failUnlock=false,nullBits=false,shortPitch=false;std::vector<unsigned char> pixels;
 Texture(D3DFORMAT f,unsigned n):format(f),w(n),h(n){using F=NorthlightActorTexture::Format;const auto type=f==D3DFMT_A8R8G8B8?F::BGRA8:f==D3DFMT_X8R8G8B8?F::BGRX8:f==D3DFMT_R5G6B5?F::RGB565:f==D3DFMT_DXT1?F::BC1:f==D3DFMT_DXT3?F::BC2:F::BC3;pitch=NorthlightActorTexture::rowBytes(w,type)+16;pixels.resize(size_t(pitch)*NorthlightActorTexture::rowCount(h,type));for(unsigned i=0;i<pixels.size();++i)pixels[i]=(i*71+i/11)%256;}
 HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT,D3DSURFACE_DESC* out)override{*out={format,D3DRTYPE_SURFACE,0,D3DPOOL_MANAGED,D3DMULTISAMPLE_NONE,0,w,h};return D3D_OK;}
 HRESULT STDMETHODCALLTYPE LockRect(UINT,D3DLOCKED_RECT* out,const RECT*,DWORD)override{out->Pitch=shortPitch?1:pitch;out->pBits=nullBits?nullptr:pixels.data();return D3D_OK;}
 HRESULT STDMETHODCALLTYPE UnlockRect(UINT)override{++unlocks;return failUnlock?E_FAIL:D3D_OK;}
};
static void lockedHeight(){using namespace NorthlightMaterialHeight;NorthlightMaterialTextures::enabled=false;unsigned cases=0;for(auto format:{D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_R5G6B5,D3DFMT_DXT1,D3DFMT_DXT3,D3DFMT_DXT5})for(unsigned n:{1u,7u,128u})for(unsigned fault=0;fault<4;++fault){Texture a(format,n),b(format,n);a.failUnlock=b.failUnlock=fault==1;a.nullBits=b.nullBits=fault==2;a.shortPitch=b.shortPitch=fault==3;Image x,y;const bool old=FrozenHeight::read(&a,x),now=read(&b,y);assert(old==now&&a.unlocks==b.unlocks&&x.width==y.width&&x.height==y.height&&x.rgba==y.rgba&&!std::memcmp(&x.mean,&y.mean,4)&&!std::memcmp(&x.accentMean,&y.accentMean,4));++cases;}
 Texture source(D3DFMT_A8R8G8B8,128);Image image;failAllocationAfter=0;bool failed=false;try{read(&source,image,false);}catch(const std::bad_alloc&){failed=true;}failAllocationAfter=-1;assert(failed&&source.unlocks==1);
 std::vector<double> times[2];volatile unsigned sink=0;for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned k=0;k<64;++k){Image value;assert(mode?read(&source,value,false):FrozenHeight::read(&source,value,false));sink=value.rgba[0];}if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64);}
 for(auto& v:times)std::sort(v.begin(),v.end());printf("LOCKED_HEIGHT_PASS cases=%u old=%.9f new=%.9f ratio=%.5f unlockOnException=1 stagingAllocationBefore=1 after=0\n",cases,times[0][5],times[1][5],times[1][5]/times[0][5]);(void)sink;
}

int main(){MaterialCaptureStateTestAccess::textureStorage();DisplacementStateTestAccess::borrowedHeight();profileStatistics();lockedHeight();puts("MATERIAL_FOLLOWUP_CPU_PASS");}
