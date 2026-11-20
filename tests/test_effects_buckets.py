#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.175 (S3) effects buckets: native model test of effects_buckets.h (clang++, plain and ASan/UBSan)
and a wiring audit: diagnostic frames keep bounded stage timings for the slow-frame tracker;
draw counters and full reports remain sampled. Disabled profiling reads no clock. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text();h=fp.src('effects_buckets.h').read_text()
effects=r[r.index('    void renderEffects('):r.index('    // One shader query/reference per original draw')]
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
world_marks=len(re.findall(r'\bbucket\(NorthlightEffectsBuckets::',render));outer_marks=effects.count('effectsBuckets.mark(')
checks={
 'diagnostic stage clocks open once for applying frames; draw counts remain sampled':('CpuScope cpu(sampled()?&cpuEffects:nullptr);' in effects # the cpuEffects scope itself (nameMaskRunEnd() may precede it)
    and 'effectsBucketed=NorthlightRenderThreadProbe::profiling();' in effects and 'effectsBuckets.begin(effectsBucketed,&qpcNow,counters,sampled()?4:0);}' in effects
    and 'struct BucketsEnd {NorthlightEffectsBuckets::Frame& f;~BucketsEnd(){f.end();}} bucketsEnd{effectsBuckets};' in effects
    and effects.index('CpuScope cpu(sampled()')<effects.index('if (applied || !enabled || failed || !projectionValid) return;')<effects.index('effectsBuckets.begin(')<effects.index('bucketsEnd{effectsBuckets}')),
 'slow frames retain values before the next frame clears the collector':'if(frameApplied&&effectsBucketed)stallFrame.effects=effectsBuckets.snapshot(cpuFrequency.QuadPart);' in r and 'FRAME slow effects frame=%u' in r and 'effectsBucketed=false;' in r,
 'off: begin and mark read no clock':'on_=on&&clock;reads_=0;total_=0;if(!on_)return;' in h and 'void mark(Bucket ended){if(!on_)return;' in h,
 # 0.3.209 (task 29): +2 = the two per-source actor sites run once per source. Worst case: begin, the world's single-use sites, 4 cascades (two exclusive sites each pass), every renderEffects site, end
 'cooperative water closes after its scene copy and before fog':render.index('bucket(NorthlightEffectsBuckets::PointLighting);')<render.index('LightWaterScene(*surface')<render.index('surface->draw(color')<render.index('"surface scene copy"')<render.index('bucket(NorthlightEffectsBuckets::WaterSurface);')<render.index('"source visibility pass"')<render.index('bucket(NorthlightEffectsBuckets::Fog);'),
 'at most 33 clock reads a frame (including cooperative water)':render.count('bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade))')==2
    and 1+(world_marks-2)+4+2+outer_marks+1<=33 and 'constexpr unsigned MaxReads=33;' in h,
 'the world marks through bucket() only (the renderer\'s frame pointer)':'void bucket(NorthlightEffectsBuckets::Bucket b){if(effectsBuckets)effectsBuckets->mark(b);}' in w and 'world->setEffectsBuckets(&effectsBuckets);' in r,
 'logged on RenderProfile sample frames with the effects span':'if(effectsBucketed){const double tick=' in r and 'logf("EFFECTS buckets frame=%u applied=%d effectsMs=%.3f sumMs=%.3f spanMs=%.3f reads=%u draws=%u (ms/draw calls)%s"' in r
    and r.index('if(NorthlightRenderThreadProbe::profiling()){\n                // Extension D3D9 calls')<r.index('logf("EFFECTS buckets'),
}
print(f'world marks {world_marks}, renderEffects marks {outer_marks}')
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-effects-buckets-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_effects_buckets.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS effects buckets: model and wiring')
