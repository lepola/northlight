#pragma once
#include <d3dcompiler.h>
#include <wincrypt.h>
#include <fstream>
#include <list>
#include <sstream>
#include <string>

// Keep include buffers stable until D3DCompile returns, and record the actual
// bytes read. Python compares these hashes with its pre-compilation snapshot.
class ShaderInputs : public ID3DInclude {
    struct Input { std::string path, text; };
    std::list<Input> inputs;
    std::string directory;
    static std::string Parent(const std::string& path) {
        const auto slash=path.find_last_of("/\\");
        return slash==std::string::npos ? "" : path.substr(0,slash+1);
    }
    static std::string Hash(const std::string& bytes) {
        HCRYPTPROV provider=0;HCRYPTHASH hash=0;BYTE digest[32]={};DWORD size=sizeof digest;
        const bool ok=CryptAcquireContextA(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)&&
            CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)&&
            CryptHashData(hash,reinterpret_cast<const BYTE*>(bytes.data()),static_cast<DWORD>(bytes.size()),0)&&
            CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0);
        if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);
        if(!ok)return {};
        std::string result;for(BYTE value:digest){result+="0123456789abcdef"[value>>4];result+="0123456789abcdef"[value&15];}
        return result;
    }
public:
    explicit ShaderInputs(const char* path,const char* source,size_t size):directory(Parent(path)) {
        inputs.push_back({path,std::string(source,size)});
    }
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE,LPCSTR name,LPCVOID parent,LPCVOID* data,UINT* bytes) override {
        std::string folder=directory;
        for(const auto& input:inputs)if(input.text.data()==parent)folder=Parent(input.path);
        const std::string path=folder+name;std::ifstream file(path,std::ios::binary);
        if(!file)return E_FAIL;
        std::ostringstream text;text<<file.rdbuf();inputs.push_back({path,text.str()});
        *data=inputs.back().text.data();*bytes=static_cast<UINT>(inputs.back().text.size());return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID) override {return S_OK;}
    bool Write(const std::string& path) const {
        std::ofstream file(path,std::ios::binary);
        if(!file)return false;
        for(const auto& input:inputs){const auto hash=Hash(input.text);if(hash.empty())return false;file<<input.path<<'\t'<<hash<<'\n';}
        file.close();return bool(file);
    }
};
