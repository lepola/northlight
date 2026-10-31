"""Real client data for the tests, read from the tester's own 3.3.5a client. Not a test.

The repository holds no game bytes or game-derived data. A test that runs on real client data
names it here by identity and a digest, and gets it from the client:
- shader programs (the archive path, the variant index and the sha256 of the bytecode) from the
  client's stock archives (patch.mpq, patch-2.mpq, patch-3.mpq), so a modded client cannot change
  them; the test declares requires=client,stormlib. Cached under out/client-shaders/ and checked
  against the sha256 on every use.
- doodad placements (map tile, category, instance id and model key) from the client's world cache
  (<client>/world-cache, built by scripts/install_world_cache.py); the test declares
  requires=world-cache. Checked against a digest of the values rounded to 1e-5 (world caches
  built on different machines differ in the last float bit).
"""
import hashlib
import os
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp

# vs_3_0 Diffuse_T1_Env #20: a four-bone palette skin (variants 20-28 are the same program).
FOUR_BONE_VS3 = ('shaders\\vertex\\vs_3_0\\diffuse_t1_env.bls', 20,
                 '34890b73162b07089f337cf1b4197ffe8fb8c6b0e1b1c2765b1e4c10b77e451c')
# Body layout observed in the live bounds-budget diagnostic (sliced hash 5267e7a8cb988451).
FOUR_BONE_BODY_VS3 = ('shaders\\vertex\\vs_3_0\\cdiffuse_t1.bls', 29,
                      '634f2d5cbf41431f82736de9edc3eee87d0242aabe6ed4edc7f1b69b9f19a371')
# The one-influence palette program every boneInfluences=1 M2 skin section uses (signs, barrels,
# lamps, weapons), its active 3.3.5a variants, and the zero-influence program.
PROGRAMS = {
    'OneBoneVs3': ('shaders\\vertex\\vs_3_0\\diffuse_t1.bls', 11,
                   '3e91eb0a169c480cb448cf5b122333a4205a13a5d284ecc6a6c517725109c27c', 'vs_3_0 Diffuse_T1 #11: r0.w first, t=r1'),
    'OneBoneVs3WLast': ('shaders\\vertex\\vs_3_0\\diffuse_t1.bls', 10,
                        '35bdfcc057e0cbced24e8d3c621118779af2cd1d44f8cf732f87df724ee8d519', 'vs_3_0 Diffuse_T1 #10: r0.w last, t=r0'),
    'OneBoneVs3WMid': ('shaders\\vertex\\vs_3_0\\cdiffuse_t1.bls', 43,
                       'c9b94457c9715ae51f94b2b597ef33a1aba0c3f4f23e1595eb46c6bd5c585904', 'vs_3_0 CDiffuse_T1 #43: r0.w between dp4 y and z'),
    'OneBoneVs2': ('shaders\\vertex\\vs_2_0\\diffuse_t1.bls', 11,
                   'cb2a89ded8df02f076e22b2139aca0f99bc6ce662b3154e2ec004372324ae04b', 'vs_2_0 Diffuse_T1 #11: mul v1.x, c0.x (operands swapped)'),
    'NoBoneVs3': ('shaders\\vertex\\vs_3_0\\diffuse_t1.bls', 0,
                  '4af184180cf9ca9c6b404eb52023ac46472189b0b324adb6f41c87e343df07c7', 'vs_3_0 Diffuse_T1 #0: boneInfluences=0 (c31..33 without a0: not a palette)'),
}


def _read(wanted):
    """{(path, variant): bytecode} for [(path, variant, sha256)], from the client's stock archives."""
    fp.use_source_modules()
    from extract_signatures import variants
    from world_scene_builder import Assets
    assets = Assets(archives='stock')
    try:
        found = {}
        for path, variant, sha in wanted:
            code = dict((i, c) for i, _, c in variants(assets.read(path))).get(variant)
            if code is None or hashlib.sha256(code).hexdigest() != sha:
                raise SystemExit(f'client_fixtures: {path} variant {variant} in {assets.client} is not the stock 3.3.5a program')
            found[path, variant] = code
        return found
    finally:
        assets.close()


def shaders_available():
    """True if a client and StormLib are configured (for optional sub-cases that use the programs)."""
    try:
        fp.client_root()
        fp.stormlib()
        return True
    except fp.Missing:
        return False


def shared_gpu_models(directory):
    """Exact stock 48-byte M2 vertices and skin subsets for cross-pass residency."""
    fp.use_source_modules()
    from world_scene_builder import Assets
    assets=Assets(archives='stock');sources=[];meshes=[]
    try:
        for model,expected_model,expected_skin in (
            ('character\\scourge\\male\\scourgemale.m2','a40783df61b12b0e053bd21fc5a930ae37cc649423b79e2f4b076e048fe2a220','ad6cc380ec007257a2ffdbbca5600c1c8f0d647ef81f70af058091d14e6a2349'),
            ('character\\human\\male\\humanmale.m2','b22478c1ab8d1e9bb7542f686e60e4e665a510d11b5360109a3e9e46d5bf91d2','cfc65fba893f05335bbd054b083b910455ef852c57ca8094ff4549741596ed0e'),
            ('character\\orc\\male\\orcmale.m2','76097ce5227249cf02064686f146721211a0a08864ff1e97a92424fd9775d703','a382a53f35469aba1854d7c941544ace29145b2c1fddec02f41529ba7c290caa')):
            data=assets.read(model);skin=assets.read(model[:-3]+'00.skin')
            assert hashlib.sha256(data).hexdigest()==expected_model and hashlib.sha256(skin).hexdigest()==expected_skin,model
            assert data[:4]==b'MD20' and skin[:4]==b'SKIN'
            sources.append({'path':model,'model_sha256':expected_model,'skin_sha256':expected_skin,'archive':assets.origin(model)})
            vertex_count,vertex_at=struct.unpack_from('<II',data,60)
            count,at=struct.unpack_from('<II',skin,4);lookup=struct.unpack_from('<%dH'%count,skin,at)
            count,at=struct.unpack_from('<II',skin,12);indices=[lookup[x] for x in struct.unpack_from('<%dH'%count,skin,at)]
            count,at=struct.unpack_from('<II',skin,28)
            for subset in struct.iter_unpack('<10H7f',skin[at:at+count*48]):
                first=subset[4]+(subset[1]<<16);n=subset[5]
                assert first+n<=len(indices) and n%3==0
                if not n:continue
                used=indices[first:first+n];lo,hi=min(used),max(used);assert hi<vertex_count
                vertices=data[vertex_at+lo*48:vertex_at+(hi+1)*48];assert len(vertices)==(hi-lo+1)*48
                meshes.append((vertices,[i-lo for i in used]))
    finally:
        assets.close()
    fixture=struct.pack('<I',len(meshes))+b''.join(struct.pack('<II',len(v),len(i))+v+struct.pack('<%dI'%len(i),*i) for v,i in meshes)
    assert len(meshes)==177 and hashlib.sha256(fixture).hexdigest()=='cb99fccf5cb39f54e485c66042e7c2dbb616a87cec0d0b306dac8f3d53c76183'
    target=Path(directory)/'stock-m2.bin';target.write_bytes(fixture)
    return target,sources,len(meshes)


def four_bone_vs3():
    """Path of the four-bone vs_3_0 program (raw D3D9 bytecode), cached in out/client-shaders/."""
    path, variant, sha = FOUR_BONE_VS3
    target = fp.out() / 'client-shaders' / 'actor_four_bone_vs3.bin'
    if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == sha:
        return target
    code = _read([FOUR_BONE_VS3])[path, variant]
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(f'{target.name}.{os.getpid()}.tmp')   # tests run in parallel: replace atomically
    tmp.write_bytes(code)
    os.replace(tmp, target)
    return target


def four_bone_body_vs3():
    """Path of the four-bone body vs_3_0 program (raw D3D9 bytecode), cached in out/client-shaders/."""
    path, variant, sha = FOUR_BONE_BODY_VS3
    target = fp.out() / 'client-shaders' / 'actor_four_bone_body_vs3.bin'
    if target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == sha:
        return target
    code = _read([FOUR_BONE_BODY_VS3])[path, variant]
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(f'{target.name}.{os.getpid()}.tmp')   # tests run in parallel: replace atomically
    tmp.write_bytes(code)
    os.replace(tmp, target)
    return target


def actor_client_programs(directory):
    """Write actor_client_programs.h (namespace ClientShaders, one uint32 array per PROGRAMS entry)
    into directory, for a native test built with -I directory."""
    rows = ['// The client vertex programs of tests/client_fixtures.py PROGRAMS, read from the tester\'s client.',
            '// Generated by tests/client_fixtures.py; never commit.', '#pragma once', '#include <cstdint>',
            'namespace ClientShaders {']
    codes = _read([(p, v, s) for p, v, s, _ in PROGRAMS.values()])
    for name, (path, variant, _, note) in PROGRAMS.items():
        code = codes[path, variant]
        words = struct.unpack(f'<{len(code) // 4}I', code)
        rows += [f'// {note}', f'static const std::uint32_t {name}[]={{' + ','.join(f'0x{w:08x}' for w in words) + '};']
    target = Path(directory) / 'actor_client_programs.h'
    target.write_text('\n'.join(rows + ['}']) + '\n')
    return target


# Stormwind trade district and Goldshire (Azeroth tiles 30_48 and 31_49): category 3 WMO doodads and
# category 1 ADT doodads of one-bone boneInfluences=1 models (signs, planters, rugs, paintings,
# banners, crates, jars). (tile, category, instance id, first 16 hex digits of the FGS3 model key)
RIGID_PLACEMENTS = [
    ('30_48', 3, 0x273f0000001c, 'f91b93bc6c736dd3'), ('30_48', 3, 0x273f000014fe, 'd29b6fd8f1bbb116'),
    ('30_48', 3, 0x273f00001505, '9ac98334f8c3f7a2'), ('30_48', 3, 0x273f000014c8, '13d9caff02860713'),
    ('30_48', 3, 0x273f00000f3a, '5c82a6c5355a5bac'), ('30_48', 3, 0x273f000001d3, 'ce0c9b7cdb8fc413'),
    ('30_48', 3, 0x273f000014f7, '6e30a861a6d36a83'), ('30_48', 3, 0x273f0000071a, '826d6d808c5049a9'),
    ('30_48', 3, 0x273f000001c9, '9b0c92c908f191e8'), ('30_48', 3, 0x273f00000f83, '714e3b53dbe8703e'),
    ('30_48', 3, 0x273f000001d1, 'cff944d76c52f6de'), ('30_48', 3, 0x273f00000019, '389424421c311be7'),
    ('31_49', 3, 0x177300000001c, '32edc8fc16514b44'), ('31_49', 3, 0x116f600000272, '5c82a6c5355a5bac'),
    ('31_49', 1, 0x30c1, 'bf8504cc5c9a1a35'), ('31_49', 1, 0x2fa5, '338cfe5d9cb91434'),
    ('31_49', 1, 0x2fa4, '70a03b325b1fdbda'), ('31_49', 1, 0x2fa6, 'ee43399235a827e9'),
    ('31_49', 1, 0x116f9, '780b00e932a5fad3'), ('31_49', 1, 0x3016, 'bc5fd6e502bfa533'),
]
RIGID_PLACEMENTS_DIGEST = '5c5449f511ef9bf879abdd0f24a75906653458db006e90e16b2ba44609b0e00e'


def rigid_placements(directory):
    """Write stormwind_rigid_placements.inc (one {category,"<model key>",{3x3 model->world matrix,
    row-major, column vector},{translation}} row per RIGID_PLACEMENTS entry) into directory."""
    root = fp.world_cache() / 'Azeroth'
    tiles, rows, values = {}, [], []
    for tile, category, uid, key in RIGID_PLACEMENTS:
        if tile not in tiles:
            data = (root / f'{tile}.fg3').read_bytes()
            tiles[tile] = list(struct.iter_unpack('<32sQI18f', data[12:]))
        found = [r for r in tiles[tile] if r[1] == uid and r[2] == category and r[0].hex().startswith(key)]
        if len(found) != 1:
            raise SystemExit(f'client_fixtures: world cache {root / tile}.fg3 has {len(found)} instances {uid:#x} of model {key}')
        m, t = found[0][9:18], found[0][18:21]   # after the key, id, category and bounds
        values += [*m, *t]
        rows.append(f'    {{{category},"{key}",{{' + ','.join(f'{v!r}f' for v in m) + '},{' + ','.join(f'{v!r}f' for v in t) + '}},')
    digest = hashlib.sha256(' '.join(f'{round(v, 5) + 0.0:.5f}' for v in values).encode()).hexdigest()
    if digest != RIGID_PLACEMENTS_DIGEST:
        raise SystemExit(f'client_fixtures: the world cache in {root.parent} does not hold the expected placements ({digest})')
    target = Path(directory) / 'stormwind_rigid_placements.inc'
    target.write_text('// Generated by tests/client_fixtures.py from the tester\'s world cache; never commit.\n' + '\n'.join(rows) + '\n')
    return target


if __name__ == '__main__':
    print(four_bone_vs3())
