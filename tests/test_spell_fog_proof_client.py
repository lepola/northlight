#!/usr/bin/env python3
# northlight-test: requires=client,cxx,stormlib
"""Task 34 SpellFog: the vertex shader fog proof (src/sky/spell_fog_proof.h) over every M2 family vertex shader of the configured client (read only). Every vs_2_0 and vs_3_0 variant
of the Particle, ParticleUnlit, Model and Projected families must be proven to write fog = min(pow(max(viewZ*c30.x + c30.y, 0), c30.z), 1) from c30 and to read c30 nowhere else; no vs_1_1 variant
(the linear form) is accepted. Reports proven / unproven per family and shader model, and how many take the bare ramp in the pow (the fit then keeps the game's ramp end)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import struct,subprocess,tempfile
import extract_m2_shader_families as fam

HARNESS=r'''
#include "spell_fog_proof.h"
#include <cstdio>
#include <cstdint>
#include <vector>
int main(int argc,char** argv){
    std::FILE* in=std::fopen(argv[1],"rb");uint32_t n=0;if(std::fread(&n,4,1,in)!=1)return 2;
    for(uint32_t k=0;k<n;++k){
        uint32_t len=0;if(std::fread(&len,4,1,in)!=1)return 2;std::vector<uint32_t> w(len);if(len&&std::fread(w.data(),4,len,in)!=len)return 2;
        bool bare=false;const bool ok=NorthlightSpellFog::vsFogProven(w.data(),len,&bare);
        std::printf("%d %d\n",int(ok),int(bare));
    }
    return 0;
}
'''
def main():
    client=fp.client_root()
    records,_=fam.collect(client)
    shaders=list(records.values())
    with tempfile.TemporaryDirectory(prefix='northlight-fog-proof-') as tmp:
        tmp=Path(tmp); (tmp/'h.cpp').write_text(HARNESS)
        subprocess.run(['clang++','-std=c++17','-O1','-Wall','-Werror',*fp.test_include_flags(),str(tmp/'h.cpp'),'-o',str(tmp/'h')],check=True)
        blob=struct.pack('<I',len(shaders))
        for _,code in shaders: blob+=struct.pack('<I',len(code)//4)+code
        (tmp/'in.bin').write_bytes(blob)
        out=subprocess.run([str(tmp/'h'),str(tmp/'in.bin')],capture_output=True,text=True,check=True).stdout.split('\n')
    assert len(out)>len(shaders)
    table={}
    for (family,code),line in zip(shaders,out):
        ok,bare=map(int,line.split())
        version=struct.unpack('<I',code[:4])[0]; model=f'vs_{(version>>8)&255}_{version&255}'
        row=table.setdefault((family,model),[0,0,0]); row[0]+=1; row[1]+=ok; row[2]+=bare
    for (family,model),(n,ok,bare) in sorted(table.items()):
        print(f'{family:12s} {model}: {ok}/{n} proven, {n-ok} unproven, {bare} with the bare ramp in the pow')
        if model=='vs_1_1': assert ok==0,f'{family} {model}: the linear form must not be accepted'
        elif family in ('Particle','ParticleUnlit','Model','Projected'): assert ok==n,f'{family} {model}: {n-ok} variants are not proven'   # Ripples is not an M2 shader: water, never folded
    total=sum(r[0] for r in table.values()); proven=sum(r[1] for r in table.values())
    print(f'{proven} of {total} family vertex shaders proven')
    print('ok')
main()
