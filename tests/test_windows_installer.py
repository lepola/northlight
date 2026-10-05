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
import types
import unittest
from datetime import datetime,timedelta
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
        self.files={'d3d9.dll':PROXY,'northlight-renderer.ini':b'[Renderer]\r\nBackend=dxvk\r\n','renderer-backends/dxvk/dxvk_d3d9.dll':b'MZ DXVK: \0v2.7.1\0','world-cache/models/a.fgm':b'model'}
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
        p=self.pkg/'payload/northlight-renderer.ini';p.write_bytes(b'[Renderer]\nBackend=native\n')
        manifest=json.loads((self.pkg/'payload-manifest.json').read_text())
        for e in manifest:
            if e['path']=='northlight-renderer.ini':e['sha256']=m.sha(p)
        (self.pkg/'payload-manifest.json').write_text(json.dumps(manifest))
        second=m.install(self.client,self.pkg)
        m.restore(self.client,second)
        self.assertIn(b'dxvk',self.read('northlight-renderer.ini'))
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
    def set_payload(self,name,data):
        (self.pkg/'payload'/name).write_bytes(data)
        manifest=json.loads((self.pkg/'payload-manifest.json').read_text())
        for e in manifest:
            if e['path']==name:e['sha256']=hashlib.sha256(data).hexdigest()
        (self.pkg/'payload-manifest.json').write_text(json.dumps(manifest))
    def stack(self,n=3):
        """n package versions installed on top of each other (distinct d3d9.dll; v2 also another ini): [backup, ...] oldest first."""
        out=[]
        for i in range(1,n+1):
            self.set_payload('d3d9.dll',PROXY+b' v%d'%i)
            if i==2:self.set_payload('northlight-renderer.ini',b'[Renderer]\r\nBackend=native\r\n')
            out.append(m.install(self.client,self.pkg,version='v%d'%i))
        return out
    def chain(self):return list(reversed(m.transactions(self.client)))
    def test_stacked_installs_chain_check_and_restore(self):
        self.foreign();before=self.snapshot();backups=self.stack()
        self.assertEqual([b for b,_ in m.transactions(self.client)],backups)
        self.assertEqual(m.chain_problems(self.client,self.chain()),[])
        self.assertTrue(m.restore_problems(self.client,backups[0]))   # alone it sees only the newest file
        for b,_ in self.chain():m.restore(self.client,b)
        self.assertEqual(self.snapshot(),before)
    def test_chain_refuses_edit_after_latest_install(self):
        backups=self.stack();(self.client/'d3d9.dll').write_bytes(b'user-edit')
        problems=m.chain_problems(self.client,self.chain())
        self.assertEqual(problems[0][0],backups[2]);self.assertIn('File changed after installation',problems[0][1])
    def test_chain_refuses_edit_between_installs(self):
        first=self.stack(1)[0];(self.client/'d3d9.dll').write_bytes(b'user-edit')
        self.set_payload('d3d9.dll',PROXY+b' v2');m.install(self.client,self.pkg,version='v2')
        problems=m.chain_problems(self.client,self.chain())
        self.assertEqual([b for b,_ in problems],[first]);self.assertIn('File changed after installation',problems[0][1])
        with self.assertRaises(ValueError):m.restore(self.client,first)   # after the newer one is restored
    def test_chain_damaged_older_backup(self):
        self.foreign();backups=self.stack();(backups[0]/'before/d3d9.dll').write_bytes(b'bad')
        problems=m.chain_problems(self.client,self.chain())
        self.assertEqual(problems,[(backups[0],'Backup damaged: d3d9.dll')])
    def test_chain_preserve_edit_kept(self):
        self.add_quality();backups=self.stack();(self.client/'northlight-quality.ini').write_bytes(b'[Quality]\nPreset=Balanced\n')
        self.assertEqual(m.chain_problems(self.client,self.chain()),[])
        for b,_ in self.chain():m.restore(self.client,b)
        self.assertEqual(self.read('northlight-quality.ini'),b'[Quality]\nPreset=Balanced\n')
    def test_chain_newer_record_deleting_a_file(self):
        name='world-cache/models/a.fgm';digest=hashlib.sha256(self.files[name]).hexdigest();payload=self.pkg/'payload'
        first=m.commit(self.client,[{'path':name,'before':None,'after':digest}],{},payload)
        second=m.commit(self.client,[{'path':name,'before':digest,'after':None}],{},payload)
        self.assertIsNone(self.read(name));self.assertEqual([b for b,_ in m.transactions(self.client)],[first,second])
        self.assertEqual(m.chain_problems(self.client,self.chain()),[])
        (self.client/name).write_bytes(b'user-file')   # created by the user after the deletion
        self.assertEqual(m.chain_problems(self.client,self.chain())[0][0],second)
        (self.client/name).unlink()
        for b,_ in self.chain():m.restore(self.client,b)
        self.assertIsNone(self.read(name))
    def test_same_second_transactions_keep_install_order(self):
        ticks=iter(range(1000));hexes=iter(range(10**6))
        fake_dt=types.SimpleNamespace(now=lambda:datetime(2030,1,1,12,0,0)+timedelta(microseconds=next(ticks)))
        # Decreasing suffixes: the later commit gets the lexically smaller folder name.
        fake_uuid=types.SimpleNamespace(uuid4=lambda:types.SimpleNamespace(hex='%08x'%(0xfffffff-next(hexes))+'0'*24))
        with patch.object(m,'datetime',fake_dt),patch.object(m,'uuid',fake_uuid):backups=self.stack()
        self.assertEqual(len({b.name[:15] for b in backups}),1);self.assertGreater(backups[0].name,backups[2].name)
        self.assertEqual([b for b,_ in m.transactions(self.client)],backups)

if __name__=='__main__':unittest.main()
