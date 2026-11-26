#pragma once
#include "world/world_texture.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace water::world
{
// Immutable CPU asset packs. No game/device state, source importer or scheduling.
// The owner supplies a 64-character hex identity covering its ordered source view
// and decoder recipe. A cache hit is usable only while that source view is valid.
enum class TexturePackError { None, Missing, Identity, Format, Corrupt, Budget, Cancelled, Io };
struct TexturePackKey
{
    std::string name; // canonical archive name
    unsigned maxDimension = 0;
    unsigned mipBias = 0; // 0/full or 1/half, after the distance detail selection
    TextureMips policy = TextureMips::RenderPrefix;
    bool operator==(const TexturePackKey& other) const;
};
struct TexturePackEntry
{
    TexturePackKey key;
    unsigned sourceArchive = 0; // ordinal in the identity's ordered archive view
    uint32_t fingerprint = 0;
    TextureFormat format = TextureFormat::Bgra8;
    unsigned ignoredMipEntries = 0, missingTailMips = 0, rejectedMipLevel = 0;
    struct Mip { uint32_t width = 0, height = 0, chunk = 0; };
    std::vector<Mip> mips;
};
struct TexturePackLimits
{
    std::size_t fileBytes = 256*1024*1024;
    std::size_t indexBytes = 4*1024*1024;
    std::size_t mipBytes = 32*1024*1024;
    std::size_t builderPayloadBytes = 128*1024*1024;
    unsigned textures = 4096, chunks = 65536;
};
struct TexturePackReadStats
{
    std::size_t inputBytes = 0;
    // Candidate Texture::Bytes() plus compressed scratch; excludes index,
    // allocator/zlib overhead and any previous output (owned/accounted by caller).
    std::size_t peakWorkspaceBytes = 0;
};
struct TexturePackWriteStats
{
    std::size_t fileBytes = 0, indexBytes = 0, uniqueMipBytes = 0, logicalMipBytes = 0;
    unsigned textures = 0, chunks = 0;
};
uint32_t TextureFingerprint(const Texture& texture);

class TexturePackReader
{
public:
    explicit TexturePackReader(TexturePackLimits limits = {}) : m_limits(limits) {}
    // Failed Open preserves the previous reader. Caller must honour the result;
    // preservation is not permission to use a previous source view for a new one.
    TexturePackError Open(const std::filesystem::path& path, const std::string& identity,
                          const std::atomic<bool>& cancelled);
    std::size_t Bytes() const;
    const std::vector<TexturePackEntry>& Entries() const { return m_entries; }
    const TexturePackEntry* Find(const TexturePackKey& key, unsigned& index) const;
    // Transactional output. No whole-pack read or retained decoded chunk cache.
    TexturePackError Read(unsigned index, const std::atomic<bool>& cancelled,
                          std::size_t workspaceLimit, Texture& output, TexturePackReadStats& stats);
private:
    struct Chunk { uint32_t offset = 0, raw = 0, stored = 0, crc = 0, codec = 0; };
    TexturePackLimits m_limits;
    std::ifstream m_file;
    std::vector<TexturePackEntry> m_entries;
    std::vector<Chunk> m_chunks;
};

// Offline/batch producer for this first implementation. Payload admission is
// bounded; compression streams one mip at a time. Background scheduling and
// incremental population are separate work, not implicit in Add/Write.
class TexturePackBuilder
{
public:
    explicit TexturePackBuilder(TexturePackLimits limits = {}) : m_limits(limits) {}
    TexturePackError Add(const TexturePackKey& key, unsigned sourceArchive,
                         const Texture& texture, unsigned& index);
    // Creates an immutable new pack; never replaces an existing destination.
    // Incomplete writes are not published at the destination. Failed writes remove
    // only their own temporary file, preserving previously published packs.
    TexturePackError Write(const std::filesystem::path& path, const std::string& identity,
                           int compressionLevel, const std::atomic<bool>& cancelled,
                           TexturePackWriteStats& stats) const;
    const std::vector<TexturePackEntry>& Entries() const { return m_entries; }
    std::size_t UniqueMipBytes() const { return m_uniqueBytes; }
    std::size_t LogicalMipBytes() const { return m_logicalBytes; }
private:
    TexturePackLimits m_limits;
    std::vector<TexturePackEntry> m_entries;
    std::vector<std::vector<uint8_t>> m_chunks;
    std::map<uint32_t, std::vector<unsigned>> m_candidates;
    std::size_t m_uniqueBytes = 0, m_logicalBytes = 0, m_entryBytes = 4;
};
}
