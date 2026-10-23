#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Server creature-entry resolution, lifecycle preservation and MPQ-only scope."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import northlight_paths
northlight_paths.use_source_modules()
from build_mount_emitters import build, creature_cache, mount_models
from test_spell_emitter_patch import Assets, model, native_sample
from mpq import Archive


def table(rows, fields, strings=b'\0'):
    return struct.pack('<4s4I', b'WDBC', len(rows), fields, fields*4, len(strings)) + b''.join(struct.pack('<%dI' % fields, *r) for r in rows) + strings


def cache(entry=284, displays=(900,)):
    body = bytearray(b'horse\0\0\0\0\0\0' + bytes(77))
    struct.pack_into('<4I', body, len(body)-77+24, *(tuple(displays)+(0,)*4)[:4])
    return b'BOMW' + struct.pack('<5I', 12340, 0, 108, 1, 0) + struct.pack('<II', entry, len(body)) + body + bytes(8)


def assets(replacement=False):
    original, particle = model()
    if replacement:
        original = bytearray(original)
        struct.pack_into('<h', original, particle+42, 0)
        original = bytes(original)
    spell = [0]*234
    spell[0], spell[71], spell[95], spell[110] = 458, 6, 78, 284
    good, trap = [0]*16, [0]*16
    good[0], good[1] = 900, 77
    trap[0], trap[1] = 284, 99  # NPC entry != display ID.
    variant = [0]*16
    variant[0], variant[1], variant[13] = 901, 77, 123 if replacement else 0
    names = b'\0Creature\\real_mount.mdx\0Creature\\wrong_ghost.mdx\0'
    mount, ghost = [0]*28, [0]*28
    mount[0], mount[2] = 77, names.index(b'Creature\\real')
    ghost[0], ghost[2] = 99, names.index(b'Creature\\wrong')
    return Assets(original, {
        'dbfilesclient\\spell.dbc':table([spell],234),
        'dbfilesclient\\creaturedisplayinfo.dbc':table([good,trap,variant],16),
        'dbfilesclient\\creaturemodeldata.dbc':table([mount,ghost],28,names),
    }), particle


class MountPatch(unittest.TestCase):
    def test_entry_is_resolved_through_server_display_ids(self):
        source, _ = assets()
        rows, missing = mount_models(source, creature_cache(cache()))
        self.assertEqual(list(rows), ['creature\\real_mount.m2'])
        self.assertEqual(rows['creature\\real_mount.m2']['references'],
                         [{'creature':284, 'display':900, 'spells':[458]}])
        self.assertEqual(missing, {})
        rows, missing = mount_models(source, {})
        self.assertEqual(rows, {})
        self.assertIn(284, missing)

    def test_cache_refuses_wrong_build_and_partial_records(self):
        self.assertEqual(creature_cache(cache()), {284:(900,)})
        for bad in (b'', cache()[:23], cache()[:24]+b'\x01',
                    cache()[:-9], cache()+b'x',
                    cache()[:4]+struct.pack('<I',5875)+cache()[8:]):
            with self.assertRaises((ValueError, struct.error)):
                creature_cache(bad)

    def test_shared_display_replacement_is_not_guessed(self):
        source, _ = assets(replacement=True)
        rows, _ = mount_models(source, creature_cache(cache()))
        self.assertEqual(rows['creature\\real_mount.m2']['replacement_colors'], [123])
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            with self.assertRaisesRegex(ValueError, 'No proven'):
                build(source, cache(), Path(folder))

    def test_model_id_aliases_share_the_same_patched_file(self):
        source, _ = assets(replacement=True)
        display = bytearray(source.extra['dbfilesclient\\creaturedisplayinfo.dbc'])
        struct.pack_into('<I', display, 20+2*64+4, 99)
        source.extra['dbfilesclient\\creaturedisplayinfo.dbc'] = bytes(display)
        models = bytearray(source.extra['dbfilesclient\\creaturemodeldata.dbc'])
        struct.pack_into('<I', models, 20+112+8, 1)
        source.extra['dbfilesclient\\creaturemodeldata.dbc'] = bytes(models)
        rows, _ = mount_models(source, creature_cache(cache()))
        self.assertEqual(rows['creature\\real_mount.m2']['replacement_colors'], [123])

    def test_mount_rate_enable_and_original_assets_are_preserved(self):
        source, particle = assets()
        original_dbc = dict(source.extra)
        wdb = cache()
        with tempfile.TemporaryDirectory(dir=northlight_paths.output_dir()) as folder:
            result = build(source, wdb, Path(folder))
            self.assertEqual(result['counts']['patched'], 1)
            self.assertEqual((result['counts']['dbc_files'], result['counts']['wdb_files']), (0,0))
            with Archive(Path(folder)/'mount-lights-staged.mpq') as archive:
                after = archive.read('creature\\real_mount.m2')
                for name in original_dbc:
                    self.assertFalse(archive.has(name))
            before = source.blob
            light = struct.unpack_from('<I', after, 268)[0]
            for time in (0,50,100,149,150,200,249,250,275,300,999):
                rate = native_sample(before, particle+176, '<f', time)
                enabled = native_sample(before, particle+456, '<B', time, default=1)
                self.assertAlmostEqual(native_sample(after,light+76,'<f',time), rate/10*.35)
                self.assertEqual(native_sample(after,light+136,'<B',time,default=1),enabled)
            preserved = bytearray(after[:len(before)])
            for a,b in ((20,28),(264,272)):
                preserved[a:b] = before[a:b]
            self.assertEqual(preserved, before)
        self.assertEqual(source.extra, original_dbc)
        self.assertEqual(cache(), wdb)


if __name__ == '__main__':
    unittest.main()
