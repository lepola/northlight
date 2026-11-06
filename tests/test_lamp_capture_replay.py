#!/usr/bin/env python3
# northlight-test:
"""Capture replay uses journal indices, source flags and native color formats."""
from pathlib import Path
import tempfile
import struct
import numpy as np
from render_lamp_self_shadow import capture_plan,display_rgb,header,load,save,missing_owner_sources,RECORD
import northlight_paths as fp

records=[dict(event='pass',index=7,entry='LocalDirect16'),
         dict(event='pass',index=38,entry='MaterialComposite',input_group=5),
         dict(event='shader-binding',**{'pass':38},shader=9),
         dict(event='target',**{'pass':38},slot=0,format=113),
         dict(event='source',source_id='101',kind=2,flags=2),
         dict(event='source',source_id='102',kind=2,flags=0),
         dict(event='source',source_id='103',kind=1,flags=3)]
plan=capture_plan(records)
assert (plan['direct_pass'],plan['material_pass'],plan['material_shader'],plan['material_group'],plan['hdr'])==(7,38,9,5,True)
assert [r['source_id'] for r in plan['sources']]==['101']
pixels=np.array([[[.25,.5,2,1],[0,1,.125,.5]]],dtype='f4')
with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
    for fmt in (113,21,22):
        path=Path(folder)/f'{fmt}.fgr';save(path,pixels,fmt)
        assert header(path)==(2,1,fmt,8 if fmt==113 else 4)
        assert np.allclose(load(path),pixels if fmt==113 else np.clip(pixels,0,1),atol=1/255)
assert display_rgb(pixels,False)[0,0].tolist()==[64,128,255]
assert display_rgb(pixels,True)[0,0].tolist()==[137,188,255]
rows=[(0,0,0,1,.5,.2,1,12,sid,2,2) for sid in (100,101)]
fgl=struct.pack('<4sIII',b'FGL1',2,RECORD.size,len(rows))+b''.join(RECORD.pack(*r) for r in rows)
mutated=list(RECORD.iter_unpack(missing_owner_sources(fgl)[16:]))
assert [r[8] for r in mutated]==[101,102]
assert all(np.allclose(r[:8],rows[0][:8]) and r[9:]==rows[0][9:] for r in mutated)
print('LAMP_CAPTURE_REPLAY_PASS missing_owner_mutation dynamic_pass_shader_selection fp16_unclipped BGRA_roundtrip linear_sRGB_preview')
