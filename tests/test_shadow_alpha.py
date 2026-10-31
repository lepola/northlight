#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Synthetic native shader masks, randomized alpha evaluation and rejection."""
from pathlib import Path
import struct, subprocess, sys, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def program(product):
    words=[0xffff0300,0x0200001f,0x8000000a,0x900f0000,
           0x0200001f,0x80000005,0x900f0001]
    if product:
        words += [0x03000042,0x800f0000,0x90e40001,0xa0e40800,
                  0x03000005,0x80080000,0x80ff0000,0x90ff0000]
    words += [0x03000002,0x80070001,0x80ff0000 if product else 0x90ff0000,
              0xa1000000,0x01000041,0x800f0001,0x0000ffff]
    return struct.pack('<'+'I'*len(words),*words)

with tempfile.TemporaryDirectory(prefix='shadow-alpha-') as temp:
    temp=Path(temp);corpus=temp/'masks.bin'
    with corpus.open('wb') as f:
        for i in range(120):
            data=program(i%2==0);f.write(struct.pack('<I',len(data)));f.write(data)
    exe=temp/'test'
    subprocess.run(['clang++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
                    *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(corpus)],check=True)
