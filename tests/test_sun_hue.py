#!/usr/bin/env python3
# northlight-test: requires=cxx
"""glow hue (sun_hue.h): native tests of the real CPU policy (clang++ plain and ASan/UBSan)
and source integration. H from the three reference band-10 sunHalo colours must read green
(Brill), orange (Durotar coast 20:00) and orange (Orgrimmar 17:00) with a warm-white hot core,
and stay near the glow of the reference screenshots. Strength 0 must give today's colours exactly.
Not a GPU or appearance test; no game, Wine or GPU. Writes sun-hue-validation.json."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import subprocess
import tempfile
from pathlib import Path

NATIVE = r'''
#include "sun_hue.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightSunHue;
static std::uint32_t pack(int r,int g,int b){return 0xff000000u|std::uint32_t(r)<<16|std::uint32_t(g)<<8|std::uint32_t(b);}
// HD client: band 9 (native glare) black, band 10 as given. Stock client: both bands lit.
static GlowHue both(std::uint32_t sun,std::uint32_t halo){std::uint32_t slots[Slots]={};slots[SlotSun]=sun;slots[SlotSunHalo]=halo;
    const float moon[3]={.94f,.97f,1};return compute(slots,moon);}
static GlowHue at(int r,int g,int b){return both(0xff000000u,pack(r,g,b));}
static void print(const char* name,const GlowHue& h,bool comma){
    std::printf("\"%s\":{\"sun\":[%.4f,%.4f,%.4f],\"core\":[%.4f,%.4f,%.4f],\"strength\":%.4f}%s",name,h.sun[0],h.sun[1],h.sun[2],h.sunCore[0],h.sunCore[1],h.sunCore[2],h.strength,comma?",":"");}
int main(){
    // reference band 10 (Light.dbc/LightIntBand.dbc, patch-z): Brill 17:00-18:00, Durotar coast 20:00, Orgrimmar 17:00.
    const GlowHue brill=at(36,63,50),durotar=at(225,67,1),orgrimmar=at(195,75,3);
    for(const GlowHue* h:{&brill,&durotar,&orgrimmar}){
        assert(h->valid&&h->strength==1);
        for(unsigned i=0;i<3;++i)assert(h->sun[i]>=0&&h->sun[i]<=1&&h->sunCore[i]>=0&&h->sunCore[i]<=1);
        assert(std::max(h->sun[0],std::max(h->sun[1],h->sun[2]))==1&&std::max(h->sunCore[0],std::max(h->sunCore[1],h->sunCore[2]))==1);
        // Hot core: warm white, closer to white than the hue in every channel, never blue-dominant.
        for(unsigned i=0;i<3;++i)assert(h->sunCore[i]>=h->sun[i]-1e-6f&&h->sunCore[i]>=.6f);
        assert(h->sunCore[2]<=h->sunCore[0]&&h->sunCore[2]<=h->sunCore[1]);
        float in[3],out[3];glowColour(*h,1,in);glowColour(*h,0,out);
        for(unsigned i=0;i<3;++i)assert(in[i]==h->sunCore[i]&&out[i]==h->sun[i]);
    }
    // Brill green: green dominant, clearly above red and blue.
    assert(brill.sun[1]==1&&brill.sun[0]<.85f&&brill.sun[2]<.75f);
    // Durotar/Orgrimmar orange: red 1, green between, blue low; the core is not red (G >= .85).
    for(const GlowHue* h:{&durotar,&orgrimmar}){assert(h->sun[0]==1&&h->sun[1]>.45f&&h->sun[1]<.75f&&h->sun[2]<.35f);assert(h->sunCore[1]>=.85f);}
    assert(durotar.sun[1]<orgrimmar.sun[1]+.1f);
    // Reference screenshots (glow edge and core, normMax): within .2 per channel (edge) and .12 (core).
    const float edge[3][3]={{.70f,1,.61f},{1,.70f,.41f},{1,.57f,.33f}},core[3][3]={{.96f,1,.83f},{1,.99f,.64f},{1,.91f,.59f}};
    const GlowHue* zones[3]={&brill,&durotar,&orgrimmar};
    for(unsigned z=0;z<3;++z)for(unsigned i=0;i<3;++i){assert(std::fabs(zones[z]->sun[i]-edge[z][i])<=.2f);assert(std::fabs(zones[z]->sunCore[i]-core[z][i])<=.12f);}
    // No hue: black band 10 (strength 0), dim band 10 ramps, reset parameters (white) are a valid white hue.
    const GlowHue black=at(0,0,0),dim=at(10,12,8),white=at(255,255,255);
    assert(black.valid&&black.strength==0&&dim.strength>0&&dim.strength<1&&white.strength==1);
    const GlowHue none=compute(nullptr,nullptr);
    assert(!none.valid&&none.strength==0&&none.moon[0]==1&&none.moon[1]==1&&none.moon[2]==1&&sunStrength(none)==0);
    // Moon: normMax of the moon_disc tint; black / NaN tint -> white.
    const float tint[3]={.47f,.485f,.5f},nan[3]={NAN,.5f,.5f};
    GlowHue m=compute(nullptr,tint);assert(m.moon[2]==1&&std::fabs(m.moon[0]-.94f)<1e-6f);
    m=compute(nullptr,nan);assert(m.moon[0]==1&&m.moon[1]==1&&m.moon[2]==1);
    // Strength 0 / invalid: exactly today's c17, c18 and c35.yzw.
    const float direct[3]={.38f,.27f,.42f},ambient[3]={.18f,.14f,.22f};
    for(const GlowHue* h:{&black,&none}){float o[3],a[3],l[3];fogDirect(direct,*h,o);coolAmbient(ambient,*h,1,a);horizonLift(*h,l);
        for(unsigned i=0;i<3;++i)assert(o[i]==direct[i]&&a[i]==ambient[i]&&l[i]==Warm[i]);}
    {float a[3];coolAmbient(ambient,durotar,0,a);for(unsigned i=0;i<3;++i)assert(a[i]==ambient[i]);} // sun down: no cooling
    // Fog direct: luminance kept, hue moves toward H by DirectTint (in place is allowed).
    for(const GlowHue* h:{&brill,&durotar,&orgrimmar}){
        float o[3]={direct[0],direct[1],direct[2]};fogDirect(o,*h,o);
        assert(std::fabs(luminance(o)-luminance(direct))<1e-5f);
        float ideal[3];const float scale=luminance(direct)/luminance(h->sun);
        for(unsigned i=0;i<3;++i){ideal[i]=h->sun[i]*scale;assert(std::fabs(o[i]-(direct[i]+(ideal[i]-direct[i])*DirectTint))<1e-5f);}
        // Cool ambient: a luminance-neutral factor (grey keeps its luminance), bluer than before.
        const float grey[3]={.2f,.2f,.2f};float c[3],g[3];coolAmbient(ambient,*h,1,c);coolAmbient(grey,*h,1,g);
        assert(std::fabs(luminance(g)-.2f)<1e-6f&&c[2]/c[0]>ambient[2]/ambient[0]&&g[2]>g[1]&&g[1]>g[0]);
        float l[3];horizonLift(*h,l);for(unsigned i=0;i<3;++i)assert(l[i]==h->sun[i]);
    }
    // Durotar direct is purple at 20:00 (97,69,107); the fog scatter turns warm (red > blue).
    {const float purple[3]={97.f/255,69.f/255,107.f/255};float o[3];fogDirect(purple,durotar,o);assert(o[0]>o[2]&&o[0]>o[1]);}
    // HD (band 9 = 0, the cases above): band 10 only.
    for(const GlowHue* h:{&brill,&durotar,&orgrimmar})assert(h->native==0);
    // Stock (band 9 lit, stock patch-z art layer at the same spots and times): the native sun colour wins.
    const GlowHue sBrill=both(pack(190,240,193),pack(34,59,48)),sDurotar=both(pack(255,124,25),pack(225,67,1)),sOrgrimmar=both(pack(255,170,99),pack(195,75,3));
    for(const GlowHue* h:{&sBrill,&sDurotar,&sOrgrimmar}){assert(h->valid&&h->native==1&&h->strength==1);
        for(unsigned i=0;i<3;++i)assert(h->sunCore[i]>=h->sun[i]-1e-6f&&h->sunCore[i]>=.6f);}
    {float e[3];const float b9[3]={190.f/255,240.f/255,193.f/255};sunHue(b9,e);for(unsigned i=0;i<3;++i)assert(sBrill.sun[i]==e[i]);}
    assert(std::fabs(sBrill.sun[0]-.90f)<.02f&&sBrill.sun[1]==1&&std::fabs(sBrill.sun[2]-.65f)<.02f);           // pale green
    assert(sDurotar.sun[0]==1&&std::fabs(sDurotar.sun[1]-.67f)<.02f&&std::fabs(sDurotar.sun[2]-.28f)<.02f);    // orange
    assert(sOrgrimmar.sun[0]==1&&std::fabs(sOrgrimmar.sun[1]-.78f)<.02f&&std::fabs(sOrgrimmar.sun[2]-.43f)<.02f); // warm orange
    // Band 9 alone (band 10 black) still gives the native hue.
    {const GlowHue n=both(pack(255,124,25),0xff000000u);assert(n.valid&&n.native==1&&n.strength==1&&n.sun[0]==1&&n.sun[1]<.75f);}
    // Continuity where band 9 fades to black (HD light borders, dusk): no step in hue or strength.
    {GlowHue last=both(0xff000000u,pack(225,67,1));
        for(int v=1;v<=255;++v){const GlowHue n=both(pack(v,v*124/255,v*25/255),pack(225,67,1));
            for(unsigned i=0;i<3;++i)assert(std::fabs(n.sun[i]-last.sun[i])<.08f);assert(n.strength==1&&n.native>=last.native);last=n;}
        assert(last.native==1);}
    // Moon: the tint is kept (moonStrength 1) even when the sun data is invalid.
    {const float tint2[3]={.8f,.9f,1};GlowHue n=compute(nullptr,tint2);assert(!n.valid&&n.strength==0&&n.moonStrength==1&&n.moon[2]==1&&std::fabs(n.moon[0]-.8f)<1e-6f);
        n=compute(nullptr,nullptr);assert(n.moonStrength==0);
        for(const GlowHue* h:{&brill,&durotar,&orgrimmar})assert(h->moonStrength==1);}
    assert(HoldMs==2000);
    // 0.3.163 stock Tirisfal log: band10=0.118,0.196,0.157 (30,50,40) is dark but chromatic: a valid green hue.
    {const GlowHue t=at(30,50,40);assert(t.valid&&t.strength==1&&t.sun[1]==1&&t.sun[0]<.85f&&t.sun[2]<.75f);
        for(unsigned i=0;i<3;++i)assert(std::fabs(t.sun[i]-brill.sun[i])<.03f);}
    std::printf("{");print("stock_brill",sBrill,true);print("stock_durotar",sDurotar,true);print("stock_orgrimmar",sOrgrimmar,true);print("brill",brill,true);print("durotar",durotar,true);print("orgrimmar",orgrimmar,false);std::printf("}\n");
}
'''


def native():
    with tempfile.TemporaryDirectory(prefix='fr-sun-hue-') as temp:
        folder = Path(temp)
        (folder/'test.cpp').write_text(NATIVE)
        out = None
        for flags in (['-O2'], ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']):
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                            str(folder/'test.cpp'), '-o', str(folder/'test')], check=True)
            out = json.loads(subprocess.run([str(folder/'test')], check=True, capture_output=True, text=True).stdout)
        return out


def main():
    values = native()
    hue = fp.src('sun_hue.h').read_text()
    cpu = fp.src('world_renderer.h').read_text()
    wmo = fp.src('wmo_context.h').read_text()
    # The contract glow codes against (decisions.md F1).
    assert 'struct GlowHue {' in hue and 'SlotSun=9,SlotSunHalo=10' in hue
    for field in ('float sun[3]=', 'float sunCore[3]=', 'float moon[3]=', 'float strength=0;', 'bool valid=false;'):
        assert field in hue, field
    assert 'const NorthlightSunHue::GlowHue& glowHue()const{return glowHueFrame;}' in cpu
    # Band read: the same coherent 0x1b8-byte sky read, slots at +0xd4, gated by the code proofs.
    assert 'SkyBytes=0x1b8,LightSlotBytes=0xd4' in wmo and 'std::memcpy(value.slots,bytes+LightSlotBytes,sizeof value.slots);' in wmo
    context = cpu[cpu.index('    void updateWorldContext('):]
    context = context[:context.index('\n    }\n')]
    assert 'if(global&&global->slotsProven){memcpy(lightSlots,global->slots,sizeof lightSlots);lightSlotsValid=true;lightSlotsAt=slotsNow;}' in context
    assert 'if(lightSlotsMap!=map||DWORD(slotsNow-lightSlotsAt)>NorthlightSunHue::HoldMs){lightSlotsValid=false;lightSlotsMap=map;}' in context
    assert 'glowHueFrame=NorthlightSunHue::compute(lightSlotsValid?lightSlots:nullptr,celestialPalette(map,camera).disc[1]);' in context
    # One sampled candidate line, gated like MEMREAD.
    assert cpu.count('logf("SUNHUE ') == 1 and 'if(profileSampled()){ /* hue candidates' in cpu
    # And the 600-frame line: first frame always, then with Diagnostics=1 (test_diagnostics_switch policy).
    assert cpu.count('logf("CELESTIAL glowHue src=%s') == 1 and 'if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled())){ /* every 600 frames' in cpu
    assert 'const std::uint32_t* slots,const float* moonTint' in hue and 'unpack(slots[SlotSun],sun);unpack(slots[SlotSunHalo],halo);' in hue
    assert 'h.native=lit(sun);' in hue and 'h.strength=std::max(h.native,lit(halo));' in hue  # band 9 where lit, else band 10
    assert '?"fallback":g.native>=.5f?"native":"sunHalo"' in cpu
    assert 'float moonStrength=0;' in hue and 'h.moonStrength=normMax(moonTint,h.moon)?1.f:0.f;' in hue
    assert 'value.slotsConsistent=value.slots[1]==packed(bytes,0x17c);' in wmo and '0x1ac);' not in wmo.split('slotsConsistent=',2)[-1][:60] and 'out.slotsProven=slots&&out.slotsConsistent;return true;' in wmo
    assert 'float skyTransmittance()const{return skyTransmittanceFrame;}' in cpu and cpu.count('a.veil[0]=std::exp(-depth);a.veil[1]=std::exp(-cloudyDepth);') == 1 and cpu.count('skyTransmittanceFrame=cf.active?atmosphere.veil[1]:atmosphere.veil[0];') == 1  # 0.3.200 (jobs): the veil computed in atmosphereWork
    result = dict(game_launched=False, gpu_tested=False, native_policy_passed=True, reference_band10=dict(
        brill=[36, 63, 50], durotar=[225, 67, 1], orgrimmar=[195, 75, 3]), hue=values)
    (fp.output_dir()/'sun-hue-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
