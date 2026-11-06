#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Duplicate restored-size policy and production primary-Present lifecycle wiring."""
import os
import re
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def member(text,start):
    at=text.index(start);brace=text.index('{',at);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at:end]

renderer=fp.src('renderer.cpp').read_text();overlay=fp.src('settings_overlay.cpp').read_text()
device=member(renderer,'HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND wnd,')
swap=member(renderer,'HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND window,')
publish=member(renderer,'void windowPresented(HWND window,HRESULT status,bool primary)')
assert device.index('ext->Present(')<device.index('windowPresented(wnd,hr,true);')
assert swap.index('real->Present(')<swap.index('windowPresentedDeviceFrame(owner,window,hr,primary);')
assert publish.index('if(!primary)')<publish.index('NorthlightWindowHook::presented(')
assert 'if(status!=D3D_OK)NorthlightWindowHook::invalidate(ext);return;' in publish
cooperative=member(renderer,'HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override')
assert 'const HRESULT status=ext->TestCooperativeLevel();if(status!=D3D_OK)NorthlightWindowHook::invalidate(ext);return status;' in cooperative
assert all(token not in publish for token in ['GetClientRect','GetForegroundWindow','GetBackBuffer','TestCooperativeLevel'])
reset=member(renderer,'HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override')
assert reset.index('NorthlightWindowHook::invalidate(ext);')<reset.index('Guard mirrorLock')<reset.index('ext->Reset(pp)')
assert reset.index('hdr.logical=*pp;')<reset.index('refreshWindowSize();')
destructor=member(renderer,'~Device()')
assert destructor.index('NorthlightWindowHook::detach(ext);')<destructor.index('overlay.reset();')<destructor.index('ext->Release();')
constructor=member(renderer,'Device(IDirect3DDevice9* d,IDirect3D9* p,')
assert constructor.index('NorthlightWindowHook::attach(ext,')<constructor.index('GetPrivateProfileIntW(L"Renderer",L"SettingsOverlay"')
create=member(overlay,'std::unique_ptr<Overlay> create(')
assert create.index('if(!enabled)')<create.index('install(')
procedure=member(overlay,'LRESULT CALLBACK windowProcedure(')
procedureCode=re.sub(r'/\*.*?\*/|//[^\n]*','',procedure,flags=re.S)
assert 'ImGui' not in procedureCode and 'TestCooperativeLevel' not in procedureCode and 'GetBackBuffer' not in procedureCode
assert procedure.index('const LRESULT result=forward();')<procedure.index('hook.deliveredSize.delivered(')
assert 'if(wparam!=SIZE_RESTORED)' in procedure
for msg in ['WM_NCDESTROY','WM_DISPLAYCHANGE','WM_DPICHANGED','WM_STYLECHANGED','WM_ACTIVATEAPP','WM_ENTERSIZEMOVE','WM_EXITSIZEMOVE']:
    assert msg in procedure
assert procedure.index('beginWindowDestruction()')<procedure.index('hook.active.store(false)')<procedure.index('finishRelease()')
detach=member(overlay,'void detach(IDirect3DDevice9* owner)')
assert detach.index('if(!owner)return;')<detach.index('beginRelease(identity)')<detach.index('uninstall(owner,true);')<detach.index('finishRelease()')
uninstall=member(overlay,'void uninstall(')
body=uninstall[uninstall.index('const auto inputOwner='):]
assert uninstall.index('beginMaintenance(')<uninstall.index('const auto inputOwner=')
assert body.index('inputOwner&&inputOwner!=expectedOwner')<body.index('hook.overlayOwner.store(nullptr')
assert 'finishMaintenance(retained)' in uninstall
out=fp.output_dir();exe=out/('window-size-policy.exe' if os.name=='nt' else 'window-size-policy')
cmd=([str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++','-pthread'])
subprocess.run([*cmd,'-std=c++17','-O2','-UNDEBUG',*fp.include_flags(),str(fp.TESTS/'test_window_size_filter.cpp'),'-o',str(exe)],env=fp.zig_env() if os.name=='nt' else None,check=True)
run=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30);(out/'policy.log').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr,end='');run.check_returncode();assert 'WINDOW_SIZE_POLICY_PASS' in run.stdout
print('WINDOW_SIZE_WIRING_PASS primary_present_only lost_override_reset_before_gate overlay_disabled teardown no_WndProc_COM')
