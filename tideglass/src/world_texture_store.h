#pragma once
#include "world_texture_pack.h"

namespace water::world
{
// Fixed name buckets share assets across regions and keep all quality/detail
// variants of an asset together. Pack-local mip deduplication then shares tails.
// No scene visibility, source import, thread or retained asset ownership here.
class TextureStore
{
public:
    static constexpr unsigned Buckets=64;
    static constexpr std::size_t Workspace=16*1024*1024;
    static constexpr std::size_t IndexWorkspace=2*1024*1024;
    static constexpr uint64_t MinFreeBytes=20ull*1024*1024*1024;
    struct Asset { TexturePackKey key; unsigned source=0; const Texture* texture=nullptr; };
    struct Result {
        TexturePackError error=TexturePackError::None;
        unsigned written=0,reused=0,skipped=0,evicted=0;
        uint64_t diskBytes=0,writtenBytes=0;
    };
    static unsigned Bucket(const std::string& name);
    static TexturePackLimits Limits();
    static std::filesystem::path Pack(const std::filesystem::path& root,unsigned bucket);
    static TexturePackError Readable(const std::filesystem::path& root);
    // Worker only. An empty asset list updates usage/limits without importing or
    // rewriting texture payload. Nonblocking exclusive writer claim; readers
    // tolerate a missing/replaced pack and continue through source fallback.
    static Result Publish(const std::filesystem::path& root,const std::string& identity,
                          unsigned bucket,const std::vector<Asset>& assets,uint64_t diskLimit,
                          const std::atomic<bool>& cancelled);
};
}
