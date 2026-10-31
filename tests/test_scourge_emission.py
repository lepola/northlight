#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Winning MPQ braziers: emission evidence and outward rays from the flame."""
from pathlib import Path
import math
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets,Builder
from world_emitter_coverage import inspect_model


def nearest_hit(origin,direction,mesh):
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    nearest=float('inf')
    for a,b,c,_ in mesh[1]:
        a,b,c=(mesh[0][i][:3] for i in (a,b,c));u,v=sub(b,a),sub(c,a)
        p=cross(direction,v);det=dot(u,p)
        if abs(det)<1e-8:continue
        t=sub(origin,a);x=dot(t,p)/det
        if not 0<=x<=1:continue
        q=cross(t,u);y=dot(direction,q)/det
        if y<0 or x+y>1:continue
        distance=dot(v,q)/det
        if distance>.002:nearest=min(nearest,distance)
    return nearest


class ScourgeEmission(unittest.TestCase):
    def test_all_brazier_variants_have_measured_emitters_and_the_bowl_still_occludes(self):
        assets=Assets(fp.client_root(),without='z')
        try:
            names=sorted(p for p in assets.providers if 'sc_brazier' in p and p.endswith('.m2'))
            self.assertGreaterEqual(len(names),10)
            for path in names:
                result=inspect_model(assets,path)
                self.assertEqual(result['status'],'addable',(path,result))
                self.assertTrue(all(0<e['intensity']<=4 for e in result['emitters']))
            path='world\\expansion02\\doodads\\generic\\scourge\\sc_brazier2_long.m2'
            light=max(inspect_model(assets,path)['emitters'],key=lambda e:e['intensity'])
            nozzle=light['evidence'][0]['local_position'];origin=light['position']
            self.assertGreater(origin[2],nozzle[2]+.5)
            mesh=Builder(assets).m2(path);blocked_nozzle=blocked_flame=0
            for angle in range(0,360,15):
                a=math.radians(angle);direction=(math.cos(a)*math.cos(math.pi/6),math.sin(a)*math.cos(math.pi/6),-.5)
                blocked_nozzle+=nearest_hit(nozzle,direction,mesh)<10
                blocked_flame+=nearest_hit(origin,direction,mesh)<10
            self.assertEqual(blocked_nozzle,24)
            self.assertLess(blocked_flame,12)
            self.assertGreater(blocked_flame,0) # The fixture's opaque body was not exempted.
        finally:assets.close()


if __name__=='__main__':unittest.main()
