// Standalone build tool. This program never loads the game or a rendering device.
// It compiles with Microsoft's d3dcompiler_47.dll 10.0.17134.12 (x64) placed next to the
// executable (shader_compiler.py pins its SHA-256) and refuses any other module, so Wine's
// builtin reimplementation can never stand in for it.
#include <windows.h>
#include <d3dcompiler.h>
#include "shader_inputs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#ifndef NORTHLIGHT_COMPILER_VERSION_MS
#define NORTHLIGHT_COMPILER_VERSION_MS MAKELONG(0,10)
#define NORTHLIGHT_COMPILER_VERSION_LS MAKELONG(12,17134)
#endif
// The module must be the file next to this executable, a real PE (not a Wine builtin or
// placeholder, which carry "Wine " in the DOS stub) and FileVersion 10.0.17134.12.
static bool microsoft_compiler(HMODULE module, const wchar_t *expected) {
    wchar_t loaded[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(module, loaded, MAX_PATH * 4);
    if (!length || length >= MAX_PATH * 4 || _wcsicmp(loaded, expected) != 0) {
        std::fprintf(stderr, "d3dcompiler_47 loaded from %ls, expected %ls\n", length ? loaded : L"?", expected);
        return false;
    }
    if (std::memcmp(reinterpret_cast<const char *>(module) + 0x40, "Wine ", 5) == 0) {
        std::fprintf(stderr, "d3dcompiler_47 is Wine's builtin, not Microsoft's\n");
        return false;
    }
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));   // RT_VERSION
    const unsigned char *data = info ? static_cast<const unsigned char *>(LockResource(LoadResource(module, info))) : nullptr;
    DWORD size = info ? SizeofResource(module, info) : 0;
    for (DWORD i = 0; data && i + sizeof(VS_FIXEDFILEINFO) <= size; i += 4) {
        VS_FIXEDFILEINFO fixed;
        std::memcpy(&fixed, data + i, sizeof fixed);
        if (fixed.dwSignature != 0xFEEF04BD) continue;
        if (fixed.dwFileVersionMS == NORTHLIGHT_COMPILER_VERSION_MS && fixed.dwFileVersionLS == NORTHLIGHT_COMPILER_VERSION_LS) return true;
        std::fprintf(stderr, "d3dcompiler_47 FileVersion %lu.%lu.%lu.%lu, expected configured checksum-pinned Microsoft profile\n",
            (unsigned long)HIWORD(fixed.dwFileVersionMS), (unsigned long)LOWORD(fixed.dwFileVersionMS),
            (unsigned long)HIWORD(fixed.dwFileVersionLS), (unsigned long)LOWORD(fixed.dwFileVersionLS));
        return false;
    }
    std::fprintf(stderr, "d3dcompiler_47 has no version resource\n");
    return false;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: compile_shaders.exe input.hlsl entry target output.bin\n");
        return 2;
    }
    wchar_t path[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH * 4);
    wchar_t *slash = length && length < MAX_PATH * 4 ? std::wcsrchr(path, L'\\') : nullptr;
    if (!slash || std::wcslen(path) + 20 >= MAX_PATH * 4) return 4;
    std::wcscpy(slash + 1, L"d3dcompiler_47.dll");
    HMODULE compiler = LoadLibraryW(path);
    if (!compiler) { std::fprintf(stderr, "LoadLibrary %ls: %lu\n", path, GetLastError()); return 4; }
    if (!microsoft_compiler(compiler, path)) { FreeLibrary(compiler); return 4; }
    FILE *input = std::fopen(argv[1], "rb");
    if (!input) { std::perror(argv[1]); return 3; }
    std::fseek(input, 0, SEEK_END);
    long size = std::ftell(input);
    std::rewind(input);
    if (size <= 0) { std::fclose(input); return 3; }
    char *source = static_cast<char *>(std::malloc(static_cast<size_t>(size)));
    if (!source || std::fread(source, 1, size, input) != static_cast<size_t>(size)) {
        std::fclose(input); std::free(source); return 3;
    }
    std::fclose(input);
    auto compile = reinterpret_cast<pD3DCompile>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) { std::free(source); FreeLibrary(compiler); return 4; }
    ID3DBlob *shader = nullptr, *diagnostics = nullptr;
    ShaderInputs inputs(argv[1],source,static_cast<size_t>(size));
    HRESULT result = compile(source, size, argv[1], nullptr, &inputs, argv[2], argv[3],
        D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_IEEE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &shader, &diagnostics);
    std::free(source);
    if (diagnostics) {
        std::fwrite(diagnostics->GetBufferPointer(), 1, diagnostics->GetBufferSize(), stderr);
        diagnostics->Release();
    }
    if (FAILED(result) || !shader) {
        std::fprintf(stderr, "D3DCompile %s/%s failed: 0x%08lx\n", argv[2], argv[3], static_cast<unsigned long>(result));
        FreeLibrary(compiler); return 5;
    }
    FILE *output = std::fopen(argv[4], "wb");
    if (!output) { std::perror(argv[4]); shader->Release(); FreeLibrary(compiler); return 6; }
    size_t written = std::fwrite(shader->GetBufferPointer(), 1, shader->GetBufferSize(), output);
    int close_result = std::fclose(output);
    bool success = written == shader->GetBufferSize() && close_result == 0 && inputs.Write(std::string(argv[4])+".inputs");
    using Disassemble = HRESULT (WINAPI *)(const void *, SIZE_T, UINT, const char *, ID3DBlob **);
    auto disassemble = reinterpret_cast<Disassemble>(GetProcAddress(compiler, "D3DDisassemble"));
    ID3DBlob *assembly = nullptr;
    if (disassemble && SUCCEEDED(disassemble(shader->GetBufferPointer(), shader->GetBufferSize(),
            D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING, nullptr, &assembly)) && assembly) {
        char asm_path[MAX_PATH * 4];
        std::snprintf(asm_path, sizeof(asm_path), "%s.asm", argv[4]);
        FILE *asm_file = std::fopen(asm_path, "wb");
        if (asm_file) {
            std::fwrite(assembly->GetBufferPointer(), 1, assembly->GetBufferSize(), asm_file);
            std::fclose(asm_file);
        }
        assembly->Release();
    }
    std::printf("%s %s: %zu bytes\n", argv[2], argv[3], static_cast<size_t>(shader->GetBufferSize()));
    shader->Release(); FreeLibrary(compiler);
    return success ? 0 : 6;
}
