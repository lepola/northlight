#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Run the production immutable-device focus-window lookup with failure/retry mocks."""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text()
start = source.index('        D3DDEVICE_CREATION_PARAMETERS cp={};', source.index('    void finishFrameImpl()'))
end = source.index('intervalFocus&=focus;', start) + len('intervalFocus&=focus;')
body = source[start:end]
harness = r'''
#include <cassert>
using HWND=void*;
#define SUCCEEDED(x) ((x)>=0)
struct D3DDEVICE_CREATION_PARAMETERS {HWND hFocusWindow=nullptr;};
struct Native {unsigned calls=0;bool fail=false;HWND window=nullptr;
    int GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p){++calls;if(fail)return -1;p->hFocusWindow=window;return 0;}};
static HWND foreground=nullptr;static HWND GetForegroundWindow(){return foreground;}
struct Device {Native* ext;HWND focusWindow=nullptr;bool focusWindowKnown=false,intervalFocus=true;
    bool frame(){BODY return focus;}};
int main(){int a,b;Native n;n.window=&a;Device d{&n};foreground=&a;
    n.fail=true;assert(!d.frame()&&n.calls==1&&!d.focusWindowKnown);n.fail=false;d.intervalFocus=true;
    for(unsigned i=0;i<10000;++i)assert(d.frame());assert(n.calls==2&&d.intervalFocus);
    foreground=&b;assert(!d.frame()&&!d.intervalFocus&&n.calls==2);
    Native other;other.window=&b;Device e{&other};assert(e.frame()&&other.calls==1);
    Native null;Device f{&null};foreground=nullptr;assert(f.frame()&&f.frame()&&null.calls==1);
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory(prefix='northlight-focus-') as tmp:
    path = Path(tmp)
    (path/'test.cpp').write_text(harness)
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('PASS production focus lookup: cached immutable HWND, failed-call retry, null HWND and device isolation; 10000 frames / one successful native lookup')
