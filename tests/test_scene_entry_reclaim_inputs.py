#!/usr/bin/env python3
"""Pure Python failure controls for the real scene-episode stand input guards."""
import ast
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
from unittest.mock import patch

source=Path(__file__).with_name('validate_scene_entry_reclaim.py')
tree=ast.parse(source.read_text())
functions=[node for node in tree.body if isinstance(node,ast.FunctionDef)]
scope={'Path':Path,'hashlib':hashlib,'json':json,'re':re}
exec(compile(ast.Module(body=functions,type_ignores=[]),str(source),'exec'),scope)
checked=scope['fixture_inputs'];fresh=scope['fresh_output'];fingerprints=scope['source_inputs'];dependencies=scope['dependency_paths']
def refuses(call):
    try:call()
    except (AssertionError,RuntimeError,OSError,KeyError):return
    raise AssertionError('A negative input control was accepted')

with tempfile.TemporaryDirectory(prefix='scene-entry-inputs-') as folder:
    root=Path(folder);fixture=root/'fixture';fixture.mkdir();models=fixture/'world-cache/models';models.mkdir(parents=True)
    tiles=fixture/'world-cache/Northrend';tiles.mkdir();model=models/('00'*32+'.fgs');tile=tiles/'31_22.fg3'
    model.write_bytes(b'FGS2'+struct.pack('<4I',2,0,0,0));tile.write_bytes(b'FGS3'+struct.pack('<2I',3,1)+bytes(116))
    manifest={'format':1,'map':'Northrend','tiles':[{'tile':[31,22]}],'outputs':{},'limitations':['Synthetic guard controls'],
              'missing_tiles':[],'unsupported':[],'missing_assets':[],'mpq_view':{'unreadable':[],'unlisted':[]}}
    def save():
        manifest['outputs']={str(p.relative_to(fixture)).replace('\\','/'):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                             for p in [model,tile] if p.exists()}
        (fixture/'manifest.json').write_text(json.dumps(manifest))
    save();assert checked(fixture)['manifest_sha256']
    for key in ['missing_tiles','unsupported','missing_assets']:
        manifest[key]=['bad'];save();refuses(lambda:checked(fixture));manifest[key]=[]
    for key in ['unreadable','unlisted']:
        manifest['mpq_view'][key]=['bad'];save();refuses(lambda:checked(fixture));manifest['mpq_view'][key]=[]
    save();good=model.read_bytes();model.write_bytes(good[:-1]+b'!');refuses(lambda:checked(fixture));model.write_bytes(good)
    model.unlink();refuses(lambda:checked(fixture));save();refuses(lambda:checked(fixture));model.write_bytes(good);save()
    model.write_bytes(b'FGS2'+struct.pack('<4I',99,0,0,0));save();refuses(lambda:checked(fixture));model.write_bytes(good);save()
    extra=fixture/'unlisted';extra.write_bytes(b'!');refuses(lambda:checked(fixture));extra.unlink()
    original=Path.open
    def unreadable(path,*args,**kwargs):
        if path==model:raise PermissionError('Controlled unreadable input')
        return original(path,*args,**kwargs)
    with patch.object(Path,'open',unreadable):refuses(lambda:checked(fixture))
    assert checked(fixture)['outputs']==manifest['outputs']
    output=root/'output';fresh(output);stale=output/'validation.json';stale.write_text('preserve stale evidence')
    refuses(lambda:fresh(output));assert stale.read_text()=='preserve stale evidence';refuses(lambda:fresh(stale))
    empty=root/'empty';empty.mkdir();fresh(empty)
    before=fingerprints([model]);model.write_bytes(good+b'!');assert fingerprints([model])!=before;model.write_bytes(good)
    spaced=root/'actual header.h';spaced.write_text('// input');dep=root/'test.d'
    escaped=lambda p:str(p).replace('\\','/').replace(' ','\\ ')
    dep.write_text(escaped(root/'target.o')+': '+escaped(model)+' \\\n '+escaped(spaced)+'\n')
    assert dependencies(dep)=={model.resolve(),spaced.resolve()}
print('PASS scene entry stand input guards: valid/hash/corrupt/missing/reference/version/unlisted/unreadable, fresh/stale output, source drift, escaped compiler dependencies')
