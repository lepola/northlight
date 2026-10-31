#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Offline point-shadow math and SM3 artifact gates; no graphics device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,math,pathlib,struct,subprocess,tempfile,unittest
HERE=pathlib.Path(__file__).resolve().parent

def dot(a,b):return sum(x*y for x,y in zip(a,b))
def stored_depth(ray,plane_normal,plane_distance,near=.1,far=30):
    """Analytic opaque plane rendered into the selected cube ray."""
    denom=dot(ray,plane_normal)
    if denom<=0:return 1
    hit=plane_distance/denom
    major=max(abs(x) for x in ray)*hit
    if major<near or major>far:return 1
    return far/(far-near)*(1-near/major)

class PointReferenceTests(unittest.TestCase):
    def test_geometry_occludes_across_faces_and_seams(self):
        cases=0
        for x in (-1,0,1):
            for y in (-1,0,1):
                for z in (-1,0,1):
                    if x==y==z==0:continue
                    ray=(x,y,z);length=math.sqrt(dot(ray,ray));normal=tuple(v/length for v in ray)
                    # Ray meets an opaque plane 4 world units from the source;
                    # receivers at 2 and 8 units establish opposite outcomes.
                    stored=stored_depth(ray,normal,4)
                    for distance,blocked in ((2,False),(8,True)):
                        major=max(abs(v) for v in ray)*distance/length
                        receiver=30/29.9*(1-.1/major)
                        self.assertEqual(receiver>stored+.000002,blocked);cases+=1
                    # No caster (alpha hole/cleared face) is always illuminated.
                    self.assertLess(receiver,1)
        self.assertEqual(cases,52)
    def test_water_coherence_and_correction_bounds(self):
        visible=lambda water,alpha,opaque:water>.1 and water<=opaque+max(.012,water*.0007) and alpha>.003
        self.assertTrue(visible(10,.5,20));self.assertFalse(visible(20,.5,10))
        self.assertFalse(visible(0,0,20));self.assertFalse(visible(10,0,20))
        # The point pass removes only its own admitted radiance. A second lamp
        # and the directional history must survive that correction independently.
        cases=0
        for distance in (0,1,4,7,10,20):
            attenuation=max(0,min(1,(10-distance)/6))
            for intensity in (0,.2,1,10000):
                for visibility in (0,.25,1):
                    for relevance in (0,.2,1):
                        for gain in (0,.3,1):
                            contribution=intensity*attenuation**2*gain
                            correction=-contribution*(1-visibility)*relevance
                            other=.37
                            combined=contribution+other+correction
                            self.assertTrue(math.isfinite(correction))
                            self.assertGreaterEqual(correction,-contribution)
                            self.assertLessEqual(correction,0)
                            self.assertGreaterEqual(combined,other-1e-9)
                            if distance>=10 or visibility==1 or relevance==0:
                                self.assertEqual(correction,0)
                            cases+=1
        self.assertEqual(cases,648)
    def test_backfacing_skips_pcf(self):
        # Task 43: localVisibility returns 1 (no cube taps) where dot(normal,delta)>=0. There facing=saturate(dot(n,-delta/d)) is 0,
        # so estimate and the correction are 0 whatever the visibility; every face toward the lamp still runs the PCF.
        src=fp.src('local_light_effects.hlsl').read_text()
        self.assertEqual(src.count('if(distance<=LocalShadow.x||distance>=LocalShadow.y||dot(normal,delta)>=0)return 1;'),1)
        self.assertIn('float occlusion=1-localVisibility(delta,geometricNormal);',src)
        self.assertIn('return float4(-contribution*(occlusion*relevance),0);',src)
        self.assertIn('float3 contribution=LocalColor.rgb*(atten*atten*facing*LocalGain.x*(1-LocalGain.y*sunlit));',src)
        import random
        rng=random.Random(43);skipped=kept=0
        for _ in range(20000):
            n=[rng.uniform(-1,1) for _ in range(3)];ln=math.sqrt(dot(n,n)) or 1;n=[v/ln for v in n]
            delta=[rng.uniform(-9,9) for _ in range(3)];d=math.sqrt(dot(delta,delta))
            if d<=.0001:continue
            facing=max(0,min(1,dot(n,[-v/d for v in delta])))
            if dot(n,delta)>=0:
                skipped+=1;self.assertEqual(facing,0)
                for visibility in (0,.25,1):self.assertEqual(-1.5*facing*(1-visibility),0)
            else:kept+=1;self.assertGreater(facing,0)
        self.assertGreater(skipped,5000);self.assertGreater(kept,5000)
    def test_compiled_artifacts(self):
        manifest=json.loads(fp.src('local-light-shader-build.json').read_text())
        self.assertEqual(manifest['source_sha256'],hashlib.sha256(fp.src('local_light_effects.hlsl').read_bytes()).hexdigest())
        self.assertFalse(manifest['game_launched']);self.assertFalse(manifest['device_created'])
        for entry,info in manifest['shaders'].items():
            data=fp.src(entry+'.bin').read_bytes()
            self.assertEqual(info['sha256'],hashlib.sha256(data).hexdigest());self.assertLessEqual(info['static_instruction_slots'],512)
            self.assertLessEqual(info['temporary_registers'],32)
        self.assertEqual(manifest['shaders']['LocalLighting']['cube_instruction_extra_slots'],12)

if __name__=='__main__':
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(PointReferenceTests))
    if not result.wasSuccessful():raise SystemExit(1)
    with tempfile.TemporaryDirectory(prefix='northlight-point-shadow-') as tmp:
        binary=str(pathlib.Path(tmp)/'test')
        subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',*fp.test_include_flags(),str(HERE/'test_point_light_shadow.cpp'),'-o',binary],check=True)
        native=subprocess.check_output([binary],text=True).strip();print(native)
    report={'status':'pass','python_tests':result.testsRun,'native_sanitizers':['address','undefined'],'native_result':native,'gpu_test':False,
            'source_sha256':{name:hashlib.sha256(fp.tracked(name).read_bytes()).hexdigest() for name in ['point_light_shadow.h','local_light_effects.hlsl','local_light_compiled_shaders.h','compile_local_light_shaders.py','test_point_light_shadow.cpp','test_point_light_shadow.py']}}
    (fp.output_dir()/'point-light-shadow-validation.json').write_text(json.dumps(report,indent=2)+'\n')
