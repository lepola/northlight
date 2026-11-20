#pragma once
#include "world_archive.h"
#include "world_texture_store.h"

namespace water::world
{
// Generated from decoder/importer inputs and compiler/dependency identity. Empty
// when an ad-hoc build omitted the generated header: source import still works,
// but such a build cannot accept or produce persistent cache hits.
std::string TextureRecipe();
std::string TextureCacheIdentity(const ArchiveReader& archives, const std::string& recipe);

// Request-local optional CPU cache. No writes, visibility or rendering ownership.
// Caller validates the complete archive view before/after scene preparation.
class TextureCache
{
public:
    // Conservative index parsing reservation includes the encoded index, parsed
    // descriptors and duplicate-key validation. Fixed pack caps bound all three.
    static constexpr std::size_t OpenReservation = 24*1024*1024;
    // Inflate state/window and the candidate source path, beyond mip payloads.
    static constexpr std::size_t ReadReservation = 128*1024;
    TexturePackError Open(const ArchiveReader&, const std::wstring& path,
                          const std::atomic<bool>& cancelled, std::size_t workspaceLimit);
    TexturePackError OpenDirectory(const ArchiveReader&, const std::wstring& path,
                                   const std::atomic<bool>& cancelled, std::size_t workspaceLimit);
    TexturePackError Read(const TexturePackKey&, const std::atomic<bool>& cancelled,
                          std::size_t workspaceLimit, Texture&, ArchiveIdentity&, TexturePackReadStats&);
    std::size_t Bytes() const { return m_archives ? m_pack.Bytes()+sizeof(*this)+m_root.capacity()*sizeof(wchar_t)+m_identity.capacity() : 0; }
    void Close();
private:
    const ArchiveReader* m_archives = nullptr;
    TexturePackReader m_pack;
    std::wstring m_root;
    std::string m_identity;
    unsigned m_bucket=TextureStore::Buckets;
};
}
