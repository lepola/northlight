"""Generate typed D3D9 forwarding methods from the installed Windows SDK header."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
import northlight_paths  # noqa: E402
SDK = northlight_paths.windows_headers() / 'd3d9.h'

# ---------------------------------------------------------------------------------------------------------------------
# Command stream (0.3.192, CS): classification of every method of the interfaces the game sees, and the generated
# record / replay / sync code (src/generated/command_stream.inl). Each method is classified exactly once:
#   record    returns at once: the arguments (and copies of input pointers, see SPECS) go into the queue; the replay
#             thread calls the Target (the real Device). Returns D3D_OK (HRESULT) or nothing (void).
#   state     like record, but the game-facing class first calls observe(CmdTag<Cmd::X>{},args...) so StreamState / the
#             proxy can mirror the call (set slots, bind refs, draw triggers) before it is recorded.
#   get       a Get* that StreamState answers locally: `if(answer(tag,args...,ret))return ret; return syncGet(tag,args...)`;
#             an unknown or sync-only slot falls back to a sync call. Never void.
#             (0.3.204: also TestCooperativeLevel, answered from the replay-published cooperative state while it is D3D_OK.)
#   local     answered on the game thread from proxy/device-held data: `return local(tag,args...)` (hand-written).
#   sync      drains the queue to this point; the replay thread runs the Target method with translated arguments and
#             writes out-parameters into the waiting caller's memory (interface out-parameters translated inner->proxy
#             on the replay thread).
#   custom    no generated body and no id: hand-written in StreamDevice / the proxies (Create*, Lock/Unlock, UP draws,
#             Present, Reset, state-block lifecycle, QueryInterface/AddRef/Release, child getters, GetData).
#   customrec custom game-facing body, but the encoder and the replay case are generated (ShowCursor).
# SPECS give the byte size of every input pointer a record/state/customrec method copies into the queue (param name ->
# C++ expression over the parameter names; a null pointer stays null, the expression is only evaluated for non-null).
# PUBLISH methods publish the queue after recording (the consumer must see scene/target boundaries at once).
STREAM_IFACES = ('IDirect3DDevice9', 'IDirect3DSwapChain9', 'IDirect3DVertexBuffer9', 'IDirect3DIndexBuffer9',
                 'IDirect3DSurface9', 'IDirect3DTexture9', 'IDirect3DCubeTexture9', 'IDirect3DVolumeTexture9',
                 'IDirect3DVolume9', 'IDirect3DVertexShader9', 'IDirect3DPixelShader9', 'IDirect3DVertexDeclaration9',
                 'IDirect3DStateBlock9', 'IDirect3DQuery9')
_UNK = 'QueryInterface AddRef Release'
_PRIV = 'GetDevice SetPrivateData GetPrivateData FreePrivateData'
_RES = _PRIV + ' SetPriority GetPriority GetType'
_TEX_LOCAL = _RES + ' SetLOD GetLOD GetLevelCount GetAutoGenFilterType GetLevelDesc'
_TEX_REC = 'PreLoad'   # GenerateMipSubLevels is `state`: it writes levels on the GPU, observed so their CPU shadows are dropped
STREAM = {
    'IDirect3DDevice9': {
        'custom': _UNK + ' Reset Present GetBackBuffer CreateAdditionalSwapChain CreateTexture CreateVolumeTexture CreateCubeTexture '
                  'CreateVertexBuffer CreateIndexBuffer CreateRenderTarget CreateDepthStencilSurface CreateOffscreenPlainSurface '
                  'CreateStateBlock BeginStateBlock EndStateBlock DrawPrimitiveUP DrawIndexedPrimitiveUP CreateVertexDeclaration '
                  'CreateVertexShader CreatePixelShader CreateQuery ShowCursor SetCursorPosition',   # the cursor is Win32 state: the StreamDevice does it on the calling thread, nothing is recorded
        'customrec': 'SetCursorProperties',   # a hardware cursor is built on the game thread; only the software-cursor case is forwarded (the encoder is generated)
        'record': 'EvictManagedResources SetDialogBoxMode SetGammaRamp BeginScene EndScene Clear SetClipStatus '
                  'DrawRectPatch DrawTriPatch DeletePatch',
        # UpdateSurface/UpdateTexture/StretchRect/ColorFill/ProcessVertices write a resource on the GPU side: observed so the
        # destination's CPU-side lock/shadow knowledge is dropped.
        'state': 'UpdateSurface UpdateTexture StretchRect ColorFill ProcessVertices SetRenderTarget SetDepthStencilSurface SetTransform MultiplyTransform SetViewport SetMaterial SetLight LightEnable '
                 'SetClipPlane SetRenderState SetTexture SetTextureStageState SetSamplerState SetPaletteEntries SetCurrentTexturePalette '
                 'SetScissorRect SetSoftwareVertexProcessing SetNPatchMode DrawPrimitive DrawIndexedPrimitive SetVertexDeclaration SetFVF '
                 'SetVertexShader SetVertexShaderConstantF SetVertexShaderConstantI SetVertexShaderConstantB SetStreamSource '
                 'SetStreamSourceFreq SetIndices SetPixelShader SetPixelShaderConstantF SetPixelShaderConstantI SetPixelShaderConstantB',
        'get': 'GetRenderTarget GetDepthStencilSurface GetTransform GetViewport GetMaterial GetLight GetLightEnable GetClipPlane '
               'GetRenderState GetTexture GetTextureStageState GetSamplerState GetPaletteEntries GetCurrentTexturePalette GetScissorRect '
               'GetSoftwareVertexProcessing GetNPatchMode GetVertexDeclaration GetFVF GetVertexShader GetVertexShaderConstantF '
               'GetVertexShaderConstantI GetVertexShaderConstantB GetStreamSource GetStreamSourceFreq GetIndices GetPixelShader '
               'GetPixelShaderConstantF GetPixelShaderConstantI GetPixelShaderConstantB TestCooperativeLevel',
        'local': 'GetAvailableTextureMem GetDirect3D GetDeviceCaps GetCreationParameters GetSwapChain GetNumberOfSwapChains',
        'sync': 'GetDisplayMode GetRasterStatus GetGammaRamp GetRenderTargetData GetFrontBufferData ValidateDevice '
                'GetClipStatus',
    },
    'IDirect3DSwapChain9': {
        'custom': _UNK + ' Present GetBackBuffer',
        'local': 'GetDevice GetPresentParameters',
        'sync': 'GetFrontBufferData GetRasterStatus GetDisplayMode',
    },
    'IDirect3DVertexBuffer9': {'custom': _UNK + ' Lock Unlock', 'local': _RES + ' GetDesc', 'record': 'PreLoad'},
    'IDirect3DIndexBuffer9': {'custom': _UNK + ' Lock Unlock', 'local': _RES + ' GetDesc', 'record': 'PreLoad'},
    'IDirect3DSurface9': {'custom': _UNK + ' GetContainer LockRect UnlockRect', 'local': _RES + ' GetDesc', 'record': 'PreLoad',
                          'sync': 'GetDC ReleaseDC'},
    'IDirect3DTexture9': {'custom': _UNK + ' GetSurfaceLevel LockRect UnlockRect', 'local': _TEX_LOCAL,
                          'record': _TEX_REC + ' AddDirtyRect', 'state': 'SetAutoGenFilterType GenerateMipSubLevels'},
    'IDirect3DCubeTexture9': {'custom': _UNK + ' GetCubeMapSurface LockRect UnlockRect', 'local': _TEX_LOCAL,
                              'record': _TEX_REC + ' AddDirtyRect', 'state': 'SetAutoGenFilterType GenerateMipSubLevels'},
    'IDirect3DVolumeTexture9': {'custom': _UNK + ' GetVolumeLevel LockBox UnlockBox', 'local': _TEX_LOCAL,
                                'record': _TEX_REC + ' AddDirtyBox', 'state': 'SetAutoGenFilterType GenerateMipSubLevels'},
    'IDirect3DVolume9': {'custom': _UNK + ' GetContainer LockBox UnlockBox', 'local': _PRIV + ' GetDesc'},
    'IDirect3DVertexShader9': {'custom': _UNK, 'local': 'GetDevice GetFunction'},
    'IDirect3DPixelShader9': {'custom': _UNK, 'local': 'GetDevice GetFunction'},
    'IDirect3DVertexDeclaration9': {'custom': _UNK, 'local': 'GetDevice GetDeclaration'},
    'IDirect3DStateBlock9': {'custom': _UNK, 'local': 'GetDevice', 'state': 'Capture Apply'},
    'IDirect3DQuery9': {'custom': _UNK + ' GetData', 'local': 'GetDevice GetType GetDataSize', 'state': 'Issue'},   # observed: counts the END issue the polled result belongs to
}
_RECT = 'sizeof(RECT)'
SPECS = {
    ('IDirect3DDevice9', 'SetTransform'): {'matrix': 'sizeof(D3DMATRIX)'},
    ('IDirect3DDevice9', 'MultiplyTransform'): {'matrix': 'sizeof(D3DMATRIX)'},
    ('IDirect3DDevice9', 'SetViewport'): {'viewport': 'sizeof(D3DVIEWPORT9)'},
    ('IDirect3DDevice9', 'SetMaterial'): {'material': 'sizeof(D3DMATERIAL9)'},
    ('IDirect3DDevice9', 'SetLight'): {'light': 'sizeof(D3DLIGHT9)'},
    ('IDirect3DDevice9', 'SetClipPlane'): {'plane': '16'},
    ('IDirect3DDevice9', 'SetClipStatus'): {'clip_status': 'sizeof(D3DCLIPSTATUS9)'},
    ('IDirect3DDevice9', 'SetScissorRect'): {'rect': _RECT},
    ('IDirect3DDevice9', 'SetPaletteEntries'): {'entries': '256*sizeof(PALETTEENTRY)'},
    ('IDirect3DDevice9', 'SetGammaRamp'): {'ramp': 'sizeof(D3DGAMMARAMP)'},
    ('IDirect3DDevice9', 'Clear'): {'rects': 'rect_count*sizeof(D3DRECT)'},
    ('IDirect3DDevice9', 'UpdateSurface'): {'src_rect': _RECT, 'dst_point': 'sizeof(POINT)'},
    ('IDirect3DDevice9', 'StretchRect'): {'src_rect': _RECT, 'dst_rect': _RECT},
    ('IDirect3DDevice9', 'ColorFill'): {'rect': _RECT},
    ('IDirect3DDevice9', 'SetVertexShaderConstantF'): {'data': 'count*16'},
    ('IDirect3DDevice9', 'SetVertexShaderConstantI'): {'data': 'count*16'},
    ('IDirect3DDevice9', 'SetVertexShaderConstantB'): {'data': 'count*sizeof(WINBOOL)'},
    ('IDirect3DDevice9', 'SetPixelShaderConstantF'): {'data': 'count*16'},
    ('IDirect3DDevice9', 'SetPixelShaderConstantI'): {'data': 'count*16'},
    ('IDirect3DDevice9', 'SetPixelShaderConstantB'): {'data': 'count*sizeof(WINBOOL)'},
    ('IDirect3DDevice9', 'DrawRectPatch'): {'segment_count': '4*sizeof(float)', 'patch_info': 'sizeof(D3DRECTPATCH_INFO)'},
    ('IDirect3DDevice9', 'DrawTriPatch'): {'segment_count': '3*sizeof(float)', 'patch_info': 'sizeof(D3DTRIPATCH_INFO)'},
    ('IDirect3DTexture9', 'AddDirtyRect'): {'dirty_rect': _RECT},
    ('IDirect3DCubeTexture9', 'AddDirtyRect'): {'dirty_rect': _RECT},
    ('IDirect3DVolumeTexture9', 'AddDirtyBox'): {'dirty_box': 'sizeof(D3DBOX)'},
}
# `state` methods whose repeat with the current value is a no-op in D3D9: the game-facing body first asks redundant(tag,args...) and
# returns at once when the StreamDevice knows the value came from a game Set (see the safety audit in stream_device.h). Never listed:
# SetRenderTarget, SetDepthStencilSurface, SetViewport, SetLight, draws, anything with a side effect.
REDUNDANT = {('IDirect3DDevice9', n) for n in (
    'SetRenderState', 'SetSamplerState', 'SetTextureStageState', 'SetTexture', 'SetStreamSource', 'SetStreamSourceFreq', 'SetIndices',
    'SetVertexShader', 'SetPixelShader', 'SetVertexDeclaration', 'SetFVF', 'SetVertexShaderConstantF', 'SetVertexShaderConstantI',
    'SetVertexShaderConstantB', 'SetPixelShaderConstantF', 'SetPixelShaderConstantI', 'SetPixelShaderConstantB', 'SetTransform', 'SetMaterial',
    'LightEnable', 'SetScissorRect', 'SetClipPlane', 'SetNPatchMode', 'SetSoftwareVertexProcessing', 'SetCurrentTexturePalette')}
# Device methods the replay thread may call on the ExtensionDevice (`ext`) with RAW backend pointers instead of through the Device. The rule:
# the Device's code for the method is either absent (GuardedMirrorDevice -> MirrorDevice::X, the very method ext->X runs) or `Guard; return
# ext->X(<unwrap of the interface arguments>)` and nothing else. Audit (renderer.cpp class Device, 0.3.192):
#   not overridden by Device: SetRenderState SetSamplerState SetTextureStageState SetTransform MultiplyTransform SetMaterial SetLight LightEnable
#     SetClipPlane SetScissorRect SetViewport SetFVF SetStreamSourceFreq Set{Vertex,Pixel}ShaderConstant{F,I,B} SetNPatchMode
#     SetSoftwareVertexProcessing SetCurrentTexturePalette SetPaletteEntries (GuardedMirrorDevice takes only the gate)
#   pure unwrap + forward to ext: SetTexture SetVertexShader SetPixelShader SetVertexDeclaration SetRenderTarget SetDepthStencilSurface
#     (mirrorResources.unwrap), SetStreamSource SetIndices (NorthlightTrackedBuffers::resolveInput; its one side effect, disabling the mirror for
#     an unwrapped input, cannot fire for a stream proxy whose raw was resolved as wrapped: a proxy without a proven raw replays through the Device)
# NEVER direct: draws (hooks, counters, census), Clear, Present, Reset, Begin/EndScene, every Create*, CreateVertexShader/PixelShader
# (registration), ProcessVertices, StretchRect/ColorFill/UpdateSurface/UpdateTexture, queries, state blocks, cursor, Get*.
# tests/test_command_stream.py checks this set against the audited list and against renderer.cpp's Device source.
DIRECT = {('IDirect3DDevice9', n) for n in (
    'SetRenderState', 'SetSamplerState', 'SetTextureStageState', 'SetTransform', 'MultiplyTransform', 'SetMaterial', 'SetLight', 'LightEnable',
    'SetClipPlane', 'SetScissorRect', 'SetViewport', 'SetFVF', 'SetStreamSourceFreq', 'SetVertexShaderConstantF', 'SetVertexShaderConstantI',
    'SetVertexShaderConstantB', 'SetPixelShaderConstantF', 'SetPixelShaderConstantI', 'SetPixelShaderConstantB', 'SetNPatchMode',
    'SetSoftwareVertexProcessing', 'SetCurrentTexturePalette', 'SetPaletteEntries', 'SetTexture', 'SetVertexShader', 'SetPixelShader',
    'SetVertexDeclaration', 'SetStreamSource', 'SetIndices', 'SetRenderTarget', 'SetDepthStencilSurface')}
PUBLISH = {('IDirect3DDevice9', n) for n in ('BeginScene', 'EndScene', 'Clear', 'SetRenderTarget')}
# Ids of commands the hand-written stream code records (no generated encoder). Append here; ids are regenerated with
# the file and nothing persists them.
CUSTOM_IDS = ('Nop', 'Sync', 'Destroy', 'Derive', 'Snapshot', 'Quiesce', 'Stop', 'Present', 'SwapPresent', 'Reset',
              'CreateTexture', 'CreateVolumeTexture', 'CreateCubeTexture', 'CreateVertexBuffer', 'CreateIndexBuffer',
              'CreateRenderTarget', 'CreateDepthStencilSurface', 'CreateOffscreenPlainSurface', 'CreateVertexDeclaration',
              'CreateVertexShader', 'CreatePixelShader', 'CreateQuery', 'CreateStateBlock', 'BeginStateBlock', 'EndStateBlock',
              'CreateAdditionalSwapChain', 'UnlockBuffer', 'UnlockRect', 'UnlockBox', 'DrawPrimitiveUP', 'DrawIndexedPrimitiveUP',
              # census labels of task-based sync calls (never recorded as commands of their own; see runTask)
              'SyncGetData', 'SyncLock', 'SyncUnlock', 'SyncCreate', 'SyncReset', 'SyncRelease', 'SyncUpDraw', 'SyncInit')
DEVICE = 'IDirect3DDevice9'
ITYPE = re.compile(r'(?:const\s+)?(?:struct\s+)?IDirect3D\w*9\s*\*')


class Param:
    def __init__(self, type_, name):
        self.type, self.name = type_, name
        bare = re.sub(r'\bconst\b|\bstruct\b', '', type_).replace(' ', '')
        self.out_iface = bare.endswith('**') and 'IDirect3D' in bare
        self.iface = bool(ITYPE.fullmatch(type_)) and not self.out_iface
        self.ptr = '*' in type_
        self.inner_type = re.sub(r'\bconst\b|\bstruct\b', '', type_.rstrip('*')).strip()   # IDirect3DSurface9 for IDirect3DSurface9**

    @property
    def decl(self):
        return f'{self.type} {self.name}'


class Method:
    def __init__(self, iface, ret, name, params, cls):
        self.iface, self.ret, self.name, self.params, self.cls = iface, ret, name, params, cls
        self.short = iface[len('IDirect3D'):-1]
        self.spec = SPECS.get((iface, name), {})
        self.self_arg = [] if iface == DEVICE else [Param(f'{iface}*', 'self')]
        self.enum = f'{self.short}_{name}'
        self.is_void = ret == 'void'


def parse_methods(text, interface):
    body = text.split('DECLARE_INTERFACE_IID_(' + interface + ',')[1].split('};')[0]
    out = []
    for m in re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\((.*?)\) PURE;', body, re.S):
        ret, special, normal, params = m.groups()
        ret, name = (ret or 'HRESULT').strip(), special or normal
        params = re.sub(r'\bTHIS_?\b', '', params).strip()
        ps = []
        for i, param in enumerate(params.split(',') if params else []):
            param = ' '.join(param.split())
            words = [w for w in re.findall(r'\w+', param) if w not in ('const', 'struct')]
            if len(words) == 1:                                   # unnamed: "D3DPRIMITIVETYPE", "D3DLIGHT9*", "void*"
                type_, pname = param, f'arg{i}'
            else:
                pname = re.search(r'(\w+)\s*$', param).group(1)
                type_ = param[:param.rindex(pname)].strip()
            ps.append(Param(re.sub(r'\s+\*', '*', type_), pname))
        out.append((ret, name, ps))
    return out


def classify(text):
    """{interface: [Method]} with every method classified exactly once; asserts completeness."""
    result = {}
    assert set(STREAM) == set(STREAM_IFACES)
    for interface in STREAM_IFACES:
        table = {}
        for cls, names in STREAM[interface].items():
            for n in names.split():
                assert n not in table, (interface, n, 'classified twice')
                table[n] = cls
        parsed = parse_methods(text, interface)
        assert {n for _, n, _ in parsed} == set(table), (interface, sorted({n for _, n, _ in parsed} ^ set(table)))
        assert len(parsed) == len(table), (interface, 'duplicate method names')
        result[interface] = [Method(interface, ret, n, ps, table[n]) for ret, n, ps in parsed]
        for m in result[interface]:
            check_method(m)
    for (interface, name), spec in SPECS.items():
        m = next(x for x in result[interface] if x.name == name)
        assert m.cls in ('record', 'state', 'customrec'), (interface, name, 'spec on a method that does not copy')
        assert set(spec) <= {p.name for p in m.params}, (interface, name, spec)
    for interface, name in DIRECT:
        assert any(m.name == name and m.cls in ('state', 'record') and m.ret == 'HRESULT' for m in result[interface]), (interface, name)
    for interface, name in REDUNDANT:
        assert any(m.name == name and m.cls == 'state' for m in result[interface]), (interface, name)
    for interface, name in PUBLISH:
        assert any(m.name == name and m.cls in ('record', 'state') for m in result[interface]), (interface, name)
    return result


def check_method(m):
    names = [p.name for p in m.params]
    assert len(set(names)) == len(names), (m.iface, m.name, names)
    if m.cls in ('record', 'state', 'customrec'):
        assert m.cls == 'customrec' or m.ret in ('HRESULT', 'void'), (m.iface, m.name, 'record/state returns HRESULT or void')
        for p in m.params:
            assert not p.out_iface, (m.iface, m.name, p.name)
            assert not p.ptr or p.iface or p.name in m.spec, (m.iface, m.name, p.name, 'input pointer without a copy spec')
    if m.cls == 'get':
        assert m.ret != 'void', (m.iface, m.name)
    if m.cls in ('get', 'sync', 'local'):
        for p in m.params:
            assert not ('**' in p.type.replace(' ', '') and not p.out_iface) or m.cls == 'local', (m.iface, m.name, p.name, 'raw ** out-parameter in a sync call')


def tag(m):
    return f'NORTHLIGHT_STREAM_TAG({m.enum})'


def encoder(m):
    fields = m.self_arg + m.params
    out = [f'struct Args_{m.enum} {{'] + [f'    {"std::uint32_t" if p.name in m.spec else p.type} {p.name};' for p in fields] + ['};']
    params = ', '.join(['Queue& q'] + [p.decl for p in fields])
    out.append(f'inline void record_{m.enum}({params}) {{')
    out.append(f'    constexpr std::size_t _base={"((sizeof(Args_%s)+7u)&~7u)" % m.enum if fields else "0u"};')
    terms = []
    for p in m.params:
        if p.name in m.spec:
            out.append(f'    const std::size_t _n_{p.name}={p.name}?(std::size_t)({m.spec[p.name]}):0;')
            terms.append(f'((_n_{p.name}+7u)&~7u)')
    out.append('    const std::size_t _total=' + '+'.join(['_base'] + terms) + ';')
    out.append('    if(_total>MaxInlinePayload){own(q.stats.oversizeDrops);return;}   // a garbage count: D3D would fail the call; drop it and count')
    if fields:
        out.append(f'    auto* _a=static_cast<Args_{m.enum}*>(q.reserve((std::uint16_t)Cmd::{m.enum},(std::uint32_t)_total));')
        out.append('    std::uint32_t _o=(std::uint32_t)_base;(void)_o;')
        for p in fields:
            if p.name in m.spec:
                out.append(f'    if({p.name}){{_a->{p.name}=_o;std::memcpy(reinterpret_cast<unsigned char*>(_a)+_o,{p.name},_n_{p.name});_o+=(std::uint32_t)((_n_{p.name}+7u)&~7u);}}else _a->{p.name}=kNoPayload;')
            else:
                out.append(f'    _a->{p.name}={p.name};')
    else:
        out.append(f'    q.reserve((std::uint16_t)Cmd::{m.enum},0);')
    out += ['    q.commit();', '}']
    return out


def decode_args(m):
    parts = []
    for p in m.params:
        if p.name in m.spec:
            parts.append(f'_a->{p.name}==kNoPayload?({p.type})nullptr:({p.type})(_b+_a->{p.name})')
        elif p.iface:
            parts.append(f'tr.inner(_a->{p.name})')
        else:
            parts.append(f'_a->{p.name}')
    return ', '.join(parts)


def dispatch_case(m):
    out = [f'    case Cmd::{m.enum}: {{']
    if m.self_arg + m.params:
        out.append(f'        const auto* _a=reinterpret_cast<const Args_{m.enum}*>(h+1);const auto* _b=reinterpret_cast<const unsigned char*>(_a);(void)_b;')
    if m.iface == DEVICE:
        target = 'tr.device()'
    else:
        out.append(f'        auto* _t=tr.inner(_a->self);if(!_t){{tr.skipped(Cmd::{m.enum});return true;}}')
        target = '_t'
    if (m.iface, m.name) in DIRECT:
        # Direct replay: ext->X with the proxies' cached raw pointers; any interface argument without one sends the call through the Device.
        raws = [p for p in m.params if p.iface]
        out.append('        if constexpr(kDirectReplay) {')
        out.append('            if(IDirect3DDevice9* _e=tr.ext()) {')
        out.append('                bool _ok=true;')
        parts = []
        for p in m.params:
            if p.name in m.spec:
                parts.append(f'_a->{p.name}==kNoPayload?({p.type})nullptr:({p.type})(_b+_a->{p.name})')
            elif p.iface:
                out.append(f'                auto* _r_{p.name}=tr.raw(_a->{p.name},_ok);')
                parts.append(f'_r_{p.name}')
            else:
                parts.append(f'_a->{p.name}')
        out.append(f'                if(_ok) {{ tr.direct(); tr.result(Cmd::{m.enum},_e->{m.name}({", ".join(parts)})); return true; }}')
        out.append('            }')
        out.append('        }')
    call = f'{target}->{m.name}({decode_args(m)})'
    out.append(f'        tr.result(Cmd::{m.enum},{call});' if m.ret == 'HRESULT' else f'        {call};')
    return out + ['        return true;', '    }']


def traits(m):
    tup = ', '.join(p.type for p in m.self_arg + m.params)
    return [f'template<> struct MethodTraits<Cmd::{m.enum}> {{ using Ret={m.ret}; using Tuple=std::tuple<{tup}>; }};']


def sync_case(m):
    out = [f'    case Cmd::{m.enum}: {{', f'        using M=MethodTraits<Cmd::{m.enum}>;auto& t=*static_cast<M::Tuple*>(sc.args);(void)t;']
    if not m.is_void:
        out.append('        auto& r=*static_cast<M::Ret*>(sc.ret);')
    args, post = [], []
    for i, p in enumerate(m.params, len(m.self_arg)):
        if p.out_iface:
            out.append(f'        {p.inner_type}* _o{i}=nullptr;')
            args.append(f'std::get<{i}>(t)?&_o{i}:nullptr')
            post.append(f'if(std::get<{i}>(t))*std::get<{i}>(t)=tr.toProxy(_o{i});')
        elif p.iface:
            args.append(f'tr.inner(std::get<{i}>(t))')
        else:
            args.append(f'std::get<{i}>(t)')
    lhs = '' if m.is_void else 'r='
    if m.iface == DEVICE:
        out.append(f'        {lhs}tr.device()->{m.name}({", ".join(args)});{"".join(post)}')
    else:
        fb = '' if m.is_void else 'r=SyncFallback<M::Ret>::value();'
        out.append('        auto* _t=tr.inner(std::get<0>(t));')
        out.append(f'        if(!_t){{{fb}tr.skipped(Cmd::{m.enum});}}else{{{lhs}_t->{m.name}({", ".join(args)});{"".join(post)}}}')
    return out + ['        sc.done.store(1,std::memory_order_release);return true;', '    }']


def join_args(*parts):
    return ', '.join(p for p in parts if p)


def macro_body(m):
    t, names = tag(m), ', '.join(p.name for p in m.params)
    this = 'this' if m.self_arg else ''
    q = 'this->streamQueue()'
    return f'[[maybe_unused]] auto _scope=this->callScope((std::uint16_t)::NorthlightStream::Cmd::{m.enum});' + macro_body_inner(m, t, names, this, q)   # 0.3.204 (task 21): sampled game-thread timer, per command


def macro_body_inner(m, t, names, this, q):
    if m.cls in ('record', 'state'):
        obs = f'this->observe({join_args(t, names)});' if m.cls == 'state' else ''
        if (m.iface, m.name) in REDUNDANT:
            assert m.cls == 'state' and m.ret == 'HRESULT'
            obs = f'if(this->redundant({join_args(t, names)}))return D3D_OK;' + obs
        pub = f'{q}.publish();' if (m.iface, m.name) in PUBLISH else ''
        return f'{obs}::NorthlightStream::record_{m.enum}({join_args(q, this, names)});{pub}{"" if m.is_void else "return D3D_OK;"}'
    if m.cls == 'get':
        return (f'{m.ret} _r{{}};if(this->answer({join_args(t, names, "_r")}))return _r;'
                f'return this->syncGet({join_args(t, this, names)});')
    if m.cls == 'local':
        return f'return this->local({join_args(t, names)});'
    if m.cls == 'sync':
        return f'return this->syncCall({join_args(t, this, names)});'
    raise AssertionError(m.cls)


SYNC_MACHINERY = r"""struct SyncCall { Cmd id; void* args; void* ret; std::atomic<std::uint32_t> done; };
template<class R> struct SyncFallback { static R value() { return R(); } };
template<> struct SyncFallback<HRESULT> { static HRESULT value() { return D3DERR_INVALIDCALL; } };
// Game thread: records a Cmd::Sync command pointing at this stack frame and waits (pumped) until the replay thread ran it.
// Device methods pass their arguments; methods of other interfaces pass the receiving proxy first. A call made from inside
// a pumped wait (a message handler DXVK's pump dispatched) never blocks: it returns D3DERR_INVALIDCALL / a zero value.
template<Cmd C, class... A> inline typename MethodTraits<C>::Ret runSync(Queue& q, CmdTag<C>, A... a) {
    using M = MethodTraits<C>; using Ret = typename M::Ret;
    own(q.stats.census[(std::size_t)C]);   // game thread only
    if(inPumpedWait) { add(q.stats.nestedSyncs); return SyncFallback<Ret>::value(); }
    own(q.stats.syncCalls);
    typename M::Tuple t(a...);
    std::conditional_t<std::is_void<Ret>::value, char, Ret> r{};
    SyncCall sc{C, &t, &r, {0}};
    SyncCall* p = &sc;
    std::memcpy(q.reserve((std::uint16_t)Cmd::Sync, sizeof p, kFlagWaitTarget), &p, sizeof p);   // 0.3.196 (task 12): the wait below targets this command
    q.commit();
    q.waitReplayed(q.recordedSeq(), WaitKind::Sync);
    if constexpr(!std::is_void<Ret>::value) return r;
}
// Replay thread: runs the Target method of a get/sync call (the Cmd::Sync command's SyncCall). false = unknown id.
template<class Tr> inline bool executeSync(SyncCall& sc, Tr& tr) {
    switch(sc.id) {"""


def stream_text(text):
    classes = classify(text)
    ided = [m for i in STREAM_IFACES for m in classes[i] if m.cls != 'custom']
    out = ['// Generated by generate_forwarders.py. Do not edit.', '#pragma once', '#include <atomic>', '#include <cstdint>', '#include <cstring>',
           '#include <tuple>', '#include <type_traits>', '#include "command_queue.h"',
           '// Needs the D3D9 types (d3d9.h, or the test stub) before it. Tr, the replay-side translator, provides:',
           '//   IDirect3DDevice9* device();                  the Target (the real Device)',
           '//   template<class T> T* inner(T* proxy);        stream proxy -> Target-level object (null stays null; a null result means a dead proxy)',
           '//   template<class T> T* toProxy(T* innerRef);   sync out-parameter: takes the reference the Target returned, yields the game-facing proxy',
           '//   void result(Cmd id, HRESULT hr);             HRESULT of a replayed record/state call',
           '//   void skipped(Cmd id);                        a call dropped because its receiver proxy is dead',
           '//   IDirect3DDevice9* ext();                     the extension device for direct replay (null: through device())',
           '//   template<class T> T* raw(T* proxy,bool& ok); the proxy\'s cached backend pointer (null proxy: null; ok=false when it has none)',
           '//   void direct();                               counts a call replayed on ext()',
           '// The game-facing class using NORTHLIGHT_STREAM_<IFACE>_METHODS provides streamQueue(), observe(tag,args...), answer(tag,args...,ret&),',
           '// syncGet(tag,[proxy,]args...), local(tag,args...), syncCall(tag,[proxy,]args...); the proxy argument is passed for non-device interfaces.',
           '// Every generated body starts with `auto _scope=this->callScope(id);` (0.3.204: the sampled game-thread recording timer, per command id; the host returns a scope object).',
           '// The device class also provides bool redundant(tag,args...) (true: a repeated Set the game side does not record; see REDUNDANT).',
           '#ifndef NORTHLIGHT_STREAM_DIRECT', '#define NORTHLIGHT_STREAM_DIRECT 1', '#endif',
           '#define NORTHLIGHT_STREAM_TAG(X) ::NorthlightStream::CmdTag<::NorthlightStream::Cmd::X>{}',
           'namespace NorthlightStream {']
    ids = list(CUSTOM_IDS) + [m.enum for m in ided]
    assert len(set(ids)) == len(ids)
    out.append('enum class Cmd : std::uint16_t {')
    out += ['    ' + ', '.join(ids[i:i + 6]) + ',' for i in range(0, len(ids), 6)]
    out += ['    Count', '};', 'constexpr bool kDirectReplay=NORTHLIGHT_STREAM_DIRECT!=0;   // replay the DIRECT methods on the extension device (see DIRECT in the generator)', f'constexpr std::uint16_t kFirstGeneratedCmd={len(CUSTOM_IDS)};',
            'static_assert((std::size_t)Cmd::Count<=kMaxCmdIds,"grow kMaxCmdIds in stream_stats.h");',
            'template<Cmd C> struct CmdTag {};', 'inline const char* cmdName(Cmd c) {', '    switch(c) {']
    out += [f'    case Cmd::{n}: return "{n}";' for n in CUSTOM_IDS]
    out += [f'    case Cmd::{m.enum}: return "{m.short}::{m.name}";' for m in ided]
    out += ['    default: return "?";', '    }', '}']
    sets = [m for m in ided if m.cls == 'state' and (m.name.startswith('Set') or m.name in ('LightEnable', 'MultiplyTransform'))]
    out += ['// A failed replay of one of these leaves the game thread\'s StreamState wrong: it invalidates itself.', 'inline bool cmdSetsState(Cmd c) {', '    switch(c) {']
    out += [f'    case Cmd::{m.enum}: return true;' for m in sets]
    out += ['    default: return false;', '    }', '}']
    recs = [m for m in ided if m.cls in ('record', 'state', 'customrec')]
    for m in recs:
        out += encoder(m)
    out += ['// Replay thread: decodes a generated record/state/customrec command and calls the Target. false = not a generated id.',
            'template<class Tr> inline bool dispatchGenerated(const CommandHeader* h, Tr& tr) {', '    switch((Cmd)h->id) {']
    for m in recs:
        out += dispatch_case(m)
    out += ['    default: return false;', '    }', '}',
            'template<Cmd C> struct MethodTraits;   // Ret and Tuple (the receiving proxy first for non-device interfaces) of get/local/sync methods']
    for m in ided:
        if m.cls in ('get', 'local', 'sync'):
            out += traits(m)
    out.append(SYNC_MACHINERY)
    for m in ided:
        if m.cls in ('get', 'sync'):
            out += sync_case(m)
    out += ['    default: return false;', '    }', '}', '}  // namespace NorthlightStream', '']
    for interface in STREAM_IFACES:
        macro = 'NORTHLIGHT_STREAM_' + interface[len('IDirect3D'):-1].upper() + '_METHODS'
        out.append(f'#define {macro} \\')
        out.append(' \\\n'.join(f'    {m.ret} STDMETHODCALLTYPE {m.name}({", ".join(p.decl for p in m.params)}) override {{ {macro_body(m)} }}'
                                for m in classes[interface] if m.cls not in ('custom', 'customrec')))
        out.append('')
    return '\n'.join(out)


def forwarders_text(text):
    result = ['// Generated by generate_forwarders.py. Do not edit.', '#pragma once']
    for interface in ('IDirect3D9', 'IDirect3DDevice9', 'IDirect3DSwapChain9', 'IDirect3DVertexBuffer9', 'IDirect3DIndexBuffer9'):
        body = text.split('DECLARE_INTERFACE_IID_(' + interface + ',')[1].split('};')[0]
        result += [f'class Forward{interface} : public {interface} {{', 'public:',
                   f'    {interface}* real;', f'    explicit Forward{interface}({interface}* p) : real(p) {{}}']
        count = 0
        for m in re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\((.*?)\) PURE;', body, re.S):
            ret, special, normal, params = m.groups()
            ret, name = ret or 'HRESULT', special or normal
            params = re.sub(r'\bTHIS_?\b', '', params).strip()
            args, declarations = [], []
            for i, param in enumerate(params.split(',') if params else []):
                param = ' '.join(param.split())
                if param in ('D3DPRIMITIVETYPE', 'D3DLIGHT9*'):
                    param += f' arg{i}'
                arg = re.search(r'(\w+)\s*$', param).group(1)
                args.append(arg)
                declarations.append(param)
            result.append(f'    {ret} STDMETHODCALLTYPE {name}({", ".join(declarations)}) override {{ return real->{name}({", ".join(args)}); }}')
            count += 1
        assert count == {'IDirect3D9':17,'IDirect3DDevice9':119,'IDirect3DSwapChain9':10,'IDirect3DVertexBuffer9':14,'IDirect3DIndexBuffer9':14}[interface], (interface, count)
        result.append('};')
    return '\n'.join(result) + '\n'

def generate():
    text = SDK.read_text()
    (northlight_paths.GENERATED / 'forwarders.h').write_text(forwarders_text(text))
    (northlight_paths.GENERATED / 'command_stream.inl').write_text(stream_text(text))

if __name__ == '__main__':
    generate()
