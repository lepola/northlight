"""Bounded FGR weather capture import; no GPU, client or optional dependencies."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('weather_lab', ROOT / 'scripts/weather_lab.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class WeatherCapture(unittest.TestCase):
    def test_capture_contract(self):
        with tempfile.TemporaryDirectory(dir=lab.fp.output_dir()) as temporary:
            folder = Path(temporary)
            constants = [[0.0] * 4 for _ in range(10)]
            constants[0] = [.25, 1 / 3, .1, 1000]
            constants[1] = [1.3, 1.7, 1, 42]
            constants[2][1] = 1
            constants[3][0] = .8
            metadata = dict(schema='northlight-weather-capture-v1', status='captured', width=4, height=3,
                            constants=constants, kind='rain', preset='Performance', intensity=.8, blend=1,
                            layer_gate=1, heat=0, sky_seen=True, complete=True, name_mask_required=True,
                            depth_file='depth.fgr', scene_file='scene.fgr', name_mask_file='mask.fgr',
                            quality={'RainDropSize': 125})
            def fgr(name, pixel_format, stride, data):
                (folder / name).write_bytes(struct.pack('<5I', 0x31524746, 4, 3, pixel_format, stride) + data)
            fgr('depth.fgr', 114, 4, struct.pack('<12f', *([1.0] * 12)))
            fgr('scene.fgr', 21, 4, bytes([30, 60, 90, 255]) * 12)
            fgr('mask.fgr', 21, 4, bytes([0, 0, 255, 255]) + bytes(44))
            path = folder / 'capture-1-weather.json'
            def prepare():
                path.write_text(json.dumps(metadata), encoding='utf-8')
                return lab.prepare_capture(path, folder).read_bytes()
            data = prepare()
            self.assertEqual(struct.unpack_from('<6I', data), (0x33504357, 4, 3, 1, 2, 27))
            self.assertEqual(len(data), 276 + 12 * 12)
            settings = struct.unpack_from('<19I', data, 184)
            self.assertEqual(settings[5], 125)
            self.assertEqual(settings[10:], (1, 1, 70, 1, 60, 100, 45, 100, 1))
            self.assertEqual(data[276 + 96 + 3], 255)
            self.assertEqual(data[276 + 96 + 7], 0)
            fgr('scene.fgr', 113, 8, struct.pack('<4e', .25, .5, 1, 1) * 12)
            hdr = prepare()
            self.assertTrue(struct.unpack_from('<6I', hdr)[5] & 4)
            self.assertEqual(hdr[276], 255)
            self.assertLess(hdr[278], hdr[277])
            fgr('scene.fgr', 21, 4, bytes([30, 60, 90, 255]) * 12)
            metadata['name_mask_file'] = None
            metadata['complete'] = False
            self.assertEqual(struct.unpack_from('<6I', prepare())[5], 18)
            metadata['depth_file'] = '../outside.fgr'
            with self.assertRaisesRegex(ValueError, 'sibling basename'):
                prepare()
            metadata['depth_file'] = 'depth.fgr'
            (folder / 'depth.fgr').write_bytes(b'truncated')
            with self.assertRaisesRegex(ValueError, 'Truncated'):
                prepare()
            fgr('depth.fgr', 114, 4, struct.pack('<12f', *([float('nan')] * 12)))
            with self.assertRaisesRegex(ValueError, 'non-finite'):
                prepare()
            metadata['width'] = 100000
            with self.assertRaisesRegex(ValueError, 'dimensions'):
                prepare()
            metadata['width'] = 4
            metadata['status'] = 'unavailable'
            metadata['reason'] = 'no sky'
            with self.assertRaisesRegex(ValueError, 'no sky'):
                prepare()


if __name__ == '__main__':
    unittest.main()
