#include "particle_texture_stats.h"
#include <cassert>
#include <cstdio>
static const void* key(unsigned n){return reinterpret_cast<const void*>(std::uintptr_t(n));}
int main(){
    using namespace NorthlightMaterialTextures;Cache c;NorthlightParticleTextures::View view;
    std::vector<std::uint8_t> atlas(512*512*4,255);for(size_t i=0;i<atlas.size();i+=4){atlas[i]=20;atlas[i+1]=80;atlas[i+2]=160;}
    c.upload(key(1),key(10),512,512,0,21,atlas.data(),atlas.size(),512*4,true,true);assert(c.lookup(key(1),view)&&view.width==128&&view.height==128&&view.rgba[0]==160&&view.rgba[2]==20);const auto sourceHash=view.rgbaHash;const auto firstRevision=view.revision;c.upload(key(1),key(10),512,512,0,21,atlas.data(),atlas.size(),512*4,true,true);assert(c.lookup(key(1),view)&&view.revision==firstRevision);
    assert(c.copy(key(1),key(2),nullptr)&&c.lookup(key(2),view)&&view.rgbaHash==sourceHash);const auto revision=view.revision;assert(c.copy(key(1),key(2),nullptr)&&c.lookup(key(2),view)&&view.revision==revision);
    std::vector<std::uint8_t> patch(64*64*4,255);for(size_t i=0;i<patch.size();i+=4){patch[i]=220;patch[i+1]=40;patch[i+2]=10;}
    assert(c.updateRect(key(2),key(20),512,512,0,21,patch.data(),patch.size(),64*4,64,64,128,128,true));assert(c.lookup(key(2),view)&&view.revision>revision&&view.rgbaHash!=sourceHash);
    const auto* pixel=view.rgba+(20*128+20)*4;assert(pixel[0]==10&&pixel[1]==40&&pixel[2]==220);assert(view.rgba[0]==160);const auto editedRevision=view.revision;assert(c.updateRect(key(2),key(20),512,512,0,21,patch.data(),patch.size(),64*4,64,64,128,128,true));assert(c.lookup(key(2),view)&&view.revision==editedRevision);
    assert(!c.updateRect(key(2),key(20),512,512,0,21,patch.data(),patch.size(),64*4,65,64,129,128,true));assert(!c.lookup(key(2),view)&&c.lookup(key(1),view)&&view.rgbaHash==sourceHash);
    // Losing CPU pixels must not erase the revision: two later GPU writes are distinct cache invalidations.
    const auto invalidated=c.version(key(2));assert(invalidated);
    c.invalidate(key(2));assert(c.version(key(2))!=invalidated&&!c.lookup(key(2),view));
    const auto written=c.version(key(2));c.invalidate(key(2));assert(c.version(key(2))!=written);
    enabled.store(false);assert(!c.version(key(2)));enabled.store(true);
    c.erase(key(2));assert(!c.version(key(2)));
    for(unsigned i=3;i<200;++i){assert(c.version(key(1)));assert(c.copy(key(1),key(i),key(i+1000)));assert(c.bytes()<=Cache::PixelBudget&&c.size()<=Cache::MaxEntries);}
    // Compact colour decoding must preserve source bytes when output aliases them.
    using Format=NorthlightActorTexture::Format;
    for(auto format:{Format::BGRA8,Format::BGRX8,Format::RGB565,Format::XRGB1555,Format::ARGB1555,Format::ARGB4444}){
        const size_t pitch=NorthlightActorTexture::rowBytes(31,format)+8;std::vector<std::uint8_t> source(pitch*17);
        for(size_t i=0;i<source.size();++i)source[i]=std::uint8_t(i*37+i/41);
        auto aliased=source;std::vector<std::uint8_t> expected;
        assert(NorthlightActorTexture::decode(source.data(),source.size(),31,17,pitch,format,expected));
        assert(NorthlightActorTexture::decode(aliased.data(),aliased.size(),31,17,pitch,format,aliased));assert(aliased==expected);
    }
    // A scaled patch can borrow retained pixels; the patch must be decoded before mutation.
    c.clear();c.upload(key(1),key(10),512,512,0,21,atlas.data(),atlas.size(),512*4,true,true);
    assert(c.lookup(key(1),view));std::vector<std::uint8_t> borrowed(view.rgba,view.rgba+64*64*4);
    assert(c.copy(key(1),key(2),nullptr));assert(c.lookup(key(1),view));
    assert(c.updateRect(key(1),key(10),512,512,0,21,view.rgba,64*64*4,64*4,64,64,128,128,true));
    assert(c.updateRect(key(2),nullptr,512,512,0,21,borrowed.data(),borrowed.size(),64*4,64,64,128,128,true));
    NorthlightParticleTextures::View other;assert(c.lookup(key(1),view)&&c.lookup(key(2),other));assert(view.rgbaHash==other.rgbaHash&&!std::memcmp(view.rgba,other.rgba,128*128*4));
    c.clear();assert(c.bytes()==0);std::puts("MATERIAL_TEXTURE_UPLOAD_PASS atlas_downsample copy_revision aligned_partial_update unaligned_invalidation bounded_eviction");
}
