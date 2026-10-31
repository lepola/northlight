#include <cstdio>
#include <cassert>
#include <chrono>
#include <cstring>
#include "particle_texture_stats.h"
#include "fixtures/material_texture_reference.h"
static void equal(const NorthlightParticleTextures::View& a,const NorthlightParticleTextures::View& b){assert(a.width==b.width&&a.height==b.height&&a.mip==b.mip&&a.format==b.format&&a.revision==b.revision&&a.rgbaHash==b.rgbaHash);assert(!std::memcmp(a.rgba,b.rgba,size_t(a.width)*a.height*4));}
static void edgeCases(){
    using F=NorthlightActorTexture::Format;unsigned cases=0;int key;
    for(unsigned format:{21u,22u,23u,24u,25u,26u,0x31545844u,0x33545844u,0x35545844u}){
        const F type=format==21?F::BGRA8:format==22?F::BGRX8:format==23?F::RGB565:format==24?F::XRGB1555:format==25?F::ARGB1555:format==26?F::ARGB4444:format==0x31545844u?F::BC1:format==0x33545844u?F::BC2:F::BC3;
        for(auto shape:{std::array<unsigned,2>{1,1024},{1024,1},{129,5},{7,513},{137,251},{512,1024},{1024,512}})for(bool lastPadding:{false,true}){
            const auto w=shape[0],h=shape[1];const size_t row=NorthlightActorTexture::rowBytes(w,type),rows=NorthlightActorTexture::rowCount(h,type),pitch=row+16,bytes=pitch*(rows-1)+(lastPadding?pitch:row);
            std::vector<unsigned char> data(bytes);for(size_t i=0;i<bytes;++i)data[i]=(i*71+i/7)%256;
            Frozen::Cache old;NorthlightMaterialTextures::Cache now;NorthlightParticleTextures::View a,b;
            old.upload(&key,&key,w,h,0,format,data.data(),bytes,pitch,true,true);now.upload(&key,&key,w,h,0,format,data.data(),bytes,pitch,true,true);assert(old.lookup(&key,a)&&now.lookup(&key,b));equal(a,b);
            if(bytes>1){now.upload(&key,&key,w,h,0,format,data.data(),pitch*(rows-1)+row-1,pitch,true,true);assert(!now.lookup(&key,b));}
            ++cases;
        }
        const auto frozenType=static_cast<FrozenActorTexture::Format>(type);const size_t pitch=NorthlightActorTexture::rowBytes(7,type)+16,bytes=pitch*NorthlightActorTexture::rowCount(7,type);
        for(unsigned prefix:{0u,4u}){std::vector<unsigned char> old(bytes+prefix),now;for(size_t i=0;i<old.size();++i)old[i]=(i*31+i/5)%256;now=old;
            assert(FrozenActorTexture::decode(old.data()+prefix,bytes,7,7,pitch,frozenType,old));assert(NorthlightActorTexture::decode(now.data()+prefix,bytes,7,7,pitch,type,now));assert(old==now);++cases;}
        if(NorthlightActorTexture::compressed(type)){
            std::vector<unsigned char> raw(bytes);for(size_t i=0;i<bytes;++i)raw[i]=(i*19+i/3)%256;std::vector<unsigned char> rgba;assert(NorthlightActorTexture::decode(raw.data(),bytes,7,7,pitch,type,rgba));std::array<std::array<unsigned,4>,4> expected{},actual{};
            for(unsigned y=0;y<7;++y)for(unsigned x=0;x<7;++x)for(unsigned c=0;c<4;++c)expected[(y/4)*2+x/4][c]+=rgba[(y*7+x)*4+c];
            auto write=[&](unsigned x,unsigned y,const std::array<unsigned,4>& sum){actual[(y/4)*2+x/4]=sum;};
            if(type==F::BC1)NorthlightActorTexture::decodeCompressed<F::BC1,true>(raw.data(),7,7,pitch,write);else if(type==F::BC2)NorthlightActorTexture::decodeCompressed<F::BC2,true>(raw.data(),7,7,pitch,write);else NorthlightActorTexture::decodeCompressed<F::BC3,true>(raw.data(),7,7,pitch,write);assert(actual==expected);++cases;
        }
    }
    for(unsigned value=0;value<256;++value){unsigned packed=0;for(unsigned i=0;i<4;++i)packed+=1u<<(((value>>(2*i))&3)*8);assert(packed==NorthlightActorTexture::SelectorCounts[value]);}
    printf("TEXTURE_EDGE_PASS cases=%u aliasesExact=1 partialBlocksExact=1 truncatedRejected=1 selectorHistogramExact=1\n",cases);
}
int main(){Frozen::enabled=true;NorthlightMaterialTextures::enabled=true;edgeCases();Frozen::Cache old;NorthlightMaterialTextures::Cache now;std::vector<unsigned char> raw(1024*1024*4);for(unsigned i=0;i<raw.size();++i)raw[i]=(i*17+i/73)%256;int keys[129];NorthlightParticleTextures::View x,y;
 for(unsigned i=0;i<128;++i){old.upload(keys+i,keys+i,128,128,0,21,raw.data(),128*128*4,128*4,true,true);now.upload(keys+i,keys+i,128,128,0,21,raw.data(),128*128*4,128*4,true,true);}
 for(unsigned i=0;i<128;++i){const bool a=old.lookup(keys+i,x),b=now.lookup(keys+i,y);assert(a==b);if(a)equal(x,y);assert(old.version(keys+i)==now.version(keys+i));}
 std::vector<double> times[2];volatile unsigned long long sink=0;
 for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<30000;++n)sink=mode?now.version(keys+127):old.version(keys+127);if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30000);}
 for(auto& v:times)std::sort(v.begin(),v.end());printf("TEXTURE_MRU_PASS old=%.9f new=%.9f ratio=%.5f\n",times[0][5],times[1][5],times[1][5]/times[0][5]);
 old.erase(keys+127);now.erase(keys+127);assert(!old.lookup(keys+127,x)&&!now.lookup(keys+127,y));old.clear();now.clear();assert(!now.lookup(keys,y));
{
 old.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);now.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);assert(old.copy(keys,keys+1,nullptr)&&now.copy(keys,keys+1,nullptr));
 std::vector<double> times[2];for(unsigned block=0;block<10;++block)for(unsigned turn=0;turn<2;++turn){const auto mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<256;++n)assert(mode?now.copy(keys,keys+1,nullptr):old.copy(keys,keys+1,nullptr));if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/256);}for(auto& v:times)std::sort(v.begin(),v.end());assert(old.lookup(keys+1,x)&&now.lookup(keys+1,y));equal(x,y);printf("TEXTURE_COPY_PASS old=%.9f new=%.9f ratio=%.5f\n",times[0][4],times[1][4],times[1][4]/times[0][4]);old.clear();now.clear();
 }
{
 old.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);now.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);std::vector<double> times[2];for(unsigned block=0;block<10;++block)for(unsigned turn=0;turn<2;++turn){const auto mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<64;++n){if(mode)now.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);else old.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);}if(block>=2)times[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/64);}for(auto& v:times)std::sort(v.begin(),v.end());assert(old.lookup(keys,x)&&now.lookup(keys,y));equal(x,y);printf("UNCHANGED_UPLOAD_PASS old=%.9f new=%.9f ratio=%.5f revisionUnchanged=1\n",times[0][4],times[1][4],times[1][4]/times[0][4]);
 for(unsigned byte:{0u,3u,511u,65535u}){raw[byte]^=71;old.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);now.upload(keys,keys,128,128,0,21,raw.data(),128*128*4,512,true,true);assert(old.lookup(keys,x)&&now.lookup(keys,y));equal(x,y);}old.clear();now.clear();
 }
 unsigned cases=0;
 for(unsigned format:{21u,22u,23u,24u,25u,26u,0x31545844u,0x33545844u,0x35545844u})for(unsigned w:{1u,127u,128u,129u,255u,512u,1024u}){const unsigned h=w;using F=NorthlightActorTexture::Format;F f=format==21?F::BGRA8:format==22?F::BGRX8:format==23?F::RGB565:format==24?F::XRGB1555:format==25?F::ARGB1555:format==26?F::ARGB4444:format==0x31545844u?F::BC1:format==0x33545844u?F::BC2:F::BC3;const size_t pitch=NorthlightActorTexture::rowBytes(w,f)+16,bytes=pitch*NorthlightActorTexture::rowCount(h,f);std::vector<unsigned char> data(bytes);for(unsigned i=0;i<bytes;++i)data[i]=(i*19+i/53)%256;old.upload(keys,keys,w,h,0,format,data.data(),bytes,pitch,true,true);now.upload(keys,keys,w,h,0,format,data.data(),bytes,pitch,true,true);const bool a=old.lookup(keys,x),b=now.lookup(keys,y);assert(a==b);if(a)equal(x,y);++cases;old.clear();now.clear();}
 std::vector<double> uploadTimes[2];
 for(unsigned block=0;block<10;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<8;++n){raw[0]=unsigned(block*19+n);if(mode)now.upload(keys,keys,1024,1024,0,21,raw.data(),raw.size(),4096,true,true);else old.upload(keys,keys,1024,1024,0,21,raw.data(),raw.size(),4096,true,true);}if(block>=2)uploadTimes[mode].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/8);}
 for(auto& v:uploadTimes)std::sort(v.begin(),v.end());printf("TEXTURE_UPLOAD_PASS cases=%u old=%.9f new=%.9f ratio=%.5f\n",cases,uploadTimes[0][4],uploadTimes[1][4],uploadTimes[1][4]/uploadTimes[0][4]);(void)sink;
}
