# northlight-test:
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import builtins
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('installer',fp.src('windows-package/install.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
PROXY=b'MZ Northlight renderer 0.3.148; new proxy'
FOREIGN=b'MZ DXVK: \0v1.10.3-20230507-async (macOS)\0'

class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.base=Path(self.tmp.name)
        self.client=self.base/'game';self.client.mkdir()
        self.pkg=self.base/'package';(self.pkg/'payload').mkdir(parents=True)
        self.exe=b'any wow.exe build';(self.client/'wow.exe').write_bytes(self.exe)
        self.files={'d3d9.dll':PROXY,'northlight-renderer.ini':b'[Renderer]\r\nBackend=dxvk\r\n','renderer-backends/dxvk/dxvk_d3d9.dll':b'MZ DXVK: \0v3.1.1\0','renderer-backends/dxvk2/dxvk2_d3d9.dll':b'MZ DXVK: \0v2.7.1\0','world-cache/models/a.fgm':b'model'}
        manifest=[]
        for name,data in self.files.items():
            p=self.pkg/'payload'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
            manifest.append({'path':name,'sha256':hashlib.sha256(data).hexdigest()})
        (self.pkg/'payload-manifest.json').write_text(json.dumps(manifest))
        # The installer never opens wow.exe (read or write) nor renames/replaces it:
        # every open, os.replace and shutil copy in the process is checked.
        self.exe_access=[];real_open,real_path_open,real_replace=builtins.open,Path.open,os.replace
        def track(path):
            if Path(os.fsdecode(path)).name.lower()=='wow.exe':self.exe_access.append(os.fsdecode(path))
        def spy_open(file,*a,**k):
            if isinstance(file,(str,bytes,os.PathLike)):track(file)
            return real_open(file,*a,**k)
        def spy_path_open(path,*a,**k):track(path);return real_path_open(path,*a,**k)
        def spy_replace(src,dst,*a,**k):track(src);track(dst);return real_replace(src,dst,*a,**k)
        self.spies=[patch('builtins.open',spy_open),patch.object(Path,'open',spy_path_open),patch('os.replace',spy_replace)]
        for p in self.spies:p.start()
    def tearDown(self):
        for p in reversed(self.spies):p.stop()
        self.assertEqual(self.exe_access,[]);self.assertEqual((self.client/'wow.exe').read_bytes(),self.exe)
        self.tmp.cleanup()
    def read(self,name):
        p=self.client/name;return p.read_bytes() if p.exists() else None
    def foreign(self):
        # A user's own d3d9.dll (DXVK, ReShade) in the game folder.
        (self.client/'d3d9.dll').write_bytes(FOREIGN)
    def snapshot(self):
        # wow.exe is left out: reading it here would trip the spy; tearDown checks it is unchanged.
        return {p.relative_to(self.client).as_posix():p.read_bytes() for p in self.client.rglob('*') if p.is_file() and 'renderer-backups' not in p.parts and p.name!='wow.exe'}
    def test_install_restore_round_trip(self):
        before=self.snapshot();backup=m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),PROXY);self.assertIsNone(self.read('renderer-backends/legacy/legacy_d3d9.dll'))
        record=json.loads((backup/'transaction.json').read_text())
        self.assertNotIn('wow.exe',[e['path'] for e in record['files']])
        self.assertIsNone(m.install(self.client,self.pkg))
        m.restore(self.client,backup)
        self.assertEqual(self.snapshot(),before)
    def test_foreign_moved_and_restore_round_trip(self):
        self.foreign();before=self.snapshot()
        backup=m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),PROXY);self.assertEqual(self.read('renderer-backends/legacy/legacy_d3d9.dll'),FOREIGN)
        self.assertIn(b'Backend=dxvk',self.read('northlight-renderer.ini'))
        self.assertFalse((backup/'staged').exists())
        self.assertIsNone(m.install(self.client,self.pkg))
        record=[e['path'] for e in json.loads((backup/'transaction.json').read_text())['files']]
        # Crash-safe order: backend copy first, proxy last.
        self.assertEqual(record[0],'renderer-backends/legacy/legacy_d3d9.dll');self.assertEqual(record[-1],'d3d9.dll')
        m.restore(self.client,backup)
        self.assertEqual(self.snapshot(),before)   # foreign d3d9.dll back; no legacy copy/ini
    def test_dxvk2_backend_is_written_and_kept(self):
        first=m.install(self.client,self.pkg,backend='dxvk2')
        self.assertEqual(self.read('northlight-renderer.ini'),b'[Renderer]\r\nBackend=dxvk2\r\n')
        self.assertEqual(self.read('renderer-backends/dxvk2/dxvk2_d3d9.dll'),self.files['renderer-backends/dxvk2/dxvk2_d3d9.dll'])
        self.assertEqual(self.read('renderer-backends/dxvk/dxvk_d3d9.dll'),self.files['renderer-backends/dxvk/dxvk_d3d9.dll'])
        self.assertIsNone(m.install(self.client,self.pkg))   # a plain reinstall keeps dxvk2
        second=m.install(self.client,self.pkg,backend='dxvk')
        self.assertEqual(self.read('northlight-renderer.ini'),b'[Renderer]\r\nBackend=dxvk\r\n')
        m.restore(self.client,second);m.restore(self.client,first)
    def test_dxvk3_marker_is_not_a_package_file(self):
        marker=self.client/m.DXVK3_MARKER;marker.parent.mkdir(parents=True);marker.write_bytes(b'pending')
        backup=m.install(self.client,self.pkg,backend='dxvk2')
        self.assertTrue(marker.is_file())
        self.assertNotIn(m.DXVK3_MARKER,[e['path'] for e in json.loads((backup/'transaction.json').read_text())['files']])
        m.restore(self.client,backup)
        self.assertTrue(marker.is_file())
    DXVK_FILE='renderer-backends/dxvk/dxvk_d3d9.dll';DXVK2_FILE='renderer-backends/dxvk2/dxvk2_d3d9.dll'
    def damage(self,name):
        (self.pkg/'payload'/name).write_bytes(b'quarantined');
    def remove(self,name):
        (self.pkg/'payload'/name).unlink()
    def quiet(self,fn,*a,**k):
        out=[]
        with patch('builtins.print',lambda *x,**y:out.append(' '.join(map(str,x)))):
            result=fn(*a,**k)
        return result,out
    def test_unselected_dxvk_file_may_be_missing(self):
        for backend,gone in [('dxvk',self.DXVK2_FILE),('dxvk2',self.DXVK_FILE),('native',self.DXVK_FILE)]:
            with self.subTest(backend=backend):
                self.remove(gone)
                backup,out=self.quiet(m.install,self.client,self.pkg,backend=backend)
                self.assertTrue(any('skipping' in line for line in out))
                self.assertEqual(self.read('d3d9.dll'),PROXY)
                self.assertIsNone(self.read(gone))
                m.restore(self.client,backup)
                (self.pkg/'payload'/gone).write_bytes(self.files[gone])
    def test_present_but_modified_dxvk_file_is_always_refused(self):
        for backend,name in [('dxvk',self.DXVK_FILE),('dxvk',self.DXVK2_FILE),('dxvk2',self.DXVK_FILE),('dxvk2',self.DXVK2_FILE),('native',self.DXVK_FILE)]:
            with self.subTest(backend=backend,file=name):
                self.damage(name)
                with self.assertRaises(ValueError) as e:m.install(self.client,self.pkg,backend=backend)
                self.assertIn('damaged or modified; download and unzip the package again',str(e.exception))
                self.assertFalse((self.client/'d3d9.dll').exists())
                (self.pkg/'payload'/name).write_bytes(self.files[name])
    def test_selected_dxvk_file_missing_names_the_alternatives(self):
        for backend,name,alt in [('dxvk',self.DXVK_FILE,'--backend dxvk2 / --backend native'),('dxvk2',self.DXVK2_FILE,'--backend dxvk / --backend native')]:
            with self.subTest(backend=backend):
                self.remove(name)
                with self.assertRaises(ValueError) as e:m.install(self.client,self.pkg,backend=backend)
                text=str(e.exception)
                self.assertIn('antivirus product may have removed it',text);self.assertIn('Allow the file and unzip the package again',text)
                self.assertIn('install with '+alt,text)
                self.assertFalse((self.client/'d3d9.dll').exists())
                (self.pkg/'payload'/name).write_bytes(self.files[name])
    def test_stale_client_dxvk2_is_removed_when_missing_from_package_and_restored(self):
        m.install(self.client,self.pkg,backend='dxvk2')
        stale=self.client/self.DXVK2_FILE;stale.write_bytes(b'older unverified dxvk2')
        self.remove(self.DXVK2_FILE)
        before=self.snapshot()
        backup,out=self.quiet(m.install,self.client,self.pkg,backend='dxvk')
        self.assertFalse(stale.exists());self.assertTrue(any('dxvk2 will not be available' in l for l in out))
        entries={e['path']:e for e in json.loads((backup/'transaction.json').read_text())['files']}
        self.assertIsNone(entries[self.DXVK2_FILE]['after'])
        m.restore(self.client,backup)
        self.assertEqual(self.snapshot(),before)
    def test_stale_client_dxvk2_rollback_when_commit_fails(self):
        m.install(self.client,self.pkg,backend='dxvk2')
        stale=self.client/self.DXVK2_FILE;stale.write_bytes(b'older unverified dxvk2')
        self.remove(self.DXVK2_FILE)
        before=self.snapshot()
        real=m.replace_file
        def fail(src,dst):
            if Path(dst).name=='late.bin':raise OSError('disk full')
            return real(src,dst)
        with patch.object(m,'replace_file',fail),self.assertRaises(OSError):
            self.quiet(m.install,self.client,self.pkg,backend='dxvk',extra={'late.bin':b'x'})   # fails after the stale file was removed
        self.assertEqual(self.snapshot(),before)
    def test_native_and_dxvk2_are_kept_on_a_plain_reinstall(self):
        for backend in ('native','dxvk2'):
            with self.subTest(backend=backend):
                m.install(self.client,self.pkg,backend=backend)
                self.assertIsNone(m.install(self.client,self.pkg))
                self.assertEqual(self.read('northlight-renderer.ini'),m.config_bytes(backend))
    def test_resolve_backend_is_the_single_source(self):
        ini=self.client/'northlight-renderer.ini'
        self.assertEqual(m.resolve_backend(self.client),'dxvk')
        for installed,expected in [('dxvk2','dxvk2'),('native','native'),('dxvk','dxvk'),('legacy','dxvk'),('bogus','dxvk')]:
            ini.write_bytes(m.config_bytes(installed));self.assertEqual(m.resolve_backend(self.client),expected)
        self.assertEqual(m.resolve_backend(self.client,'dxvk'),'dxvk')   # explicit wins
        self.assertEqual(m.resolve_backend(self.client,'native'),'native')
        ini.write_bytes(m.config_bytes('native'))
        with patch.object(m,'resolve_backend',side_effect=AssertionError('payload_plan re-resolved')):
            m.payload_plan(self.client,self.pkg,backend='dxvk2')
        with self.assertRaises(ValueError):m.payload_plan(self.client,self.pkg)
    def test_backend_path_survives_a_plain_reinstall(self):
        m.install(self.client,self.pkg)
        ini=self.client/'northlight-renderer.ini';custom=b'[Renderer]\r\nBackend=dxvk\r\nBackendPath=my_d3d9.dll\r\n'
        ini.write_bytes(custom)
        self.assertIsNone(m.install(self.client,self.pkg));self.assertEqual(ini.read_bytes(),custom)
        m.install(self.client,self.pkg,backend='dxvk2');self.assertEqual(ini.read_bytes(),m.config_bytes('dxvk2'))
    def test_legacy_stays_selected_while_the_foreign_dll_is_there(self):
        self.foreign();m.install(self.client,self.pkg,backend='legacy')
        self.assertEqual(self.read('northlight-renderer.ini'),m.config_bytes('legacy'))
        self.assertIsNone(m.install(self.client,self.pkg))
    def test_other_dxvk_predicate(self):
        self.assertEqual(m.other_dxvk(self.DXVK2_FILE,'dxvk'),'dxvk2');self.assertEqual(m.other_dxvk(self.DXVK_FILE,'dxvk2'),'dxvk')
        self.assertIsNone(m.other_dxvk(self.DXVK_FILE,'dxvk'));self.assertIsNone(m.other_dxvk('d3d9.dll','native'))
        self.assertEqual(m.other_dxvk(self.DXVK_FILE,'native'),'dxvk')
    def test_package_sha_is_memoised_per_file_state(self):
        p=self.pkg/'payload'/self.DXVK_FILE
        with patch.object(m,'sha',wraps=m.sha) as spy:
            m.package_sha(p);m.package_sha(p);self.assertEqual(spy.call_count,1)
            p.write_bytes(b'changed content');self.assertEqual(m.package_sha(p),hashlib.sha256(b'changed content').hexdigest())
    def test_dxvk_folder_link_refused(self):
        (self.client/'renderer-backends').mkdir();other=self.base/'elsewhere';other.mkdir()
        (self.client/'renderer-backends/dxvk').symlink_to(other,target_is_directory=True)
        with self.assertRaises(ValueError):m.safe_path(self.client,m.DXVK3_MARKER)
        with self.assertRaises(ValueError):m.install(self.client,self.pkg)
        self.assertEqual(list(other.iterdir()),[])
    def test_safe_path_of_a_missing_file_is_allowed(self):
        self.assertEqual(m.safe_path(self.client,m.DXVK3_MARKER),self.client/m.DXVK3_MARKER)
    def test_use_existing_selects_legacy_and_is_kept_on_update(self):
        self.foreign();first=m.install(self.client,self.pkg,use_existing=True)
        self.assertEqual(self.read('northlight-renderer.ini'),b'[Renderer]\r\nBackend=legacy\r\n')
        self.assertIsNone(m.install(self.client,self.pkg))   # the choice survives a plain reinstall
        m.restore(self.client,first)
        self.assertIsNone(self.read('northlight-renderer.ini'));self.assertEqual(self.read('d3d9.dll'),FOREIGN)
    def test_other_tool_backup_refused(self):
        backup=m.install(self.client,self.pkg);path=backup/'transaction.json'
        record=json.loads(path.read_text());record['kind']='mac-proxy-migration';path.write_text(json.dumps(record))
        with self.assertRaises(ValueError):m.restore(self.client,backup)
    def test_use_existing_without_foreign_keeps_package_backend(self):
        m.install(self.client,self.pkg,use_existing=True)
        self.assertIn(b'Backend=dxvk',self.read('northlight-renderer.ini'))
    def test_legacy_slot_replaced_and_restored(self):
        slot=self.client/'renderer-backends/legacy/legacy_d3d9.dll';slot.parent.mkdir(parents=True);slot.write_bytes(b'MZ older legacy')
        (self.client/'d3d9.dll').write_bytes(FOREIGN);before=self.snapshot()
        backup=m.install(self.client,self.pkg)
        self.assertEqual(slot.read_bytes(),FOREIGN)
        m.restore(self.client,backup);self.assertEqual(self.snapshot(),before)
    def test_older_proxy_is_upgraded_not_moved(self):
        (self.client/'d3d9.dll').write_bytes(b'MZ Northlight renderer 0.3.148-old')
        backup=m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),PROXY);self.assertIsNone(self.read('renderer-backends/legacy/legacy_d3d9.dll'))
        m.restore(self.client,backup);self.assertEqual(self.read('d3d9.dll'),b'MZ Northlight renderer 0.3.148-old')
    def test_proxy_owned_by_record_or_signature_is_upgraded_not_moved(self):
        # Builds from before a rename carry neither today's banner nor a name we still match: a proxy is ours
        # by the digest an unrestored transaction of this client wrote, or by its PROXY start-up line.
        old=b'MZ renamed-away build 0.3.165';record=self.client/'renderer-backups/20260101-000000-old/transaction.json'
        for data,recorded in [(old,True),(b'MZ renamed-away build; PROXY module=%ls root=%ls',False)]:
            with self.subTest(data=data):
                (self.client/'d3d9.dll').write_bytes(data)
                if recorded:
                    record.parent.mkdir(parents=True);record.write_text(json.dumps({'version':'0.3.165','kind':'package','client':str(self.client.resolve()),
                        'status':'installed','files':[{'path':'d3d9.dll','before':None,'after':hashlib.sha256(data).hexdigest()}]}))
                self.assertTrue(m.is_ours(self.client))
                backup=m.install(self.client,self.pkg)
                self.assertEqual(self.read('d3d9.dll'),PROXY);self.assertIsNone(self.read('renderer-backends/legacy/legacy_d3d9.dll'))
                m.restore(self.client,backup);self.assertEqual(self.read('d3d9.dll'),data)
                if recorded:record.unlink()
    def test_unrecorded_foreign_proxy_is_still_moved(self):
        # The same bytes without a record, or a record of another client or a restored one, stay foreign.
        old=b'MZ renamed-away build 0.3.165';(self.client/'d3d9.dll').write_bytes(old)
        record=self.client/'renderer-backups/20260101-000000-old/transaction.json';record.parent.mkdir(parents=True)
        for client,status in [(str(self.base/'other'),'installed'),(str(self.client.resolve()),'restored')]:
            record.write_text(json.dumps({'kind':'package','client':client,'status':status,
                'files':[{'path':'d3d9.dll','before':None,'after':hashlib.sha256(old).hexdigest()}]}))
            self.assertFalse(m.is_ours(self.client))
        m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),PROXY);self.assertEqual(self.read('renderer-backends/legacy/legacy_d3d9.dll'),old)
    def test_update_requires_exact_cache_before_writes(self):
        self.foreign()
        cache=self.client/'world-cache/required.fgm';cache.parent.mkdir();cache.write_bytes(b'wrong')
        (self.pkg/'required-world-cache.json').write_text(json.dumps([{'path':'world-cache/required.fgm','sha256':hashlib.sha256(b'correct').hexdigest()}]))
        with self.assertRaises(ValueError):m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),FOREIGN);self.assertFalse((self.client/'renderer-backups').exists())
    def test_matching_update_cache_is_preserved(self):
        cache=self.client/'world-cache/required.fgm';cache.parent.mkdir();cache.write_bytes(b'correct')
        (self.pkg/'required-world-cache.json').write_text(json.dumps([{'path':'world-cache/required.fgm','sha256':m.sha(cache)}]))
        backup=m.install(self.client,self.pkg)
        m.restore(self.client,backup)
        self.assertEqual(cache.read_bytes(),b'correct')
    def test_installer_has_no_exe_code(self):
        src=fp.src('windows-package/install.py').read_text()
        for gone in ['OFFSET','frd9.dll','OLD_MOD','OLD_DXVK']:self.assertNotIn(gone,src)
    def test_spy_sees_exe_access(self):
        (self.client/'wow.exe').read_bytes();self.assertEqual(len(self.exe_access),1);self.exe_access.clear()   # the guard itself works
    def test_payload_corruption_rejected(self):
        self.foreign()
        (self.pkg/'payload/d3d9.dll').write_bytes(b'bad')
        with self.assertRaises(ValueError):m.install(self.client,self.pkg)
        self.assertEqual(self.read('d3d9.dll'),FOREIGN)
    def test_failure_mid_install_rolls_back(self):
        for failing in [1,2,3,4,5]:
            with self.subTest(failing=failing):
                self.foreign();before=self.snapshot()
                real=m.replace_file;count=[0]
                def fail(src,dst):
                    count[0]+=1
                    if count[0]==failing:raise OSError('simulated disk failure')
                    return real(src,dst)
                with patch.object(m,'replace_file',fail):
                    with self.assertRaises(OSError):m.install(self.client,self.pkg)
                self.assertEqual(self.snapshot(),before)
    def test_rollback_preserves_subsequent_edits(self):
        backup=m.install(self.client,self.pkg)
        (self.client/'d3d9.dll').write_bytes(b'user-later-edit')
        with self.assertRaises(ValueError):m.restore(self.client,backup)
        self.assertEqual(self.read('d3d9.dll'),b'user-later-edit')
    def test_damaged_backup_refused(self):
        self.foreign();backup=m.install(self.client,self.pkg)
        (backup/'before/d3d9.dll').write_bytes(b'bad')
        with self.assertRaises(ValueError):m.restore(self.client,backup)
        self.assertEqual(self.read('d3d9.dll'),PROXY)
    def test_path_escape_and_symlink_refused(self):
        for name in ['../outside','/absolute','C:/file','a/../../file','a\\..\\b']:
            with self.assertRaises(ValueError):m.safe_path(self.client,name)
        (self.client/'link').symlink_to(self.base,target_is_directory=True)
        with self.assertRaises(ValueError):m.safe_path(self.client,'link/file')
    def test_backend_switch_and_two_level_rollback(self):
        first=m.install(self.client,self.pkg)
        second=m.install(self.client,self.pkg,backend='native')
        self.assertEqual(self.read('northlight-renderer.ini'),b'[Renderer]\r\nBackend=native\r\n')
        m.restore(self.client,second)
        self.assertIn(b'Backend=dxvk',self.read('northlight-renderer.ini'))
        m.restore(self.client,first)
        self.assertIsNone(self.read('northlight-renderer.ini'))
    def add_quality(self,data=b'[Quality]\nPreset=Quality\n'):
        (self.pkg/'payload/northlight-quality.ini').write_bytes(data)
        manifest=json.loads((self.pkg/'payload-manifest.json').read_text())
        manifest.append({'path':'northlight-quality.ini','sha256':hashlib.sha256(data).hexdigest(),'preserve':True})
        (self.pkg/'payload-manifest.json').write_text(json.dumps(manifest))
    def test_quality_settings_added_only_when_missing(self):
        self.add_quality()
        backup=m.install(self.client,self.pkg)
        self.assertEqual(self.read('northlight-quality.ini'),b'[Quality]\nPreset=Quality\n')
        m.restore(self.client,backup)
        self.assertIsNone(self.read('northlight-quality.ini'))
    def test_quality_settings_never_overwritten(self):
        self.add_quality();(self.client/'northlight-quality.ini').write_bytes(b'[Quality]\nPreset=Performance\n')
        backup=m.install(self.client,self.pkg)
        self.assertEqual(self.read('northlight-quality.ini'),b'[Quality]\nPreset=Performance\n')
        record=json.loads((backup/'transaction.json').read_text())
        self.assertNotIn('northlight-quality.ini',[e['path'] for e in record['files']])
        m.restore(self.client,backup)
        self.assertEqual(self.read('northlight-quality.ini'),b'[Quality]\nPreset=Performance\n')
    def test_quality_settings_edited_after_install_survive_restore(self):
        self.foreign();self.add_quality();backup=m.install(self.client,self.pkg)
        (self.client/'northlight-quality.ini').write_bytes(b'[Quality]\nPreset=Balanced\n')
        m.restore(self.client,backup)
        self.assertEqual(self.read('northlight-quality.ini'),b'[Quality]\nPreset=Balanced\n')
        self.assertEqual(self.read('d3d9.dll'),FOREIGN)

if __name__=='__main__':unittest.main()
