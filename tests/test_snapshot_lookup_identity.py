#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Prove allocation-free lookup keeps complete serialized source/provenance equality."""
import ast,hashlib,json,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def literal(file,name):
    return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')==name for t in n.targets))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

source=fp.TESTS/'test_snapshot_lookup_identity.cpp'
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[source,Path(__file__),fp.TESTS/'test_terrain_snapshot.py',fp.TESTS/'test_draw_snapshot.py',fp.REPO/'northlight_paths.py']
inputs={str(p.resolve()):sha(p) for p in watched}
records=[]
with tempfile.TemporaryDirectory(prefix='snapshot-lookup-identity-') as temporary:
    root=Path(temporary)
    (root/'d3d9.h').write_text(literal(fp.TESTS/'test_terrain_snapshot.py','stub'))
    (root/'lookup_identity_harness.h').write_text(literal(fp.TESTS/'test_draw_snapshot.py','harness').split('int main()')[0])
    for name,flags in [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        exe=root/('fixture-'+name);command=['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,'-I',str(root),*fp.test_include_flags(),str(source),'-o',str(exe)]
        subprocess.run(command,check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        assert 'LOOKUP_IDENTITY_PASS' in result.stdout
        records.append({'mode':name,'command':command,'stdout':result.stdout,'stderr':result.stderr,'exe_sha256':sha(exe)})
        print(result.stdout.strip())
    mutant=root/'negative';mutant.mkdir()
    header=fp.src('draw_snapshot.h').read_text();proof='&&std::equal(words.begin(),words.end(),stored->words.begin())'
    assert header.count(proof)==1;header=header.replace(proof,'&&true')
    (mutant/'draw_snapshot.h').write_bytes(header.encode());exe=mutant/'fixture-unsafe'
    subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG','-I',str(mutant),'-I',str(root),*fp.test_include_flags(),str(source),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],capture_output=True,text=True)
    assert result.returncode!=0 and 'LOOKUP_COLLISION_ACCEPTED' in result.stderr,result.stdout+result.stderr
    assert all(sha(Path(p))==expected for p,expected in inputs.items()),'Source/helper changed during final validation'
    out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
    (out/'snapshot-lookup-identity-validation.json').write_text(json.dumps({'inputs':inputs,'inputs_unchanged':True,'records':records,'unsafe_equality_rejected':True,'negative_stdout':result.stdout,'negative_stderr':result.stderr,'game_launched':False},indent=2)+'\n')
print('PASS complete lookup identity, stored allocation/capacity, real hash collision, optional failures and resident-peer/current-revision proofs.')
