#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace NorthlightSettings {
inline std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==s.npos?"":s.substr(a,b-a+1);}
inline std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
inline std::string hash(const std::string& s){std::uint64_t h=14695981039346656037ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}std::ostringstream o;o<<std::hex<<h;return o.str();}
inline constexpr const char* FileNames[]={"northlight-quality.ini","northlight-renderer.ini","celestial-profiles.ini","shadow-range-profiles.ini","water.ini","water-host.ini"};
inline constexpr unsigned WaterFile=4, WaterHostFile=5, FileCount=std::size(FileNames);
inline constexpr char FileIds[]="QRCSWH";
static_assert(sizeof(FileIds)==FileCount+1);
class TransactionLock {
#ifdef _WIN32
    HANDLE handle=nullptr;
#else
    int handle=-1;
#endif
public:
    explicit TransactionLock(const std::filesystem::path& root,unsigned waitMs=200){
#ifdef _WIN32
        const auto name="Local\\NorthlightSettings-"+hash(lower(std::filesystem::weakly_canonical(root).generic_u8string()));const std::wstring wide(name.begin(),name.end());handle=CreateMutexW(nullptr,FALSE,wide.c_str());
        if(!handle)throw std::runtime_error("Cannot lock settings");const DWORD result=WaitForSingleObject(handle,waitMs);
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Settings are being saved by another client; retry shortly");}
#else
        handle=::open((root/"northlight-settings.lock").c_str(),O_CREAT|O_RDWR,0600);
        if(handle<0)throw std::runtime_error("Cannot lock settings");const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(waitMs);
        while(flock(handle,LOCK_EX|LOCK_NB)){if(std::chrono::steady_clock::now()>=deadline){::close(handle);handle=-1;throw std::runtime_error("Settings are being saved by another client; retry shortly");}std::this_thread::sleep_for(std::chrono::milliseconds(5));}
#endif
    }
    ~TransactionLock(){
#ifdef _WIN32
        if(handle){ReleaseMutex(handle);CloseHandle(handle);}
#else
        if(handle>=0){flock(handle,LOCK_UN);::close(handle);}
#endif
    }
    TransactionLock(const TransactionLock&)=delete;
};
// Strict UTF-16LE <-> UTF-8 (the project targets C++17; std::codecvt is deprecated). Invalid input (a lone surrogate, an overlong or
// truncated UTF-8 sequence, a code point above U+10FFFF) is rejected rather than replaced: a silent U+FFFD would corrupt the user's INI on save.
inline std::string utf8FromUtf16le(const std::string& raw,size_t from){
    std::string out;auto unit=[&](size_t i){return unsigned(static_cast<unsigned char>(raw[i]))|(unsigned(static_cast<unsigned char>(raw[i+1]))<<8);};
    for(size_t i=from;i+1<raw.size();i+=2){
        unsigned c=unit(i);
        if(c>=0xdc00&&c<=0xdfff)throw std::runtime_error("Invalid UTF-16LE INI (unpaired surrogate)");
        if(c>=0xd800&&c<=0xdbff){if(i+3>=raw.size())throw std::runtime_error("Invalid UTF-16LE INI (unpaired surrogate)");unsigned d=unit(i+2);
            if(d<0xdc00||d>0xdfff)throw std::runtime_error("Invalid UTF-16LE INI (unpaired surrogate)");c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i+=2;}
        if(c<0x80)out+=char(c);
        else if(c<0x800){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else if(c<0x10000){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
    }
    return out;
}
inline std::string utf16leFromUtf8(const std::string& s){
    std::string out="\xff\xfe";auto put=[&](unsigned u){out+=char(u&255);out+=char(u>>8);};
    for(size_t i=0;i<s.size();){
        const unsigned b=static_cast<unsigned char>(s[i]);unsigned c,n;
        if(b<0x80){c=b;n=0;}else if(b>=0xc2&&b<=0xdf){c=b&0x1f;n=1;}else if(b>=0xe0&&b<=0xef){c=b&0x0f;n=2;}else if(b>=0xf0&&b<=0xf4){c=b&0x07;n=3;}else throw std::runtime_error("Invalid UTF-8 in INI");
        if(n&&i+n>=s.size())throw std::runtime_error("Truncated UTF-8 in INI");
        for(unsigned k=1;k<=n;++k){const unsigned x=static_cast<unsigned char>(s[i+k]);if((x&0xc0)!=0x80)throw std::runtime_error("Invalid UTF-8 in INI");c=(c<<6)|(x&63);}
        if((n==2&&c<0x800)||(n==3&&c<0x10000)||c>0x10ffff||(c>=0xd800&&c<=0xdfff))throw std::runtime_error("Invalid UTF-8 in INI");
        i+=n+1;
        if(c>=0x10000){c-=0x10000;put(0xd800+(c>>10));put(0xdc00+(c&0x3ff));}else put(c);
    }
    return out;
}
struct Document {
    struct Line{std::string text,end;};
    std::vector<Line> lines;enum Encoding{Bytes,Utf8Bom,Utf16LE} encoding=Bytes;std::string newline="\r\n";
    Document()=default;
    explicit Document(std::string raw){
        if(raw.size()>524288)throw std::runtime_error("INI exceeds 512 KiB");
        if(raw.rfind("\xfe\xff",0)==0)throw std::runtime_error("UTF-16BE is unsupported; save the INI as UTF-8");
        if(raw.rfind("\xff\xfe",0)==0){
            if(raw.size()%2)throw std::runtime_error("Truncated UTF-16LE INI");encoding=Utf16LE;raw=utf8FromUtf16le(raw,2);
        }else if(raw.rfind("\xef\xbb\xbf",0)==0){encoding=Utf8Bom;raw.erase(0,3);}
        if(raw.find('\n')!=raw.npos)newline=raw.find("\r\n")!=raw.npos?"\r\n":"\n";
        size_t start=0;while(start<raw.size()){auto n=raw.find('\n',start);if(n==raw.npos){lines.push_back({raw.substr(start),""});break;}
            bool cr=n>start&&raw[n-1]=='\r';lines.push_back({raw.substr(start,n-start-(cr?1:0)),cr?"\r\n":"\n"});start=n+1;}
    }
    std::string text()const{std::string s;for(const auto& l:lines)s+=l.text+l.end;return s;}
    std::string bytes()const{
        auto s=text();if(encoding==Utf8Bom)return "\xef\xbb\xbf"+s;if(encoding!=Utf16LE)return s;
        return utf16leFromUtf8(s);
    }
    static std::string content(const std::string& line){return trim(line.substr(0,line.find_first_of("#;")));}
    std::map<std::string,std::map<std::string,std::string>> values()const{
        std::map<std::string,std::map<std::string,std::string>> out;std::string section;
        for(const auto& l:lines){auto s=content(l.text);if(s.size()>1&&s.front()=='['&&s.back()==']'){section=lower(trim(s.substr(1,s.size()-2)));out[section];}
            else if(auto e=s.find('=');e!=s.npos)out[section][lower(trim(s.substr(0,e)))]=trim(s.substr(e+1));}return out;
    }
    // Only the value span changes. Resets remove all occurrences, retaining comments and other keys.
    void set(const std::string& section,const std::string& key,const std::string* value){
        const auto want=lower(section),name=lower(key);std::string current;bool found=false,hasSection=false;size_t insert=lines.size();
        for(size_t i=0;i<lines.size();){auto s=content(lines[i].text);
            if(s.size()>1&&s.front()=='['&&s.back()==']'){if(current==want)insert=i;current=lower(trim(s.substr(1,s.size()-2)));if(current==want){hasSection=true;insert=i+1;}++i;continue;}
            auto e=s.find('=');if(current==want&&e!=s.npos&&lower(trim(s.substr(0,e)))==name){
                if(!value||found){auto c=lines[i].text.find_first_of("#;");if(c!=std::string::npos){lines[i].text=lines[i].text.substr(c);++i;}else lines.erase(lines.begin()+i);continue;}
                auto& t=lines[i].text;auto eq=t.find('=');size_t a=t.find_first_not_of(" \t",eq+1),b=t.find_first_of("#;",eq+1);
                if(a==t.npos||a>=b)a=eq+1;size_t end=b==t.npos?t.size():b;while(end>a&&(t[end-1]==' '||t[end-1]=='\t'))--end;
                t.replace(a,end-a,*value);found=true;
            }if(current==want)insert=i+1;++i;
        }
        if(!value||found)return;
        if(!hasSection){if(!lines.empty()&&lines.back().end.empty())lines.back().end=newline;lines.push_back({"["+section+"]",newline});insert=lines.size();}
        if(insert&&lines[insert-1].end.empty())lines[insert-1].end=newline;
        lines.insert(lines.begin()+insert,{key+"="+*value,newline});
    }
};
struct FileImage{bool exists=false;std::string raw;};
inline bool operator==(const FileImage& a,const FileImage& b){return a.exists==b.exists&&a.raw==b.raw;}
inline FileImage readImage(const std::filesystem::path& p){
    std::error_code ec;if(!std::filesystem::exists(p,ec)){if(ec)throw std::runtime_error("Cannot inspect INI");return {};}
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read INI");
    std::string s;char b[4096];while(f){f.read(b,sizeof b);s.append(b,size_t(f.gcount()));if(s.size()>524288)throw std::runtime_error("INI exceeds 512 KiB");}
    if(!f.eof())throw std::runtime_error("INI read failed");return {true,s};
}
inline std::string leaf(const std::filesystem::path& p){return p.filename().string();}
inline void writeImage(const std::filesystem::path& p,const std::string& bytes){
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create "+leaf(p));
    DWORD n=0;bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Write to "+leaf(p)+" failed");
#else
    std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("Cannot create "+leaf(p));f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();if(!f)throw std::runtime_error("Write to "+leaf(p)+" failed");
#endif
}
inline void replaceFile(const std::filesystem::path& from,const std::filesystem::path& to){
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace "+leaf(to)+" (read-only or locked by another program)");
#else
    std::error_code ec;std::filesystem::rename(from,to,ec);if(ec)throw std::runtime_error("Cannot replace "+leaf(to)+": "+ec.message());
#endif
}
// The Windows replace fails on a read-only target and POSIX rename ignores the file mode, so test the attribute up front: the user gets the real cause before any transaction file exists.
inline void requireWritable(const std::filesystem::path& p){
    std::error_code ec;if(!std::filesystem::exists(p,ec))return;
#ifdef _WIN32
    const DWORD a=GetFileAttributesW(p.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_READONLY))throw std::runtime_error(leaf(p)+" is read-only");
#else
    if(::access(p.c_str(),W_OK)!=0)throw std::runtime_error(leaf(p)+" is read-only");
#endif
}
class Store {
    std::filesystem::path root;unsigned waitMs;bool supportsWater;
    std::filesystem::path path(unsigned i,const char* suffix="")const{return root/(std::string(FileNames[i])+suffix);}
    std::filesystem::path journal()const{return root/"northlight-settings.journal";}
    bool journalExists()const{std::error_code ec;return std::filesystem::exists(journal(),ec);}
    void mark(const std::string& value){auto temp=root/"northlight-settings.journal.tmp";writeImage(temp,value);replaceFile(temp,journal());}
    void clean(const std::vector<unsigned>& touched){for(unsigned i:touched)for(auto s:{".northlight.tmp",".northlight.bak"}){std::error_code ec;std::filesystem::remove(path(i,s),ec);}std::error_code ec;std::filesystem::remove(journal(),ec);std::filesystem::remove(root/"northlight-settings.journal.tmp",ec);}
public:
    // No I/O here: a failed recovery must not keep the menu from starting. Every locked read and commit finishes a pending journal first.
    explicit Store(std::filesystem::path directory,unsigned lockWaitMs=2000,bool water=true):root(std::move(directory)),waitMs(lockWaitMs),supportsWater(water){}
    bool hasWaterCore()const{
        std::error_code ec;
        return supportsWater&&std::filesystem::is_regular_file(root/"water-core.dll",ec);
    }
    std::array<FileImage,FileCount> readUnlocked()const{
        std::array<FileImage,FileCount> images{};
        const unsigned count=hasWaterCore()?FileCount:WaterFile;
        for(unsigned i=0;i<count;++i)images[i]=readImage(path(i));
        return images;
    }
    std::array<FileImage,FileCount> read(){TransactionLock lease(root,waitMs);if(journalExists())recoverUnlocked();return readUnlocked();}
    // Like read(), but a recovery that cannot finish (external edit, damaged backup) is reported in `problem` and the current files are still returned.
    std::array<FileImage,FileCount> readLenient(std::string& problem){
        TransactionLock lease(root,waitMs);problem.clear();
        if(journalExists())try{recoverUnlocked();}catch(const std::exception& e){problem=e.what();}
        return readUnlocked();
    }
    static std::string revision(const std::array<FileImage,FileCount>& a){std::string s;for(const auto& f:a)s+=(f.exists?"1":"0")+hash(f.raw)+";";return hash(s);}
    void recover(){TransactionLock lease(root,waitMs);recoverUnlocked();}
    // Process start, before anything reads the INIs: finish a commit that a crash interrupted. Returns the failure text ("" = nothing to do or done); never throws.
    static std::string recoverAtStartup(const std::filesystem::path& directory,unsigned lockWaitMs=3000){
        try{std::error_code ec;if(!std::filesystem::exists(directory/"northlight-settings.journal",ec))return "";Store(directory,lockWaitMs).recover();return "";}
        catch(const std::exception& e){return e.what();}catch(...){return "unknown error";}
    }
    void recoverUnlocked(){
        auto j=readImage(journal());if(!j.exists)return;
        std::istringstream in(j.raw);std::string state;if(!(in>>state)||(state!="prepared"&&state!="committed"))throw std::runtime_error("Invalid settings journal; keep backups for manual recovery");
        struct Item{unsigned i,exists;std::string oldHash,newHash;};std::vector<Item> items;Item a{};
        while(in>>a.i>>a.exists>>a.oldHash>>a.newHash){if(a.i>=FileCount||a.exists>1)throw std::runtime_error("Invalid settings journal");items.push_back(a);}
        if(!in.eof()||items.empty()||items.size()>FileCount)throw std::runtime_error("Invalid settings journal");
        std::vector<unsigned> touched;for(const auto& x:items){if(std::find(touched.begin(),touched.end(),x.i)!=touched.end())throw std::runtime_error("Duplicate settings journal file");touched.push_back(x.i);}
        if(state=="committed"){clean(touched);return;}
        // Check the complete recovery before restoring any file. A file still at its old hash was never replaced (nothing to restore); one at the
        // new hash is rolled back from its backup; anything else is an external edit and is never overwritten.
        std::vector<Item> restore;
        for(auto& x:items){auto f=readImage(path(x.i));
            if(f.exists==bool(x.exists)&&hash(f.raw)==x.oldHash)continue;
            if(!f.exists||hash(f.raw)!=x.newHash)throw std::runtime_error("Settings recovery conflicts with an external edit of "+leaf(path(x.i))+"; backups retained");
            auto backup=readImage(path(x.i,".northlight.bak"));
            if(x.exists&&(!backup.exists||hash(backup.raw)!=x.oldHash))throw std::runtime_error("Settings backup of "+leaf(path(x.i))+" is missing or damaged");
            restore.push_back(x);}
        for(auto& x:restore){
            if(x.exists){requireWritable(path(x.i));writeImage(path(x.i,".northlight.tmp"),readImage(path(x.i,".northlight.bak")).raw);replaceFile(path(x.i,".northlight.tmp"),path(x.i));}
            else{requireWritable(path(x.i));std::error_code ec;std::filesystem::remove(path(x.i),ec);if(ec)throw std::runtime_error("Cannot remove "+leaf(path(x.i))+": "+ec.message());}}
        clean(touched);
    }
    // failAfter is used only by the transaction fault-injection test.
    void commit(const std::array<FileImage,FileCount>& before,const std::array<FileImage,FileCount>& after,int failAfter=-1){
        TransactionLock lease(root,waitMs);
        if(journalExists())recoverUnlocked();
        if(readUnlocked()!=before)throw std::runtime_error("INI files changed externally; refresh the menu before saving");
        std::vector<unsigned> touched;std::string manifest="prepared\n";
        for(unsigned i=0;i<FileCount;++i)if(!(before[i]==after[i])){
            if(!after[i].exists)throw std::runtime_error("Settings commits cannot delete an INI");
            requireWritable(path(i));
            // No journal exists here (recovery above), so no file has been replaced: leftovers of a crash before the journal was written are stale copies.
            for(auto s:{".northlight.tmp",".northlight.bak"}){std::error_code ec;std::filesystem::remove(path(i,s),ec);if(ec||std::filesystem::exists(path(i,s),ec))throw std::runtime_error("Cannot remove stale "+leaf(path(i,s)));}
            touched.push_back(i);manifest+=std::to_string(i)+" "+(before[i].exists?"1":"0")+" "+hash(before[i].raw)+" "+hash(after[i].raw)+"\n";}
        if(touched.empty())return;
        try{
            for(auto i:touched){writeImage(path(i,".northlight.tmp"),after[i].raw);if(before[i].exists)writeImage(path(i,".northlight.bak"),before[i].raw);}
            if(readUnlocked()!=before)throw std::runtime_error("INI files changed externally; save cancelled");
            mark(manifest);int count=0;
            for(auto i:touched){replaceFile(path(i,".northlight.tmp"),path(i));if(++count==failAfter)throw std::runtime_error("Injected transaction failure");}
            mark("committed\n"+manifest.substr(9));clean(touched);
        }catch(const std::exception& e){
            const std::string cause=e.what();
            // Roll back. If that fails too the journal stays for the next locked read/commit/start; report both causes.
            try{if(journalExists())recoverUnlocked();else clean(touched);}
            catch(const std::exception& r){throw std::runtime_error(cause+" (rollback incomplete, will retry: "+r.what()+")");}
            throw;
        }catch(...){try{if(journalExists())recoverUnlocked();else clean(touched);}catch(...){}throw;}
    }
};
} // namespace NorthlightSettings
