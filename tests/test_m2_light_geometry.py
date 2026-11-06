#!/usr/bin/env python3
"""Source enclosure proof keeps frames, rooms, ambiguous owners and moving parts."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'renderer'))
from m2_light_geometry import source_components,paired_source_layers


def cube(vertices,triangles,center,size):
    for axis in range(3):
        for sign in (-1,1):
            first=len(vertices);normal=[0,0,0];normal[axis]=sign
            for u,v in ((-1,-1),(1,-1),(1,1),(-1,1)):
                p=list(center);p[axis]+=sign*size;p[(axis+1)%3]+=u*size;p[(axis+2)%3]+=v*size
                vertices.append((*p,*normal,0,0))
            triangles.extend(((first,first+1,first+2,0),(first,first+2,first+3,0)))


class Enclosure(unittest.TestCase):
    def setUp(self):
        self.vertices=[];self.triangles=[];self.materials=[('shared-frame-and-bulb',0,False)]
        cube(self.vertices,self.triangles,(0,0,2),.25)
        cube(self.vertices,self.triangles,(0,0,-2),.5)
        self.light=((0,0,2),(1,.5,.2),1,8,7,2,2)

    def detect(self,lights=None,static=lambda _:True):
        return source_components(self.vertices,self.triangles,self.materials,[self.light] if lights is None else lights,static)

    def test_shared_material_keeps_the_frame(self):
        self.assertEqual(self.detect(),dict.fromkeys(range(12),7))
        for scale in (.2,3):
            v=[(*(x*scale for x in p[:3]),*p[3:]) for p in self.vertices]
            light=((0,0,2*scale),self.light[1],scale,8*scale,*self.light[4:])
            self.assertEqual(source_components(v,self.triangles,self.materials,[light],lambda _:True),self.detect())

    def test_no_source_or_two_sources_never_guesses(self):
        self.assertFalse(self.detect([]))
        self.assertFalse(self.detect([self.light,(*self.light[:4],8,*self.light[5:])]))
        self.assertFalse(self.detect([(*self.light[:-1],0)]))

    def test_moving_or_alpha_material_keeps_geometry(self):
        self.assertFalse(self.detect(static=lambda _:False))
        self.materials=[('cutout',1,False)];self.assertFalse(self.detect())
        self.assertEqual(source_components(self.vertices,self.triangles,self.materials,[self.light],lambda _:True,lambda *_:True),dict.fromkeys(range(12),7))
        self.assertFalse(source_components(self.vertices,self.triangles,self.materials,[self.light],lambda _:True,lambda *_:False))

    def test_room_and_whole_model_are_not_emitter_cores(self):
        self.triangles=self.triangles[:12];self.assertFalse(self.detect())
        self.vertices=[];self.triangles=[]
        cube(self.vertices,self.triangles,(0,0,2),2)
        cube(self.vertices,self.triangles,(0,0,-1),.1)
        self.assertFalse(self.detect())

    def test_front_facing_frame_or_open_surface_is_not_an_enclosure(self):
        self.vertices=[(*v[:3],*(-x for x in v[3:6]),*v[6:]) for v in self.vertices]
        self.assertFalse(self.detect())
        self.vertices=[(*v[:3],*(-x for x in v[3:6]),*v[6:]) for v in self.vertices]
        self.triangles=self.triangles[:4]+self.triangles[12:]
        self.assertFalse(self.detect())


class PairedLayers(unittest.TestCase):
    def setUp(self):
        self.vertices=[];self.triangles=[]
        self.materials=[('glass',0,False,0x800007),('glass',0,False)]
        cube(self.vertices,self.triangles,(0,0,2),.25)
        first=len(self.triangles);cube(self.vertices,self.triangles,(0,0,2),.2495)
        self.triangles[first:]=[(*t[:3],1) for t in self.triangles[first:]]
        first=len(self.triangles);cube(self.vertices,self.triangles,(0,0,-2),.5)
        self.triangles[first:]=[(*t[:3],1) for t in self.triangles[first:]]
        self.light=((0,0,2),(1,.5,.2),1,8,7,2,2)

    def detect(self,static=lambda _:True):
        return paired_source_layers(self.vertices,self.triangles,self.materials,[self.light],static)

    def test_complete_duplicate_shares_ownership_but_frame_stays(self):
        self.assertEqual(self.detect(),dict.fromkeys(range(12,24),7))
        for scale in (.2,3):
            vertices=[(*(x*scale for x in p[:3]),*p[3:]) for p in self.vertices]
            light=((0,0,2*scale),self.light[1],scale,8*scale,*self.light[4:])
            self.assertEqual(paired_source_layers(vertices,self.triangles,self.materials,[light],lambda _:True),self.detect())

    def test_uv_normal_displacement_and_partial_copies_fail_closed(self):
        for change in ('uv','normal','offset','partial','winding','ambiguous','moving','texture','blend'):
            with self.subTest(change=change):
                self.setUp();vertex=self.triangles[12][0];p=list(self.vertices[vertex])
                if change=='uv':p[6]+=.01
                if change=='normal':p[3:6]=[-x for x in p[3:6]]
                if change=='offset':p[0]+=.01
                self.vertices[vertex]=tuple(p)
                if change=='partial':self.triangles.pop(12)
                if change=='winding':a,b,c,m=self.triangles[12];self.triangles[12]=(c,b,a,m)
                if change=='ambiguous':self.triangles.append(self.triangles[12])
                if change=='texture':self.materials[1]=('metal',0,False)
                if change=='blend':self.materials[1]=('glass',1,False)
                self.assertFalse(self.detect((lambda v:v!=vertex) if change=='moving' else lambda _:True))

if __name__=='__main__':unittest.main()
