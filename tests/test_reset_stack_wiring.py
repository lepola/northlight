from pathlib import Path

source = (Path(__file__).resolve().parents[1] / 'src/stream/stream_device.h').read_text()
reset = source[source.index('HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p)'):source.index('HRESULT STDMETHODCALLTYPE GetBackBuffer(', source.index('HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p)'))]
assert reset.index('if(!p)return D3DERR_INVALIDCALL') < reset.index('if(profile)') < reset.index('CaptureStackBackTrace(0,12,stack,nullptr)') < reset.index('runTask(core,')
assert 'resetTraceCount<32' in reset and 'resetTraceCount==32' in reset and 'suppressed after=32' in reset
assert 'core.callerModule(stack[i])' in reset and 'index=%u' in reset
for field in ('focus=', 'active=', 'foreground=', 'oldWindow=', 'newWindow=', 'oldSwap=', 'newSwap=', 'oldAutoDepth=', 'newAutoDepth=', 'oldDepthFormat=', 'newDepthFormat=', 'oldFlags=', 'newFlags=', 'oldRefresh=', 'newRefresh='):
    assert field in reset, field
assert 'hr=c.target->Reset(p)' in reset and 'if(SUCCEEDED(hr))pp=sc0->pp' in reset
print('PASS bounded Reset stack and parameter diagnostics wiring')
