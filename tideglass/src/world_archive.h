#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace water::world
{
struct ArchiveLimits
{
    std::size_t entryBytes = 32*1024*1024;
    std::size_t tableBytes = 32*1024*1024; // declared hash/block/high-block payload, not measured process RAM
    std::size_t pathBytes = 64*1024;
    unsigned archives = 64;
    unsigned cachedEntries = 4096;
    std::size_t nameBytes = 4*1024*1024; // library + tracking string payload; excludes allocator overhead
};
enum class ArchiveError { None, Descriptor, Header, Budget, Io, Missing, Cancelled, Changed, Unsupported };
struct ArchiveIdentity
{
    std::wstring path;
    uint64_t size = 0, modified = 0, file = 0;
    uint32_t volume = 0;
};
// Where archive time goes on the reader's thread (diagnostic counters).
struct ArchiveStats
{
    unsigned lookups=0,reads=0,fileChecks=0;
    double findMilliseconds=0,readMilliseconds=0,fileCheckMilliseconds=0;
};
struct ArchiveEntry
{
    std::string name; // canonical lower-case ASCII with backslashes
    ArchiveIdentity source;
    std::vector<uint8_t> bytes;
};

// CPU worker owner only; no device, game pointer, host scheduler or file writes.
// Paths are an explicit low-to-high precedence view. Unsupported archive headers
// fail before StormLib allocates their tables. Open/Read preserve old output on failure.
class ArchiveReader
{
public:
    ArchiveReader();
    ~ArchiveReader();
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;
    ArchiveError Open(const std::vector<std::wstring>& paths, const ArchiveLimits& limits);
    // UTF-8 low-to-high source paths, one per line (CRLF/BOM accepted), at most
    // 64 KiB. Captures the list identity as well as its archives. Worker only.
    ArchiveError OpenList(const std::wstring& path, const ArchiveLimits& limits, const std::atomic<bool>& cancelled);
    // Per-read payload admission composes with the caller's scene/workspace cap.
    ArchiveError Read(const std::string& name, const std::atomic<bool>& cancelled, ArchiveEntry& output,
                      std::size_t byteLimit = std::numeric_limits<std::size_t>::max());
    // Scene preparation validates the complete view before/after a transaction.
    // Within it, reads trust the identities captured at Open: probing each source
    // per read costs a file query per archive (0.26 ms each under Wine, ~7 s per
    // scene). Never publish this intermediate data without a final Validate().
    ArchiveError ReadSnapshot(const std::string& name,const std::atomic<bool>& cancelled,ArchiveEntry& output,
                              std::size_t byteLimit=std::numeric_limits<std::size_t>::max());
    // Whether the ordered view holds `name` (hash lookup only, no payload). Missing when absent;
    // completeView follows Read (true) or ReadSnapshot (false) validation.
    ArchiveError Contains(const std::string& name,const std::atomic<bool>& cancelled,bool completeView=true);
    ArchiveError Validate() const;
    void Close();
    std::size_t TableBytes() const { return m_tables; }
    unsigned OpenArchives() const;
    // Immutable metadata for this admitted ordered view; does not open archives.
    // Borrowed on the reader owner until Close/Open. Validate still owns freshness.
    const ArchiveIdentity* Source(unsigned ordinal) const;
    unsigned SourceCount() const;
    std::size_t NameBytes() const { return m_namesBytes; }
    const ArchiveStats& Stats() const { return m_stats; }
    static bool CanonicalName(const std::string& input, std::string& output);
private:
    struct Slot { ArchiveIdentity identity; void* handle = nullptr; std::unordered_set<std::string> names; };
    ArchiveError ReadEntry(const std::string&,const std::atomic<bool>&,ArchiveEntry&,std::size_t,bool);
    ArchiveError FindEntry(const std::string&,const std::atomic<bool>&,bool,std::size_t&,void*&);
    bool Owner() const;
    const std::thread::id m_owner;
    ArchiveLimits m_limits;
    std::vector<Slot> m_slots;
    std::size_t m_tables = 0;
    std::size_t m_namesBytes = 0;
    unsigned m_names = 0;
    ArchiveIdentity m_descriptor;
    mutable ArchiveStats m_stats;
    bool Check(const std::wstring& path,ArchiveIdentity& identity,uint8_t* header=nullptr) const;
};
}
