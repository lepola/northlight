#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace water::world
{
// Non-owning file bytes. All offsets/counts are checked before pointer arithmetic.
// Accesses archive asset bytes; never points into game memory.
struct Bytes
{
    const uint8_t* data = nullptr;
    std::size_t size = 0;
    bool Contains(std::size_t offset, std::size_t count) const
    { return data && offset <= size && count <= size-offset; }
    bool U16(std::size_t offset, uint16_t& value) const
    {
        if (!Contains(offset, 2)) return false;
        value = uint16_t(data[offset]) | (uint16_t(data[offset+1]) << 8);
        return true;
    }
    bool F32(std::size_t offset, float& value) const
    {
        static_assert(sizeof(float) == 4, "asset floats require IEEE binary32");
        uint32_t bits = 0;
        if (!U32(offset, bits)) return false;
        std::memcpy(&value, &bits, 4);
        return true;
    }
    bool U32(std::size_t offset, uint32_t& value) const
    {
        if (!Contains(offset, 4)) return false;
        value = uint32_t(data[offset]) | (uint32_t(data[offset+1]) << 8) |
                (uint32_t(data[offset+2]) << 16) | (uint32_t(data[offset+3]) << 24);
        return true;
    }
};
}
