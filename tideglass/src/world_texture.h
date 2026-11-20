#pragma once
#include "world_bytes.h"
#include <vector>

namespace water::world
{
enum class TextureFormat { Bgra8, Bc1, Bc2, Bc3 };
struct MipLayout
{
    unsigned rowBytes=0, rows=0, blockHeight=0;
    std::size_t Bytes() const { return std::size_t(rowBytes)*rows; }
};
// Decoded layout only. Each consumer applies its own file, staging and residency limits.
inline bool GetMipLayout(TextureFormat format,unsigned width,unsigned height,MipLayout& layout)
{
    if (!width || !height || width>16384 || height>16384) return false;
    switch (format) {
    case TextureFormat::Bgra8: layout={width*4,height,1}; return true;
    case TextureFormat::Bc1: case TextureFormat::Bc2: case TextureFormat::Bc3:
        layout={((width+3)/4)*(format==TextureFormat::Bc1?8u:16u),(height+3)/4,4};
        return true;
    }
    return false;
}
struct TextureMip
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> bytes;
};
struct Texture
{
    TextureFormat format = TextureFormat::Bgra8;
    std::vector<TextureMip> mips;
    // Nonzero records beyond the dimension-limited chain are not uploaded.
    unsigned ignoredMipEntries = 0;
    unsigned missingTailMips = 0; // explicit prefix policies only; no pixels are generated
    unsigned rejectedMipLevel = 0; // RenderPrefix discarded an invalid non-base record and its tail
    std::size_t Bytes() const;
};
enum class TextureError { None, Header, Dimensions, Format, Mips, Budget };
enum class TextureMips { Complete, StoredPrefix, RenderPrefix };

// CPU only. The payload limit applies to decoded pixel/block bytes. Bytes() also
// accounts vector capacity/metadata for the enclosing scene's residency budget.
// Failed decoding leaves output unchanged. Complete is the strict repair/audit
// policy. StoredPrefix permits only a contiguous validated chain from level 0;
// its GPU consumer must allocate exactly mips.size() levels, never an unfilled
// complete chain. Interior gaps/truncation still fail. RenderPrefix additionally
// stops before the first invalid non-base record and ignores the remaining tail.
// It never reads rejected spans, reduces quality to fit a budget, or repairs art.
// Explicit maxDimension selects authored lower mip levels for distant consumers;
// zero retains the full validated chain. Skipped levels are still span-validated.
// mipBias 1 selects the next authored level after maxDimension (half resolution);
// a one-level source keeps its only level. Values other than 0/1 are rejected.
TextureError DecodeBlp(Bytes input, std::size_t payloadLimit, Texture& output,
                       TextureMips policy = TextureMips::Complete,unsigned maxDimension=0,unsigned mipBias=0);
}
