"""Transitive shader inputs and embedded bytecode must agree before host linking."""
# northlight-test:
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/shaders'))
import shader_manifest as manifest


class ShaderManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.shaders=self.root/'shaders';self.shaders.mkdir()
        (self.shaders/'compiled').mkdir();(self.root/'src/generated').mkdir(parents=True)
        self.source=self.shaders/'effects.hlsl';self.source.write_bytes(b'#include "colour.hlsli"\nmain')
        self.include=self.shaders/'colour.hlsli';self.include.write_bytes(b'colour')
        for name in manifest.TOOLS:
            p=self.root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'tool')
        self.before=manifest.snapshot(self.root,self.root/'scripts/shaders/shader_compiler.py')
        self.binary=self.shaders/'compiled/Test.bin';self.binary.write_bytes(struct.pack('<II',0xffff0300,0xffff))
        self.asm=Path(str(self.binary)+'.asm');self.asm.write_bytes(b'ps_3_0\n')
        self.entry={'source':'shaders/effects.hlsl','dependencies':{str(p.relative_to(self.root)):manifest.sha256(p) for p in (self.source,self.include)},
            'compiler_inputs':{name:self.before[name] for name in manifest.TOOLS},
            'compiler_driver_sha256':'a'*64,'compiler_sha256':sorted(manifest.TRUSTED_COMPILERS)[0],
            'sha256':manifest.sha256(self.binary),'assembly_sha256':manifest.sha256(self.asm),
            'target':'ps_3_0','symbol':'kTestShader',**manifest.validate_assembly(self.asm,'ps_3_0')}
        self.record={'shaders':{'Test':self.entry}}
        self.headers={'src/generated/test.h':b'const DWORD kTestShader[] = {0xffff0300, 0x0000ffff};\n'}

    def publish(self):
        manifest.publish(self.root,self.shaders/'test-build.json',self.record,self.headers)

    def test_published_header_and_complete_inputs_validate(self):
        self.publish();manifest.validate_manifest(self.root,self.record)
        self.assertEqual(manifest.validate_entry(self.root,'Test',self.entry),self.binary.read_bytes())

    def test_include_tool_binary_or_header_edit_rejected(self):
        self.publish()
        for p in (self.include,self.root/manifest.TOOLS[0],self.binary,self.asm,self.root/'src/generated/test.h'):
            with self.subTest(path=p):
                original=p.read_bytes();p.write_bytes(original+b'changed')
                with self.assertRaises(ValueError):manifest.validate_manifest(self.root,self.record)
                p.write_bytes(original)
        manifest.validate_manifest(self.root,self.record)

    def test_publisher_rejects_wrong_embedded_bytecode_before_writing(self):
        self.headers['src/generated/test.h']=b'const DWORD kTestShader[]={0xffff0300,0x00000000};'
        with self.assertRaisesRegex(ValueError,'embedded header'):self.publish()
        self.assertFalse((self.shaders/'test-build.json').exists())
        self.assertFalse((self.root/'src/generated/test.h').exists())

    def test_retained_entry_cannot_be_relabelled_after_header_only_edit(self):
        self.include.write_bytes(b'new colour')
        with self.assertRaisesRegex(ValueError,'stale shaders/colour.hlsli'):
            manifest.validate_entry(self.root,'Test',self.entry)

    def test_incomplete_or_unrecognised_compiler_provenance_rejected(self):
        original=self.entry['compiler_sha256'];self.entry['compiler_sha256']='unknown'
        with self.assertRaisesRegex(ValueError,'compiler'):self.publish()
        self.entry['compiler_sha256']=original;self.entry.pop('dependencies')
        with self.assertRaisesRegex(ValueError,'incomplete'):self.publish()

    def test_actual_reads_reject_reverted_intermediate_input(self):
        path=self.root/'reads.inputs'
        text=''.join(f'{p}\t{manifest.sha256(p)}\n' for p in (self.source,self.include))
        path.write_bytes(text.encode())
        self.assertEqual(manifest.actual_inputs(self.root,path,self.before,self.source),self.entry['dependencies'])
        path.write_bytes(text.replace(manifest.sha256(self.include),'0'*64).encode())
        with self.assertRaisesRegex(ValueError,'during compilation'):
            manifest.actual_inputs(self.root,path,self.before,self.source)

    def test_resource_report_must_match_disassembly(self):
        self.entry['temporary_registers']=32
        with self.assertRaisesRegex(ValueError,'resource report'):self.publish()
        self.asm.write_bytes(b'ps_3_0\ndcl_cube s0\ntexldl r0, c0, s0\n')
        stats=manifest.validate_assembly(self.asm,'ps_3_0')
        self.assertEqual(stats['static_instruction_slots'],5)
        self.assertEqual(stats['cube_instruction_extra_slots'],3)

    def test_unsafe_paths_and_duplicate_symbols_rejected(self):
        self.entry['dependencies']['../outside.hlsli']='bad'
        with self.assertRaisesRegex(ValueError,'Unsafe'):self.publish()
        del self.entry['dependencies']['../outside.hlsli']
        self.headers['src/generated/duplicate.h']=self.headers['src/generated/test.h']
        with self.assertRaisesRegex(ValueError,'duplicated'):self.publish()


if __name__=='__main__':unittest.main()
