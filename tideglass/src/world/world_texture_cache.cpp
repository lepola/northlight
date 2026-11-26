#include "world/world_texture_cache.h"
#include <windows.h>
#include <wincrypt.h>
#if __has_include("texture_recipe.h")
#include "texture_recipe.h"
#endif
#include <algorithm>
#include <new>

namespace water::world
{
std::string TextureRecipe()
{
#ifdef WATER_TEXTURE_RECIPE
    return WATER_TEXTURE_RECIPE;
#else
    return {};
#endif
}

std::string TextureCacheIdentity(const ArchiveReader& archives, const std::string& recipe)
{
    if (recipe.size()!=64 || !archives.SourceCount()) return {};
    struct Hash
    {
        HCRYPTPROV provider=0; HCRYPTHASH value=0;
        ~Hash() { if(value)CryptDestroyHash(value); if(provider)CryptReleaseContext(provider,0); }
        bool Bytes(const void* data, std::size_t size)
        { return CryptHashData(value,static_cast<const BYTE*>(data),DWORD(size),0)!=FALSE; }
        bool Number(uint64_t n)
        {
            BYTE bytes[8]; for(unsigned i=0;i<8;++i)bytes[i]=BYTE(n>>(i*8));
            return Bytes(bytes,8);
        }
    } hash;
    if (!CryptAcquireContextW(&hash.provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(hash.provider,CALG_SHA_256,0,0,&hash.value) ||
        !hash.Bytes(recipe.data(),recipe.size()) || !hash.Number(archives.SourceCount())) return {};
    for(unsigned i=0;i<archives.SourceCount();++i) {
        const auto& source=*archives.Source(i);
        // Length framing and ordinal order prevent concatenation/precedence aliasing.
        if(!hash.Number(source.path.size()) || !hash.Bytes(source.path.data(),source.path.size()*sizeof(wchar_t)) ||
           !hash.Number(source.size) || !hash.Number(source.modified) || !hash.Number(source.file) ||
           !hash.Number(source.volume)) return {};
    }
    BYTE bytes[32]; DWORD size=sizeof(bytes);
    if(!CryptGetHashParam(hash.value,HP_HASHVAL,bytes,&size,0) || size!=sizeof(bytes))return {};
    constexpr char digits[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto b:bytes){result+=digits[b>>4];result+=digits[b&15];}return result;
}

void TextureCache::Close()
{ m_archives=nullptr; m_pack=TexturePackReader{}; std::wstring{}.swap(m_root);std::string{}.swap(m_identity);m_bucket=TextureStore::Buckets; }

TexturePackError TextureCache::Open(const ArchiveReader& archives, const std::wstring& path,
                                   const std::atomic<bool>& cancelled, std::size_t workspaceLimit)
{
    // Unlike the low-level reader, a rejected replacement disables this cache;
    // previous identities can never remain discoverable through this facade.
    Close();
    if(cancelled.load(std::memory_order_relaxed))return TexturePackError::Cancelled;
    if(workspaceLimit<OpenReservation)return TexturePackError::Budget;
    try {
        const auto identity=TextureCacheIdentity(archives,TextureRecipe());
        if(identity.empty())return TexturePackError::Identity;
        const auto error=m_pack.Open(path,identity,cancelled);
        if(error==TexturePackError::None)m_archives=&archives;
        return error;
    } catch(const std::bad_alloc&) { return TexturePackError::Budget; }
}

TexturePackError TextureCache::OpenDirectory(const ArchiveReader& archives,const std::wstring& path,
                                            const std::atomic<bool>& cancelled,std::size_t workspaceLimit)
{
    Close();
    if(cancelled.load(std::memory_order_relaxed))return TexturePackError::Cancelled;
    if(workspaceLimit<TextureStore::IndexWorkspace)return TexturePackError::Budget;
    try {
        const auto error=TextureStore::Readable(path);if(error!=TexturePackError::None)return error;
        m_identity=TextureCacheIdentity(archives,TextureRecipe());
        if(m_identity.empty())return TexturePackError::Identity;
        m_root=path;m_archives=&archives;return TexturePackError::None;
    } catch(const std::bad_alloc&) {Close();return TexturePackError::Budget;}
}

TexturePackError TextureCache::Read(const TexturePackKey& key, const std::atomic<bool>& cancelled,
                                   std::size_t workspaceLimit, Texture& output, ArchiveIdentity& source,
                                   TexturePackReadStats& stats)
{
    if(cancelled.load(std::memory_order_relaxed))return TexturePackError::Cancelled;
    if(!m_archives)return TexturePackError::Missing;
    if(!m_root.empty() && m_bucket!=TextureStore::Bucket(key.name)) {
        m_pack=TexturePackReader(TextureStore::Limits());m_bucket=TextureStore::Buckets;
        if(workspaceLimit<TextureStore::IndexWorkspace)return TexturePackError::Budget;
        const auto error=m_pack.Open(TextureStore::Pack(m_root,TextureStore::Bucket(key.name)),m_identity,cancelled);
        stats.peakWorkspaceBytes=TextureStore::IndexWorkspace;
        if(error!=TexturePackError::None)return error;
        m_bucket=TextureStore::Bucket(key.name);
    }
    unsigned index=0;const auto* entry=m_pack.Find(key,index);
    if(!entry)return TexturePackError::Missing;
    const auto* origin=m_archives->Source(entry->sourceArchive);
    if(!origin)return TexturePackError::Identity;
    if(workspaceLimit<ReadReservation)return TexturePackError::Budget;
    try {
        ArchiveIdentity candidate=*origin;
        const auto indexReserve=m_root.empty()?0:TextureStore::IndexWorkspace;
        if(workspaceLimit<ReadReservation+indexReserve)return TexturePackError::Budget;
        const auto error=m_pack.Read(index,cancelled,workspaceLimit-ReadReservation-indexReserve,output,stats);
        stats.peakWorkspaceBytes+=ReadReservation+indexReserve;
        if(error==TexturePackError::None)source=std::move(candidate);
        return error;
    } catch(const std::bad_alloc&) { return TexturePackError::Budget; }
}
}
