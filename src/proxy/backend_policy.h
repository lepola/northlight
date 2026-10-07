#pragma once
#include <cwchar>
#include <cwctype>
#include <cctype>
#include <string>
#include <vector>
namespace NorthlightBackend {
// 0.3.189: Dxvk2 = the package's DXVK 2.7.1 alternative backend, chosen by the user (dxvk2_d3d9.dll), treated as Dxvk everywhere (isPackagedDxvk).
enum class Kind { Legacy, Native, Dxvk, Dxvk2, Invalid };
// Missing file retains the pre-0.3.98 installation. A present but incomplete
// config must never accidentally select a different renderer.
inline Kind parse(const wchar_t* text, bool configExists) {
    if (!configExists) return Kind::Legacy;
    if (!text) return Kind::Invalid;
    while (iswspace(*text)) ++text;
    wchar_t value[16]={}; unsigned n=0;
    while (*text && n<15) value[n++]=towlower(*text++);
    if (*text) return Kind::Invalid;
    while (n && iswspace(value[n-1])) value[--n]=0;
    if (!wcscmp(value,L"native")) return Kind::Native;
    if (!wcscmp(value,L"dxvk")) return Kind::Dxvk;
    if (!wcscmp(value,L"dxvk2")) return Kind::Dxvk2;
    if (!wcscmp(value,L"legacy")) return Kind::Legacy;
    return Kind::Invalid;
}
inline const char* name(Kind k) {
    switch(k){case Kind::Native:return "native";case Kind::Dxvk:return "dxvk";
    case Kind::Dxvk2:return "dxvk2";case Kind::Legacy:return "legacy";default:return "invalid";}
}
// 0.3.192 (DXVK3): the major number of a DXVK version string ("v3.1.1", "2.7.1"); unknown, empty or unparsable = 0.
inline int dxvkMajor(const std::string& version){
    size_t i=0;while(i<version.size()&&(version[i]==' '||version[i]=='v'||version[i]=='V'))++i;
    int major=0;bool any=false;
    for(;i<version.size()&&version[i]>='0'&&version[i]<='9';++i){any=true;major=major*10+(version[i]-'0');if(major>9999)return 0;}
    return any?major:0;
}
// The two DXVK builds the Windows package ships (3.1.1 as dxvk, 2.7.1 as dxvk2): same compat defaults and rules.
inline bool isPackagedDxvk(Kind k){return k==Kind::Dxvk||k==Kind::Dxvk2;}
// Case-folded, '/'-normalised path for identity comparisons of Windows paths.
inline std::wstring fold(const std::wstring& path){
    std::wstring out(path);for(auto& c:out)c=c==L'/'?L'\\':wchar_t(towlower(c));return out;
}
inline bool samePath(const std::wstring& a,const std::wstring& b){return !a.empty()&&fold(a)==fold(b);}
inline std::wstring baseName(const std::wstring& path){
    const size_t slash=path.find_last_of(L"\\/");return slash==std::wstring::npos?path:path.substr(slash+1);
}
// Directory with trailing separator ("" when the path has none).
inline std::wstring directory(const std::wstring& path){
    const size_t slash=path.find_last_of(L"\\/");return slash==std::wstring::npos?std::wstring():path.substr(0,slash+1);
}
// Only one module may carry the base name d3d9.dll (this proxy): Wine's and
// Windows' bare-name lookups can otherwise return a backend in its place.
inline bool d3d9Name(const std::wstring& path){return fold(baseName(path))==L"d3d9.dll";}
// Where the proxy sits relative to the game folder (the host exe's directory):
// Root = <game>\d3d9.dll (Windows, loaded by wow's LoadLibraryA("d3d9.dll")),
// Mods = <game>\mods\d3d9.dll (macOS WoWSilicon, preloaded from dlls.txt).
enum class Location { Root, Mods, Other };
inline Location location(const std::wstring& selfPath,const std::wstring& root){
    const std::wstring dir=fold(directory(selfPath)),r=fold(root);
    if(dir==r)return Location::Root;
    if(dir==r+L"mods\\")return Location::Mods;
    return Location::Other;
}
// Game folder = the host exe's directory (config, logs, world-cache, backends),
// never the proxy's own directory (mods\ on macOS). Without an exe path: the
// module directory minus a trailing mods\.
inline std::wstring gameRoot(const std::wstring& exePath,const std::wstring& selfPath){
    const std::wstring exeDir=directory(exePath);
    if(!exeDir.empty())return exeDir;
    std::wstring dir=directory(selfPath);
    if(dir.size()>=5&&fold(dir.substr(dir.size()-5))==L"mods\\")dir=directory(dir.substr(0,dir.size()-1));
    return dir;
}
// dlls.txt (WoWSilicon libDllLdr preload list) lists the macOS proxy entry.
inline bool isProxyEntry(std::string line){
    while(!line.empty()&&(line.back()=='\r'||line.back()==' '||line.back()=='\t'))line.pop_back();
    size_t s=0;while(s<line.size()&&(line[s]==' '||line[s]=='\t'))++s;line.erase(0,s);
    for(auto& c:line)c=c=='\\'?'/':char(tolower((unsigned char)c));
    return line=="mods/d3d9.dll";
}
inline bool dllsListed(const std::string& content){
    size_t start=0;
    while(start<=content.size()){
        const size_t end=content.find('\n',start);
        if(isProxyEntry(content.substr(start,end==std::string::npos?std::string::npos:end-start)))return true;
        if(end==std::string::npos)break;start=end+1;
    }
    return false;
}
inline const char* name(Location l){return l==Location::Root?"game-folder (LoadLibrary d3d9.dll)":l==Location::Mods?"mods (dlls.txt preload)":"other directory";}
// The game-folder d3d9.dll is this proxy, so no default path points there, and
// no backend but the system runtime keeps the d3d9.dll base name.
// legacy = the game folder's previous d3d9.dll, moved aside by the installer.
inline std::wstring defaultPath(Kind k,const std::wstring& root,const std::wstring& systemDir){
    switch(k){
    case Kind::Native:return systemDir.empty()?std::wstring():systemDir+L"\\d3d9.dll";
    case Kind::Dxvk:return root+L"renderer-backends\\dxvk\\dxvk_d3d9.dll";
    case Kind::Dxvk2:return root+L"renderer-backends\\dxvk2\\dxvk2_d3d9.dll"; // distinct base name: two DXVKs never share a module name
    case Kind::Legacy:return root+L"renderer-backends\\legacy\\legacy_d3d9.dll";
    default:return std::wstring();}
}
// [Renderer] BackendPath: absolute (X:\..., \\server\...) or relative to the
// client folder; optional quotes, '/' accepted. Empty = the kind's default.
inline std::wstring overridePath(const wchar_t* text,const std::wstring& root){
    if(!text)return std::wstring();
    std::wstring v(text);
    while(!v.empty()&&iswspace(v.back()))v.pop_back();
    size_t s=0;while(s<v.size()&&iswspace(v[s]))++s;v.erase(0,s);
    if(v.size()>=2&&v.front()==L'"'&&v.back()==L'"')v=v.substr(1,v.size()-2);
    for(auto& c:v)if(c==L'/')c=L'\\';
    if(v.empty())return v;
    const bool absolute=(v.size()>=3&&iswalpha(v[0])&&v[1]==L':'&&v[2]==L'\\')||v.rfind(L"\\\\",0)==0;
    return absolute?v:root+v;
}
// Load candidate: BackendPath, else the kind's default (none for Invalid).
inline std::vector<std::wstring> candidates(Kind k,const std::wstring& overridden,const std::wstring& root,const std::wstring& systemDir){
    if(k==Kind::Invalid)return {};
    if(!overridden.empty())return {overridden};
    std::wstring path=defaultPath(k,root,systemDir);
    return path.empty()?std::vector<std::wstring>{}:std::vector<std::wstring>{path};
}
}
