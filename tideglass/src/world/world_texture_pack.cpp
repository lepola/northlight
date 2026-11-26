#include "world/world_texture_pack.h"
#include "world/world_asset_name.h"
#include <windows.h>
#include <zlib/zlib.h>
#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <tuple>

namespace water::world
{
namespace
{
using Blob = std::vector<uint8_t>;
constexpr uint32_t Magic = 0x324b4254, Version = 2, HeaderBytes = 88;
struct Failure { TexturePackError error; };
void Need(bool okay, TexturePackError error) { if (!okay) throw Failure{error}; }
void CheckCancelled(const std::atomic<bool>& cancelled)
{ Need(!cancelled.load(std::memory_order_relaxed), TexturePackError::Cancelled); }

template<class F> TexturePackError Attempt(F&& operation)
{
    try { operation(); return TexturePackError::None; }
    catch (const Failure& failure) { return failure.error; }
    catch (const std::bad_alloc&) { return TexturePackError::Budget; }
    catch (const std::filesystem::filesystem_error&) { return TexturePackError::Io; }
    catch (const std::ios_base::failure&) { return TexturePackError::Io; }
}
uint32_t Crc(const Blob& bytes)
{ return crc32(0, bytes.data(), static_cast<uInt>(bytes.size())); }
void Put(Blob& bytes, uint32_t value)
{ for (unsigned i=0; i<4; ++i) bytes.push_back(uint8_t(value>>(i*8))); }
void PutString(Blob& bytes, const std::string& text)
{ Put(bytes, uint32_t(text.size())); bytes.insert(bytes.end(), text.begin(), text.end()); }
struct Cursor
{
    const Blob& bytes;
    std::size_t at = 0;
    uint32_t Get()
    {
        Need(at<=bytes.size() && bytes.size()-at>=4, TexturePackError::Format);
        uint32_t value=0;
        for (unsigned i=0; i<4; ++i) value|=uint32_t(bytes[at++])<<(8*i);
        return value;
    }
    std::string Text()
    {
        const auto size=Get();
        Need(size<=1024 && size<=bytes.size()-at, TexturePackError::Format);
        std::string result(bytes.begin()+at, bytes.begin()+at+size); at+=size;
        return result;
    }
};
bool Identity(const std::string& identity)
{
    return identity.size()==64 && std::all_of(identity.begin(), identity.end(), [](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
bool Key(const TexturePackKey& key)
{
    if (!ValidAssetName(key.name) || key.maxDimension>16384 || key.mipBias>1 ||
        (key.policy!=TextureMips::Complete && key.policy!=TextureMips::StoredPrefix &&
         key.policy!=TextureMips::RenderPrefix)) return false;
    return std::none_of(key.name.begin(), key.name.end(), [](char c) { return c=='/' || (c>='A' && c<='Z'); });
}
bool Limits(const TexturePackLimits& limits)
{
    return limits.fileBytes>=HeaderBytes && limits.fileBytes<=256*1024*1024 &&
           limits.indexBytes<=4*1024*1024 && limits.mipBytes<=32*1024*1024 &&
           limits.builderPayloadBytes<=128*1024*1024 && limits.textures<=4096 && limits.chunks<=65536;
}
std::size_t MipBytes(TextureFormat format, uint32_t width, uint32_t height)
{
    Need(width && height && width<=16384 && height<=16384 &&
         !(width&(width-1)) && !(height&(height-1)), TexturePackError::Format);
    MipLayout layout;
    Need(GetMipLayout(format,width,height,layout),TexturePackError::Format);
    Need(layout.Bytes()<=32*1024*1024,TexturePackError::Budget);
    return layout.Bytes();
}
void Metadata(const TexturePackEntry& entry)
{
    Need(Key(entry.key) && entry.sourceArchive<64 && !entry.mips.empty() && entry.mips.size()<=16 &&
         entry.ignoredMipEntries<=16 && entry.missingTailMips<=16 && entry.rejectedMipLevel<=16,
         TexturePackError::Format);
    for (unsigned i=0; i<entry.mips.size(); ++i) {
        const auto& mip=entry.mips[i]; MipBytes(entry.format, mip.width, mip.height);
        if (i) Need(mip.width==std::max(1u, entry.mips[i-1].width/2) &&
                    mip.height==std::max(1u, entry.mips[i-1].height/2) &&
                    (entry.mips[i-1].width>1 || entry.mips[i-1].height>1), TexturePackError::Format);
    }
}
void ReadBytes(std::ifstream& file, Blob& bytes)
{
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Need(bool(file), TexturePackError::Io);
}
Blob ReadBytes(std::ifstream& file, std::size_t count)
{ Blob bytes(count); ReadBytes(file, bytes); return bytes; }
void Inflate(const Blob& stored, Blob& raw)
{
    z_stream stream{};
    stream.next_in=const_cast<Bytef*>(stored.data()); stream.avail_in=uInt(stored.size());
    stream.next_out=raw.data(); stream.avail_out=uInt(raw.size());
    Need(inflateInit(&stream)==Z_OK, TexturePackError::Budget);
    const auto code=inflate(&stream, Z_FINISH);
    const bool complete=code==Z_STREAM_END && stream.total_in==stored.size() && stream.total_out==raw.size();
    inflateEnd(&stream);
    Need(complete, TexturePackError::Corrupt);
}
// A unique temporary path is claimed with CREATE_NEW. An interrupted producer
// cannot overwrite another producer's partial file or any completed destination.
class PendingFile
{
public:
    std::filesystem::path path;
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit PendingFile(const std::filesystem::path& destination) : path(destination.wstring()+L".partial")
    {
        handle=CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Need(handle!=INVALID_HANDLE_VALUE, TexturePackError::Io);
    }
    ~PendingFile()
    {
        if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!path.empty()) DeleteFileW(path.c_str());
    }
    void Write(const Blob& bytes)
    {
        DWORD written=0;
        Need(WriteFile(handle, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written==bytes.size(), TexturePackError::Io);
    }
    void Publish(const std::filesystem::path& destination, const Blob& header)
    {
        LARGE_INTEGER zero{};
        Need(SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN), TexturePackError::Io);
        Write(header);
        Need(FlushFileBuffers(handle), TexturePackError::Io);
        const auto saved=handle; handle=INVALID_HANDLE_VALUE;
        Need(CloseHandle(saved), TexturePackError::Io);
        Need(MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH), TexturePackError::Io);
        path.clear();
    }
};
}

bool TexturePackKey::operator==(const TexturePackKey& other) const
{ return name==other.name && maxDimension==other.maxDimension && mipBias==other.mipBias && policy==other.policy; }

uint32_t TextureFingerprint(const Texture& texture)
{
    // Fixed-size metadata needs no allocation; fingerprint is diagnostic, while
    // pack CRCs and exact byte comparison govern integrity/deduplication.
    std::array<uint32_t,5> header={uint32_t(texture.format),texture.ignoredMipEntries,
        texture.missingTailMips,texture.rejectedMipLevel,uint32_t(texture.mips.size())};
    uLong crc=0;
    const auto word=[&](uint32_t value) {
        const uint8_t bytes[]={uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};
        crc=crc32(crc, bytes, 4);
    };
    for (auto value:header) word(value);
    for (const auto& mip:texture.mips) {
        word(mip.width); word(mip.height);
        crc=crc32(crc, mip.bytes.data(), static_cast<uInt>(mip.bytes.size()));
    }
    return uint32_t(crc);
}

TexturePackError TexturePackReader::Open(const std::filesystem::path& path, const std::string& identity,
                                         const std::atomic<bool>& cancelled)
{
    return Attempt([&] {
        CheckCancelled(cancelled);
        Need(Limits(m_limits) && Identity(identity), TexturePackError::Format);
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            const auto attributes=GetFileAttributesW(path.c_str());
            Need(attributes!=INVALID_FILE_ATTRIBUTES || (GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND), TexturePackError::Missing);
            throw Failure{TexturePackError::Io};
        }
        file.seekg(0, std::ios::end); const auto size=file.tellg();
        Need(size>=HeaderBytes, TexturePackError::Format);
        Need(uint64_t(size)<=m_limits.fileBytes, TexturePackError::Budget); file.seekg(0);
        const auto header=ReadBytes(file, HeaderBytes); Cursor h{header};
        Need(h.Get()==Magic && h.Get()==Version, TexturePackError::Format);
        Need(std::string(header.begin()+8,header.begin()+72)==identity, TexturePackError::Identity); h.at=72;
        const auto indexAt=h.Get(), indexSize=h.Get(), crc=h.Get(), declaredSize=h.Get();
        Need(indexAt>=HeaderBytes && uint64_t(indexAt)+indexSize==uint64_t(size) && declaredSize==uint64_t(size), TexturePackError::Format);
        Need(indexSize<=m_limits.indexBytes, TexturePackError::Budget);
        file.seekg(indexAt); const auto index=ReadBytes(file, indexSize);
        Need(Crc(index)==crc, TexturePackError::Corrupt); Cursor r{index};
        const auto count=r.Get(); Need(count<=m_limits.textures, TexturePackError::Budget);
        std::vector<TexturePackEntry> entries; entries.reserve(count);
        std::set<std::tuple<std::string,unsigned,unsigned,TextureMips>> keys;
        for (unsigned i=0; i<count; ++i) {
            CheckCancelled(cancelled);
            TexturePackEntry entry; entry.key.name=r.Text(); entry.key.maxDimension=r.Get();
            entry.key.mipBias=r.Get(); entry.key.policy=TextureMips(r.Get()); entry.sourceArchive=r.Get();
            entry.format=TextureFormat(r.Get()); entry.ignoredMipEntries=r.Get(); entry.missingTailMips=r.Get();
            entry.rejectedMipLevel=r.Get(); entry.fingerprint=r.Get();
            const auto levels=r.Get(); Need(levels<=16, TexturePackError::Format); entry.mips.reserve(levels);
            for (unsigned level=0; level<levels; ++level) entry.mips.push_back({r.Get(),r.Get(),r.Get()});
            Metadata(entry);
            Need(keys.emplace(entry.key.name,entry.key.maxDimension,entry.key.mipBias,entry.key.policy).second, TexturePackError::Format);
            entries.push_back(std::move(entry));
        }
        const auto countChunks=r.Get(); Need(countChunks<=m_limits.chunks, TexturePackError::Budget);
        std::vector<Chunk> chunks; chunks.reserve(countChunks); uint64_t end=HeaderBytes;
        for (unsigned i=0; i<countChunks; ++i) {
            Chunk chunk{r.Get(),r.Get(),r.Get(),r.Get(),r.Get()};
            Need(chunk.offset==end && chunk.raw && chunk.stored && chunk.stored<=chunk.raw &&
                 chunk.codec<=1 && (chunk.codec || chunk.stored==chunk.raw), TexturePackError::Format);
            Need(chunk.raw<=m_limits.mipBytes, TexturePackError::Budget);
            end+=chunk.stored; Need(end<=indexAt, TexturePackError::Format); chunks.push_back(chunk);
        }
        Need(r.at==index.size() && end==indexAt, TexturePackError::Format);
        for (const auto& entry:entries) for (const auto& mip:entry.mips) {
            Need(mip.chunk<chunks.size(), TexturePackError::Format);
            Need(MipBytes(entry.format,mip.width,mip.height)==chunks[mip.chunk].raw, TexturePackError::Format);
        }
        CheckCancelled(cancelled);
        m_file=std::move(file); m_entries=std::move(entries); m_chunks=std::move(chunks);
    });
}

std::size_t TexturePackReader::Bytes() const
{
    std::size_t bytes=sizeof(*this)+m_entries.capacity()*sizeof(TexturePackEntry)+m_chunks.capacity()*sizeof(Chunk);
    for (const auto& entry:m_entries) bytes+=entry.key.name.capacity()+1+entry.mips.capacity()*sizeof(TexturePackEntry::Mip);
    return bytes;
}

const TexturePackEntry* TexturePackReader::Find(const TexturePackKey& key, unsigned& index) const
{
    for (unsigned i=0; i<m_entries.size(); ++i) if (m_entries[i].key==key) { index=i; return &m_entries[i]; }
    return nullptr;
}

TexturePackError TexturePackReader::Read(unsigned index, const std::atomic<bool>& cancelled,
                                         std::size_t workspaceLimit, Texture& output, TexturePackReadStats& stats)
{
    return Attempt([&] {
        CheckCancelled(cancelled); Need(index<m_entries.size(), TexturePackError::Missing);
        const auto& entry=m_entries[index];
        Texture candidate; candidate.format=entry.format; candidate.ignoredMipEntries=entry.ignoredMipEntries;
        candidate.missingTailMips=entry.missingTailMips; candidate.rejectedMipLevel=entry.rejectedMipLevel;
        const auto metadata=sizeof(Texture)+entry.mips.size()*sizeof(TextureMip);
        Need(metadata<=workspaceLimit, TexturePackError::Budget); candidate.mips.reserve(entry.mips.size());
        for (const auto& mip:entry.mips) {
            CheckCancelled(cancelled); const auto& chunk=m_chunks[mip.chunk];
            const auto retained=candidate.Bytes();
            const std::size_t scratch=chunk.codec ? chunk.stored : 0;
            Need(retained<=workspaceLimit && chunk.raw<=workspaceLimit-retained &&
                 scratch<=workspaceLimit-retained-chunk.raw, TexturePackError::Budget);
            TextureMip decoded{mip.width,mip.height,{}}; decoded.bytes.resize(chunk.raw);
            m_file.clear(); m_file.seekg(chunk.offset);
            if (chunk.codec) {
                const auto stored=ReadBytes(m_file, chunk.stored); Inflate(stored,decoded.bytes);
            } else ReadBytes(m_file,decoded.bytes);
            stats.inputBytes+=chunk.stored;
            stats.peakWorkspaceBytes=std::max(stats.peakWorkspaceBytes, retained+decoded.bytes.capacity()+scratch);
            Need(Crc(decoded.bytes)==chunk.crc, TexturePackError::Corrupt);
            candidate.mips.push_back(std::move(decoded));
        }
        Need(TextureFingerprint(candidate)==entry.fingerprint, TexturePackError::Corrupt);
        CheckCancelled(cancelled); output=std::move(candidate);
    });
}

TexturePackError TexturePackBuilder::Add(const TexturePackKey& key, unsigned sourceArchive,
                                        const Texture& texture, unsigned& index)
{
    const auto beforeChunks=m_chunks.size(), beforeEntries=m_entries.size();
    const auto beforeUnique=m_uniqueBytes, beforeLogical=m_logicalBytes, beforeIndex=m_entryBytes;
    const auto result=Attempt([&] {
        Need(Limits(m_limits), TexturePackError::Format);
        TexturePackEntry entry; entry.key=key; entry.sourceArchive=sourceArchive;
        entry.format=texture.format; entry.ignoredMipEntries=texture.ignoredMipEntries;
        entry.missingTailMips=texture.missingTailMips; entry.rejectedMipLevel=texture.rejectedMipLevel;
        entry.fingerprint=TextureFingerprint(texture);
        Need(texture.mips.size()<=16, TexturePackError::Format);
        entry.mips.reserve(texture.mips.size());
        for (const auto& mip:texture.mips) {
            Need(MipBytes(texture.format,mip.width,mip.height)==mip.bytes.size(), TexturePackError::Format);
            Need(mip.bytes.size()<=m_limits.mipBytes, TexturePackError::Budget);
            entry.mips.push_back({mip.width,mip.height,0});
        }
        Metadata(entry);
        for (unsigned i=0; i<m_entries.size(); ++i) if (m_entries[i].key==key) {
            const auto& previous=m_entries[i];
            Need(previous.sourceArchive==sourceArchive && previous.fingerprint==entry.fingerprint &&
                 previous.format==entry.format && previous.ignoredMipEntries==entry.ignoredMipEntries &&
                 previous.missingTailMips==entry.missingTailMips && previous.rejectedMipLevel==entry.rejectedMipLevel &&
                 previous.mips.size()==entry.mips.size(), TexturePackError::Identity);
            for (unsigned m=0; m<entry.mips.size(); ++m)
                Need(previous.mips[m].width==entry.mips[m].width && previous.mips[m].height==entry.mips[m].height &&
                     m_chunks[previous.mips[m].chunk]==texture.mips[m].bytes, TexturePackError::Identity);
            index=i; return;
        }
        Need(m_entries.size()<m_limits.textures, TexturePackError::Budget);
        const auto entryBytes=4+key.name.size()+10*4+entry.mips.size()*12;
        Need(entryBytes<=m_limits.indexBytes && m_entryBytes<=m_limits.indexBytes-entryBytes, TexturePackError::Budget);
        for (unsigned i=0; i<texture.mips.size(); ++i) {
            const auto& bytes=texture.mips[i].bytes; const auto crc=Crc(bytes);
            auto& matches=m_candidates[crc]; unsigned chunk=unsigned(m_chunks.size());
            for (auto candidate:matches) if (m_chunks[candidate]==bytes) { chunk=candidate; break; }
            if (chunk==m_chunks.size()) {
                Need(m_chunks.size()<m_limits.chunks && bytes.size()<=m_limits.builderPayloadBytes &&
                     m_uniqueBytes<=m_limits.builderPayloadBytes-bytes.size(), TexturePackError::Budget);
                m_chunks.push_back(bytes); matches.push_back(chunk); m_uniqueBytes+=bytes.size();
            }
            Need(bytes.size()<=std::numeric_limits<std::size_t>::max()-m_logicalBytes, TexturePackError::Budget);
            entry.mips[i].chunk=chunk; m_logicalBytes+=bytes.size();
        }
        Need(m_entryBytes+entryBytes+4+m_chunks.size()*20<=m_limits.indexBytes, TexturePackError::Budget);
        m_entries.push_back(std::move(entry)); m_entryBytes+=entryBytes; index=unsigned(m_entries.size()-1);
    });
    if (result!=TexturePackError::None) {
        m_chunks.resize(beforeChunks); m_entries.resize(beforeEntries);
        m_uniqueBytes=beforeUnique; m_logicalBytes=beforeLogical; m_entryBytes=beforeIndex;
        for (auto it=m_candidates.begin(); it!=m_candidates.end();) {
            auto& ids=it->second;
            ids.erase(std::remove_if(ids.begin(),ids.end(),[&](unsigned id){return id>=beforeChunks;}),ids.end());
            if (ids.empty()) it=m_candidates.erase(it); else ++it;
        }
    }
    return result;
}

TexturePackError TexturePackBuilder::Write(const std::filesystem::path& path, const std::string& identity,
                                           int compressionLevel, const std::atomic<bool>& cancelled,
                                           TexturePackWriteStats& stats) const
{
    return Attempt([&] {
        CheckCancelled(cancelled);
        Need(Limits(m_limits) && Identity(identity) && (compressionLevel==0 || compressionLevel==1 || compressionLevel==6), TexturePackError::Format);
        Need(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES &&
             (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND), TexturePackError::Io);
        PendingFile file(path); file.Write(Blob(HeaderBytes,0));
        struct Chunk { uint32_t offset,raw,stored,crc,codec; };
        std::vector<Chunk> chunks; chunks.reserve(m_chunks.size()); std::size_t offset=HeaderBytes;
        for (const auto& raw:m_chunks) {
            CheckCancelled(cancelled); Blob compressed; unsigned codec=0;
            if (compressionLevel) {
                compressed.resize(compressBound(uLong(raw.size()))); uLongf size=uLongf(compressed.size());
                Need(compress2(compressed.data(),&size,raw.data(),uLong(raw.size()),compressionLevel)==Z_OK, TexturePackError::Budget);
                compressed.resize(size); if (compressed.size()<raw.size()) codec=1;
            }
            const auto& stored=codec ? compressed : raw;
            Need(stored.size()<=m_limits.fileBytes && offset<=m_limits.fileBytes-stored.size(), TexturePackError::Budget);
            chunks.push_back({uint32_t(offset),uint32_t(raw.size()),uint32_t(stored.size()),Crc(raw),codec});
            file.Write(stored); offset+=stored.size();
        }
        Blob index; index.reserve(m_entryBytes+4+chunks.size()*20); Put(index,uint32_t(m_entries.size()));
        for (const auto& entry:m_entries) {
            PutString(index,entry.key.name); Put(index,entry.key.maxDimension); Put(index,entry.key.mipBias); Put(index,uint32_t(entry.key.policy)); Put(index,entry.sourceArchive);
            Put(index,uint32_t(entry.format)); Put(index,entry.ignoredMipEntries); Put(index,entry.missingTailMips); Put(index,entry.rejectedMipLevel); Put(index,entry.fingerprint); Put(index,uint32_t(entry.mips.size()));
            for (const auto& mip:entry.mips) { Put(index,mip.width); Put(index,mip.height); Put(index,mip.chunk); }
        }
        Put(index,uint32_t(chunks.size()));
        for (const auto& chunk:chunks) { Put(index,chunk.offset); Put(index,chunk.raw); Put(index,chunk.stored); Put(index,chunk.crc); Put(index,chunk.codec); }
        Need(index.size()<=m_limits.indexBytes && index.size()<=m_limits.fileBytes && offset<=m_limits.fileBytes-index.size(), TexturePackError::Budget);
        file.Write(index);
        Blob header; Put(header,Magic); Put(header,Version); header.insert(header.end(),identity.begin(),identity.end());
        Put(header,uint32_t(offset)); Put(header,uint32_t(index.size())); Put(header,Crc(index)); Put(header,uint32_t(offset+index.size()));
        CheckCancelled(cancelled); file.Publish(path,header);
        stats={offset+index.size(),HeaderBytes+index.size(),m_uniqueBytes,m_logicalBytes,unsigned(m_entries.size()),unsigned(m_chunks.size())};
    });
}
}
