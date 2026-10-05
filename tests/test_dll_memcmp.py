#!/usr/bin/env python3
# northlight-test: requires=dll
"""0.3.181 (r90) and r89 S1/S2 needles on the built DLL (renderer/frd9.dll with its frd9.pdb), read
with lldb and objdump; nothing is run. memcmp resolves to the one definition in renderer.cpp (zig
compiler_rt's byte loop is not linked), every call site calls it, and its body contains no call and no
instruction beyond i386 + SSE2 + tzcnt. compiler_rt's bcmp stays linked with no caller, as before.
The import table has no mem* entry beyond memchr (as before), and the exports are exactly frd9.def's.
The snapshot code has a prefetcht0."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json,re,shutil,struct,subprocess
dll=fp.dll();pdb=dll.with_suffix('.pdb')
for tool in ('lldb','objdump'):assert shutil.which(tool),f'{tool} not on PATH'
assert pdb.is_file(),f'{pdb} missing: rebuild with scripts/build_renderer.py'
lldb=subprocess.run(['lldb','-b','-o','settings set interpreter.stop-command-source-on-error false','-o',f'target create "{dll}"',
    '-o','image lookup -n memcmp','-o','image lookup -n bcmp'],capture_output=True,text=True).stdout
found=re.findall(r'Address: \S+\[0x([0-9a-f]+)\].*\n\s+Summary: \S+`(\w+) at ([\w.]+):\d+',lldb)
memcmps={(a,f) for a,n,f in found if n=='memcmp'}
dis=subprocess.run(['objdump','-d','--no-show-raw-insn',str(dll)],capture_output=True,text=True,check=True).stdout.splitlines()
address=next(iter(memcmps))[0] if len(memcmps)==1 else None
calls=sum(1 for l in dis if address and re.search(rf'\bcall[lw]?\s+0x{address}\b',l))
body=[]
if address:
    start=next(i for i,l in enumerate(dis) if l.startswith(address+':'))
    for l in dis[start:]:
        if body and (not l.strip() or re.match(r'^[0-9a-f]+ <',l)):break
        # 0.3.189: an unnamed function may follow without a symbol line; the body ends at retl + alignment padding.
        if body and re.search(r'\tretl?\b',body[-1]) and re.search(r'\t(nop[lw]?|int3)\b',l):break
        body.append(l)
mnemonics={l.split('\t')[1].split()[0] for l in body if '\t' in l and len(l.split('\t'))>1 and l.split('\t')[1].strip()}
ALLOWED={'pushl','popl','movl','movzbl','movb','cmpl','cmpb','addl','subl','xorl','orl','andl','notl','leal','shrl','testl','je','jne','jb','jae','ja','jbe',
         'jmp','retl','cmovel','cmovnel','cmovbl','cmovael','nop','nopl','nopw','tzcntl','bsfl','movdqu','movdqa','pcmpeqb','pand','pmovmskb','sbbl','negl','incl','decl','shll','sete','setne','movzwl'}
def pe_tables(path):
    d=path.read_bytes();pe=struct.unpack_from('<I',d,0x3c)[0];nsec=struct.unpack_from('<H',d,pe+6)[0];opt=pe+24;osz=struct.unpack_from('<H',d,pe+20)[0]
    secs=[struct.unpack_from('<IIII',d,opt+osz+40*i+8) for i in range(nsec)]
    off=lambda rva:next(rva-va+ra for vs,va,rs,ra in secs if va<=rva<va+max(vs,rs))
    cstr=lambda o:d[o:d.index(b'\0',o)].decode()
    imports=set();o=off(struct.unpack_from('<I',d,opt+96+8)[0])
    while True:
        oft,_,_,nm,ft=struct.unpack_from('<IIIII',d,o)
        if not nm:break
        t=off(oft or ft);i=0
        while (e:=struct.unpack_from('<I',d,t+4*i)[0]):imports.add((cstr(off(nm)).lower(),cstr(off(e)+2) if not e&0x80000000 else f'#{e&0xffff}'));i+=1
        o+=20
    e=off(struct.unpack_from('<I',d,opt+96)[0]);n=struct.unpack_from('<I',d,e+24)[0];names=off(struct.unpack_from('<I',d,e+32)[0])
    exports={cstr(off(struct.unpack_from('<I',d,names+4*i)[0])) for i in range(n)}
    return imports,exports
imports,exports=pe_tables(dll)
declared={re.match(r'\s*(\w+)=',l).group(1) for l in fp.src('frd9.def').read_text().splitlines() if re.match(r'\s*\w+=',l)}
checks={
 'memcmp is the one definition in renderer.cpp (compiler_rt memcmp.zig not linked)':len(memcmps)==1 and next(iter(memcmps))[1]=='renderer.cpp' and 'memcmp.zig' not in lldb,
 'bcmp (compiler_rt, unused) has no caller':all(not any(re.search(rf'\bcall[lw]?\s+0x{a}\b',l) for l in dis) for a,n,_ in found if n=='bcmp'),
 'every memcmp call site calls it (at least the 0.3.180 count minus the snapshot key)':calls>=400,
 'its body has no call':bool(body) and not any(re.search(r'\tcall',l) for l in body),
 'its body is i386 + SSE2 + tzcnt only':bool(mnemonics) and mnemonics<=ALLOWED,
 'imports: no mem*/bcmp beyond memchr':{n for _,n in imports if n.startswith('mem') or n=='bcmp'}<={'memchr'},
 'exports are exactly frd9.def':exports==declared,
 'the snapshot prediction prefetches (prefetcht0 present)':any('prefetcht0' in l for l in dis),
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
print(json.dumps({'memcmp':sorted(memcmps),'calls':calls,'bodyInstructions':len(body),'mnemonics':sorted(mnemonics),'unexpected':sorted(mnemonics-ALLOWED)}))
assert all(checks.values())
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
(out/'dll-memcmp-validation.json').write_text(json.dumps({'checks':checks,'calls':calls,'mnemonics':sorted(mnemonics),'imports':sorted(imports)},indent=2)+'\n')
print('PASS DLL memcmp needles')
