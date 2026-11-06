#pragma once
#include <windows.h>
#include <cwchar>
#include <cstring>
#include <map>
#include <string>

// A one-shot check before the backend is loaded, never a draw-time poll.
namespace NorthlightArtInputs {
inline std::wstring lower(std::wstring value){
    for(auto& c:value){if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';if(c==L'/')c=L'\\';}return value;
}
inline std::wstring setting(const std::wstring& file,const wchar_t* section,const wchar_t* key){
    wchar_t value[1024]={};const DWORD n=GetPrivateProfileStringW(section,key,L"",value,1024,file.c_str());
    return n<1023?std::wstring(value,n):std::wstring();
}
inline bool number(const std::wstring& text,unsigned long long& value){
    if(text.empty()||text.size()>20)return false;
    value=0;for(auto c:text){if(c<L'0'||c>L'9')return false;const auto digit=unsigned(c-L'0');
        if(value>(~0ull-digit)/10)return false;value=value*10+digit;}return true;
}
struct Stamp {unsigned long long size=0,modified=0;};
inline bool stat(const std::wstring& path,Stamp& stamp){
    WIN32_FILE_ATTRIBUTE_DATA data={};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)||(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    stamp.size=(static_cast<unsigned long long>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
    stamp.modified=(static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;
    return true;
}
inline bool loaded(std::wstring name,const std::wstring& locale,wchar_t own,bool stock){
    name=lower(name);
    if(name.size()<5||name.substr(name.size()-4)!=L".mpq")return false;
    name.resize(name.size()-4);
    if(locale.empty()){
        if(name==L"common"||name==L"common-2"||name==L"expansion"||name==L"lichking")return true;
    }else if(name==L"locale-"+locale||name==L"expansion-locale-"+locale||name==L"lichking-locale-"+locale)return true;
    const std::wstring prefix=locale.empty()?L"patch":L"patch-"+locale;
    if(name==prefix)return true;
    if(name.size()!=prefix.size()+2||name.compare(0,prefix.size(),prefix)||name[prefix.size()]!=L'-')return false;
    const wchar_t suffix=name.back();
    return suffix!=own&&((suffix>=L'2'&&suffix<=L'3')||(!stock&&((suffix>=L'4'&&suffix<=L'9')||(suffix>=L'a'&&suffix<=L'z'))));
}
inline bool inventory(const std::wstring& root,const std::wstring& folder,const std::wstring& locale,wchar_t own,bool stock,
                      std::map<std::wstring,Stamp>& files){
    WIN32_FIND_DATAW data={};HANDLE handle=FindFirstFileW((root+folder+L"\\*.mpq").c_str(),&data);
    if(handle==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
    bool ok=true;
    do{
        if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||!loaded(data.cFileName,locale,own,stock))continue;
        const auto name=lower(folder+L"\\"+data.cFileName);Stamp stamp;
        if(!stat(root+name,stamp)){ok=false;break;}files[name]=stamp;
    }while(FindNextFileW(handle,&data));
    if(ok&&GetLastError()!=ERROR_NO_MORE_FILES)ok=false;
    FindClose(handle);return ok;
}
struct Result {const char* status="legacy-no-manifest";std::wstring path;bool blocked=false;};
inline Result check(std::wstring root){
    if(root.empty())return {"root-unavailable",L"",true};
    if(root.back()!=L'\\'&&root.back()!=L'/')root+=L'\\';
    const auto file=root+L"northlight-art-inputs.ini";
    const DWORD attributes=GetFileAttributesW(file.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){
        const auto error=GetLastError();
        return (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?Result{}:Result{"manifest-unreadable",file,true};
    }
    const auto locale=lower(setting(file,L"NorthlightAssets",L"Locale"));
    const auto letter=lower(setting(file,L"NorthlightAssets",L"Letter"));
    const auto view=setting(file,L"NorthlightAssets",L"View");unsigned long long count=0;
    if(setting(file,L"NorthlightAssets",L"Version")!=L"1"||locale.size()!=4||letter.size()!=1||letter[0]<L'a'||letter[0]>L'z'||
       (view!=L"all"&&view!=L"stock")||!number(setting(file,L"NorthlightAssets",L"Count"),count)||!count||count>256)
        return {"manifest-invalid",file,true};
    for(auto c:locale)if(c<L'a'||c>L'z')return {"manifest-invalid",file,true};
    std::map<std::wstring,Stamp> actual;
    if(!inventory(root,L"Data",L"",letter[0],view==L"stock",actual)||
       !inventory(root,L"Data\\"+locale,locale,letter[0],view==L"stock",actual))return {"archives-unreadable",L"Data",true};
    for(unsigned long long i=0;i<count;++i){
        const auto section=L"Archive"+std::to_wstring(i);
        const auto path=lower(setting(file,section.c_str(),L"Path"));Stamp expected;
        if(path.empty()||path.find(L"..")!=std::wstring::npos||path.front()==L'\\'||path.find(L':')!=std::wstring::npos||
           !number(setting(file,section.c_str(),L"Size"),expected.size)||!number(setting(file,section.c_str(),L"Modified"),expected.modified))
            return {"manifest-invalid",file,true};
        const auto found=actual.find(path);
        if(found==actual.end())return {"archive-removed",path,true};
        if(found->second.size!=expected.size||found->second.modified!=expected.modified)return {"archive-changed",path,true};
        actual.erase(found);
    }
    if(!actual.empty())return {"archive-added",actual.begin()->first,true};
    return {"current",L"",false};
}
inline std::wstring message(const Result& result,bool russian){
    const bool removed=std::strcmp(result.status,"archive-removed")==0;
    const bool added=std::strcmp(result.status,"archive-added")==0;
    const bool changed=std::strcmp(result.status,"archive-changed")==0;
    const std::wstring detail=(russian?(removed?L"Удалён архив: ":added?L"Добавлен архив: ":changed?L"Изменён архив: ":L"Проверка данных: "):
        (removed?L"Removed archive: ":added?L"Added archive: ":changed?L"Changed archive: ":L"Data check: "))+result.path;
    return russian?
        L"Набор MPQ изменился после сборки данных Northlight.\n\n"+detail+
        L"\n\nНужна пересборка: запустите установщик Northlight для этой папки игры. Дождитесь завершения и снова запустите WoW.\n\nИгра будет закрыта до загрузки несовместимых моделей.":
        L"The MPQ files have changed since Northlight data was built.\n\n"+detail+
        L"\n\nRebuild required: run the Northlight installer for this game folder, let it finish, then start WoW again.\n\nThe game will close before incompatible models are loaded.";
}
} // namespace NorthlightArtInputs
