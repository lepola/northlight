#pragma once
#include <array>
#include <cstdint>
#include <limits>

// Is the running game the build these addresses were read from? Checks the loaded image (base address, 32-bit PE,
// link timestamp) through the caller's memory reader, before any fixed address is trusted.
namespace water::client12340
{
template<class Read> bool SupportedImage(Read read, uintptr_t base, uint32_t expectedTimestamp,
                                        uint32_t expectedBase = 0x00400000)
{
    if (base != expectedBase) return false;
    std::array<unsigned char, 64> dos;
    if (!read(base, dos.data(), dos.size()) || dos[0] != 'M' || dos[1] != 'Z') return false;
    auto word = [](const auto& bytes, unsigned at) {
        return uint32_t(bytes[at]) | (uint32_t(bytes[at + 1]) << 8);
    };
    auto dword = [&](const auto& bytes, unsigned at) { return word(bytes, at) | (word(bytes, at + 2) << 16); };
    const uint32_t offset = dword(dos, 60);
    if (offset < dos.size() || offset > 1024 * 1024 || offset > std::numeric_limits<uintptr_t>::max() - base) return false;
    std::array<unsigned char, 56> pe;
    if (!read(base + offset, pe.data(), pe.size())) return false;
    return dword(pe, 0) == 0x00004550 && word(pe, 4) == 0x014c && dword(pe, 8) == expectedTimestamp &&
           word(pe, 20) >= 32 && word(pe, 24) == 0x010b && dword(pe, 52) == expectedBase;
}
}
