#!/usr/bin/env python3
# northlight-test: requires=cxx,zig
"""Command-stream queue, wait and generated record/dispatch/sync code: native tests against an SDK stub, and a compile
check of the generated code against the real d3d9.h with zig (x86-windows-gnu)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re, subprocess, tempfile
fp.use_source_modules()
import generate_forwarders as gf

HERE = Path(__file__).resolve().parent
SDK_TEXT = gf.SDK.read_text()
CLASSES = gf.classify(SDK_TEXT)

# ---- freshness: the generated headers regenerate byte-identically, and every method is classified (classify asserts) ----
assert fp.src('forwarders.h').read_text() == gf.forwarders_text(SDK_TEXT), 'forwarders.h is stale: run scripts/generate_forwarders.py'
assert fp.src('command_stream.inl').read_text() == gf.stream_text(SDK_TEXT), 'command_stream.inl is stale: run scripts/generate_forwarders.py'
for iface, methods in CLASSES.items():
    assert {m.name for m in methods} == {n for _, n, _ in gf.parse_methods(SDK_TEXT, iface)}
    assert len(methods) == sum(len(v.split()) for v in gf.STREAM[iface].values()), iface
classified = sum(len(v) for v in CLASSES.values())
by_class = {}
for methods in CLASSES.values():
    for m in methods:
        by_class[m.cls] = by_class.get(m.cls, 0) + 1
print('freshness ok:', classified, 'methods classified', by_class, flush=True)

# ---- the SDK stub: every type the 14 interfaces mention, and the interfaces with default (non-pure) methods ----
SCALARS = {'HRESULT': 'int32_t', 'ULONG': 'uint32_t', 'DWORD': 'uint32_t', 'UINT': 'uint32_t', 'INT': 'int32_t', 'WINBOOL': 'int32_t',
           'D3DCOLOR': 'uint32_t', 'HANDLE': 'void*', 'HWND': 'void*', 'HDC': 'void*', 'float': 'float'}
ENUMS = ('D3DTRANSFORMSTATETYPE D3DRENDERSTATETYPE D3DSTATEBLOCKTYPE D3DTEXTURESTAGESTATETYPE D3DSAMPLERSTATETYPE D3DPRIMITIVETYPE '
         'D3DFORMAT D3DPOOL D3DMULTISAMPLE_TYPE D3DBACKBUFFER_TYPE D3DTEXTUREFILTERTYPE D3DCUBEMAP_FACES D3DQUERYTYPE D3DRESOURCETYPE').split()
SIZES = {'D3DMATRIX': 64, 'D3DVIEWPORT9': 28, 'D3DMATERIAL9': 68, 'D3DLIGHT9': 104, 'D3DCLIPSTATUS9': 8, 'RECT': 16, 'POINT': 8, 'PALETTEENTRY': 4,
         'D3DGAMMARAMP': 1536, 'D3DRECT': 16, 'D3DBOX': 24, 'D3DRECTPATCH_INFO': 20, 'D3DTRIPATCH_INFO': 16}
KEYWORDS = {'const', 'struct', 'void', 'int', 'unsigned', 'char', 'long', 'double'}
EXTRA_IFACES = ('IDirect3DResource9', 'IDirect3DBaseTexture9')


def all_methods():
    for iface in EXTRA_IFACES + gf.STREAM_IFACES:
        for ret, name, params in gf.parse_methods(SDK_TEXT, iface):
            yield iface, ret, name, params


def stub():
    tokens = set()
    for _, ret, _, params in all_methods():
        tokens |= set(re.findall(r'\w+', ret + ' ' + ' '.join(p.type for p in params)))
    out = ['#pragma once', '#include <cstdint>', '#define STDMETHODCALLTYPE', 'using REFIID=const int&;using REFGUID=const int&;',
           'constexpr int32_t S_OK=0,D3D_OK=0,E_NOINTERFACE=-2,D3DERR_INVALIDCALL=-3;']
    out += [f'using {k}={v};' for k, v in SCALARS.items() if k != 'float']
    out += [f'using {e}=unsigned;' for e in ENUMS]
    known = set(SCALARS) | set(ENUMS) | KEYWORDS | {'REFIID', 'REFGUID', 'IUnknown'}
    ifaces = set(EXTRA_IFACES + gf.STREAM_IFACES) | {'IDirect3D9'}
    for t in sorted(tokens - known - ifaces):
        out.append(f'struct {t} {{{f"unsigned char b[{SIZES[t]}];" if t in SIZES else ""}}};')
    out += [f'struct {i};' for i in sorted(ifaces)]
    out.append('struct IUnknown {virtual int32_t QueryInterface(REFIID,void**){return 0;}virtual uint32_t AddRef(){return 1;}virtual uint32_t Release(){return 1;}virtual ~IUnknown()=default;};')
    for iface in EXTRA_IFACES + gf.STREAM_IFACES:
        base = SDK_TEXT.split('DECLARE_INTERFACE_IID_(' + iface + ',')[1].split('};')[0].split(',')[0].strip()
        out.append(f'struct {iface}:{base} {{')
        for ret, name, params in gf.parse_methods(SDK_TEXT, iface):
            out.append(f'virtual {ret} {name}({", ".join(p.type for p in params)}) {{' + ('return;' if ret == 'void' else 'return {};') + '}')
        out.append('};')
    return '\n'.join(out) + '\n'


# ---- generated test code: per-method trace formatters, fakes, round-trip cases, and stub proxy classes using the macros ----
def value(p, i):
    if p.iface or p.type in ('HDC', 'HWND', 'HANDLE'):
        return f'({p.type})(uintptr_t)(0x3000+{i}*16)'
    return f'({p.type})({i}+2)'


def fmt_function(m):
    params = ', '.join(p.decl for p in m.params)
    lines = [f'template<bool Inner> static std::string fmt_{m.enum}({params}) {{ std::string s;']
    for p in m.params:
        if p.name in m.spec:
            lines.append(f's+=fb({p.name},{p.name}?(std::size_t)({m.spec[p.name]}):0);s+=\' \';')
        elif p.out_iface:
            lines.append(f's+=fo({p.name});s+=\' \';')
        elif p.iface:
            lines.append(f's+=fi<Inner>({p.name});s+=\' \';')
        elif p.ptr:
            lines.append(f's+=fa({p.name});s+=\' \';')
        else:
            lines.append(f's+=fv({p.name});s+=\' \';')
    lines.append('return s; }')
    return ''.join(lines)


def fake_method(m, idx):
    params = ', '.join(p.decl for p in m.params)
    names = ', '.join(p.name for p in m.params)
    body = [f'gTrace.push_back("{m.short}::{m.name} "+fmt_{m.enum}<true>({names}));']
    for i, p in enumerate(m.params):
        if p.out_iface:
            body.append(f'if({p.name})*{p.name}=({p.inner_type}*)(uintptr_t)(0x5000+{i});')
    if not m.is_void:
        body.append(f'return ({m.ret})5;')
    return f'    {m.ret} STDMETHODCALLTYPE {m.name}({params}) override {{ {" ".join(body)} }}'


def generated_cpp():
    out = []
    replay = [m for i in gf.STREAM_IFACES for m in CLASSES[i] if m.cls in ('record', 'state', 'customrec', 'get', 'sync')]
    for m in replay:
        out.append(fmt_function(m))
    for iface in gf.STREAM_IFACES:
        short = iface[len('IDirect3D'):-1]
        out.append(f'struct Fake{short} final:{iface} {{')
        out += [fake_method(m, 0) for m in CLASSES[iface] if m.cls in ('record', 'state', 'customrec', 'get', 'sync')]
        out.append('};')
        out.append(f'template<> struct SelfOf<{iface}> {{ static {iface}* proxy() {{ return ({iface}*)(uintptr_t)(0x7000+{gf.STREAM_IFACES.index(iface)}); }} static {iface}* fake() {{ static Fake{short} f; return &f; }} }};')
        out.append(f'struct Proxy{short}:{iface},HostBase {{ NORTHLIGHT_STREAM_{short.upper()}_METHODS }};')
    # Round-trip cases.
    out.append('static void generatedRecordCases() {')
    for m in replay:
        if m.cls in ('get', 'sync'):
            continue
        for variant in ((0, 1) if m.spec else (0,)):
            out.append('  { Queue q; TestTr tr; gTrace.clear(); tr.results.clear();')
            for i, p in enumerate(m.params):
                if p.name in m.spec:
                    out.append(f'    unsigned char src_{p.name}[4096];for(int k=0;k<4096;++k)src_{p.name}[k]=(unsigned char)(k*7+{i});')
                    out.append(f'    {p.type} a_{p.name}={"nullptr" if variant else f"({p.type})src_{p.name}"};')
                else:
                    out.append(f'    {p.type} a_{p.name}={value(p, i)};')
            args = ', '.join(f'a_{p.name}' for p in m.params)
            selfp = f'SelfOf<{m.iface}>::proxy()' if m.self_arg else ''
            call_args = ', '.join(x for x in (selfp, args) if x)
            out.append(f'    std::string expect="{m.short}::{m.name} "+fmt_{m.enum}<false>({args});')
            out.append(f'    record_{m.enum}(q{", " if call_args else ""}{call_args});')
            for p in m.params:
                if p.name in m.spec:
                    out.append(f'    std::memset(src_{p.name},0xEE,sizeof src_{p.name});')
            out.append(f'    q.publish();const CommandHeader* h=q.next(false);CHECK(h&&h->id==(std::uint16_t)Cmd::{m.enum}&&h->size%8==0);')
            out.append('    CHECK(dispatchGenerated(h,tr));q.retire(h);CHECK(!q.next(false));')
            out.append(f'    CHECK(gTrace.size()==1&&gTrace[0]==expect);')
            out.append(f'    CHECK(tr.results.size()=={1 if m.ret == "HRESULT" else 0}&&tr.skips==0);')
            out.append(f'    CHECK(std::string(cmdName(Cmd::{m.enum}))=="{m.short}::{m.name}"); }}')
    out.append('}')
    out.append('static void generatedSyncCases() {')
    out.append('  Queue q; TestTr tr; ReplayThread rt(q,tr);')
    for m in replay:
        if m.cls not in ('get', 'sync'):
            continue
        out.append('  { gTrace.clear();')
        for i, p in enumerate(m.params):
            if p.out_iface:
                out.append(f'    {p.inner_type}* o_{p.name}=nullptr;{p.type} a_{p.name}=&o_{p.name};')
            elif p.ptr and not p.iface:
                out.append(f'    {p.inner_type} dummy_{p.name}{{}};{p.type} a_{p.name}=&dummy_{p.name};')
            else:
                out.append(f'    {p.type} a_{p.name}={value(p, i)};')
        args = ', '.join(f'a_{p.name}' for p in m.params)
        selfp = f'SelfOf<{m.iface}>::proxy()' if m.self_arg else ''
        call_args = ', '.join(x for x in (selfp, args) if x)
        out.append(f'    std::string expect="{m.short}::{m.name} "+fmt_{m.enum}<false>({args});')
        call = f'runSync(q,NORTHLIGHT_STREAM_TAG({m.enum}){", " if call_args else ""}{call_args})'
        out.append(f'    {"" if m.is_void else "auto r="}{call};')
        if not m.is_void:
            out.append(f'    CHECK(r==({m.ret})5);')
        out.append('    CHECK(gTrace.size()==1&&gTrace[0]==expect);')
        for i, p in enumerate(m.params):
            if p.out_iface:
                out.append(f'    CHECK(o_{p.name}==({p.inner_type}*)(uintptr_t)(0x5000+{i}+0x20000));')
        out.append(f'    CHECK(get(q.stats.census[(std::size_t)Cmd::{m.enum}])==1); }}')
    out.append('  rt.stop(); }')
    return '\n'.join(out) + '\n'


def compile_run(tmp, name, flags, args=(), expect_fail=False):
    exe = Path(tmp) / name
    command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DNL_STUB', *flags, '-I', str(tmp), *fp.test_include_flags(),
               str(HERE / 'test_command_stream.cpp'), '-o', str(exe)]
    subprocess.run(command, check=True)
    run = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=900)
    print(run.stdout, run.stderr, flush=True)
    run.check_returncode()
    assert 'ThreadSanitizer' not in run.stderr and 'runtime error' not in run.stderr and 'AddressSanitizer' not in run.stderr
    return run


with tempfile.TemporaryDirectory(prefix='command-stream-') as tmp:
    (Path(tmp) / 'd3d9_stub.h').write_text(stub())
    (Path(tmp) / 'cs_generated.inc').write_text(generated_cpp())
    compile_run(tmp, 'o2', ['-O2'])
    compile_run(tmp, 'asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
    compile_run(tmp, 'tsan', ['-O1', '-g', '-fsanitize=thread'], ['threads'])
    # The same generated code against the real d3d9.h, 32-bit Windows: compile only (the DLL's own toolchain).
    zig = fp.zig()
    obj = Path(tmp) / 'compile.o'
    command = [str(zig), 'c++', '-target', 'x86-windows-gnu', '-O2', '-std=c++17', '-fno-rtti', '-Wall', '-Wextra', '-Werror',
               '-Wno-microsoft-exception-spec', *fp.include_flags(), '-c', str(HERE / 'test_command_stream_compile.cpp'), '-o', str(obj)]
    subprocess.run(command, check=True, env=fp.zig_env())
print('test_command_stream: ok')
