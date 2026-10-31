#!/usr/bin/env python3
# northlight-test: requires=client,stormlib
"""Independent segment oracle plus the configured client's actual Dalaran inn.

Actual-asset prerequisite: a configured 3.3.5 client with Northrend ADTs,
nd_dalaran.wmo/group files and its placed candle/table M2/skin assets. The
private JSON includes both visible and opaque-cache intersections; a passing
geometry test does not certify the runtime's translucent-shadow policy.
"""
import json
from pathlib import Path
import sys
import unittest

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parents[1] / 'renderer')]
import northlight_paths as fp
from dalaran_candle_audit import CITY, audit, segment_hits, tabletop_proof
from world_scene_builder import Assets


class SegmentOracle(unittest.TestCase):
    def test_table_blocks_underneath_but_not_the_lit_side(self):
        # Independent opaque plank at z=1; expected hit is halfway down.
        vertices = [(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]
        triangles = [(0,1,2),(0,2,3)]
        source, under = (0,0,2),(0,0,0)
        hits = segment_hits(source,under,vertices,triangles)
        self.assertTrue(hits)
        self.assertEqual(hits[0]['point'],(0.,0.,1.))
        self.assertAlmostEqual(hits[0]['fraction'],.5)
        self.assertTrue(segment_hits(under,source,vertices,triangles))
        self.assertFalse(segment_hits(source,(0,0,1.2),vertices,triangles))
        self.assertFalse(segment_hits(source,(3,0,0),vertices,triangles))
        self.assertFalse(segment_hits(source,under,vertices,[]))
        mesh = vertices,triangles,[]
        table = 'synthetic_plank',(0,0,0),(0,0,0,1),1.
        proof = tabletop_proof(source,[source],table,mesh,mesh)
        self.assertEqual(proof['blocked_rays'],9)
        self.assertEqual(proof['opaque_cache_blocked_rays'],9)
        self.assertEqual(proof['anchor_above_table_m'],1.)
        # Removing the caster geometry must expose the precise gap, not pass
        # because the visual tabletop intersects an unrelated test ray.
        missing = tabletop_proof(source,[source],table,mesh,(vertices,[],[]))
        self.assertEqual(missing['blocked_rays'],9)
        self.assertEqual(missing['opaque_cache_blocked_rays'],0)


class ActualDalaran(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        client = fp.client_root(required=False)
        if client is None:
            raise unittest.SkipTest('Requires configured 3.3.5 client; no game launch')
        assets = Assets(client,without='y')
        try:
            if assets.provider(CITY) is None:
                raise unittest.SkipTest('Configured client lacks the Dalaran WMO prerequisite')
            cls.result = audit(assets)
        finally:
            assets.close()
        (fp.output_dir()/'dalaran-candle-table-geometry.json').write_text(json.dumps(cls.result,indent=2)+'\n')

    def test_actual_placed_flames_are_above_actual_tabletops(self):
        result = self.result
        self.assertGreaterEqual(len(result['placements']),1)
        self.assertGreaterEqual(result['counts']['paired_sources'],8)
        self.assertEqual(result['counts']['above_table_sources'],result['counts']['paired_sources'])
        self.assertTrue({'alliance_inn_interior','corner_inn_interior'} <= {c['group'] for c in result['cases']})
        for case in result['cases']:
            self.assertGreaterEqual(case['flame_source_count'],3)
            self.assertGreater(case['minimum_flame_above_table_m'],.2)
            self.assertGreater(case['table_triangles'],100)
            self.assertTrue(case['candle_sha256'] and case['table_sha256'])
            self.assertIn(case['city_uid'],{p['uid'] for p in result['placements']})

    def test_under_table_segments_hit_real_table_geometry(self):
        counts = self.result['counts']
        self.assertGreaterEqual(counts['rays'],72)
        self.assertEqual(counts['blocked_rays'],counts['rays'])
        # Cache results remain an explicit measured output. They must not be
        # substituted for visible-surface intersections in the evidence.
        self.assertTrue(all('opaque_cache_blocked' in ray for case in self.result['cases'] for ray in case['under_table_rays']))
        print('Dalaran actual geometry:',json.dumps(counts))


if __name__ == '__main__':
    unittest.main()
