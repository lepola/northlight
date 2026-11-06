#pragma once
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

// Renderer narrow paths are UTF-8; Windows CRT narrow file APIs use the system code page.
namespace NorthlightFiles {
inline FILE* open(const char* path,const char* mode){
#ifdef _WIN32
    const auto native=std::filesystem::u8path(path);
    const std::wstring wideMode(mode,mode+std::strlen(mode));
    return _wfopen(native.c_str(),wideMode.c_str());
#else
    return std::fopen(path,mode);
#endif
}
}
