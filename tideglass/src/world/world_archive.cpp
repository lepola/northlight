#define UNICODE
#define _UNICODE
#ifndef __STORMLIB_NO_STATIC_LINK__
#define __STORMLIB_NO_STATIC_LINK__
#endif
#include <StormLib.h>
#include "world/world_archive.h"
#include "base/file_reference.h"
#include "world/world_bytes.h"
#include "world/world_asset_name.h"
#include <algorithm>
#include <chrono>
#include <cassert>
#include <utility>

namespace water::world
{
namespace
{
struct File
{
    HANDLE value = INVALID_HANDLE_VALUE;
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct EntryFile
{
    HANDLE value = nullptr;
    ~EntryFile() { if (value) SFileCloseFile(value); }
};
bool FileIdentity(HANDLE file, const std::wstring& path, ArchiveIdentity& identity)
{
    BY_HANDLE_FILE_INFORMATION info{};
    if (file == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    identity.path = path;
    identity.size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    identity.modified = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    identity.file = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    identity.volume = info.dwVolumeSerialNumber;
    return true;
}
bool Identity(const std::wstring& path, ArchiveIdentity& identity, uint8_t* header = nullptr)
{
    File file;
    file.value = CreateFileW(path.c_str(), header ? GENERIC_READ : FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (!FileIdentity(file.value, path, identity)) return false;
    DWORD count = 0;
    return !header || (ReadFile(file.value, header, 44, &count, nullptr) && count == 44);
}
bool Same(const ArchiveIdentity& a, const ArchiveIdentity& b)
{ return a.size == b.size && a.modified == b.modified && a.file == b.file && a.volume == b.volume; }
}

ArchiveReader::ArchiveReader() : m_owner(std::this_thread::get_id()) {}
bool ArchiveReader::Check(const std::wstring& path,ArchiveIdentity& identity,uint8_t* header) const
{
    const auto start=std::chrono::steady_clock::now();
    const bool ok=Identity(path,identity,header);
    ++m_stats.fileChecks;
    m_stats.fileCheckMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return ok;
}
ArchiveReader::~ArchiveReader() { Close(); }
bool ArchiveReader::Owner() const { return m_owner == std::this_thread::get_id(); }
void ArchiveReader::Close()
{
    assert(Owner());
    for (auto& slot : m_slots) if (slot.handle) SFileCloseArchive(slot.handle);
    m_slots.clear(); m_tables = m_namesBytes = 0; m_names = 0;
    m_descriptor = {};
}
unsigned ArchiveReader::OpenArchives() const
{
    assert(Owner());
    unsigned count = 0;
    for (const auto& slot : m_slots) if (slot.handle) ++count;
    return count;
}
bool ArchiveReader::CanonicalName(const std::string& input, std::string& output)
{
    return CanonicalAssetName(input, output);
}
ArchiveError ArchiveReader::OpenList(const std::wstring& path, const ArchiveLimits& limits,
                                   const std::atomic<bool>& cancelled)
{
    assert(Owner());
    if (cancelled.load()) return ArchiveError::Cancelled;
    if (path.size() < 3 || path.size() >= 32768 || path.find(L'\0') != std::wstring::npos ||
        path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
        return ArchiveError::Descriptor;
    File file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, 0, nullptr);
    ArchiveIdentity descriptor;
    if (!FileIdentity(file.value, path, descriptor)) return ArchiveError::Io;
    if (!descriptor.size || descriptor.size > 64*1024) return ArchiveError::Budget;
    std::vector<char> bytes(static_cast<std::size_t>(descriptor.size)); DWORD count = 0;
    if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || count != bytes.size())
        return ArchiveError::Io;
    if (cancelled.load()) return ArchiveError::Cancelled;
    std::vector<std::wstring> paths;
    std::size_t start = bytes.size() >= 3 && static_cast<uint8_t>(bytes[0]) == 0xef &&
        static_cast<uint8_t>(bytes[1]) == 0xbb && static_cast<uint8_t>(bytes[2]) == 0xbf ? 3 : 0;
    std::size_t pathBytes = 0;
    for (std::size_t at = start; at <= bytes.size(); ++at) if (at == bytes.size() || bytes[at] == '\n') {
        std::size_t end = at;
        if (end > start && bytes[end-1] == '\r') --end;
        if (end > start) {
            if (paths.size() >= limits.archives) return ArchiveError::Budget;
            const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data()+start,
                                              static_cast<int>(end-start), nullptr, 0);
            if (n <= 0 || n >= 32768) return ArchiveError::Descriptor;
            const auto needed = (std::size_t(n)+1)*sizeof(wchar_t);
            if (pathBytes > limits.pathBytes || needed > limits.pathBytes-pathBytes) return ArchiveError::Budget;
            pathBytes += needed;
            std::wstring converted(n, 0);
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data()+start,
                                    static_cast<int>(end-start), converted.data(), n)) return ArchiveError::Descriptor;
            if (!ResolveFileReference(path, converted)) return ArchiveError::Descriptor;
            paths.push_back(std::move(converted));
        }
        start = at+1;
    }
    ArchiveReader candidate;
    auto error = candidate.Open(paths, limits);
    if (error != ArchiveError::None) return error;
    if (cancelled.load()) return ArchiveError::Cancelled;
    ArchiveIdentity now;
    if (!Check(path, now) || !Same(now, descriptor)) return ArchiveError::Changed;
    Close();
    m_limits = candidate.m_limits; m_slots = std::move(candidate.m_slots);
    m_tables = candidate.m_tables; m_namesBytes = candidate.m_namesBytes; m_names = candidate.m_names;
    m_descriptor = std::move(descriptor);
    return ArchiveError::None;
}
ArchiveError ArchiveReader::Open(const std::vector<std::wstring>& paths, const ArchiveLimits& limits)
{
    assert(Owner());
    if (paths.empty() || paths.size() > limits.archives || !limits.entryBytes || !limits.tableBytes ||
        !limits.cachedEntries || !limits.nameBytes)
        return ArchiveError::Descriptor;
    std::vector<Slot> candidate;
    candidate.reserve(paths.size());
    uint64_t tables = 0, pathBytes = 0;
    for (const auto& path : paths) {
        pathBytes += uint64_t(path.size()+1)*sizeof(wchar_t);
        if (path.empty() || path.size() >= 32768 || path.find(L'\0') != std::wstring::npos || pathBytes > limits.pathBytes)
            return ArchiveError::Budget;
        if (path.size() < 3 || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/') ||
            !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
            return ArchiveError::Descriptor;
        if (std::any_of(candidate.begin(), candidate.end(), [&](const Slot& s) { return s.identity.path == path; }))
            return ArchiveError::Descriptor;
        uint8_t header[44]{}; Slot slot;
        if (!Identity(path, slot.identity, header)) return ArchiveError::Io;
        Bytes raw{header, sizeof(header)};
        uint32_t headerSize = 0, hashes = 0, blocks = 0;
        raw.U32(4, headerSize); raw.U32(24, hashes); raw.U32(28, blocks);
        const unsigned version = unsigned(header[12]) | (unsigned(header[13]) << 8);
        if (std::memcmp(header, "MPQ\x1a", 4) || version > 1 || headerSize < (version ? 44u : 32u) ||
            headerSize > slot.identity.size || !hashes || !blocks) return ArchiveError::Header;
        tables += (uint64_t(hashes)+blocks)*16;
        if (version) {
            uint32_t low = 0, high = 0; raw.U32(32, low); raw.U32(36, high);
            if (low || high) tables += uint64_t(blocks)*2;
        }
        if (tables > limits.tableBytes) return ArchiveError::Budget;
        candidate.push_back(std::move(slot));
    }
    Close(); m_slots = std::move(candidate); m_tables = static_cast<std::size_t>(tables); m_limits = limits;
    return ArchiveError::None;
}
const ArchiveIdentity* ArchiveReader::Source(unsigned ordinal) const
{ assert(Owner()); return ordinal<m_slots.size() ? &m_slots[ordinal].identity : nullptr; }
unsigned ArchiveReader::SourceCount() const
{ assert(Owner()); return unsigned(m_slots.size()); }
ArchiveError ArchiveReader::Validate() const
{
    assert(Owner());
    if (m_slots.empty()) return ArchiveError::Descriptor;
    if (!m_descriptor.path.empty()) {
        ArchiveIdentity now;
        if (!Check(m_descriptor.path, now) || !Same(now, m_descriptor)) return ArchiveError::Changed;
    }
    for (const auto& slot : m_slots) {
        ArchiveIdentity now;
        if (!Check(slot.identity.path, now) || !Same(now, slot.identity)) return ArchiveError::Changed;
    }
    return ArchiveError::None;
}
ArchiveError ArchiveReader::Read(const std::string& name, const std::atomic<bool>& cancelled, ArchiveEntry& output,
                                 std::size_t byteLimit)
{
    return ReadEntry(name,cancelled,output,byteLimit,true);
}
ArchiveError ArchiveReader::ReadSnapshot(const std::string& name,const std::atomic<bool>& cancelled,ArchiveEntry& output,std::size_t byteLimit)
{
    return ReadEntry(name,cancelled,output,byteLimit,false);
}
// Highest-precedence archive holding the canonical name, its file left open in `file`.
// Missing when no archive has it; a broken or deleted override is an error, never skipped.
ArchiveError ArchiveReader::FindEntry(const std::string& canonical,const std::atomic<bool>& cancelled,bool completeView,
                                      std::size_t& slot,void*& file)
{
    assert(Owner());
    if (cancelled.load()) return ArchiveError::Cancelled;
    auto valid = completeView?Validate():(m_slots.empty()?ArchiveError::Descriptor:ArchiveError::None);
    if (valid != ArchiveError::None) return valid;
    for (std::size_t n = m_slots.size(); n-- > 0;) {
        auto* i = &m_slots[n];
        if (cancelled.load()) return ArchiveError::Cancelled;
        const bool known = i->names.count(canonical) != 0;
        const auto nameBytes = 2*(canonical.size()+1);
        if (!known && (m_names >= m_limits.cachedEntries || nameBytes > m_limits.nameBytes-m_namesBytes))
            return ArchiveError::Budget;
        // Hash lookup needs neither the potentially large name list nor attributes.
        if (!i->handle && !SFileOpenArchive(i->identity.path.c_str(), 0,
                MPQ_OPEN_READ_ONLY | MPQ_OPEN_NO_LISTFILE | MPQ_OPEN_NO_ATTRIBUTES, &i->handle))
            return ArchiveError::Io;
        HANDLE opened = nullptr;
        if (!SFileOpenFileEx(i->handle, canonical.c_str(), SFILE_OPEN_FROM_MPQ, &opened)) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;
            return ArchiveError::Io;
        }
        if (!known) { m_namesBytes += nameBytes; ++m_names; i->names.insert(canonical); }
        slot = n; file = opened;
        return ArchiveError::None;
    }
    return ArchiveError::Missing;
}
ArchiveError ArchiveReader::Contains(const std::string& name,const std::atomic<bool>& cancelled,bool completeView)
{
    std::string canonical;
    if (!CanonicalName(name, canonical)) return ArchiveError::Descriptor;
    std::size_t slot = 0; EntryFile file;
    const auto start=std::chrono::steady_clock::now();
    const auto found=FindEntry(canonical,cancelled,completeView,slot,file.value);
    ++m_stats.lookups;m_stats.findMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return found;
}
ArchiveError ArchiveReader::ReadEntry(const std::string& name,const std::atomic<bool>& cancelled,ArchiveEntry& output,std::size_t byteLimit,bool completeView)
{
    assert(Owner());
    ArchiveEntry candidate;
    if (!CanonicalName(name, candidate.name)) return ArchiveError::Descriptor;
    if (cancelled.load()) return ArchiveError::Cancelled;
    if (!byteLimit) return ArchiveError::Budget;
    std::size_t slot = 0; EntryFile file;
    const auto start=std::chrono::steady_clock::now();
    auto valid = FindEntry(candidate.name,cancelled,completeView,slot,file.value);
    const auto found=std::chrono::steady_clock::now();
    ++m_stats.lookups;m_stats.findMilliseconds+=std::chrono::duration<double,std::milli>(found-start).count();
    if (valid != ArchiveError::None) return valid;
    auto* i = &m_slots[slot];
    DWORD flags = 0, needed = 0;
    if (!SFileGetFileInfo(file.value, SFileInfoFlags, &flags, sizeof(flags), &needed)) return ArchiveError::Io;
    if (flags & (MPQ_FILE_PATCH_FILE | MPQ_FILE_DELETE_MARKER)) return ArchiveError::Unsupported;
    DWORD high = 0;
    const DWORD size = SFileGetFileSize(file.value, &high);
    if (high || size == SFILE_INVALID_SIZE || !size || size > std::min(m_limits.entryBytes, byteLimit)) return ArchiveError::Budget;
    candidate.bytes.resize(size);
    for (DWORD offset = 0; offset < size;) {
        if (cancelled.load()) return ArchiveError::Cancelled;
        const DWORD wanted = std::min<DWORD>(256*1024, size-offset);
        DWORD read = 0;
        if (!SFileReadFile(file.value, candidate.bytes.data()+offset, wanted, &read, nullptr) || read != wanted)
            return ArchiveError::Io;
        offset += read;
    }
    ++m_stats.reads;m_stats.readMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-found).count();
    if (cancelled.load()) return ArchiveError::Cancelled;
    if (completeView && (valid=Validate()) != ArchiveError::None) return valid;
    candidate.source = i->identity;
    output = std::move(candidate);
    return ArchiveError::None;
}
}
