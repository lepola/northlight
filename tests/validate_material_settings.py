from pathlib import Path
import os,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
out=fp.out()/'material-settings';out.mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
for name in ('test_settings_service','test_material_texture_uploads','test_particle_texture_stats'):
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86_64-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(root/'tests'/(name+'.cpp')),'-o',str(exe)],env=env,check=True)
    with tempfile.TemporaryDirectory(prefix=name+'-',dir=out) as fixture:
        subprocess.run([str(exe),fixture],env=env,check=True)
exe=out/'test_quality_settings.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86_64-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(root/'tests/test_quality_settings.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=env,check=True)
subprocess.run([str(exe)],cwd=root/'renderer',env=env,check=True)
print('MATERIAL_SETTINGS_PASS')
