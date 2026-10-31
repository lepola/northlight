#!/usr/bin/env python3
# northlight-test: requires=stormlib
"""Native fire loops preserve average energy, source bytes and strict track bounds."""
from pathlib import Path
import copy
import struct
import sys
import unittest
sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'renderer')]
from emitter_tracks import NativeTracks, pulse_for, validate_track
from m2_emitter_patch import append_lights, verify_lights


class Tracks(unittest.TestCase):
    def model(self):
        blob=bytearray(512);blob[:4]=b'MD20';struct.pack_into('<I',blob,4,264)
        struct.pack_into('<II',blob,20,2,400);struct.pack_into('<II',blob,400,1700,2300)
        return blob

    def test_native_loops_energy_and_preservation(self):
        for seed in range(160):
            source=dict(position=(1.,2.,3.),rgb=(.2,.7,1.),intensity=.03 if seed%2 else 2.,start=.2,end=4.,emission='fire')
            blob=self.model();out=bytearray(blob);tracks=NativeTracks(out);pulse=pulse_for(source,seed)
            encoded=tracks.track('<3f',source['rgb'],pulse);at=tracks.add(encoded);tracks.finish()
            validate_track(out,at,'<3f',source['rgb'],pulse)
            ng,og=struct.unpack_from('<II',out,20)
            self.assertEqual(struct.unpack_from('<2I',out,og),(1700,2300))
            self.assertEqual(ng,4)
            times,factors=pulse['times_ms'],pulse['factors']
            mean=sum((b-a)*(x+y)/2 for a,b,x,y in zip(times,times[1:],factors,factors[1:]))/pulse['period_ms']
            self.assertAlmostEqual(mean,1.,places=12)
            self.assertGreater(max(factors)-min(factors),.1)
            self.assertEqual(factors[0],factors[-1])
            patched=append_lights(bytes(blob),[source]);verify_lights(bytes(blob),patched,[source])

    def test_nonfire_and_shared_period(self):
        self.assertIsNone(pulse_for({'emission':'energy'}))
        out=self.model();tracks=NativeTracks(out);pulse=pulse_for({'emission':'candle'})
        tracks.track('<3f',(1.,.2,.01),pulse);tracks.track('<3f',(.1,.2,1.),pulse);tracks.finish()
        self.assertEqual(struct.unpack_from('<I',out,20)[0],4)

    def test_malformed_and_corrupt_output_rejected(self):
        good=pulse_for({'emission':'fire'})
        for key,value in [('period_ms',0),('times_ms',[0,3,2]),('factors',[float('nan')]*9)]:
            bad=copy.deepcopy(good);bad[key]=value
            with self.assertRaises(ValueError):NativeTracks(self.model()).track('<3f',(1,1,1),bad)
        out=self.model();tracks=NativeTracks(out);encoded=tracks.track('<f',(.4,),good);at=tracks.add(encoded);tracks.finish()
        _,_,_,_,_,ok=struct.unpack_from('<hh4I',out,at);_,ko=struct.unpack_from('<II',out,ok)
        struct.pack_into('<f',out,ko,8.)
        with self.assertRaises(ValueError):validate_track(out,at,'<f',(.4,),good)


if __name__=='__main__':unittest.main()
