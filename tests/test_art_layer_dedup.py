#!/usr/bin/env python3
# northlight-test:
"""Unproven source equality must never discard a generated art-layer replacement."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from build_art_layer import unchanged_files
from mpq import ReadError

class Assets:
    providers={name:0 for name in ('same','changed','unreadable','missing')}
    def __init__(self):self.reads=[]
    def read(self,name):
        self.reads.append(name)
        if name=='unreadable':raise ReadError('fixture damaged archive member')
        if name=='missing':raise FileNotFoundError('fixture stale listing')
        return b'original'

payload={'Same':b'original','changed':b'replacement','unreadable':b'generated',
         'missing':b'generated','new':b'generated'}
assets=Assets()
assert unchanged_files(payload,assets)==['Same']
assert assets.reads==['Same','changed','unreadable','missing']
assert {key:value for key,value in payload.items() if key not in unchanged_files(payload,Assets())}=={
    'changed':b'replacement','unreadable':b'generated','missing':b'generated','new':b'generated'}
# Required input reads keep their original errors; the exception policy belongs
# only to optional equality checking of an already generated replacement.
try:assets.read('unreadable')
except ReadError:pass
else:raise AssertionError('required source read failure was hidden')
print('PASS art-layer equality: identical omitted; changed/new/unreadable/missing replacements retained')
