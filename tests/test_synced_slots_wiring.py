#!/usr/bin/env python3
# northlight-test:
"""Task 34 dirty record: every way the device state the spell layer's Model is compared with can change must clear the layer's synced bits (synced_slots.h), or the layer would
skip a slot that changed. Static audit of device_mirror.h: the MirrorDevice setters of the tracked slots clear exactly theirs, invalidate() clears all, every other Set* override is
classified (its slot has no bit: the layer examines it on every record); the renderer hands the bits to the mirror and the layer. The behaviour is exercised in test_spell_layer.cpp."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
m=fp.src('device_mirror.h').read_text();r=fp.src('renderer.cpp').read_text();sl=fp.src('spell_layer.h').read_text()
dev=m.split('class MirrorDevice :')[1].split('// Extension calls')[0]
bodies={}
for mt in re.finditer(r'HRESULT STDMETHODCALLTYPE (Set\w+)\(',dev):
    end=dev.find('\n    HRESULT STDMETHODCALLTYPE',mt.end());bodies.setdefault(mt.group(1),dev[mt.start():end if end>0 else len(dev)])
HOOKED={'SetRenderState':'renderState(','SetSamplerState':'sampler(','SetTexture':'texture(','SetViewport':'viewport()','SetRenderTarget':'viewport()',
        'SetVertexShaderConstantF':'vsFloat(','SetPixelShaderConstantF':'psFloat('}
UNTRACKED={'SetVertexShader','SetPixelShader','SetVertexDeclaration','SetFVF','SetIndices','SetStreamSource','SetStreamSourceFreq','SetDepthStencilSurface',
           'SetVertexShaderConstantI','SetVertexShaderConstantB','SetSoftwareVertexProcessing'} # no bit: shaders, declaration, ints/bools are examined on every record; streams, indices, depth are not part of the delta
checks={
 'every tracked setter clears its slot':all(k in bodies and 'm->synced->'+v in bodies[k] and 'm->synced)' in bodies[k] for k,v in HOOKED.items()),
 'SetRenderTarget clears the viewport only for target 0':'if(n==0&&m->synced)m->synced->viewport()' in bodies.get('SetRenderTarget',''),
 'no unclassified Set* override in MirrorDevice (a new setter must be hooked or listed as untracked)':set(bodies)<=set(HOOKED)|UNTRACKED,
 'invalidate() clears every bit (state block Apply, Reset, RawScope entry and exit, vendor states, failed Sets, disable)':re.search(r'void invalidate\(\)\{[^}]*if\(synced\)synced->clearAll\(\);',m) is not None,
 'the mirror holds the bits by pointer, null until the renderer installs them':'SyncedSlots* synced=nullptr;' in m,
 'the renderer gives the bits to the layer and the mirror and takes them back at teardown':'cfg.synced=&spellSynced;mirrorState.synced=&spellSynced;' in r and 'mirrorState.synced=nullptr;' in r,
 'the layer uses the bits only with the state cache in use, and only when no write interleaved with the record':'(cfg_.dirtyRecord&&cfg_.synced&&mv_)' in sl and 'sy->epoch==epoch0' in sl,
 'the layer clears the bits whenever it resets its Model':'model_.reset();if(cfg_.synced)cfg_.synced->clearAll();' in sl,
}
for k,v in checks.items():print('PASS' if v else 'FAIL',k)
assert all(checks.values())

# ---- the extension device: its raw methods and the stream's dispatch ----
raw=fp.src('extension_raw_methods.inl').read_text()
rawLines={mt.group(1):mt.group(0) for mt in re.finditer(r'HRESULT STDMETHODCALLTYPE (\w+)\([^\n]*',raw)}
trackedRaw=[k for k in HOOKED]
def rawOk(line):
    # every call of the backend vtable sits inside the rawActive() branch (a RawScope brackets it with invalidate()); the other branch is the mirror's own setter
    i=line.find('if(rawActive()){');j=line.find('real->')
    return i>=0 and 0<=j and j>i and line.find('return MirrorDevice::')>line.find('}return')>0 and line.count('real->')==1
checks2={
 'every raw Set of a tracked slot is inside the rawActive() branch and falls through to the mirror setter':all(k in rawLines and rawOk(rawLines[k]) for k in trackedRaw),
 'RawScope brackets its work with invalidate() (entry and exit)':re.search(r'explicit RawScope\(ExtensionDevice& owner\)[^{]*\{\s*owner\.m->invalidate\(\);\+\+owner\.m->rawDepth',m) is not None and re.search(r'~RawScope\(\)\{[^}]*device->m->invalidate\(\);--device->m->rawDepth',m) is not None,
 'the raw methods are only generated from the MirrorDevice overrides (a new tracked setter gets its raw branch automatically)':'selected = set(re.findall(' in (fp.SCRIPTS/'generate_extension_raw_methods.py').read_text(),
}
stream=fp.src('command_stream.inl').read_text()
def caseBlock(name):
    a=stream.find('    case Cmd::Device_'+name+': {',stream.find('inline bool dispatchGenerated'));b=stream.find('\n    case Cmd::',a+10)
    return stream[a:b] if a>=0 else ''
for k in HOOKED:
    blk=caseBlock(k)
    calls=re.findall(r'([\w.]+(?:\(\))?)->'+k+r'\(',blk)
    checks2['stream dispatch of '+k+' goes to the extension device or the Device, never a backend pointer']=bool(blk) and set(calls)==({'tr.device()'} if k=='SetRenderTarget' else {'_e','tr.device()'})
sd=fp.src('stream_device.h').read_text()
checks2['the stream replays direct calls on the ExtensionDevice the renderer gives it (same mirror)']='core.ext=opt.extension;' in sd and 'options.extension=target->extensionDevice();' in r and re.search(r'IDirect3DDevice9\* extensionDevice\(\)\s*\{\s*return ext;',r) is not None
sp=fp.src('stream_proxies.h').read_text()
checks2['StreamCore::ext is documented as the ExtensionDevice (same real device, same mirror)']='the ExtensionDevice (same real device, same mirror)' in sp
for k,v in checks2.items():print('PASS' if v else 'FAIL',k)
assert all(checks2.values())
