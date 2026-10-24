# northlight-test: requires=client
"""Read-only static proof of the CVar call ABI against build 12340; does not execute the client."""
from pathlib import Path
import hashlib
import re
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

image = (fp.client_root() / 'Wow.exe').read_bytes()
pe = struct.unpack_from('<I', image, 60)[0]
assert image[pe:pe+4] == b'PE\0\0'
assert struct.unpack_from('<H', image, pe+4)[0] == 0x14c
assert struct.unpack_from('<I', image, pe+8)[0] == 0x4c2452fe
assert struct.unpack_from('<I', image, pe+24+56)[0] == 0x9fd000
count, optional = struct.unpack_from('<H', image, pe+6)[0], struct.unpack_from('<H', image, pe+20)[0]
sections = [struct.unpack_from('<IIII', image, pe+24+optional+i*40+8) for i in range(count)]
source = fp.src('settings_cvar_bridge.h').read_text()
for name, va in [('lookup', 0x767460), ('setter', 0x7668c0)]:
    expected = bytes(int(n, 16) for n in re.findall(r'0x([0-9a-f]{2})', re.search(r'char '+name+r'\[\]=\{([^}]+)', source)[1]))
    for size, start, raw_size, offset in sections:
        if start <= va-0x400000 < start+raw_size:
            at = offset + va-0x400000-start
            assert image[at:at+len(expected)] == expected
            break
    else: raise AssertionError('CVar function outside the image')
print('PASS static CVar ABI signatures and PE gate; client SHA256=' + hashlib.sha256(image).hexdigest() + '; no client execution')
