#pragma once

#include <d3d9.h>

// Generated beside bytecode from the compiler's disassembly. Slot usage is static;
// it does not describe execution time or instructions executed by dynamic loops.
struct ShaderRequirements
{
    DWORD version;
    UINT temporaries, samplerRegisters, instructionSlots;

    bool Supported(const D3DCAPS9& caps) const
    {
        const bool pixel = (version >> 16) == 0xffff;
        const DWORD available = pixel ? caps.PixelShaderVersion : caps.VertexShaderVersion;
        const DWORD slots = pixel ? caps.MaxPixelShader30InstructionSlots : caps.MaxVertexShader30InstructionSlots;
        // Both SM3 profiles provide 32 temporaries. PS20Caps describes SM2 and is
        // not the capacity of an SM3 shader. Vertex sampling additionally needs a
        // supported texture format, checked by the resource owner when applicable.
        return (available & 0xffff) >= (version & 0xffff) && temporaries <= 32 &&
               samplerRegisters <= (pixel ? 16u : 4u) && instructionSlots <= slots;
    }
};

struct ShaderProgram
{
    const DWORD* code;
    const char* name;
    ShaderRequirements requirements;

    template<class Shader> HRESULT Create(IDirect3DDevice9* device, Shader** output) const
    {
        D3DCAPS9 caps = {};
        if (FAILED(device->GetDeviceCaps(&caps)) || !requirements.Supported(caps))
            return D3DERR_NOTAVAILABLE;
        // The caller owns reporting/retry policy. This contract is also used by
        // the host's shared depth converter, which has its own logging service.
        return CreateOnDevice(device, output);
    }

private:
    HRESULT CreateOnDevice(IDirect3DDevice9* device, IDirect3DPixelShader9** output) const
    { return device->CreatePixelShader(code, output); }
    HRESULT CreateOnDevice(IDirect3DDevice9* device, IDirect3DVertexShader9** output) const
    { return device->CreateVertexShader(code, output); }
};
