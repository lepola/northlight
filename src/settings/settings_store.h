#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
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
inline constexpr const char* FileNames[]={"northlight-quality.ini","northlight-renderer.ini","celestial-profiles.ini","shadow-range-profiles.ini"};
class TransactionLock {
#ifdef _WIN32
    HANDLE handle=nullptr;
#else
    int handle=-1;
#endif
public:
    explicit TransactionLock(const std::filesystem::path& root){
#ifdef _WIN32
        const auto name="Local\\NorthlightSettings-"+hash(lower(std::filesystem::weakly_canonical(root).generic_u8string()));const std::wstring wide(name.begin(),name.end());handle=CreateMutexW(nullptr,FALSE,wide.c_str());
        if(!handle)throw std::runtime_error("Cannot lock settings");const DWORD result=WaitForSingleObject(handle,200);
        if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;throw std::runtime_error("Settings are being saved by another client; retry shortly");}
#else
        handle=::open((root/"northlight-settings.lock").c_str(),O_CREAT|O_RDWR,0600);
        if(handle<0)throw std::runtime_error("Cannot lock settings");if(flock(handle,LOCK_EX|LOCK_NB)){::close(handle);handle=-1;throw std::runtime_error("Settings are being saved by another client");}
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
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
// The project targets C++17; its standard UTF-8/UTF-16 codec preserves Unicode comments.
struct Document {
    struct Line{std::string text,end;};
    std::vector<Line> lines;enum Encoding{Bytes,Utf8Bom,Utf16LE} encoding=Bytes;std::string newline="\r\n";
    Document()=default;
    explicit Document(std::string raw){
        if(raw.size()>524288)throw std::runtime_error("INI exceeds 512 KiB");
        if(raw.rfind("\xfe\xff",0)==0)throw std::runtime_error("UTF-16BE is unsupported; save the INI as UTF-8");
        if(raw.rfind("\xff\xfe",0)==0){
            if(raw.size()%2)throw std::runtime_error("Truncated UTF-16LE INI");encoding=Utf16LE;std::u16string u;
            for(size_t i=2;i<raw.size();i+=2)u+=char16_t(static_cast<unsigned char>(raw[i])|(unsigned(static_cast<unsigned char>(raw[i+1]))<<8));
            raw=std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.to_bytes(u);
        }else if(raw.rfind("\xef\xbb\xbf",0)==0){encoding=Utf8Bom;raw.erase(0,3);}
        if(raw.find('\n')!=raw.npos)newline=raw.find("\r\n")!=raw.npos?"\r\n":"\n";
        size_t start=0;while(start<raw.size()){auto n=raw.find('\n',start);if(n==raw.npos){lines.push_back({raw.substr(start),""});break;}
            bool cr=n>start&&raw[n-1]=='\r';lines.push_back({raw.substr(start,n-start-(cr?1:0)),cr?"\r\n":"\n"});start=n+1;}
    }
    std::string text()const{std::string s;for(const auto& l:lines)s+=l.text+l.end;return s;}
    std::string bytes()const{
        auto s=text();if(encoding==Utf8Bom)return "\xef\xbb\xbf"+s;if(encoding!=Utf16LE)return s;
        auto u=std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>{}.from_bytes(s);s="\xff\xfe";for(auto c:u){s+=char(c&255);s+=char(c>>8);}return s;
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
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
struct FileImage{bool exists=false;std::string raw;};
inline bool operator==(const FileImage& a,const FileImage& b){return a.exists==b.exists&&a.raw==b.raw;}
inline FileImage readImage(const std::filesystem::path& p){
    std::error_code ec;if(!std::filesystem::exists(p,ec)){if(ec)throw std::runtime_error("Cannot inspect INI");return {};}
    std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read INI");
    std::string s;char b[4096];while(f){f.read(b,sizeof b);s.append(b,size_t(f.gcount()));if(s.size()>524288)throw std::runtime_error("INI exceeds 512 KiB");}
    if(!f.eof())throw std::runtime_error("INI read failed");return {true,s};
}
inline void writeImage(const std::filesystem::path& p,const std::string& bytes){
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create settings transaction file");
    DWORD n=0;bool ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);if(!ok)throw std::runtime_error("Settings transaction write failed");
#else
    std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();if(!f)throw std::runtime_error("Settings transaction write failed");
#endif
}
inline void replaceFile(const std::filesystem::path& from,const std::filesystem::path& to){
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace INI (file may be read-only or locked)");
#else
    std::filesystem::rename(from,to);
#endif
}
class Store {
    std::filesystem::path root;
    std::filesystem::path path(unsigned i,const char* suffix="")const{return root/(std::string(FileNames[i])+suffix);}
    std::filesystem::path journal()const{return root/"northlight-settings.journal";}
    void mark(const std::string& value){auto temp=root/"northlight-settings.journal.tmp";writeImage(temp,value);replaceFile(temp,journal());}
    void clean(const std::vector<unsigned>& touched){for(unsigned i:touched)for(auto s:{".northlight.tmp",".northlight.bak"}){std::error_code ec;std::filesystem::remove(path(i,s),ec);}std::error_code ec;std::filesystem::remove(journal(),ec);std::filesystem::remove(root/"northlight-settings.journal.tmp",ec);}
public:
    explicit Store(std::filesystem::path directory):root(std::move(directory)){recover();}
    std::array<FileImage,4> readUnlocked()const{std::array<FileImage,4> a;for(unsigned i=0;i<4;++i)a[i]=readImage(path(i));return a;}
    std::array<FileImage,4> read()const{TransactionLock lease(root);return readUnlocked();}
    static std::string revision(const std::array<FileImage,4>& a){std::string s;for(const auto& f:a)s+=(f.exists?"1":"0")+hash(f.raw)+";";return hash(s);}
    void recover(){TransactionLock lease(root);recoverUnlocked();}
    void recoverUnlocked(){
        auto j=readImage(journal());if(!j.exists)return;
        std::istringstream in(j.raw);std::string state;if(!(in>>state)||(state!="prepared"&&state!="committed"))throw std::runtime_error("Invalid settings journal; keep backups for manual recovery");
        struct Item{unsigned i,exists;std::string oldHash,newHash;};std::vector<Item> items;Item a{};
        while(in>>a.i>>a.exists>>a.oldHash>>a.newHash){if(a.i>=4||a.exists>1)throw std::runtime_error("Invalid settings journal");items.push_back(a);}
        if(!in.eof()||items.empty()||items.size()>4)throw std::runtime_error("Invalid settings journal");
        std::vector<unsigned> touched;for(const auto& x:items){if(std::find(touched.begin(),touched.end(),x.i)!=touched.end())throw std::runtime_error("Duplicate settings journal file");touched.push_back(x.i);}
        if(state=="committed"){clean(touched);return;}
        // Check the complete recovery before restoring any file. Never overwrite an external edit.
        for(auto& x:items){auto f=readImage(path(x.i));auto backup=readImage(path(x.i,".northlight.bak"));
            bool old=f.exists==bool(x.exists)&&hash(f.raw)==x.oldHash;
            if(!old&&(!f.exists||hash(f.raw)!=x.newHash))throw std::runtime_error("Settings recovery conflicts with an external edit; backups retained");
            if(x.exists&&(!backup.exists||hash(backup.raw)!=x.oldHash))throw std::runtime_error("Settings backup is missing or damaged");}
        for(auto& x:items)if(x.exists){writeImage(path(x.i,".northlight.tmp"),readImage(path(x.i,".northlight.bak")).raw);replaceFile(path(x.i,".northlight.tmp"),path(x.i));}else std::filesystem::remove(path(x.i));clean(touched);
    }
    // failAfter is used only by the transaction fault-injection test.
    void commit(const std::array<FileImage,4>& before,const std::array<FileImage,4>& after,int failAfter=-1){
        TransactionLock lease(root);
        if(readUnlocked()!=before)throw std::runtime_error("INI files changed externally; refresh the menu before saving");
        std::vector<unsigned> touched;std::string manifest="prepared\n";
        for(unsigned i=0;i<4;++i)if(!(before[i]==after[i])){
            if(!after[i].exists)throw std::runtime_error("Settings commits cannot delete an INI");
            if(readImage(path(i,".northlight.tmp")).exists||readImage(path(i,".northlight.bak")).exists)throw std::runtime_error("Unclaimed settings backup; preserve it and resolve before saving");
            touched.push_back(i);manifest+=std::to_string(i)+" "+(before[i].exists?"1":"0")+" "+hash(before[i].raw)+" "+hash(after[i].raw)+"\n";}
        if(touched.empty())return;
        try{
            for(auto i:touched){writeImage(path(i,".northlight.tmp"),after[i].raw);if(before[i].exists)writeImage(path(i,".northlight.bak"),before[i].raw);}
            if(readUnlocked()!=before)throw std::runtime_error("INI files changed externally; save cancelled");
            mark(manifest);int count=0;
            for(auto i:touched){replaceFile(path(i,".northlight.tmp"),path(i));if(++count==failAfter)throw std::runtime_error("Injected transaction failure");}
            mark("committed\n"+manifest.substr(9));clean(touched);
        }catch(...){if(readImage(journal()).exists)recoverUnlocked();else clean(touched);throw;}
    }
};
} // namespace NorthlightSettings
