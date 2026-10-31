# northlight-test: requires=cxx
"""Exact generated cloud bytes and fog texels against frozen upstream code."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix="northlight-fog-exact-") as tmp:
 for source in [Path(__file__).with_suffix(".cpp"),fp.TESTS/"test_regional_fog_water_mask.cpp"]:
  for label,flags in [("O2",["-O2"]),("asan",["-O1","-g","-fsanitize=address,undefined","-fno-omit-frame-pointer"])]:
   exe=Path(tmp)/(source.stem+"-"+label)
   subprocess.run(["c++","-std=c++17","-UNDEBUG",*flags,*fp.test_include_flags(),str(source),"-o",str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
