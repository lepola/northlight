#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Shared source conversion exactly matches native compact decode on real FGS2 assets."""
from pathlib import Path
import hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.out()/"shared-static-preparation";out.mkdir(parents=True,exist_ok=True)
models=[fp.world_cache()/"models"/(key+".fgs") for key in ("2ea94a11618b725ac3c52510584a9c07d816ec0254fbacb99ca85226e2fec973","71fa9df1ff2709956faebf9fada7cb6a1e46eb2833b092727f6e83e38c2b6cbd","ce84f4425c1fc8f1895785a840c96613823e818f07afe1d7262604fcf2033a17")]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in models}
exe=out/"shared-static.exe"
subprocess.run([str(fp.zig()),"c++","-target","x86-windows-gnu","-std=c++17","-O2","-UNDEBUG","-Wall","-Wextra","-Werror",*fp.test_include_flags(),str(fp.TESTS/"test_shared_static_preparation.cpp"),str(fp.src("world_gi.cpp")),"-o",str(exe)],env=fp.zig_env(),check=True)
result=subprocess.run([str(exe),*map(str,models)],capture_output=True,text=True);(out/"result.log").write_bytes((result.stdout+result.stderr).encode());print(result.stdout+result.stderr,end="");result.check_returncode()
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==value for p,value in before.items())
(out/"validation.json").write_bytes((json.dumps({"checks":6,"models_sha256":before,"game_launched":False},indent=2)+"\n").encode())
