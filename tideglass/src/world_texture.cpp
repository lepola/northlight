#include "world_texture.h"
#include <algorithm>
#include <array>
#include <utility>

namespace water::world
{
std::size_t Texture::Bytes() const
{
    std::size_t result = sizeof(*this) + mips.capacity()*sizeof(TextureMip);
    for (const auto& mip : mips) result += mip.bytes.capacity();
    return result;
}

TextureError DecodeBlp(world::Bytes input, std::size_t payloadLimit, Texture& output, TextureMips policy,unsigned maxDimension,unsigned mipBias)
{
    if (mipBias>1) return TextureError::Format;
    if (policy != TextureMips::Complete && policy != TextureMips::StoredPrefix &&
        policy != TextureMips::RenderPrefix) return TextureError::Format;
    constexpr std::size_t Header = 148, PaletteEnd = Header+1024;
    uint32_t kind = 0, width = 0, height = 0;
    if (!input.Contains(0, Header) || std::memcmp(input.data, "BLP2", 4) ||
        !input.U32(4, kind) || kind != 1) return TextureError::Header;
    input.U32(12, width); input.U32(16, height);
    const auto encoding = input.data[8], alphaDepth = input.data[9], alphaEncoding = input.data[10];
    // A terrain cube map: six square faces in a row, for the game's shine on ice. Its first face serves as a plain layer here, and
    // its chain ends for good at the first level a block no longer divides by faces.
    const bool faces = encoding == 2 && width == height*6 && height >= 4 && !(height & (height-1)) && width <= 16384;
    if (!faces && (!width || !height || width > 16384 || height > 16384 || (width & (width-1)) || (height & (height-1))))
        return TextureError::Dimensions;
    const bool palette = encoding == 1;
    Texture candidate;
    if (palette) {
        if ((alphaDepth != 0 && alphaDepth != 1 && alphaDepth != 4 && alphaDepth != 8) ||
            !input.Contains(Header, 1024)) return TextureError::Format;
    } else if (encoding == 2) {
        switch (alphaEncoding) {
        case 0: candidate.format = TextureFormat::Bc1; break;
        case 1: candidate.format = TextureFormat::Bc2; break;
        case 7: candidate.format = TextureFormat::Bc3; break;
        default: return TextureError::Format;
        }
    } else return TextureError::Format;
    // The mip flag is a presence flag: shipped specular tilesets (e.g. stranglethorndirt03_s)
    // store 2 with a complete chain, and the client draws them. The chain itself is validated below.
    unsigned legalLevels = 1;
    if (input.data[11]) for (auto edge = std::max(width, height); edge > 1; edge >>= 1) ++legalLevels;
    unsigned levels = legalLevels;
    bool ended = false;
    struct Level { std::size_t offset = 0, encoded = 0, decoded = 0; uint32_t width = 0, height = 0; };
    std::array<Level, 16> layout{};
    std::size_t total = 0;
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t offset = 0, size = 0;
        input.U32(20+i*4, offset); input.U32(84+i*4, size);
        if (i >= legalLevels) { if (size) ++candidate.ignoredMipEntries; continue; }
        if (faces && (height>>i) < 4) {
            if (!ended) { ended = true; levels = i; candidate.missingTailMips = legalLevels-i; }
            continue;
        }
        if (ended && policy == TextureMips::RenderPrefix) continue;
        if (!size) {
            if (policy == TextureMips::Complete || !i) return TextureError::Mips;
            if (!ended) { ended = true; levels = i; candidate.missingTailMips = legalLevels-i; }
            continue;
        }
        if (ended) return TextureError::Mips;
        auto& mip = layout[i];
        mip.width = std::max(1u, width >> i); mip.height = std::max(1u, height >> i);
        const std::size_t pixels = std::size_t(mip.width)*mip.height;
        MipLayout decoded;
        if (!GetMipLayout(candidate.format,mip.width,mip.height,decoded)) return TextureError::Dimensions;
        mip.decoded=decoded.Bytes();
        mip.encoded=palette ? pixels+(pixels*alphaDepth+7)/8 : mip.decoded;
        mip.offset = offset;
        bool valid = size == mip.encoded && offset >= (palette ? PaletteEnd : Header) && input.Contains(offset, size);
        for (unsigned previous = 0; previous < i; ++previous)
            if (mip.offset < layout[previous].offset+layout[previous].encoded &&
                layout[previous].offset < mip.offset+mip.encoded) valid = false;
        if (!valid) {
            if (policy != TextureMips::RenderPrefix || !i) return TextureError::Mips;
            ended = true; levels = i; candidate.missingTailMips = legalLevels-i; candidate.rejectedMipLevel = i;
            continue;
        }
    }
    unsigned first=0;
    // If a valid authored chain stops early, keep its smallest available level.
    while(maxDimension && first+1<levels && std::max(layout[first].width,layout[first].height)>maxDimension)++first;
    first=std::min(first+mipBias,levels-1);
    for(unsigned i=first;i<levels;++i){
        if(layout[i].decoded>payloadLimit-total)return TextureError::Budget;total+=layout[i].decoded;
    }
    candidate.mips.reserve(levels-first);
    for (unsigned i = first; i < levels; ++i) {
        const auto& level = layout[i];
        TextureMip mip;
        mip.width = level.width; mip.height = level.height;
        const auto* source = input.data+level.offset;
        if (faces) {
            // Whole blocks, so the first face's are the first of each row.
            MipLayout strip,face;
            mip.width=level.height;
            if (!GetMipLayout(candidate.format,level.width,level.height,strip) ||
                !GetMipLayout(candidate.format,mip.width,mip.height,face)) return TextureError::Dimensions;
            mip.bytes.resize(face.Bytes());
            for (unsigned row=0;row<face.rows;++row)
                std::memcpy(mip.bytes.data()+row*face.rowBytes,source+row*strip.rowBytes,face.rowBytes);
        }
        else if (!palette) mip.bytes.assign(source, source+level.encoded);
        else {
            mip.bytes.resize(level.decoded);
            const auto pixels = std::size_t(level.width)*level.height;
            const auto* alpha = source+pixels;
            for (std::size_t p = 0; p < pixels; ++p) {
                const auto* colour = input.data+Header+std::size_t(source[p])*4;
                auto* dest = mip.bytes.data()+p*4;
                std::memcpy(dest, colour, 3); // The palette's fourth byte is unused; alpha follows the indices.
                unsigned value = 255;
                if (alphaDepth) {
                    const auto bit = p*alphaDepth;
                    value = (alpha[bit/8] >> (bit%8)) & ((1u << alphaDepth)-1);
                    value = value*255/((1u << alphaDepth)-1);
                }
                dest[3] = static_cast<uint8_t>(value);
            }
        }
        candidate.mips.push_back(std::move(mip));
    }
    output = std::move(candidate);
    return TextureError::None;
}
}
