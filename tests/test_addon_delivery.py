#!/usr/bin/env python3
# northlight-test:
"""Install the actual packaged addon, preserve profiles, repeat and restore exactly."""
from pathlib import Path
import hashlib,json,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
import build_packages
import test_windows_installer as fixture

class AddonDelivery(unittest.TestCase):
    def test_actual_payload_round_trip(self):
        case=fixture.InstallerTests('test_install_restore_round_trip');case.setUp()
        try:
            manifest=json.loads((case.pkg/'payload-manifest.json').read_text())
            for name,source in build_packages.PAYLOAD_COMMON.items():
                data=(fp.REPO/source).read_bytes();path=case.pkg/'payload'/name
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
                manifest.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),
                                 **({'preserve':True} if name in build_packages.PRESERVE else {})})
                installed=case.client/name;installed.parent.mkdir(parents=True,exist_ok=True)
                if name.endswith('.lua') or name.endswith('.toc'):installed.write_bytes(b'previous addon\n')
                else:installed.write_bytes(b'; user values\n[Quality]\nPreset=Quality\nGI=0\n')
            (case.pkg/'payload-manifest.json').write_text(json.dumps(manifest))
            before=case.snapshot();backup=fixture.m.install(case.client,case.pkg)
            for name,source in build_packages.PAYLOAD_COMMON.items():
                actual=case.read(name)
                if name not in build_packages.PRESERVE:self.assertEqual(actual,(fp.REPO/source).read_bytes())
                elif name!='northlight-quality.ini':self.assertEqual(actual,before[name])
                else:self.assertTrue(actual.startswith(before[name]))
            self.assertIsNone(fixture.m.install(case.client,case.pkg))
            fixture.m.restore(case.client,backup);self.assertEqual(case.snapshot(),before)
        finally:case.tearDown()

if __name__=='__main__':unittest.main()
