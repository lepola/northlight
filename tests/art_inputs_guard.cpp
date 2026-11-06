#include "art_inputs_guard.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;
    const auto result=NorthlightArtInputs::check(argv[1]);
    std::printf("%s blocked=%u\n",result.status,unsigned(result.blocked));
    wchar_t expected[64]={};std::mbstowcs(expected,result.status,63);
    if(std::wcscmp(expected,argv[2]))return 3;
    const auto english=NorthlightArtInputs::message(result,false);
    const auto russian=NorthlightArtInputs::message(result,true);
    if(english.find(L"Rebuild required")==std::wstring::npos||russian.find(L"пересборка")==std::wstring::npos)return 4;
    return 0;
}
