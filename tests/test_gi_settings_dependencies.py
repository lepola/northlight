#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Production settings invalidation: every quality key, palette field and retained pending request."""
import ast
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source=fp.src('world_renderer.h').read_text()
start=source.index('    void applyLiveSettings(');end=source.index('{',start)+1;depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
method=source[start:end]
tree=ast.parse((fp.TESTS/'test_live_settings.py').read_text())
node=next(n.value for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets))
fixture=ast.literal_eval(node.func.value).split('int main(){')[0].replace('__METHOD__',method)
body=r'''
static bool relevant(const std::string& key){return key=="GI"||key=="GIBounces"||key=="GIDynamicProbes"||key=="GIProbeMoveStep"||key=="GIProbeAhead"||key=="ShadowDirectionSteps";}
static NorthlightSettings::Config current(const World& w){NorthlightSettings::Config c;c.quality=w.quality;c.celestial=w.celestialProfiles;return c;}
int main(){unsigned qualityCases=0,profileCases=0;
 for(const auto& key:NorthlightQuality::Keys){World w;auto next=current(w);next.quality.*key.field=w.quality.*key.field==key.low?key.high:key.low;
  const auto effective=NorthlightSettings::liveQualityUpdate(w.quality,next.quality);
  const bool changed=(w.quality.*key.field)!=(effective.*key.field);
  w.applyLiveSettings(next);assert(w.settingsRequestDirty==(changed&&relevant(key.name)));assert(!w.paletteFrameValid);++qualityCases;
 }
 // Repeating no-op/HDR/material changes cannot cancel a solver's request ID.
 for(unsigned n=0;n<1000;++n){World w;auto next=current(w);next.hdr[n%next.hdr.size()]+=1;
  w.applyLiveSettings(next);assert(!w.settingsRequestDirty);w.applyLiveSettings(next);assert(!w.settingsRequestDirty);
  w.settingsRequestDirty=true;w.applyLiveSettings(next);assert(w.settingsRequestDirty);
 }
 using P=NorthlightCelestialProfiles::Profile;
 for(bool zone:{false,true})for(unsigned source=0;source<2;++source)for(unsigned field=0;field<19;++field){
  World w;const auto key=std::make_pair(std::string("Northrend"),uint32_t(42));if(zone)w.celestialProfiles.zones[key]=P{};
  auto next=current(w);P& p=zone?next.celestial.zones[key]:next.celestial.fallback;
  float* value=nullptr;bool gi=false;
  if(field<3){value=&p.light[source][field];gi=true;}
  else if(field==3){value=&p.mix[source];gi=true;}
  else if(field==4){value=&p.strength[source];gi=true;}
  else if(field<8)value=&p.disc[source][field-5];
  else if(field==8)value=&p.gain[source];
  else if(field==9)value=&p.fogGain[source];
  else if(field==10)value=&p.fogHeightScale[source];
  else if(field==11)value=&p.horizonHaze[source];
  else if(field==12)value=&p.glowStrength[source];
  else if(field==13)value=&p.glowMix[source];
  else if(field<17)value=&p.glow[source][field-14];
  else if(field==17){next.celestial.explicitKeys[key].insert("sun_light");}
  else {next.quality.giStrength=next.quality.giStrength==100?60:100;}
  if(value)*value+=.01f;
  w.applyLiveSettings(next);assert(w.settingsRequestDirty==gi);assert(!w.paletteFrameValid);++profileCases;
 }
 // Zone insertion/removal equal to the fallback cannot change any sampled GI light.
 for(bool remove:{false,true}){World w;const auto key=std::make_pair(std::string("Northrend"),uint32_t(42));
  if(remove)w.celestialProfiles.zones[key]=w.celestialProfiles.fallback;
  auto next=current(w);if(remove)next.celestial.zones.erase(key);else next.celestial.zones[key]=next.celestial.fallback;
  w.applyLiveSettings(next);assert(!w.settingsRequestDirty);
 }
 // GI enable/disable and dynamic actor removal must force even at a stationary camera.
 for(unsigned which=0;which<3;++which){World w;auto next=current(w);
  if(which==0)next.quality.gi=0;else if(which==1){w.quality.gi=0;next.quality.gi=1;}else next.quality.giDynamicProbes=0;
  w.applyLiveSettings(next);assert(w.settingsRequestDirty);if(which<2)assert(w.effects.gi==bool(next.quality.gi));
 }
 std::cout<<"PASS GI settings invalidation: qualityKeys="<<qualityCases<<" profileFields="<<profileCases<<" noOpHDR=1000 pendingPreserved=1 equalFallbackZones=2 GIReactivation=1 dynamicActorRemoval=1\n";
}
'''
out=fp.out()/'gi-settings-dependencies';out.mkdir(parents=True,exist_ok=True)
(out/'dependencies.cpp').write_bytes((fixture+body).encode())
for label,flags in [('O2',['-O2']),('debug',['-O0','-g'])]:
    exe=out/(label+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,
                    '-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(out/'dependencies.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
