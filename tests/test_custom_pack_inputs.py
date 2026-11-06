#!/usr/bin/env python3
# northlight-test:
"""Bound custom BLP tables and reject mismatched M2/SKIN without archive rewrites."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'renderer')]
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import blp_mips,decode_blp,skin_vertex_indices
from build_spell_asset_manifest import texture_manifest
import emitter_pin_report


def bc_texture(width=64,height=32,padding=0):
    data=bytearray(148);data[:4]=b'BLP2';struct.pack_into('<I',data,4,1)
    struct.pack_into('<4B2I',data,8,2,0,0,1,width,height)
    for level in range(max(width,height).bit_length()):
        w,h=max(1,width>>level),max(1,height>>level)
        block=struct.pack('<HHI',0xf800,0x001f,0)
        raw=block*(((w+3)//4)*((h+3)//4))+bytes(padding)
        struct.pack_into('<I',data,20+4*level,len(data));struct.pack_into('<I',data,84+4*level,len(raw));data.extend(raw)
    return data


class Assets:
    def __init__(self,blob):self.blob=blob;self.providers={'world\\lamp.m2':0}
    def read(self,path):return self.blob
    def origin(self,path):return 'memory/custom.mpq'
    def fingerprint(self):return {}


class CustomInputs(unittest.TestCase):
    def test_spare_mip_slots_are_ignored_without_changing_the_payload(self):
        for width,height in ((1,1),(17,3),(64,32),(1024,1024)):
            blob=bc_texture(width,height);levels=max(width,height).bit_length()
            for level in range(levels,16):
                struct.pack_into('<I',blob,20+4*level,0xfffffff0)
                struct.pack_into('<I',blob,84+4*level,0xfffffff0)
            before=bytes(blob);mips=blp_mips(blob)
            self.assertEqual(len(mips),levels)
            w,h,rgba=decode_blp(blob,1);self.assertEqual((w,h,rgba),(1,1,bytes((255,0,0,255))))
            manifest=texture_manifest(Assets(blob),'custom.blp')
            self.assertEqual(len(manifest['mips']),levels)
            self.assertEqual(manifest['sha256'],hashlib.sha256(before).hexdigest())
            self.assertEqual(bytes(blob),before)

    def test_padding_is_not_uploaded_as_extra_blocks(self):
        blob=bc_texture(8,4,padding=16);manifest=texture_manifest(Assets(blob),'custom.blp')
        for level,w,h,offset,size in blp_mips(blob):
            self.assertEqual(size,((w+3)//4)*((h+3)//4)*8)
            self.assertEqual(manifest['mips'][level]['raw_sha256'],hashlib.sha256(blob[offset:offset+size]).hexdigest())

    def test_real_mip_corruption_still_fails(self):
        for field,value in ((12,0),(20,100),(20,0xfffffff0),(84,1),(24,0xfffffff0)):
            blob=bc_texture();struct.pack_into('<I',blob,field,value)
            with self.assertRaises(ValueError):decode_blp(blob)
        for length in (0,20,147):
            with self.assertRaises(ValueError):blp_mips(bc_texture()[:length])

    def test_partial_chain_with_repeated_eof_offsets(self):
        for size in (0,8,0xfffffff0):
            blob=bc_texture(8,4);last=blp_mips(blob)[-1];del blob[last[3]:]
            struct.pack_into('<I',blob,20+4*last[0],len(blob));struct.pack_into('<I',blob,84+4*last[0],size)
            self.assertEqual(len(blp_mips(blob)),3)
            w,h,rgba=decode_blp(blob,1);self.assertEqual((w,h),(2,1));self.assertEqual(rgba,bytes((255,0,0,255))*2)
        blob=bc_texture();struct.pack_into('<I',blob,88,0)
        with self.assertRaisesRegex(ValueError,'Noncontiguous'):blp_mips(blob)

    def test_palette_alpha_extent_and_uncompressed_mips(self):
        blob=bytearray(1172);blob[:4]=b'BLP2';struct.pack_into('<4B2I',blob,8,1,4,0,1,3,1)
        blob[152:156]=bytes((10,20,30,0));struct.pack_into('<I',blob,20,1172);struct.pack_into('<I',blob,84,5)
        blob.extend(bytes((1,1,1,0x80,0x0f)))
        self.assertEqual(decode_blp(blob),(3,1,bytes((30,20,10,0,30,20,10,136,30,20,10,255))))
        struct.pack_into('<I',blob,84,4)
        with self.assertRaises(ValueError):decode_blp(blob)
        blob=bytearray(148);blob[:4]=b'BLP2';struct.pack_into('<4B2I',blob,8,3,8,0,1,1,1)
        struct.pack_into('<I',blob,20,148);struct.pack_into('<I',blob,84,4);blob.extend(bytes((10,20,30,40)))
        self.assertEqual(decode_blp(blob),(1,1,bytes((30,20,10,40))))

    def test_sub_byte_alpha_tail_preserves_the_last_complete_level(self):
        for bits,last_width in ((1,4),(4,2)):
            blob=bytearray(1172);blob[:4]=b'BLP2';blob[148:152]=bytes((0,0,255,0))
            struct.pack_into('<4B2I',blob,8,1,bits,0,1,32,16)
            for level in range(6):
                w,h=max(1,32>>level),max(1,16>>level);pixels=w*h
                raw=bytes(pixels)+bytes([255])*(pixels*bits//8)
                struct.pack_into('<I',blob,20+4*level,len(blob));struct.pack_into('<I',blob,84+4*level,len(raw));blob.extend(raw)
            self.assertEqual(blp_mips(blob)[-1][1],last_width)
            w,h,rgba=decode_blp(blob,1)
            self.assertEqual(w,last_width);self.assertEqual(rgba,bytes((255,0,0,255))*(w*h))

    def test_rectangular_bc_tail_does_not_invent_missing_blocks(self):
        blob=bc_texture(768,128);mip=blp_mips(blob)[6]
        struct.pack_into('<I',blob,84+4*mip[0],8)
        self.assertEqual(len(blp_mips(blob)),6)
        w,h,rgba=decode_blp(blob,1)
        self.assertEqual((w,h),(24,4));self.assertEqual(rgba,bytes((255,0,0,255))*(w*h))

    def test_skin_lookup_is_bounded_by_the_winning_model(self):
        model=bytearray(64);struct.pack_into('<I',model,60,3)
        skin=bytearray(44);skin[:4]=b'SKIN';struct.pack_into('<4I',skin,4,3,44,3,50)
        skin.extend(struct.pack('<6H',2,0,1,0,1,2))
        self.assertEqual(skin_vertex_indices(model,skin),[2,0,1])
        for offset in (44,50):
            bad=bytearray(skin);struct.pack_into('<H',bad,offset,3)
            with self.assertRaises(ValueError):skin_vertex_indices(model,bad)

    def test_one_incompatible_skin_does_not_abort_the_emitter_report(self):
        assets=Assets(b'model');path='world\\lamp.m2'
        profile={'asset_sha256':hashlib.sha256(assets.blob).hexdigest(),'position':[0,0,1],
                 'rgb':[1,1,1],'intensity':1,'start':0,'end':4}
        with patch.object(emitter_pin_report,'batches',side_effect=ValueError('SKIN vertex lookup exceeds winning M2')):
            record=emitter_pin_report.report(assets,{path:profile},with_batches=True)['profiles'][path]
        self.assertEqual(record['status'],'matched')
        self.assertIn('geometry_error',record);self.assertNotIn('batches',record)


if __name__=='__main__':unittest.main()
