#!/usr/bin/env python3
"""Install this macOS (WoWSilicon) client's renderer as the d3d9.dll proxy preloaded
from dlls.txt, or undo that. Dry run unless --apply.
Never launches WoW, WoWSilicon or Wine; --apply refuses while any of them runs.
wow.exe is never read or written: the tool only checks that the file exists (so the
folder is a client) and that no wow.exe process runs.

  python3 migrate_mac_proxy.py --client "<game folder>"              # dry run
  python3 migrate_mac_proxy.py --client "<game folder>" --apply
  python3 migrate_mac_proxy.py --client "<game folder>" --restore [--apply]
  python3 migrate_mac_proxy.py --client "<game folder>" --status

WoWSilicon compares <game>/d3d9.dll with its bundled DXVK (Play is disabled on a
mismatch; Patch overwrites it), so that file is never touched. It only has to be
a DXVK build (not one of our renderer builds); its hash is shown, not required. In
this order (each step recorded; any failure undoes the steps done):
  1 renderer-backends/dxvk/dxvk_d3d9.dll  copy of <game>/d3d9.dll (WoWSilicon DXVK)
  2 northlight-renderer.ini                  Backend=legacy + BackendPath=<1>: today's
                                          runtime rules (no vendor 1002)
  3 mods/d3d9.dll                         this renderer build
  4 dlls.txt                              + "mods/d3d9.dll" (libDllLdr preloads it,
                                          so wow's LoadLibraryA("d3d9.dll") gets it)
Refused before any write unless WoWSilicon's dlls.txt preload is active and it does
not use MTLD3D for this client.
Restore removes only what is still ours (dlls.txt line, mods/d3d9.dll, backend
copy, ini); a recorded path outside these four is left as it is.
The player installer (northlight_install.py) calls plan() and apply() with its own
backup root (<client>/renderer-backups/mac-proxy, never the package or a repository).
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,sys,uuid
from datetime import datetime
HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('northlight_installer',HERE/'windows-package/install.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
KIND='mac-proxy-migration'
WOWSILICON_DXVK='675e41e8467bc5a4dad6268269fa73e41f8c2e4979da5a9a6aca56199f0073c0'   # d9vk bundled with WoWSilicon 3.2.1; informational
GAME_D3D9='d3d9.dll';BACKEND='renderer-backends/dxvk/dxvk_d3d9.dll';PROXY='mods/d3d9.dll';DLLS='dlls.txt';ENTRY='mods/d3d9.dll'
CONFIG=b'[Renderer]\r\nBackend=legacy\r\nBackendPath=renderer-backends/dxvk/dxvk_d3d9.dll\r\n'
MANAGED=(BACKEND,m.CONFIG,PROXY,DLLS)   # the only client files this tool writes
# WoWSilicon settings, read-only: which client it launches and its D3D9 backend.
VERSIONS=Path.home()/'Library/Application Support/WoWSilicon/versions.json'
BUSY=('wow.exe','wineserver','WoWSilicon')   # processes that hold or rewrite the game folder

def busy(procs,me):
    """BUSY names among procs [(pid, ppid, executable, command line)], leaving out `me` and its ancestors (the
    launcher's bash may carry a path with 'WoWSilicon' in its arguments). WoWSilicon and wineserver match by
    executable name only; wow.exe anywhere in the command line (Wine shows it as an argument)."""
    parent={pid:ppid for pid,ppid,_,_ in procs};mine=set()
    while me and me not in mine:mine.add(me);me=parent.get(me)
    found=set()
    for pid,_,exe,cmd in procs:
        if pid in mine:continue
        name=exe.replace('\\','/').rsplit('/',1)[-1].lower()
        found|={n for n in BUSY if (n.lower() in cmd.lower() if n=='wow.exe' else n.lower()==name)}
    return sorted(found)
def ps(fields):
    """Rows of `ps -axo <fields>`; the last field keeps its spaces."""
    out=subprocess.run(['ps','-axo',','.join(f+'=' for f in fields)],capture_output=True,text=True,check=True).stdout
    return [r+['']*(len(fields)-len(r)) for r in (l.strip().split(None,len(fields)-1) for l in out.splitlines() if l.strip())]
def running():
    commands={int(pid):cmd for pid,cmd in ps(['pid','command'])}
    return busy([(int(pid),int(ppid),exe,commands.get(int(pid),'')) for pid,ppid,exe in ps(['pid','ppid','comm'])],os.getpid())
def digest(data):return hashlib.sha256(data).hexdigest()
def read(path):return path.read_bytes() if path.is_file() else None
def version(dll):
    found=re.search(rb'Northlight renderer (\d+\.\d+\.\d+);',dll)
    return found.group(1).decode() if found else 'unknown'
def is_entry(line):return line.strip().replace('\\','/').lower()==ENTRY
def with_entry(text):
    # As WoWSilicon's PatchService.ensureEntry: keep every line, append ours.
    if any(is_entry(l) for l in text.splitlines()):return text
    return text+('' if not text or text.endswith('\n') else '\n')+ENTRY+'\n'
def without_entry(text):
    # Semantic: drop only our line(s), wherever a Mod Manager re-sort put them.
    lines=text.splitlines(keepends=True);kept=[l for l in lines if not is_entry(l)]
    return text if len(kept)==len(lines) else ''.join(kept)

def preload(client):
    """WoWSilicon's dlls.txt preload must be live, or wow.exe would load no renderer:
    libDllLdr.dll present, DivxDecoder.dll patched (differs from its .bak), winerosetta listed."""
    divx,bak=read(client/'DivxDecoder.dll'),read(client/'DivxDecoder.dll.bak')
    dlls=(read(client/DLLS) or b'').decode('utf-8','replace')
    return {'libDllLdr':(client/'libDllLdr.dll').is_file(),'DivxDecoder_patched':bool(divx and bak and divx!=bak),
            'winerosetta_listed':any(l.strip().replace('\\','/').lower()=='mods/winerosetta.dll' for l in dlls.splitlines())}

def launcher(client):
    """Warnings from WoWSilicon's versions.json (read-only). MTLD3D sets d3d9=b: the
    builtin replaces mods/d3d9.dll, so the renderer never loads."""
    try:versions=json.loads(VERSIONS.read_text(encoding='utf-8')).get('versions',{})
    except (OSError,ValueError):return ['WoWSilicon versions.json not readable; launcher backend not checked']
    mine=[(name,v) for name,v in versions.items() if v.get('game_path') and Path(v['game_path']).parent.resolve()==client.resolve()]
    if not mine:
        others=[v['game_path'] for v in versions.values() if v.get('game_path')]
        return ['WoWSilicon is not configured for this client (game_path: '+(', '.join(others) or 'none')+'); point it here before launching']
    return [f"WoWSilicon '{name}' uses the {backend(v)} backend; the renderer needs d9vk (DXVK), not mtld3d"
            for name,v in mine if backend(v)!='d9vk']
def backend(version):return version.get('settings',{}).get('graphicsSettings',{}).get('backend','?')
def mtld3d(client):
    """True when readable versions.json sets MTLD3D (d3d9=b) for this client."""
    try:versions=json.loads(VERSIONS.read_text(encoding='utf-8')).get('versions',{})
    except (OSError,ValueError):return False
    return any(v.get('game_path') and Path(v['game_path']).parent.resolve()==client.resolve() and backend(v)=='mtld3d' for v in versions.values())

def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    temp=path.with_name(path.name+'.frd9-'+uuid.uuid4().hex+'.tmp')
    try:
        temp.write_bytes(data)
        if path.exists():shutil.copymode(path,temp)
        os.replace(temp,path)
    finally:
        if temp.exists():temp.unlink()

def recorded(client,roots):
    """sha256 values our unrestored migrations of this client wrote to mods/d3d9.dll."""
    return {e['after'] for d in backups(client,roots) if (r:=json.loads((d/'transaction.json').read_text())).get('status')!='restored'
            for e in r['files'] if e['path']==PROXY}
def ours(data,digests=()):
    """A renderer build (banner or PROXY start-up line), or exactly what one of our migrations wrote."""
    return m.is_renderer(data) or digest(data) in digests

def plan(client,dll,roots=()):
    """Steps (path, before, after bytes) in crash-safe order; raises before any write.
    roots: backup roots whose records also mark an existing mods/d3d9.dll as ours."""
    data=dll.read_bytes()
    # Only a proxy-capable build (guarded loader, root = exe directory) may be preloaded.
    if m.MARKER not in data or m.PROXY_SIGNATURE not in data:raise ValueError('Not a proxy-capable Northlight renderer build: '+str(dll))
    if not (client/'wow.exe').is_file():raise ValueError('wow.exe missing in '+str(client))   # presence only
    game=read(client/GAME_D3D9)
    if game is None or m.is_renderer(game) or not m.is_dxvk(game):
        raise ValueError('<game>/d3d9.dll is not a DXVK build ('+('absent' if game is None else 'our renderer build' if m.is_renderer(game) else 'not DXVK')+
                         '). Patch the client in WoWSilicon with the DXVK backend; nothing changed.')
    missing=[k for k,ok in preload(client).items() if not ok]
    if missing:raise ValueError('WoWSilicon dlls.txt preload is not active ('+', '.join(missing)+'): mods/d3d9.dll would never load. Patch the client in WoWSilicon first; nothing changed.')
    if mtld3d(client):raise ValueError('WoWSilicon uses the MTLD3D backend for this client (d3d9=b): mods/d3d9.dll would never load. Switch to DXVK (d9vk); nothing changed.')
    current={p:read(client/p) for p in [BACKEND,m.CONFIG,PROXY,DLLS]}
    if current[PROXY] is not None and not ours(current[PROXY],recorded(client,roots)):
        raise ValueError('mods/d3d9.dll exists and is not our renderer build (another Mod Manager mod?); nothing changed.')
    if current[BACKEND] is not None and current[BACKEND]!=game and not m.is_dxvk(current[BACKEND]):
        raise ValueError(BACKEND+' exists and is not DXVK; nothing changed.')
    dlls=(current[DLLS] or b'').decode('utf-8')
    steps=[(BACKEND,current[BACKEND],game),(m.CONFIG,current[m.CONFIG],CONFIG),(PROXY,current[PROXY],data),
           (DLLS,current[DLLS],with_entry(dlls).encode('utf-8'))]
    return [{'path':p,'before':None if b is None else digest(b),'after':digest(a),'data':a,'old':b} for p,b,a in steps if b!=a]

def undo(client,steps,record_dir):
    """Semantic restore; later user/launcher edits are kept. Only MANAGED paths are touched."""
    report=[]
    for s in reversed(steps):
        if s['path'] not in MANAGED:report.append(s['path']+': not written by this tool; left as it is');continue
        path=client/s['path'];now=read(path)
        if s['path']==DLLS:
            text=(now or b'').decode('utf-8');new=without_entry(text)
            if new!=text:write(path,new.encode('utf-8'));report.append('dlls.txt: removed '+ENTRY)
            else:report.append('dlls.txt: '+ENTRY+' already absent')
            continue
        if now is None:report.append(s['path']+': already absent');continue
        mine=digest(now)==s['after'] or (s['path']==PROXY and m.is_renderer(now))
        if not mine:report.append(s['path']+': changed since the migration; left as it is');continue
        if s['before'] is None:path.unlink();report.append(s['path']+': removed')
        else:
            old=(record_dir/'before'/s['path']).read_bytes()
            if digest(old)!=s['before']:raise ValueError('Backup damaged: '+s['path'])
            write(path,old);report.append(s['path']+': previous file restored')
    for d in ['renderer-backends/dxvk','renderer-backends']:
        try:(client/d).rmdir()
        except OSError:pass
    return report

def apply(client,dll,backup_root,roots=()):
    if backup_root is None:raise ValueError('backup_root is required')
    steps=plan(client,dll,[backup_root,*roots])
    if not steps:return None
    stamp=datetime.now()
    record_dir=backup_root/(stamp.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8]);record_dir.mkdir(parents=True)
    for s in steps:   # every replaced file, before the first write
        if s['old'] is not None:
            target=record_dir/'before'/s['path'];target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(s['old'])
            if digest(target.read_bytes())!=s['before']:raise ValueError('Backup verification failed: '+s['path'])
    record={'kind':KIND,'created':stamp.strftime('%Y%m%d-%H%M%S.%f'),'version':version(dll.read_bytes()),'client':str(client),'status':'pending','files':[{k:s[k] for k in ('path','before','after')} for s in steps]}
    m.atomic_json(record_dir/'transaction.json',record)
    done=[]
    try:
        for s in steps:
            path=client/s['path'];now=read(path)
            if (None if now is None else digest(now))!=s['before']:raise ValueError('Changed during migration: '+s['path'])
            write(path,s['data'])
            done.append(s)
            if digest(path.read_bytes())!=s['after']:raise ValueError('Written checksum mismatch: '+s['path'])
        record['status']='installed';m.atomic_json(record_dir/'transaction.json',record)
    except BaseException:
        print('Migration interrupted; undoing the steps done:',flush=True)
        for line in undo(client,record['files'][:len(done)],record_dir):print('  '+line)
        record['status']='restored';m.atomic_json(record_dir/'transaction.json',record)
        raise
    return record_dir

def backups(client,roots):
    found=sorted((p.parent for root in roots for p in root.glob('*/transaction.json')),key=lambda d:d.name)
    return [d for d in found if (r:=json.loads((d/'transaction.json').read_text())).get('kind')==KIND and Path(r['client'])==client]

def info(client):
    """Informational identity lines (never preconditions): the game-folder d3d9.dll."""
    game=read(client/GAME_D3D9)
    return {'game_d3d9':('absent' if game is None else 'sha256 '+digest(game)[:12]+(', known WoWSilicon 3.2.1 DXVK' if digest(game)==WOWSILICON_DXVK else ', other build')+
                         (', DXVK '+(version_of(game) or '?') if m.is_dxvk(game) else ', not DXVK'))}
def version_of(dll):
    found=re.search(rb'DXVK: \0+(v[\x20-\x7e]{1,63})',dll);return found.group(1).decode() if found else None

def status(client):
    game=read(client/GAME_D3D9);proxy=read(client/PROXY);backend=read(client/BACKEND)
    dlls=(read(client/DLLS) or b'').decode('utf-8','replace')
    return {**info(client),
            'proxy_preloaded':any(is_entry(l) for l in dlls.splitlines()),
            'proxy_is_ours':bool(proxy and m.is_renderer(proxy)),'proxy_version':version(proxy) if proxy else None,
            'backend_present':backend is not None,'game_d3d9_is_dxvk':bool(game and not m.is_renderer(game) and m.is_dxvk(game)),
            'backend_matches_game_d3d9':bool(game and backend and digest(game)==digest(backend)),
            'config':(read(client/m.CONFIG) or b'').decode('utf-8','replace').strip(),
            'preload':preload(client),'launcher_warnings':launcher(client),
            'game_started_by_this_tool':False}

def main(argv=None):
    ap=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--client',type=Path,required=True)
    ap.add_argument('--dll',type=Path,default=HERE/'frd9.dll',help='renderer build to install as mods/d3d9.dll')
    ap.add_argument('--backup-root',type=Path,help='default: <renderer>/validation-<dll version>/migration-backup')
    mode=ap.add_mutually_exclusive_group()
    mode.add_argument('--restore',action='store_true');mode.add_argument('--status',action='store_true')
    ap.add_argument('--apply',action='store_true',help='write files (default: dry run)')
    args=ap.parse_args(argv)
    client=args.client.resolve(strict=True)
    if not (client/'wow.exe').is_file():raise ValueError('wow.exe missing in '+str(client))
    if args.status:print(json.dumps(status(client),indent=2));return
    if args.apply and (busy:=running()):raise ValueError('Close these first: '+', '.join(busy))
    root=args.backup_root or HERE/'records'/('validation-'+version(args.dll.read_bytes() if args.dll.is_file() else b''))/'migration-backup'
    roots=[args.backup_root] if args.backup_root else sorted((HERE/'records').glob('validation-*/migration-backup'))+sorted(HERE.glob('validation-*/migration-backup'))
    if args.restore:
        pending=[d for d in backups(client,roots) if json.loads((d/'transaction.json').read_text())['status']!='restored']
        if not pending:raise ValueError('No unrestored migration backup found')
        record_dir=pending[-1];record=json.loads((record_dir/'transaction.json').read_text())
        print('Restore',record_dir,'(renderer '+record['version']+')')
        for s in reversed(record['files']):print('  undo',s['path'])
        if not args.apply:print('Dry run: nothing written. Add --apply to restore.');return
        for line in undo(client,record['files'],record_dir):print('  '+line)
        record['status']='restored';m.atomic_json(record_dir/'transaction.json',record)
        print('Restored. WoWSilicon DXVK remains <game>/d3d9.dll.');return
    steps=plan(client,args.dll,roots)
    print('Client',client,'| renderer',version(args.dll.read_bytes()),'| backups',root)
    for key,line in info(client).items():print(f'  {key}: {line}')
    if not steps:print('Already migrated. Nothing to do.');return
    for s in steps:print(f"  {s['path']}: {(s['before'] or 'absent')[:12]} -> {s['after'][:12]}")
    print('  unchanged: d3d9.dll (WoWSilicon DXVK), wow.exe')
    for warning in launcher(client):print('  WARNING:',warning)
    if not args.apply:print('Dry run: nothing written. Add --apply to migrate.');return
    record_dir=apply(client,args.dll,root,roots)
    print('Migrated. Proxy preloaded from dlls.txt. Rollback record:',record_dir)

if __name__=='__main__':
    try:main()
    except Exception as e:
        print('ERROR:',e,file=sys.stderr);sys.exit(1)
