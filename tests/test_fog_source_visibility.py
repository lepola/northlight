#!/usr/bin/env python3
"""Check the dead fog source-visibility output and retained state contract."""
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
host = (root / 'src/world/world_renderer.h').read_text()
shader = (root / 'shaders/world_effects.hlsl').read_text()
assert 'sourceVis' not in host and 'kSourceVisibilityPSShader' not in host
assert len(re.findall(r'\bSourceVisibility\b', shader)) == 1
assert len(re.findall(r'\bSourceInfo\.[xy]\b', shader)) == 2
pass_start = shader.index('float4 SourceVisibilityPS(')
pass_end = min(i for marker in ('float4 WorldFog(', 'float4 worldFog(') if (i := shader.find(marker, pass_start)) >= 0)
assert all(x in shader[pass_start:pass_end] for x in ('SourceInfo.x', 'SourceInfo.y', 'PreviousSourceVisibility'))
section = host[host.index('bool fogResolved=false;'):host.index('d->SetRenderTarget(0,fogSurface);', host.index('bool fogResolved=false;'))]
for state in ('D3DSAMP_ADDRESSU','D3DSAMP_ADDRESSV','D3DSAMP_MINFILTER','D3DSAMP_MAGFILTER','D3DSAMP_MIPFILTER'):
    assert 'SetSamplerState(15,' + state in section
before_fog = host[:host.index('d->SetRenderTarget(0,fogSurface);', host.index('bool fogResolved=false;'))]
for state, value in (('D3DRS_ALPHABLENDENABLE', 'FALSE'), ('D3DRS_COLORWRITEENABLE', '15')):
    writes = re.findall(r'SetRenderState\(' + state + r',([^)]*)\)', before_fog)
    assert writes and writes[-1].strip() == value
assert 'c[67][2]=haze.sun[0];c[67][3]=haze.sun[1]' in host
assert (root / 'src/sky/celestial_disc_renderer.h').exists()
print('source-visibility dataflow and state contract PASS')
