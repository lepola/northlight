// Compiles the renderer's shaders with Microsoft's D3DCompiler (run under Wine; see fetch_shader_compiler.sh).
// Usage: compile_shaders.exe <D3DCompiler dll> <list> <output dir>
// Each line of the list is "<name>\t<source>\t<profile>" (paths may hold spaces); writes <output dir>/<name>.bin and <name>.asm (its
// disassembly). Includes are found next to the file that includes them.
#include <windows.h>
#include <d3dcompiler.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <list>

namespace
{
DWORD WINAPI CompileTimeout(void* seconds)
{
    Sleep(static_cast<DWORD>(reinterpret_cast<uintptr_t>(seconds)) * 1000);
    std::fputs("shader compiler exceeded its configured time limit\n", stderr);
    TerminateProcess(GetCurrentProcess(), 124);
    return 124;
}

bool SetCompileTimeout()
{
    char value[32] = {};
    const DWORD length = GetEnvironmentVariableA("WATER_SHADER_TIMEOUT_SECONDS", value, sizeof(value));
    if (!length) return true;
    if (length >= sizeof(value)) return false;
    char* end = nullptr;
    const auto seconds = std::strtoul(value, &end, 10);
    if (!end || *end || seconds == 0 || seconds > 3600) return false;
    HANDLE thread = CreateThread(nullptr, 0, CompileTimeout, reinterpret_cast<void*>(static_cast<uintptr_t>(seconds)), 0, nullptr);
    if (!thread) return false;
    CloseHandle(thread);
    return true;
}

std::string Directory(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

bool ReadFile(const std::string& path, std::string& contents)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream text;
    text << file.rdbuf();
    contents = text.str();
    return true;
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool ok = std::fwrite(data, 1, size, file) == size;
    return std::fclose(file) == 0 && ok;
}

// Record the bytes actually handed to D3DCompile, including transitive includes.
// The Python publisher compares these against the pre-build snapshot and disk.
std::string Digest(const std::string& text)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[32] = {};
    DWORD length = sizeof(digest);
    bool ok = CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) &&
              CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) &&
              CryptHashData(hash, reinterpret_cast<const BYTE*>(text.data()), static_cast<DWORD>(text.size()), 0) &&
              CryptGetHashParam(hash, HP_HASHVAL, digest, &length, 0);
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    if (!ok) return {};
    std::string result;
    for (BYTE value : digest) {
        result += "0123456789abcdef"[value >> 4];
        result += "0123456789abcdef"[value & 15];
    }
    return result;
}

// Opens each include next to the file that includes it (the compiler says which by the data it was given).
class Includes : public ID3DInclude
{
public:
    explicit Includes(const std::string& mainDirectory) : m_mainDirectory(mainDirectory) {}

    bool Record(const std::string& path, const std::string& text)
    {
        const std::string hash = Digest(text);
        if (hash.empty()) return false;
        records += path + "\t" + hash + "\n";
        return true;
    }
    std::string records;

    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parentData, LPCVOID* data, UINT* bytes) override
    {
        std::string directory = m_mainDirectory;
        for (const Opened& opened : m_opened)
            if (opened.text.data() == parentData)
                directory = opened.directory;
        const std::string path = directory + name;
        Opened opened{path, Directory(path), {}};
        if (!ReadFile(path, opened.text) || !Record(path, opened.text))
        {
            std::fprintf(stderr, "cannot open include %s\n", path.c_str());
            return E_FAIL;
        }
        m_opened.push_back(std::move(opened));
        *data = m_opened.back().text.data();
        *bytes = static_cast<UINT>(m_opened.back().text.size());
        return S_OK;
    }

    HRESULT __stdcall Close(LPCVOID) override { return S_OK; }

private:
    struct Opened
    {
        std::string path;
        std::string directory;
        std::string text;
    };
    std::string m_mainDirectory;
    // Kept for the whole compile: the compiler holds on to the text it was given.
    std::list<Opened> m_opened;
};
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr, "usage: compile_shaders.exe <D3DCompiler dll> <list> <output dir>\n");
        return 2;
    }
    if (!SetCompileTimeout())
    {
        std::fputs("invalid or unavailable shader compiler time limit\n", stderr);
        return 2;
    }
    HMODULE compiler = LoadLibraryA(argv[1]);
    if (!compiler)
    {
        std::fprintf(stderr, "cannot load %s (error %lu)\n", argv[1], GetLastError());
        return 3;
    }
    const auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
    const auto disassemble = reinterpret_cast<pD3DDisassemble>(GetProcAddress(compiler, "D3DDisassemble"));
    if (!compile || !disassemble)
    {
        std::fprintf(stderr, "%s has no D3DCompile or D3DDisassemble\n", argv[1]);
        return 3;
    }
    char compilerPath[32768] = {};
    std::string compilerBytes;
    if (!GetModuleFileNameA(compiler, compilerPath, sizeof(compilerPath)) || !ReadFile(compilerPath, compilerBytes))
        return 3;
    std::ifstream list(argv[2]);
    if (!list)
    {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 2;
    }
    const std::string output = std::string(argv[3]) + "\\";
    int failures = 0;
    std::string line;
    while (std::getline(list, line))
    {
        std::istringstream fields(line);
        std::string name, source, profile, definitions;
        if (!std::getline(fields, name, '\t') || !std::getline(fields, source, '\t') || !std::getline(fields, profile, '\t'))
            continue;
        std::getline(fields, definitions);
        std::vector<std::string> names, values;
        std::istringstream options(definitions);
        std::string option;
        while (std::getline(options, option, ';'))
        {
            const auto equals = option.find('=');
            names.push_back(option.substr(0, equals));
            values.push_back(equals == std::string::npos ? "1" : option.substr(equals + 1));
        }
        std::vector<D3D_SHADER_MACRO> macros;
        for (size_t i = 0; i < names.size(); ++i)
            macros.push_back({names[i].c_str(), values[i].c_str()});
        macros.push_back({nullptr, nullptr});
        std::string text;
        if (!ReadFile(source, text))
        {
            std::fprintf(stderr, "%s: cannot open %s\n", name.c_str(), source.c_str());
            ++failures;
            continue;
        }
        Includes includes(Directory(source));
        if (!includes.Record(source, text) || !includes.Record(compilerPath, compilerBytes)) { ++failures; continue; }
        ID3DBlob* code = nullptr;
        ID3DBlob* messages = nullptr;
        const HRESULT result = compile(text.data(), text.size(), source.c_str(), macros.data(), &includes, "main",
                                       profile.c_str(), D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &messages);
        if (messages)
        {
            std::fprintf(stderr, "%s", static_cast<const char*>(messages->GetBufferPointer()));
            messages->Release();
        }
        ID3DBlob* listing = nullptr;
        const bool ok = SUCCEEDED(result) && code &&
                        WriteFile(output + name + ".bin", code->GetBufferPointer(), code->GetBufferSize()) &&
                        SUCCEEDED(disassemble(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &listing)) &&
                        WriteFile(output + name + ".asm", listing->GetBufferPointer(),
                                  std::strlen(static_cast<const char*>(listing->GetBufferPointer()))) &&
                        WriteFile(output + name + ".inputs", includes.records.data(), includes.records.size());
        if (!ok)
        {
            std::fprintf(stderr, "%s: failed (0x%08lx)\n", name.c_str(), static_cast<unsigned long>(result));
            ++failures;
        }
        if (listing)
            listing->Release();
        if (code)
            code->Release();
    }
    return failures ? 1 : 0;
}
