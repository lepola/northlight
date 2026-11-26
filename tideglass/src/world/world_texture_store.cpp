#include "world/world_texture_store.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <new>

namespace water::world
{
namespace
{
constexpr char Owner[]="Tideglass texture cache v1\n";
struct Failure { TexturePackError error; };
void Need(bool value,TexturePackError error=TexturePackError::Io) { if(!value)throw Failure{error}; }
void Check(const std::atomic<bool>& cancelled) { Need(!cancelled.load(std::memory_order_relaxed),TexturePackError::Cancelled); }
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
bool Regular(const std::filesystem::path& path)
{
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
}
void Root(const std::filesystem::path& root)
{
    Need(root.is_absolute());
    if(!std::filesystem::exists(root))std::filesystem::create_directories(root);
    const auto attributes=GetFileAttributesW(root.c_str());
    Need(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT));
    if(TextureStore::Readable(root)==TexturePackError::None)return;
    // Never adopt a populated directory or remove files without our marker.
    Need(std::filesystem::is_empty(root));
    Handle file;file.value=CreateFileW((root/L"owner.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Need(file.value!=INVALID_HANDLE_VALUE);DWORD written=0;
    const bool good=WriteFile(file.value,Owner,sizeof(Owner)-1,&written,nullptr) && written==sizeof(Owner)-1 && FlushFileBuffers(file.value);
    if(!good){CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;DeleteFileW((root/L"owner.txt").c_str());}
    Need(good);
}
void Remove(const std::filesystem::path& path)
{
    if(!std::filesystem::exists(path))return;
    Need(Regular(path));Need(DeleteFileW(path.c_str()));
}
}

unsigned TextureStore::Bucket(const std::string& name)
{ uint32_t hash=2166136261u;for(unsigned char c:name)hash=(hash^c)*16777619u;return hash%Buckets; }
TexturePackLimits TextureStore::Limits()
{
    TexturePackLimits limits;limits.fileBytes=4*1024*1024+256*1024;limits.indexBytes=256*1024;
    limits.builderPayloadBytes=4*1024*1024;limits.mipBytes=1024*1024;limits.textures=128;limits.chunks=2048;return limits;
}
std::filesystem::path TextureStore::Pack(const std::filesystem::path& root,unsigned bucket)
{ return root/(L"textures-"+std::to_wstring(bucket)+L".bin"); }
TexturePackError TextureStore::Readable(const std::filesystem::path& root)
{
    try {
        if(!root.is_absolute())return TexturePackError::Io;
        const auto attributes=GetFileAttributesW(root.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)return TexturePackError::Missing;
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !Regular(root/L"owner.txt"))return TexturePackError::Io;
        std::ifstream file(root/L"owner.txt",std::ios::binary);std::array<char,sizeof(Owner)-1> value{};
        file.read(value.data(),value.size());
        return file && std::equal(value.begin(),value.end(),Owner) && file.peek()==EOF ? TexturePackError::None : TexturePackError::Io;
    } catch(const std::filesystem::filesystem_error&) { return TexturePackError::Io; }
}

TextureStore::Result TextureStore::Publish(const std::filesystem::path& root,const std::string& identity,
                                         unsigned bucket,const std::vector<Asset>& assets,uint64_t diskLimit,
                                         const std::atomic<bool>& cancelled)
{
    Result result;
    try {
        Check(cancelled);
        Need(bucket<Buckets && assets.size()<=1024 && diskLimit>=16*1024*1024 && diskLimit<=1024ull*1024*1024,
             TexturePackError::Format);
        Need(identity.size()==64 && std::all_of(identity.begin(),identity.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),TexturePackError::Identity);
        Root(root);Handle lock;
        lock.value=CreateFileW((root/L"writer.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        Need(lock.value!=INVALID_HANDLE_VALUE);Check(cancelled);
        // Only these owned staging names are reclaimable, under the writer claim.
        for(unsigned i=0;i<Buckets;++i) {
            Check(cancelled);Remove(Pack(root,i).wstring()+L".new");Remove(Pack(root,i).wstring()+L".new.partial");
        }
        struct File { unsigned bucket;uint64_t bytes;std::filesystem::file_time_type used; };
        std::vector<File> files;
        uint64_t used=sizeof(Owner)-1; // marker + zero-length writer lock
        for(unsigned i=0;i<Buckets;++i)if(std::filesystem::exists(Pack(root,i))) {
            Check(cancelled);const auto path=Pack(root,i);Need(Regular(path));const auto bytes=std::filesystem::file_size(path);
            used+=bytes;files.push_back({i,bytes,std::filesystem::last_write_time(path)});
        }
        const auto makeRoom=[&](uint64_t reserve) {
            std::sort(files.begin(),files.end(),[&](const File& a,const File& b){
                if((a.bucket==bucket)!=(b.bucket==bucket))return b.bucket==bucket;
                return a.used<b.used;
            });
            for(const auto& file:files) {
                if(used+reserve<=diskLimit)break;
                Check(cancelled);Remove(Pack(root,file.bucket));used-=file.bytes;++result.evicted;
            }
            result.diskBytes=used;Need(used+reserve<=diskLimit,TexturePackError::Budget);
        };
        if(assets.empty()) {
            makeRoom(0);Check(cancelled);
            if(std::filesystem::exists(Pack(root,bucket)))std::filesystem::last_write_time(Pack(root,bucket),std::filesystem::file_time_type::clock::now());
            return result;
        }
        TexturePackBuilder builder(Limits());TexturePackReader old(Limits());
        const auto opened=old.Open(Pack(root,bucket),identity,cancelled);
        if(opened==TexturePackError::Cancelled)throw Failure{opened};
        bool changed=opened!=TexturePackError::None;
        for(const auto& asset:assets) {
            Check(cancelled);Need(asset.texture && Bucket(asset.key.name)==bucket,TexturePackError::Format);
            unsigned index=0;
            const auto admitted=builder.Add(asset.key,asset.source,*asset.texture,index);
            if(admitted==TexturePackError::Budget){++result.skipped;continue;}
            Need(admitted==TexturePackError::None,admitted);
            if(!old.Find(asset.key,index))changed=true;
        }
        if(builder.Entries().empty()){makeRoom(0);return result;}
        if(opened==TexturePackError::None)for(unsigned i=0;i<old.Entries().size();++i) {
            Check(cancelled);const auto& entry=old.Entries()[i];Texture texture;TexturePackReadStats stats;
            const auto read=old.Read(i,cancelled,6*1024*1024,texture,stats);
            if(read==TexturePackError::Cancelled)throw Failure{read};
            if(read!=TexturePackError::None){changed=true;continue;}
            unsigned index=0;const auto admitted=builder.Add(entry.key,entry.sourceArchive,texture,index);
            if(admitted==TexturePackError::None)++result.reused;
            else { changed=true;++result.skipped; } // new requested data has priority
        }
        changed=changed || builder.Entries().size()!=old.Entries().size();
        old=TexturePackReader(Limits()); // readers must release the destination before replacement
        if(!changed) {
            makeRoom(0);
            if(std::filesystem::exists(Pack(root,bucket)))std::filesystem::last_write_time(Pack(root,bucket),std::filesystem::file_time_type::clock::now());
            return result;
        }
        makeRoom(Limits().fileBytes); // includes the simultaneous old + temporary new pack
        ULARGE_INTEGER free{};Need(GetDiskFreeSpaceExW(root.c_str(),&free,nullptr,nullptr));
        Need(free.QuadPart>=MinFreeBytes+Limits().fileBytes,TexturePackError::Budget);
        Check(cancelled);const auto next=std::filesystem::path(Pack(root,bucket).wstring()+L".new");
        TexturePackWriteStats written;
        const auto write=builder.Write(next,identity,1,cancelled,written);
        Need(write==TexturePackError::None,write);
        if(cancelled.load(std::memory_order_relaxed)){Remove(next);throw Failure{TexturePackError::Cancelled};}
        const auto previous=std::filesystem::exists(Pack(root,bucket))?std::filesystem::file_size(Pack(root,bucket)):0;
        if(!MoveFileExW(next.c_str(),Pack(root,bucket).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            Remove(next);throw Failure{TexturePackError::Io};
        }
        result.written=1;result.writtenBytes=written.fileBytes;result.diskBytes=used-previous+written.fileBytes;
    } catch(const Failure& failure) { result.error=failure.error; }
      catch(const std::bad_alloc&) { result.error=TexturePackError::Budget; }
      catch(const std::filesystem::filesystem_error&) { result.error=TexturePackError::Io; }
    return result;
}
}
