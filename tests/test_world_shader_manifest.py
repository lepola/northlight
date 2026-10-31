#!/usr/bin/env python3
# northlight-test:
"""Every shipped world shader agrees with its bytecode, header, assembly and device budget."""
from pathlib import Path
import hashlib,json,re,struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from compile_world_shaders import ENTRIES
from shader_compiler import bytecode,validate_assembly,DLL_SHA256,COMPATIBLE_DLL_SHA256

manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())
header=fp.src('world_compiled_shaders.h').read_text()
assert manifest['source_sha256']==hashlib.sha256((fp.SHADERS/'world_effects.hlsl').read_bytes()).hexdigest()
assert manifest['bump_include_sha256']==hashlib.sha256((fp.SHADERS/'auto_bump_normal.hlsl').read_bytes()).hexdigest()
assert set(manifest['shaders'])=={entry for entry,_ in ENTRIES}
extended={'WorldFogActor','FogCloudsActor'}
tiled={'WorldFogActorTiled','FogCloudsActorTiled'}

def verify(entry,target,info,data,words):
    assert info['target']==target and info['bytes']==len(data)
    assert info['sha256']==hashlib.sha256(data).hexdigest(),entry
    assert info.get('compiler_sha256',manifest['compiler_sha256']) in (DLL_SHA256,COMPATIBLE_DLL_SHA256)
    assert re.fullmatch('[0-9a-f]{64}',info.get('source_sha256',manifest['source_sha256']))
    compiled=re.search(r'\bk'+entry+r'Shader\[\]\s*=\s*\{([^}]+)\}',header)[1]
    shipped=[int(value,16) for value in re.findall(r'0x[0-9a-fA-F]+',compiled)]
    assert shipped==list(words),entry
    assert struct.pack('<'+'I'*len(words),*words)==data
    stats=validate_assembly(fp.COMPILED/(entry+'.bin.asm'),target,
                            slot_limit=768 if entry in tiled else 600 if entry in extended else 512)
    assert all(info[key]==value for key,value in stats.items()),entry
    if entry in extended|tiled:
        assert f'k{entry}ShaderSlots = {stats["static_instruction_slots"]};' in header

for entry,target in ENTRIES:
    data,words=bytecode(fp.COMPILED/(entry+'.bin'),target)
    info=manifest['shaders'][entry]
    verify(entry,target,info,data,words)
    for wrong_data,wrong_info in ((data[:-4]+bytes(4),info),(data,{**info,'static_instruction_slots':0})):
        try:verify(entry,target,wrong_info,wrong_data,words)
        except AssertionError:pass
        else:raise AssertionError('corrupt bytecode/metadata was accepted: '+entry)
world=fp.src('world_renderer.h').read_text()
assert 'fogCaps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorShaderSlots,kFogCloudsActorShaderSlots)' in world
assert 'fogCaps.MaxPixelShader30InstructionSlots>=std::max(kWorldFogActorTiledShaderSlots,kFogCloudsActorTiledShaderSlots)' in world
print(f'PASS {len(ENTRIES)} shader binaries/header arrays, assembly metrics, capabilities; corrupt data/slot controls rejected')
