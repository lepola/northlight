#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact legacy/current FGR register banks and honest offline input provenance; no GPU/game."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

spec = importlib.util.spec_from_file_location('world_diagnostics', fp.RENDERER / 'analyze_world_diagnostics.py')
diagnostics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostics)

harness = r'''
#define NORTHLIGHT_FGR_CONSTANT_BANK_ONLY
#include "removal_distance_gpu.cpp"
int main(int argc,char** argv){
    assert(argc==2);
    const auto bytes=read(argv[1]);const auto bank=constantBank(bytes);
    assert(bank.count==bytes.size()/16);
    assert(std::memcmp(bank.values,bytes.data(),bytes.size())==0);
    const unsigned char zero[16]{};
    for(unsigned i=bank.count;i<79;++i)assert(std::memcmp(bank.values[i],zero,16)==0);
    for(unsigned size=0;size<=79*16+16;++size){
        bool accepted=false;
        try{const auto value=constantBank(std::vector<unsigned char>(size));accepted=value.count*16==size;}
        catch(const std::invalid_argument&){}
        assert(accepted==(size==68*16||size==79*16));
    }
    for(unsigned size:{4096u,65536u}){
        bool rejected=false;
        try{constantBank(std::vector<unsigned char>(size));}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
    std::printf("PASS %u registers: exact bytes, zero unused tail, malformed lengths refused\n",bank.count);
}
'''


def write_image(path, width, height, fmt, layout, value):
    pixel = struct.pack('<' + layout, *value)
    path.write_bytes(struct.pack('<5I', 0x31524746, width, height, fmt, len(pixel)) + pixel * (width * height))


with tempfile.TemporaryDirectory(prefix='fgr-constant-bank-') as temporary:
    folder = Path(temporary)
    source = folder / 'fixture.cpp'
    source.write_text(harness, encoding='utf-8')
    executable = folder / 'fixture'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-I', str(fp.TESTS), str(source), '-o', str(executable)], check=True)
    for count in (68, 79):
        # Includes signed zero, NaNs, infinities and subnormals: register transport
        # must copy bits, even when an unused register is not a finite float.
        words = [0x00000000, 0x80000000, 0x7fc12345, 0x7f812345, 0x7f800000, 0xff800000, 0x00000001]
        raw = struct.pack('<' + 'I' * (count * 4), *(words[i % len(words)] for i in range(count * 4)))
        bank_path = folder / 'transport.f32'
        bank_path.write_bytes(raw)
        subprocess.run([str(executable), str(bank_path)], check=True)

    for size in (*range(79 * 16 + 17), 4096, 65536):
        try:
            bank = diagnostics.constant_bank(bytes(size))
        except ValueError:
            assert size not in (68 * 16, 79 * 16), size
        else:
            assert size in (68 * 16, 79 * 16) and len(bank) * 16 == size, size

    # Exercise the real analyzer on a complete tiny buffer set, comparing all
    # legacy results while preserving every additional current-bank register.
    prefix = folder / 'capture-1-'
    for name in ('light', 'temporal-light', 'baseline', 'fog'):
        write_image(Path(str(prefix) + name + '.fgr'), 2, 2, 113, '4e', (.2, .3, .4, 1.))
    write_image(Path(str(prefix) + 'normals.fgr'), 2, 2, 113, '4e', (0., 0., 1., 1.))
    write_image(Path(str(prefix) + 'distance.fgr'), 2, 2, 114, 'f', (25.,))
    write_image(Path(str(prefix) + 'shadow-far.fgr'), 2, 2, 114, 'f', (1.,))
    write_image(Path(str(prefix) + 'scene.fgr'), 4, 4, 22, '4B', (30, 40, 50, 255))
    c = [[float(i * 4 + j) / 1024 for j in range(4)] for i in range(79)]
    c[0] = [.25, .25, .2, 1277.]
    c[1] = [1., 1., 1., 0.]
    for first in (3, 11):
        for i in range(4):
            c[first + i] = [float(i == j) for j in range(4)]
    reports = []
    for count in (68, 79):
        raw = struct.pack('<' + 'f' * (count * 4), *(x for row in c[:count] for x in row))
        Path(str(prefix) + 'constants.f32').write_bytes(raw)
        parsed = diagnostics.constant_bank(raw)
        assert struct.pack('<' + 'f' * (count * 4), *(x for row in parsed for x in row)) == raw
        report = diagnostics.analyze(folder)
        assert report['constant_bank'] == dict(register_count=count, raw_hex=raw.hex(), stage='unversioned')
        provenance = report['input_provenance']
        assert provenance['depth_reconstructed'] and provenance['per_source_bank_missing']
        assert not any(provenance[key] for key in ('invented_fog_environment', 'material_inputs_complete',
                                                   'authentic_source_replay', 'whole_scene_replay'))
        reports.append({key: value for key, value in report.items() if key != 'constant_bank'})
    assert reports[0] == reports[1], 'Legacy analysis changed with an appended register bank'
    for name, device in (('removal_distance_gpu.cpp', 'dev'), ('world_pass_gpu.cpp', 'd'),
                         ('composite_distance_gpu.cpp', 'dev'), ('regional_fog_cache_gpu.cpp', 'd')):
        text = (fp.TESTS / name).read_text()
        assert f'{device}->SetPixelShaderConstantF(0,c[0],bank.count)' in text
        assert 'constantBank(read(prefix+"constants.f32"))' in text

print('PASS FGR 68/79 banks: exact register transport, strict lengths, legacy analysis and incomplete-source provenance.')
