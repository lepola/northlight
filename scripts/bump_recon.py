"""Extract local shader fixtures into ignored output; optionally disassemble them."""
from pathlib import Path
import argparse,ctypes as C,re,sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent/'shaders')]
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--disassemble',action='store_true')
parser.add_argument('--compatible-compiler',action='store_true')
args=parser.parse_args()
out=fp.out()/'bump-recon';out.mkdir(parents=True,exist_ok=True)
assets=Assets();count=0
try:
    for name in assets.providers:
        if not re.fullmatch(r'shaders\\(?:vertex\\vs_[23]_0|pixel\\ps_[23]_0)\\[^\\]+\.bls',name):continue
        for index,_,code in variants(assets.read(name)):
            (out/(name.replace('\\','_')+'-'+str(index)+'.bin')).write_bytes(code);count+=1
finally:assets.close()
if args.disassemble:
    from shader_compiler import Compiler
    compiler=Compiler(allow_compatible_binary=args.compatible_compiler)
    dll=C.WinDLL(str(compiler.dir/'d3dcompiler_47.dll'));dis=dll.D3DDisassemble
    dis.argtypes=[C.c_void_p,C.c_size_t,C.c_uint,C.c_char_p,C.POINTER(C.c_void_p)];dis.restype=C.c_long
    for path in out.glob('*.bin'):
        data=path.read_bytes();blob=C.c_void_p();assert dis(data,len(data),0,None,C.byref(blob))>=0
        table=C.cast(blob,C.POINTER(C.POINTER(C.c_void_p))).contents
        pointer=C.WINFUNCTYPE(C.c_void_p,C.c_void_p)(table[3])(blob)
        size=C.WINFUNCTYPE(C.c_size_t,C.c_void_p)(table[4])(blob)
        path.with_suffix(path.suffix+'.asm').write_bytes(C.string_at(pointer,size))
        C.WINFUNCTYPE(C.c_ulong,C.c_void_p)(table[2])(blob)
print('Prepared',count,'local shader fixtures in',out)
