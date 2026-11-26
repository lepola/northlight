#include "reflection/reflection_capture.h"
#include <cstddef>
#include "screen_pass.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include "ps_reflection_capture_copy.h"
#include "vs_fullscreen.h"

namespace reflection
{
namespace
{
template<class T> void Release(T*& value) { if (value) { value->Release(); value = nullptr; } }
bool RawDepthFormat(D3DFORMAT format)
{
    return format==static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')) ||
           format==static_cast<D3DFORMAT>(MAKEFOURCC('D','F','2','4'));
}
UINT PixelBytes(D3DFORMAT format)
{
    switch (format)
    {
        case D3DFMT_R32F: case D3DFMT_G16R16F: case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
        case D3DFMT_A16B16G16R16F: case D3DFMT_G32R32F: return 8;
        case D3DFMT_A32B32G32R32F: return 16;
        default: return 0;
    }
}
bool Write(const std::string& path, const void* data, size_t bytes)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(data, 1, bytes, file) == bytes;
    return std::fclose(file) == 0 && ok;
}
// Capture conversion is enclosed in a full state block and explicit target/depth
// restoration. D3D9 state blocks do not include render targets or depth surfaces.
struct Saved
{
    IDirect3DDevice9* dev;
    IDirect3DStateBlock9* state = nullptr;
    IDirect3DSurface9* targets[4] = {};
    IDirect3DSurface9* depth = nullptr;
    bool valid;
    explicit Saved(IDirect3DDevice9* device) : dev(device), valid(SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &state)))
    {
        for (DWORD i = 0; i < 4; ++i) dev->GetRenderTarget(i, &targets[i]);
        dev->GetDepthStencilSurface(&depth);
    }
    ~Saved()
    {
        if (!valid) { Release(state); Release(depth); for (auto& target : targets) Release(target); return; }
        for (DWORD i = 1; i < 4; ++i) dev->SetRenderTarget(i, nullptr);
        dev->SetRenderTarget(0, targets[0]);
        for (DWORD i = 1; i < 4; ++i) dev->SetRenderTarget(i, targets[i]);
        dev->SetDepthStencilSurface(depth);
        if (state) state->Apply();
        Release(state); Release(depth);
        for (auto& target : targets) Release(target);
    }
};
// Readback may begin at a client draw boundary, not inside our fullscreen
// passes. Own the vertex layout and all rejection/colour-conversion state.
// Saved encloses every caller and restores the original game bindings.
bool CopyState(IDirect3DDevice9* dev, IDirect3DPixelShader9* shader, IDirect3DTexture9* texture)
{
    IDirect3DVertexShader9* vs=nullptr;
    IDirect3DVertexDeclaration9* declaration=nullptr;
    const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    bool ok=SUCCEEDED(p_vs_fullscreen.Create(dev,&vs)) &&
        SUCCEEDED(dev->CreateVertexDeclaration(elements,&declaration));
    if(ok) ok=SetScreenPassState(dev,vs,declaration);
    Release(vs);Release(declaration);
    ok=SUCCEEDED(dev->SetPixelShader(shader)) && SUCCEEDED(dev->SetTexture(0,texture)) && ok;
    ok=SUCCEEDED(dev->SetRenderState(D3DRS_DITHERENABLE,FALSE)) && ok;
    const struct { D3DSAMPLERSTATETYPE name; DWORD value; } samplers[]={
        {D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},
        {D3DSAMP_MIPFILTER,D3DTEXF_POINT},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0},
        {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP}};
    for(const auto& state:samplers) ok=SUCCEEDED(dev->SetSamplerState(0,state.name,state.value)) && ok;
    return ok;
}
}

ProjectionAudit::~ProjectionAudit()
{
    for (auto& texture : m_textures) Release(texture);
    Release(m_vs); Release(m_ps);
}
void ProjectionAudit::CaptureOnce(IDirect3DDevice9* dev)
{
    if (m_captured) return;
    m_captured = true;
    m_ok = SUCCEEDED(dev->GetViewport(&m_viewport)) &&
        SUCCEEDED(dev->GetVertexShaderConstantF(0,&m_uniforms[0][0],9)) &&
        SUCCEEDED(dev->GetVertexShader(&m_vs)) && m_vs &&
        SUCCEEDED(dev->GetPixelShader(&m_ps)) && m_ps;
    const D3DRENDERSTATETYPE states[] = {
        D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,
        D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_TWOSIDEDSTENCILMODE,D3DRS_CULLMODE,
        D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_COLORWRITEENABLE,
        D3DRS_COLORWRITEENABLE1,D3DRS_DITHERENABLE,D3DRS_POINTSPRITEENABLE,D3DRS_POINTSCALEENABLE,
        D3DRS_POINTSIZE,D3DRS_POINTSIZE_MIN,D3DRS_POINTSIZE_MAX,D3DRS_POINTSCALE_A,D3DRS_POINTSCALE_B,
        D3DRS_POINTSCALE_C,D3DRS_CLIPPLANEENABLE,D3DRS_FILLMODE,D3DRS_CLIPPING,D3DRS_LASTPIXEL,
        D3DRS_ANTIALIASEDLINEENABLE,D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,
        D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS};
    static_assert(sizeof(states)/sizeof(states[0])==32,"projection audit state count");
    for (UINT i=0;i<32;++i)
    {
        m_states[i][0]=states[i];
        m_ok = SUCCEEDED(dev->GetRenderState(states[i],&m_states[i][1])) && m_ok;
    }
    const DWORD stages[] = {0,D3DVERTEXTEXTURESAMPLER0,D3DVERTEXTEXTURESAMPLER1};
    for (UINT i=0;i<3;++i)
    {
        IDirect3DBaseTexture9* texture = nullptr;
        bool bound = SUCCEEDED(dev->GetTexture(stages[i],&texture)) && texture && texture->GetType()==D3DRTYPE_TEXTURE;
        if (bound) { m_textures[i]=static_cast<IDirect3DTexture9*>(texture); texture=nullptr; }
        Release(texture);
        m_ok = bound && m_ok;
        if (bound) m_ok = SUCCEEDED(m_textures[i]->GetLevelDesc(0,&m_inputs[i])) &&
            !(m_inputs[i].Usage & (D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL)) && m_ok;
        m_samplers[i][0]=stages[i];
        for (DWORD state=1;state<=13;++state)
            m_ok = SUCCEEDED(dev->GetSamplerState(stages[i],static_cast<D3DSAMPLERSTATETYPE>(state),&m_samplers[i][state])) && m_ok;
    }
    for (DWORD i=0;i<2;++i)
    {
        IDirect3DSurface9* target = nullptr;
        const HRESULT result = dev->GetRenderTarget(i,&target);
        const bool bound = SUCCEEDED(result) && target;
        // A geometry-only point draw has one attachment. Preserve a zero
        // descriptor for an explicitly absent RT1; other getter errors fail.
        m_ok = (bound || (i==1 && result==D3DERR_NOTFOUND)) && m_ok;
        if (bound) m_ok = SUCCEEDED(target->GetDesc(&m_targets[i])) && m_ok;
        Release(target);
    }
    IDirect3DSurface9* depth = nullptr;
    const bool depthBound = SUCCEEDED(dev->GetDepthStencilSurface(&depth)) && depth;
    m_ok = depthBound && m_ok;
    if (depthBound) m_ok = SUCCEEDED(depth->GetDesc(&m_depth)) && m_ok;
    Release(depth);
    IDirect3DVertexBuffer9* stream = nullptr;
    const bool streamBound = SUCCEEDED(dev->GetStreamSource(0,&stream,&m_offset,&m_stride)) && stream;
    m_ok = streamBound && SUCCEEDED(dev->GetStreamSourceFreq(0,&m_frequency)) && m_ok;
    if (streamBound) m_ok = SUCCEEDED(stream->GetDesc(&m_stream)) && m_ok;
    Release(stream);
    IDirect3DVertexDeclaration9* declaration = nullptr;
    const bool declared = SUCCEEDED(dev->GetVertexDeclaration(&declaration)) && declaration;
    m_ok = declared && m_ok;
    if (declared) m_ok = SUCCEEDED(declaration->GetDeclaration(m_elements,&m_declCount)) && m_declCount<=64 && m_ok;
    Release(declaration);
    D3DCAPS9 caps = {};
    m_ok = SUCCEEDED(dev->GetDeviceCaps(&caps)) && m_ok;
    m_maxPrimitiveCount=caps.MaxPrimitiveCount;
}
bool ProjectionAudit::Save(IDirect3DDevice9* dev, const std::string& directory)
{
    if (!m_captured || !m_ok || !CreateDirectoryA(directory.c_str(),nullptr)) return false;
    FILE* file = std::fopen((directory+"/state.json").c_str(),"wb");
    if (!file) return false;
    std::fprintf(file,"{\n\"version\":1,\n\"viewport\":[%lu,%lu,%lu,%lu,%.9g,%.9g],\n\"vertex_uniforms\":[",
        m_viewport.X,m_viewport.Y,m_viewport.Width,m_viewport.Height,m_viewport.MinZ,m_viewport.MaxZ);
    for (UINT i=0;i<9;++i) std::fprintf(file,"%s[%.9g,%.9g,%.9g,%.9g]",i ? "," : "",m_uniforms[i][0],m_uniforms[i][1],m_uniforms[i][2],m_uniforms[i][3]);
    std::fprintf(file,"],\n\"render_states\":[");
    for (UINT i=0;i<32;++i) std::fprintf(file,"%s[%lu,%lu]",i ? "," : "",static_cast<unsigned long>(m_states[i][0]),static_cast<unsigned long>(m_states[i][1]));
    std::fprintf(file,"],\n\"samplers\":[");
    for (UINT i=0;i<3;++i)
    {
        std::fprintf(file,"%s[",i ? "," : "");
        for (UINT j=0;j<14;++j) std::fprintf(file,"%s%lu",j ? "," : "",static_cast<unsigned long>(m_samplers[i][j]));
        std::fprintf(file,"]");
    }
    auto descriptor = [&](const D3DSURFACE_DESC& d) {
        std::fprintf(file,"[%u,%u,%lu,%u,%u,%lu,%u,%u]",static_cast<UINT>(d.Format),static_cast<UINT>(d.Type),
            static_cast<unsigned long>(d.Usage),static_cast<UINT>(d.Pool),static_cast<UINT>(d.MultiSampleType),
            static_cast<unsigned long>(d.MultiSampleQuality),d.Width,d.Height);
    };
    std::fprintf(file,"],\n\"inputs\":[");
    for (UINT i=0;i<3;++i) { if (i) std::fprintf(file,","); descriptor(m_inputs[i]); }
    std::fprintf(file,"],\n\"targets\":["); descriptor(m_targets[0]); std::fprintf(file,","); descriptor(m_targets[1]);
    std::fprintf(file,"],\n\"depth\":"); descriptor(m_depth);
    std::fprintf(file,",\n\"stream\":[%u,%u,%u,%u,%lu,%u,%u,%u],\n\"max_primitive_count\":%u,\n\"declaration\":[",
        m_offset,m_stride,m_frequency,static_cast<UINT>(m_stream.Format),static_cast<unsigned long>(m_stream.Usage),
        static_cast<UINT>(m_stream.Pool),m_stream.Size,static_cast<UINT>(m_stream.Type),m_maxPrimitiveCount);
    for (UINT i=0;i<m_declCount;++i) std::fprintf(file,"%s[%u,%u,%u,%u,%u,%u]",i ? "," : "",
        m_elements[i].Stream,m_elements[i].Offset,m_elements[i].Type,m_elements[i].Method,m_elements[i].Usage,m_elements[i].UsageIndex);
    std::fprintf(file,"]\n}\n");
    bool ok = !std::ferror(file); ok = std::fclose(file)==0 && ok;
    // Only immutable input readback happens here, after all producer draws.
    for (UINT i=0;i<3 && ok;++i) ok = SaveTexture(dev,m_textures[i],directory+"/input-"+std::to_string(i)+".texture");
    auto program = [&](auto* shader,const char* name) {
        UINT bytes = 0;
        if (FAILED(shader->GetFunction(nullptr,&bytes)) || !bytes || bytes>1024*1024) return false;
        std::vector<unsigned char> code(bytes);
        return SUCCEEDED(shader->GetFunction(code.data(),&bytes)) && Write(directory+"/"+name,code.data(),bytes);
    };
    ok = program(m_vs,"vertex-shader.bin") && program(m_ps,"pixel-shader.bin") && ok;
    return ok && Write(directory+"/complete.ok","ok\n",3);
}

bool CaptureBurst::Reserve(uint64_t bytes)
{
    if (m_failed || bytes > m_budget - m_bytes) { m_failed = true; return false; }
    m_bytes += bytes;
    return true;
}
bool CaptureBurst::QueueSurface(IDirect3DDevice9* dev, IDirect3DSurface9* surface, const std::string& path)
{
    LARGE_INTEGER start, end, frequency; QueryPerformanceCounter(&start); QueryPerformanceFrequency(&frequency);
    D3DSURFACE_DESC desc = {}; IDirect3DTexture9* copy = nullptr; IDirect3DSurface9* destination = nullptr;
    bool ok = surface && SUCCEEDED(surface->GetDesc(&desc)) && PixelBytes(desc.Format) &&
        Reserve(uint64_t(desc.Width)*desc.Height*PixelBytes(desc.Format)) &&
        SUCCEEDED(dev->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,desc.Format,D3DPOOL_DEFAULT,&copy,nullptr)) &&
        SUCCEEDED(copy->GetSurfaceLevel(0,&destination)) &&
        SUCCEEDED(dev->StretchRect(surface,nullptr,destination,nullptr,D3DTEXF_NONE));
    Release(destination);
    if (ok) m_entries.push_back({path,copy}); else { Release(copy); m_failed = true; }
    QueryPerformanceCounter(&end); m_submitMs += double(end.QuadPart-start.QuadPart)*1000/frequency.QuadPart;
    return ok;
}
bool CaptureBurst::Queue(IDirect3DDevice9* dev, IDirect3DTexture9* texture, const std::string& path)
{
    LARGE_INTEGER start, end, frequency; QueryPerformanceCounter(&start); QueryPerformanceFrequency(&frequency);
    D3DSURFACE_DESC desc = {}; IDirect3DTexture9* copy = nullptr; IDirect3DPixelShader9* shader = nullptr;
    bool ok = texture && SUCCEEDED(texture->GetLevelDesc(0,&desc));
    const D3DFORMAT format = RawDepthFormat(desc.Format) ? D3DFMT_R32F : desc.Format;
    const UINT levels = texture ? texture->GetLevelCount() : 0;
    uint64_t bytes = 0;
    for (UINT i = 0; i < levels && ok; ++i)
    {
        D3DSURFACE_DESC mip = {}; ok = SUCCEEDED(texture->GetLevelDesc(i,&mip));
        bytes += uint64_t(mip.Width)*mip.Height*PixelBytes(format);
    }
    ok = ok && levels > 0 && levels <= 15 && PixelBytes(format) && Reserve(bytes);
    if (ok)
    {
        Saved saved(dev);
        if (!saved.valid) { m_failed = true; return false; }
        ok = saved.valid && SUCCEEDED(dev->CreateTexture(desc.Width,desc.Height,levels,D3DUSAGE_RENDERTARGET,
            format,D3DPOOL_DEFAULT,&copy,nullptr)) && SUCCEEDED(p_ps_reflection_capture_copy.Create(dev,&shader));
        for (DWORD i = 1; i < 4; ++i) dev->SetRenderTarget(i,nullptr);
        ok=CopyState(dev,shader,texture) && ok;
        for (UINT i = 0; i < levels && ok; ++i)
        {
            D3DSURFACE_DESC mip = {}; IDirect3DSurface9* surface = nullptr;
            ok = SUCCEEDED(copy->GetLevelDesc(i,&mip)) && SUCCEEDED(copy->GetSurfaceLevel(i,&surface));
            const D3DVIEWPORT9 vp = {0,0,mip.Width,mip.Height,0,1};
            const float grid[4] = {1.0f/mip.Width,1.0f/mip.Height,float(i),0};
            ok = SUCCEEDED(dev->SetRenderTarget(0,surface)) && SUCCEEDED(dev->SetViewport(&vp)) &&
                SUCCEEDED(dev->SetPixelShaderConstantF(1,grid,1)) && ok;
            if (ok) ok = SUCCEEDED(DrawScreenTriangle(dev));
            Release(surface);
        }
    }
    Release(shader);
    if (ok) m_entries.push_back({path,copy}); else { Release(copy); m_failed = true; }
    QueryPerformanceCounter(&end); m_submitMs += double(end.QuadPart-start.QuadPart)*1000/frequency.QuadPart;
    return ok;
}
bool CaptureBurst::Flush()
{
    LARGE_INTEGER start, end, frequency; QueryPerformanceCounter(&start); QueryPerformanceFrequency(&frequency);
    bool ok = !m_failed && !m_entries.empty(); std::vector<std::string> directories;
    for (const Entry& entry : m_entries)
    {
        IDirect3DDevice9* dev = nullptr;
        ok = SUCCEEDED(entry.texture->GetDevice(&dev)) && ok;
        if (ok) ok = SaveTexture(dev,entry.texture,entry.path);
        Release(dev);
        const std::string directory = entry.path.substr(0,entry.path.find_last_of("/\\"));
        if (std::find(directories.begin(),directories.end(),directory) == directories.end()) directories.push_back(directory);
    }
    // A failed draw may have queued inputs but no outputs. Durability alone
    // cannot make such a pass replayable.
    if (ok) for (const auto& directory : directories)
    {
        PassHeader packet = {};
        ok = ReadPassHeader(directory+"/pass.bin",packet) && packet.outputs >= 1 && packet.outputs <= 2 && ok;
        for (UINT i = 0; i < packet.outputs && ok; ++i)
            for (const char* prefix : {"initial","output"})
                ok = GetFileAttributesA((directory+"/"+prefix+"-"+std::to_string(i)+".texture").c_str()) != INVALID_FILE_ATTRIBUTES && ok;
    }
    // No pass becomes replayable until every queued native texture is durable.
    if (ok) for (const auto& directory : directories)
        ok = Write(directory+"/inputs.ok","1\n",2) && Write(directory+"/outputs.ok","1\n",2) && ok;
    QueryPerformanceCounter(&end); m_flushMs = double(end.QuadPart-start.QuadPart)*1000/frequency.QuadPart;
    for (auto& entry : m_entries) Release(entry.texture);
    m_entries.clear(); m_failed = !ok;
    return ok;
}
void CaptureBurst::Discard()
{
    for (auto& entry : m_entries) Release(entry.texture);
    m_entries.clear(); m_bytes = 0; m_submitMs = m_flushMs = 0; m_failed = false;
}

bool SaveTexture(IDirect3DDevice9* dev, IDirect3DTexture9* texture, const std::string& path,
                 IDirect3DPixelShader9* rawDepthShader, CaptureBurst* burst)
{
    if (burst) return burst->Queue(dev,texture,path);
    if (!texture) return false;
    D3DSURFACE_DESC desc = {};
    if (FAILED(texture->GetLevelDesc(0, &desc))) return false;
    IDirect3DTexture9* converted = nullptr;
    // Northlight's depth-only RESZ snapshot is DF24 on DXVK. Both formats
    // sample as raw normalized depth and must be converted before CPU readback
    // (or burst staging); their driver surfaces cannot be locked as colour.
    if (RawDepthFormat(desc.Format))
    {
        Saved saved(dev);
        if (!saved.valid || !rawDepthShader || FAILED(dev->CreateTexture(desc.Width, desc.Height, 1,
            D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &converted, nullptr))) return false;
        IDirect3DSurface9* target = nullptr;
        converted->GetSurfaceLevel(0, &target);
        for (DWORD i = 1; i < 4; ++i) dev->SetRenderTarget(i, nullptr);
        dev->SetDepthStencilSurface(nullptr);
        bool convertedOk = SUCCEEDED(dev->SetRenderTarget(0, target));
        Release(target);
        const D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0, 1};
        convertedOk = SUCCEEDED(dev->SetViewport(&vp)) && SUCCEEDED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)) &&
                      SUCCEEDED(dev->SetPixelShader(rawDepthShader)) && convertedOk;
        convertedOk=CopyState(dev,rawDepthShader,texture) && convertedOk;
        const float grid[4] = {1.0f / desc.Width, 1.0f / desc.Height, 0, 0};
        dev->SetPixelShaderConstantF(1, grid, 1);
        convertedOk = SUCCEEDED(DrawScreenTriangle(dev)) && convertedOk;
        if (!convertedOk) { Release(converted); return false; }
        texture = converted;
        texture->GetLevelDesc(0, &desc);
    }
    const UINT levels = texture->GetLevelCount();
    FILE* file = PixelBytes(desc.Format) ? std::fopen(path.c_str(), "wb") : nullptr;
    bool ok = file != nullptr && levels > 0 && levels <= 15;
    if (ok)
    {
        const TextureHeader header = {levels > 1 ? kMipTextureVersion : kPacketVersion,
            static_cast<UINT>(desc.Format), desc.Width, desc.Height, desc.Width * PixelBytes(desc.Format)};
        ok = std::fwrite(&header, sizeof(header), 1, file) == 1;
        if (levels > 1) ok = std::fwrite(&levels, sizeof(levels), 1, file) == 1 && ok;
    }
    for (UINT level = 0; level < levels && ok; ++level)
    {
        IDirect3DSurface9 *source = nullptr, *copy = nullptr;
        D3DLOCKED_RECT lock = {};
        ok = SUCCEEDED(texture->GetLevelDesc(level, &desc)) && SUCCEEDED(texture->GetSurfaceLevel(level, &source));
        bool direct = ok && SUCCEEDED(source->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        if (ok && !direct)
            ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
                 SUCCEEDED(dev->GetRenderTargetData(source, copy)) && SUCCEEDED(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        const UINT rowBytes = desc.Width * PixelBytes(desc.Format);
        for (UINT row = 0; row < desc.Height && ok; ++row)
            ok = std::fwrite(static_cast<const char*>(lock.pBits) + row * lock.Pitch, 1, rowBytes, file) == rowBytes;
        if (direct) source->UnlockRect(); else if (copy && lock.pBits) copy->UnlockRect();
        Release(copy); Release(source);
    }
    if (file) ok = std::fclose(file) == 0 && ok;
    Release(converted);
    return ok;
}

bool ReadPassHeader(const std::string& path, PassHeader& header)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    UINT version = 0;
    bool ok = std::fread(&version,sizeof(version),1,file) == 1;
    std::rewind(file);
    header = {};
    if (ok && version == kPassVersion) ok = std::fread(&header,sizeof(header),1,file) == 1;
    else if (ok && (version == kPacketVersion || version == 3 || version == 4 || version == 5 || version == 6 || version == 7 || version == 8))
    {
        const size_t rows = version == kPacketVersion ? 57 : version == 3 ? 67 : version == 4 ? 68 : version == 5 ? 76 : version == 6 ? 78 : version == 7 ? 88 : 90;
        const size_t prefix = offsetof(PassHeader,uniforms);
        ok = std::fread(&header,1,prefix,file) == prefix &&
             std::fread(header.uniforms,sizeof(float)*4,rows,file) == rows &&
             std::fread(header.samplers,sizeof(header.samplers),1,file) == 1;
    }
    else ok = false;
    // Reject incompatible layout or trailing/truncated records.
    ok = ok && std::fgetc(file) == EOF;
    std::fclose(file);
    return ok;
}

bool CapturePassInputs(IDirect3DDevice9* dev, const std::string& directory, IDirect3DPixelShader9* rawDepthShader, CaptureBurst* burst)
{
    if (!CreateDirectoryA(directory.c_str(), nullptr)) return false; // never replace evidence
    PassHeader header = {};
    header.version = kPassVersion;
    bool ok = SUCCEEDED(dev->GetViewport(&header.viewport)) &&
              SUCCEEDED(dev->GetPixelShaderConstantF(0, &header.uniforms[0][0], kUniformRows)) &&
              SUCCEEDED(dev->GetRenderState(D3DRS_DITHERENABLE, &header.dither));
    for (DWORD i = 0; i < 2; ++i)
    {
        IDirect3DSurface9* target = nullptr;
        if (SUCCEEDED(dev->GetRenderTarget(i, &target)) && target)
        {
            ++header.outputs;
            D3DSURFACE_DESC desc = {}; target->GetDesc(&desc);
            header.width = desc.Width; header.height = desc.Height;
            ok = Write(directory + "/target-" + std::to_string(i) + ".format", &desc.Format, sizeof(desc.Format)) && ok;
        }
        Release(target);
        dev->GetRenderState(i ? D3DRS_COLORWRITEENABLE1 : D3DRS_COLORWRITEENABLE, &header.colorWrite[i]);
    }
    ok = CapturePassOutputs(dev, directory, "initial",burst) && ok;
    for (DWORD i = 0; i < 16; ++i)
    {
        for (DWORD s = 1; s <= 13; ++s)
            dev->GetSamplerState(i, static_cast<D3DSAMPLERSTATETYPE>(s), &header.samplers[i][s]);
        IDirect3DBaseTexture9* base = nullptr;
        dev->GetTexture(i, &base);
        if (base)
        {
            header.boundStages |= 1u << i;
            if (base->GetType() != D3DRTYPE_TEXTURE) ok = false;
            else ok = SaveTexture(dev, static_cast<IDirect3DTexture9*>(base),
                                 directory + "/stage-" + std::to_string(i) + ".texture", rawDepthShader,burst) && ok;
        }
        Release(base);
    }
    IDirect3DPixelShader9* shader = nullptr;
    dev->GetPixelShader(&shader);
    UINT size = 0;
    ok = shader && SUCCEEDED(shader->GetFunction(nullptr, &size)) && ok;
    std::vector<unsigned char> code(size);
    if (shader && size)
        ok = SUCCEEDED(shader->GetFunction(code.data(), &size)) && Write(directory + "/shader.bin", code.data(), size) && ok;
    Release(shader);
    ok = Write(directory + "/pass.bin", &header, sizeof(header)) && ok;
    // Config/time/camera are represented by the exact bound uniforms; original ini is copied by Renderer.
    const char* status = ok ? "inputs complete\n" : "inputs failed\n";
    ok = Write(directory + "/status.txt", status, std::strlen(status)) && ok;
    if (ok && !burst) ok = Write(directory + "/inputs.ok", "1\n", 2);
    return ok;
}

bool CapturePassOutputs(IDirect3DDevice9* dev, const std::string& directory, const char* prefix, CaptureBurst* burst)
{
    bool ok = true;
    for (DWORD i = 0; i < 2; ++i)
    {
        IDirect3DSurface9* target = nullptr;
        IDirect3DTexture9* copy = nullptr;
        if (SUCCEEDED(dev->GetRenderTarget(i, &target)) && target)
        {
            if (burst)
            {
                ok = burst->QueueSurface(dev,target,directory+"/"+prefix+"-"+std::to_string(i)+".texture") && ok;
                Release(target);
                continue;
            }
            D3DSURFACE_DESC desc = {}; target->GetDesc(&desc);
            IDirect3DSurface9* surface = nullptr;
            bool copied = SUCCEEDED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                desc.Format, D3DPOOL_DEFAULT, &copy, nullptr)) &&
                          SUCCEEDED(copy->GetSurfaceLevel(0, &surface)) &&
                          SUCCEEDED(dev->StretchRect(target, nullptr, surface, nullptr, D3DTEXF_NONE));
            Release(surface);
            ok = copied && SaveTexture(dev, copy, directory + "/" + prefix + "-" + std::to_string(i) + ".texture") && ok;
        }
        Release(copy); Release(target);
    }
    const char* status = ok ? "complete\n" : "outputs failed\n";
    ok = Write(directory + "/status.txt", status, std::strlen(status)) && ok;
    if (ok && !burst && std::strcmp(prefix, "output") == 0) ok = Write(directory + "/outputs.ok", "1\n", 2);
    return ok;
}

bool LoadTexture(IDirect3DDevice9* dev, const std::string& path, IDirect3DTexture9** texture, D3DPOOL pool)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    TextureHeader header = {};
    UINT levels = 1;
    bool ok = std::fread(&header, sizeof(header), 1, file) == 1 &&
              (header.version == kPacketVersion || header.version == kMipTextureVersion) &&
              header.width && header.height && header.width <= 16384 && header.height <= 16384 &&
              PixelBytes(static_cast<D3DFORMAT>(header.format)) > 0 &&
              header.rowBytes == header.width * PixelBytes(static_cast<D3DFORMAT>(header.format));
    if (ok && header.version == kMipTextureVersion)
        ok = std::fread(&levels, sizeof(levels), 1, file) == 1 && levels >= 2 && levels <= 15 &&
             (header.width >> (levels - 1) || header.height >> (levels - 1));
    ok = ok && SUCCEEDED(dev->CreateTexture(header.width, header.height, levels, 0, static_cast<D3DFORMAT>(header.format),
                                          pool, texture, nullptr));
    for (UINT level = 0; level < levels && ok; ++level)
    {
        D3DLOCKED_RECT lock = {}; D3DSURFACE_DESC desc = {};
        ok = SUCCEEDED((*texture)->GetLevelDesc(level, &desc)) && SUCCEEDED((*texture)->LockRect(level, &lock, nullptr, 0));
        const UINT rowBytes = desc.Width * PixelBytes(desc.Format);
        for (UINT row = 0; row < desc.Height && ok; ++row)
            ok = std::fread(static_cast<char*>(lock.pBits) + row * lock.Pitch, 1, rowBytes, file) == rowBytes;
        if (lock.pBits) (*texture)->UnlockRect(level);
    }
    ok = std::fgetc(file) == EOF && ok;
    std::fclose(file);
    if (!ok) Release(*texture);
    return ok;
}
}
