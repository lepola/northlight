#!/usr/bin/env python3
# northlight-test: requires=cxx,client,dll
"""The renderer as the game-folder d3d9.dll proxy. Native tests of backend_policy.h
(paths, BackendPath, pre-proxy compat) and backend_loader.h (DXVK/own-build scan,
self-load refusals, system fallback, re-entry, SHA-256) at -O2 and with
ASan/UBSan; read-only scans of real DLLs when present; source audit of the
renderer.cpp wiring and frd9.def against the real d3d9.dll export table.
No Wine, Windows binary or game is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re,struct,subprocess,tempfile
HERE=Path(__file__).resolve().parent
CLIENT=fp.client_root()
DXVK=fp.tools()/'dxvk-2.7.1-download/d3d9.dll'   # optional third-party download
DXVK3=fp.tools()/'dxvk-3.1.1-download/d3d9.dll'
# Named exports of Windows d3d9.dll (and DXVK) with their ordinals.
D3D9={'Direct3DCreate9On12':20,'Direct3DCreate9On12Ex':21,'Direct3DShaderValidatorCreate9':24,'PSGPError':25,'PSGPSampleTexture':26,
      'D3DPERF_BeginEvent':27,'D3DPERF_EndEvent':28,'D3DPERF_GetStatus':29,'D3DPERF_QueryRepeatFrame':30,'D3DPERF_SetMarker':31,
      'D3DPERF_SetOptions':32,'D3DPERF_SetRegion':33,'DebugSetLevel':34,'DebugSetMute':35,'Direct3D9EnableMaximizedWindowedModeShim':36,
      'Direct3DCreate9':37,'Direct3DCreate9Ex':38}

def pe_exports(data):
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24
    sections=[struct.unpack_from('<IIII',data,opt+struct.unpack_from('<H',data,pe+20)[0]+40*i+8) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
    off=lambda rva:next(raw+rva-va for size,va,rawsize,raw in sections if va<=rva<va+max(size,rawsize))
    e=off(struct.unpack_from('<I',data,opt+96)[0]);base,_,count,_,names,ords=struct.unpack_from('<6I',data,e+16)
    name=lambda rva:data[off(rva):data.index(b'\0',off(rva))].decode()
    return {name(struct.unpack_from('<I',data,off(names)+4*i)[0]):base+struct.unpack_from('<H',data,off(ords)+2*i)[0] for i in range(count)}

def native():
    real=[]
    for expect,path in [('dxvk-v3.1.1+env',DXVK3),('dxvk-v2.7.1+env',DXVK),('ours',fp.dll()),('plain',CLIENT/'wow.exe')]:
        if path.is_file():real.append(expect+':'+str(path))
    real.append('ours:'+str(fp.dll()))
    with tempfile.TemporaryDirectory() as t:
        for label,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
            for test,args in [('test_backend_policy.cpp',[]),('test_backend_loader.cpp',real)]:
                exe=Path(t)/(label+test)
                subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/test),'-o',str(exe)],check=True)
                out=subprocess.run([str(exe),*args],check=True,capture_output=True,text=True).stdout
                print(label,test,out.strip().replace('\n',' | ') or 'PASS')

def audit():
    src=fp.src('renderer.cpp').read_text()
    exports=re.findall(r'extern "C" [^(]*?\bWINAPI (\w+)\(([^)]*)\)\s*\{(.{0,40})',src,re.S)
    names={n for n,_,_ in exports}
    assert names==set(D3D9),sorted(names^set(D3D9))
    for n,params,body in exports:
        assert body.lstrip().startswith('NORTHLIGHT_EXPORT;'),n   # every export tracks re-entry
    # .def: same names, real ordinals, stdcall bytes = 4 per parameter (x86).
    arity={n:0 if not p.strip() else p.count(',')+1 for n,p,_ in exports}
    defs=re.findall(r'^\s+(\w+)=(\w+)@(\d+) @(\d+)$',fp.src('frd9.def').read_text(),re.M)
    assert {n:int(o) for n,_,_,o in defs}==D3D9
    for n,alias,size,_ in defs:assert alias==n and int(size)==4*arity[n],(n,size,arity[n])
    if DXVK.is_file():assert pe_exports(DXVK.read_bytes())==D3D9
    else:print('SKIP sub-check: DXVK export table (no',DXVK,')')
    built=fp.dll()
    if b'BACKEND selected=' in built.read_bytes():assert pe_exports(built.read_bytes())==D3D9
    # The game-folder d3d9.dll is this proxy: no backend default may point there.
    policy=fp.src('backend_policy.h').read_text()
    assert 'L"%lsd3d9.dll"' not in src and 'renderer-backends\\\\legacy\\\\legacy_d3d9.dll' in policy and 'renderer-backends\\\\dxvk\\\\dxvk_d3d9.dll' in policy
    # DllMain runs under the loader lock (macOS: inside DivxDecoder's dlls.txt preload): handle only.
    main=src[src.index('BOOL WINAPI DllMain'):]
    main=main[:main.index('return TRUE;')]
    for forbidden in ['GetModuleFileName','LoadLibrary','logf','CreateThread','rootPath','backend(']:assert forbidden not in main,forbidden
    # Root = host exe directory, resolved lazily as backend()'s first step (every export reaches it before logging).
    assert 'static HMODULE cached=[]() -> HMODULE {\n    ensureRootPath();' in src and 'NorthlightBackend::gameRoot(' in src
    start,end=src.index('struct Win32BackendSys'),src.index('static HMODULE recursionBackend')
    loader=src[start:end]
    for forbidden in ['GENERIC_WRITE','WriteFile','MoveFile','DeleteFile','CopyFile','VirtualProtect','WriteProcessMemory','CREATE_ALWAYS','OPEN_ALWAYS']:
        assert forbidden not in loader,forbidden   # the executable and candidates are only read
    # 0.3.175: DXVK_ASYNC is never set (the runtime's own setting applies); vendor/buffer defaults only for Backend=dxvk.
    assert 'DXVK_ASYNC' not in src.replace('DXVK_ASYNC is left to the runtime','')
    assert 'if(NorthlightBackend::isPackagedDxvk(kind))configureDxvkCompatibility(info);' in loader and src.count('configureDxvkCompatibility(')==2
    # 0.3.189: both option names (2.x / >=3.0), dxvk2 shares the DXVK rules, default-path dxvk falls back to dxvk2 once.
    compat=fp.src('dxvk_compatibility.h').read_text()
    assert 'd3d9.cachedDynamicBuffers = True' in compat and 'd3d9.cachedWriteOnlyBuffers = True' in compat and 'd3d9.customVendorId = 1002' in compat
    assert 'L"renderer-backends\\\\dxvk2\\\\dxvk2_d3d9.dll"' in policy and 'isPackagedDxvk(configured)' in src
    # No in-process runtime swap.
    assert 'dxvkFallback' not in src and 'dxvkFallbackModule' not in src and 'dxvkFallbackArmed' not in src
    proc=src[src.index('template<class T> static T procedure'):src.index('#define NORTHLIGHT_EXPORT')]
    assert proc.count('backend()')==1 and 'GetModuleHandle' not in proc and 'Module' not in proc.replace('HMODULE','')
    # 0.3.195: no automatic dxvk -> dxvk2 fallback: no crash marker is written, read or honoured; the configured backend always loads.
    for gone in ['dxvkInitProbe','dxvkInitMarker','dxvk3Probe','dxvk3Sha','northlight-dxvk3-init','loadDxvk2','BACKEND FALLBACK','FILE_ATTRIBUTE_REPARSE_POINT','GENERIC_WRITE','CREATE_ALWAYS']:
        assert gone not in src,gone
    assert 'dxvkInitMarker' not in policy and '.pending' not in policy
    be=src[src.index('static HMODULE backend()'):src.index('static HMODULE recursionBackend')]
    assert be.count('NorthlightBackendLoader::load(')==1 and 'NorthlightBackendLoader::Result<HMODULE> result=NorthlightBackendLoader::load(sys,candidates,system);' in be
    c9=src[src.index('Direct3DCreate9(UINT'):src.index('Direct3DCreate9Ex(UINT')]
    assert 'IDirect3D9* p=fn(sdk);' in c9 and 'return p&&!NorthlightBackendLoader::ExportScope::reentered()?new Factory(p):p;' in c9
    cex=src[src.index('Direct3DCreate9Ex(UINT'):];cex=cex[:cex.index('\n}\n')]
    assert 'return fn(sdk,out);' in cex
    assert 'static bool applied=false;if(applied)return;applied=true;' in src   # one prefix, even after a refusal
    assert 'NorthlightBackendLoader::ExportScope::reentered()?recursionBackend():backend()' in src
    assert '!NorthlightBackendLoader::ExportScope::reentered()?new Factory(p):p' in src
    # Nothing depends on the host exe: its path is logged, it is never read or hashed.
    host=src[src.index('static void logHostExecutable'):];host=host[:host.index('\n}\n')]
    assert 'logHostExecutable(sys.selfPath);' in src and 'HOST exe=%ls module=%ls' in host
    for forbidden in ['CreateFileW','ReadFile','Sha256','sha256','exeStatus']:assert forbidden not in host,forbidden
    assert 'exeStatus' not in fp.src('backend_loader.h').read_text() and 'bfe77b47' not in src+fp.src('backend_loader.h').read_text()
    assert 'BACKEND SELF-LOAD REFUSED' in src and 'BACKEND RECURSION' in src and 'system d3d9 fallback FAILED too' in src
    # Only ever a d3d9.dll proxy: no pre-proxy (frd9.dll) compatibility paths.
    assert 'frd9' not in src and 'loadedAsProxy' not in src+policy and 'root+L"d3d9.dll"' not in policy
    assert 'GAME WARNING: the game-folder d3d9.dll differs from the backend copy' in src and 'PROXY module=%ls root=%ls' in src
    print('audit: exports/ordinals/stdcall sizes = d3d9.dll, every export re-entry scoped, trivial DllMain, root = exe dir, renamed backends, loader read-only, 0.3.147 DXVK rules, no frd9 compat')

if __name__=='__main__':
    native();audit()
    print('PASS backend proxy')
