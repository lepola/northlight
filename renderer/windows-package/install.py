"""Transactional installer for the WoW 3.3.5a HD client mod. Never launches WoW.

The renderer is installed as the game-folder d3d9.dll (a D3D9 proxy); wow.exe is
never read or written, so any wow.exe works. A foreign game-folder d3d9.dll
(DXVK, ReShade) is moved to renderer-backends/legacy/legacy_d3d9.dll: only the
proxy may carry the module name d3d9.dll. Restore returns every file exactly."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import uuid
from datetime import datetime

PROXY = 'd3d9.dll'
LEGACY = 'renderer-backends/legacy/legacy_d3d9.dll'
DXVK = 'renderer-backends/dxvk/dxvk_d3d9.dll'
CONFIG = 'northlight-renderer.ini'
MARKER = b'Northlight renderer '
PROXY_SIGNATURE = b'PROXY module=%ls root=%ls'   # the proxy's start-up log line; carries no product name
PACKAGE = Path(__file__).resolve().parent

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024*1024), b''): h.update(data)
    return h.hexdigest()

def safe_path(root, name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or any(x in ('..', '.') for x in name.split('/')) or '\\' in name or ':' in name:
        raise ValueError('Unsafe package path: '+name)
    target = root.joinpath(*p.parts)
    for parent in [target, *target.parents]:
        if parent == root.parent: break
        if parent.is_symlink() or (hasattr(parent, 'is_junction') and parent.is_junction()):
            raise ValueError('Links are not supported: '+str(parent))
    if not target.resolve().is_relative_to(root.resolve()): raise ValueError('Path escapes client')
    return target

def atomic_json(path, value):
    temp = path.with_suffix('.new')
    temp.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')
    os.replace(temp, path)

def replace_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    temp = target.with_name(target.name+'.frd9-'+uuid.uuid4().hex+'.tmp')
    try:
        shutil.copy2(source, temp)
        os.replace(temp, target)
    finally:
        if temp.exists(): temp.unlink()

def is_dxvk(data):
    """DXVK's log banner or its version resource (ProductName DXVK)."""
    if b'DXVK: \0' in data: return True
    key = 'ProductName\0'.encode('utf-16-le'); at = data.find(key)
    while at >= 0:
        if 'DXVK\0'.encode('utf-16-le') in data[at+len(key):at+len(key)+16]: return True
        at = data.find(key, at+1)
    return False

def is_renderer(data):
    """Any renderer build carries its log banner; every proxy build also its PROXY start-up line."""
    return MARKER in data or PROXY_SIGNATURE in data

def is_ours(client, name=PROXY):
    """client/name is a renderer build, or holds exactly what one of our unrestored transactions wrote there
    (ownership by recorded digest: a build is recognised without matching any name inside it)."""
    path = safe_path(client, name)
    if not path.is_file(): return False
    data = path.read_bytes()
    return is_renderer(data) or hashlib.sha256(data).hexdigest() in \
        {e['after'] for _, r in transactions(client) for e in r['files'] if e['path'] == name}

def config_backend(path):
    if not path.is_file(): return None
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        key, _, value = line.partition('=')
        if key.strip().lower() == 'backend': return value.strip().lower()
    return None

def chain_problems(client, chain):
    """Why restoring chain [(backup, record)], newest first, would refuse, checked without writing
    anything: [(backup, message)] ([] = the whole chain restorable). Each record is checked against the
    files as the restore of the newer records leaves them."""
    state, problems = {}, []   # state: {path: sha | None} after the simulated restores
    def now(path):
        if path not in state:
            target = safe_path(client, path)
            state[path] = sha(target) if target.exists() else None
        return state[path]
    for backup, record in chain:
        # Other kinds (macOS mac-proxy-migration) are skipped: their files (migrate_mac_proxy.MANAGED) are disjoint
        # from the package records on macOS (payload_plan proxy=False, backend None) and their undo is
        # semantic (keeps later edits), so it never refuses.
        if record.get('kind', 'package') != 'package': continue
        if Path(record['client']).resolve() != client.resolve():
            problems.append((backup, 'Backup belongs to another client')); continue
        for e in record['files']:
            current = now(e['path'])
            # A user-editable settings file that was edited after installation is left as it is.
            if e.get('preserve') and current not in (e['before'], e['after']): continue
            if current not in (e['before'], e['after']):
                problems.append((backup, 'File changed after installation: '+str(safe_path(client, e['path'])))); continue
            if e['before'] and sha(safe_path(backup/'before', e['path'])) != e['before']:
                problems.append((backup, 'Backup damaged: '+e['path']))
            state[e['path']] = e['before']
    return problems

def restore_problems(client, backup):
    """Why restore(client, backup) would refuse, checked without writing anything ([] = restorable)."""
    record = json.loads((backup/'transaction.json').read_text(encoding='utf-8'))
    if Path(record['client']).resolve() != client.resolve(): return ['Backup belongs to another client']
    if record.get('kind', 'package') != 'package': return ['This backup was made by '+record['kind']+'; restore it with that tool']
    return [m for _, m in chain_problems(client, [(backup, record)])]

def restore(client, backup):
    record = json.loads((backup/'transaction.json').read_text(encoding='utf-8'))
    # Validate the complete rollback BEFORE changing anything. Never destroy later edits.
    problems = restore_problems(client, backup)
    if problems: raise ValueError(problems[0])
    for e in reversed(record['files']):
        target = safe_path(client, e['path'])
        current = sha(target) if target.exists() else None
        if current == e['before'] or (e.get('preserve') and current != e['after']): continue
        if e['before'] is None: target.unlink(missing_ok=True)
        else: replace_file(safe_path(backup/'before', e['path']), target)
    record['status'] = 'restored'
    atomic_json(backup/'transaction.json', record)
    print('Restored previous files:', backup)

def transactions(client, root=None):
    """Unrestored package transactions of this client, oldest first: [(backup folder, record)]."""
    found = []
    for path in (root or client/'renderer-backups').glob('*/transaction.json'):
        try: record = json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError): continue
        if record.get('kind', 'package') == 'package' and record.get('status') != 'restored' and \
                Path(record.get('client', '')).resolve() == client.resolve():
            found.append((path.parent, record))
    # Install order: second, then the exact creation time (same-second installs), then the folder name.
    return sorted(found, key=lambda t: (t[0].name[:15], t[1].get('created', ''), t[0].name))

def legacy_cache_transactions(client):
    """Unrestored transactions of the old full packages (0.3.98-0.3.144), which recorded every world-cache file."""
    return [(b, r) for b, r in transactions(client) if any(e['path'].startswith('world-cache/') for e in r['files'])]

def switch_plan(client, use_existing=False):
    """Entries that make the game-folder d3d9.dll this renderer (wow.exe is not
    involved). Returns (first, staged, config, foreign)."""
    first, staged = [], {}
    # A foreign d3d9.dll is kept as the legacy backend; Backend=legacy selects it.
    proxy, legacy = safe_path(client, PROXY), safe_path(client, LEGACY)
    foreign = proxy.is_file() and not is_ours(client)
    if foreign:
        moved = sha(proxy)
        if not legacy.exists() or sha(legacy) != moved:
            first.append({'path':LEGACY, 'before':sha(legacy) if legacy.exists() else None, 'after':moved, 'source':'backup:'+PROXY})
    select_legacy = (foreign or legacy.is_file()) and (use_existing or config_backend(safe_path(client, CONFIG)) == 'legacy')
    config = b'[Renderer]\r\nBackend=legacy\r\n' if select_legacy else None
    return first, staged, config, foreign

def commit(client, plan, staged, payload=None, version='__RELEASE_VERSION__', kind='package'):
    """Back up every replaced file, record the transaction, apply and verify it; any
    failure restores the recorded transaction. Sources: staged bytes, a backed-up
    client file ('backup:<path>') or the package payload."""
    def size(e):
        source = e.get('source') or ''
        if e['after'] is None: return 0
        if source == 'staged': return len(staged[e['path']])
        if source.startswith('backup:'): return safe_path(client, source[7:]).stat().st_size
        return safe_path(payload, e['path']).stat().st_size
    needed = sum(safe_path(client,e['path']).stat().st_size if e['before'] else 0 for e in plan)+sum(size(e) for e in plan)
    if shutil.disk_usage(client).free < needed+128*1024*1024: raise ValueError('Insufficient disk space for payload and rollback')
    backups = safe_path(client, 'renderer-backups')
    backups.mkdir(exist_ok=True)
    now = datetime.now()
    backup = backups/(now.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
    backup.mkdir()
    # Save every original before modifying the first destination.
    for e in plan:
        if e['before']:
            target = safe_path(backup/'before', e['path']); target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(safe_path(client,e['path']), target)
            if sha(target) != e['before']: raise ValueError('Backup verification failed: '+e['path'])
    record = {'version':version,'kind':kind,'created':now.strftime('%Y%m%d-%H%M%S.%f'),'client':str(client.resolve()),'status':'pending','files':plan}
    atomic_json(backup/'transaction.json', record)
    stage = backup/'staged'
    for name, data in staged.items():
        target = safe_path(stage, name); target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
    def source(e):
        kind = e.get('source')
        if kind == 'staged': return safe_path(stage, e['path'])
        if kind: return safe_path(backup/'before', kind.split(':', 1)[1])
        return safe_path(payload, e['path'])
    try:
        for i,e in enumerate(plan):
            dst = safe_path(client,e['path'])
            # Catch edits since the comparison; the game must remain closed.
            if (sha(dst) if dst.exists() else None) != e['before']: raise ValueError('Destination changed: '+str(dst))
            if e['after'] is None: dst.unlink()
            else:
                replace_file(source(e),dst)
                if sha(dst) != e['after']: raise ValueError('Installed checksum mismatch: '+e['path'])
            if i==0: print('Installing',len(plan),'files (each replaced file is backed up first)...',flush=True)
            elif i%1000==0: print('Installed',i,'of',len(plan),'files',flush=True)
        record['status']='installed'; atomic_json(backup/'transaction.json',record)
    except BaseException:
        print('Installation interrupted. Restoring the recorded transaction...',flush=True)
        restore(client,backup)
        raise
    finally:
        shutil.rmtree(stage, ignore_errors=True)
    return backup

def config_bytes(backend):
    return ('[Renderer]\r\nBackend='+backend+'\r\n').encode('ascii')

def payload_plan(client, package, use_existing=False, backend=None, extra=None, proxy=True):
    """(plan, staged, foreign): the entries that bring client to the package payload.
    backend None keeps the payload's northlight-renderer.ini; 'dxvk'/'native' writes Backend=<backend>;
    'legacy' (like use_existing) selects a foreign d3d9.dll kept as the legacy backend.
    extra {client path: bytes} adds staged files (the art layer). proxy=False (macOS, where
    migrate_mac_proxy owns d3d9.dll and northlight-renderer.ini) leaves the game-folder d3d9.dll alone."""
    if backend == 'legacy': use_existing = True
    first, staged, config, foreign = switch_plan(client, use_existing) if proxy else ([], {}, None, False)
    if config is None and backend in ('dxvk', 'native'): config = config_bytes(backend)
    manifest = json.loads((package/'payload-manifest.json').read_text(encoding='utf-8'))
    if proxy and PROXY not in [e['path'] for e in manifest]: raise ValueError('Package lacks the renderer '+PROXY)
    if foreign:
        print('Existing', PROXY, 'is not this renderer; it is kept as', LEGACY, flush=True)
        if not is_dxvk(safe_path(client, PROXY).read_bytes()):
            print('Note: its settings (e.g. ReShade.ini, reshade-shaders, enbseries.ini) stay in the game folder;'
                  ' a ReShade/ENB used as Backend=legacy may not find them there.', flush=True)
    plan, proxy_entry = list(first), []
    if proxy and backend is not None and CONFIG not in [e['path'] for e in manifest]:
        manifest = manifest+[{'path':CONFIG, 'sha256':None}]
    print('Verifying package and comparing installed files...', flush=True)
    for e in manifest:
        dst = safe_path(client, e['path'])
        if e['sha256'] is not None:
            src = safe_path(package/'payload', e['path'])
            if not src.is_file() or sha(src) != e['sha256']: raise ValueError('Package checksum mismatch: '+e['path'])
        old = sha(dst) if dst.exists() else None
        # User settings (northlight-quality.ini) are installed only when missing, never overwritten.
        if e.get('preserve') and dst.exists():
            print('Keeping your existing', e['path'], flush=True); continue
        entry = {'path':e['path'], 'before':old, 'after':e['sha256'], **({'preserve':True} if e.get('preserve') else {})}
        if e['path'] == CONFIG and config:
            staged[CONFIG] = config; entry.update(after=hashlib.sha256(config).hexdigest(), source='staged')
        if entry['after'] is None: continue
        # The proxy is written last: every earlier crash point still starts a working game.
        if old != entry['after']: (proxy_entry if e['path'] == PROXY else plan).append(entry)
    for name, data in (extra or {}).items():
        dst = safe_path(client, name)
        old, new = (sha(dst) if dst.exists() else None), hashlib.sha256(data).hexdigest()
        if old != new:
            staged[name] = data; plan.append({'path':name, 'before':old, 'after':new, 'source':'staged'})
    return plan+proxy_entry, staged, foreign

def install(client, package=PACKAGE, use_existing=False, backend=None, extra=None, version='__RELEASE_VERSION__'):
    if client == package or client in package.parents:
        raise ValueError('Extract the package OUTSIDE the game folder before installing.')
    requirements=package/'required-world-cache.json'
    if requirements.exists():
        print('Checking existing world-cache for this update...',flush=True)
        for e in json.loads(requirements.read_text(encoding='utf-8')):
            src=safe_path(client,e['path'])
            if not src.is_file() or sha(src)!=e['sha256']:
                raise ValueError('This small update needs the matching world-cache. Use the FULL package: '+e['path'])
    plan, staged, foreign = payload_plan(client, package, use_existing, backend, extra)
    if not plan:
        print('This package is already installed.'); return None
    backup = commit(client, plan, staged, package/'payload', version)
    print('Installed', version+'. Rollback:', backup)
    print('wow.exe is not modified. The renderer is loaded as', PROXY, 'from the game folder.')
    if foreign: print('Your previous', PROXY, 'is in', LEGACY, '(Backend=legacy uses it).')
    print('Start wow.exe yourself. Check northlight-renderer.log for selected backend and capabilities.')
    return backup

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('action',choices=['install','restore'])
    ap.add_argument('--client',type=Path)
    ap.add_argument('--backup',type=Path)
    ap.add_argument('--use-existing-d3d9',action='store_true',help='select the previous game-folder d3d9.dll (moved to '+LEGACY+') as backend')
    args=ap.parse_args()
    if os.name!='nt': raise ValueError('This installer is for Windows only')
    # Read-only process check. Never starts a game or modifies system DLLs/settings.
    running=subprocess.check_output(['tasklist.exe','/FI','IMAGENAME eq wow.exe','/FO','CSV','/NH'],text=True)
    if 'wow.exe' in running.lower(): raise ValueError('Close WoW before installing or restoring')
    raw=str(args.client) if args.client else input('Game folder (containing wow.exe): ').strip().strip('"')
    client=Path(raw).resolve(strict=True)
    if not (client/'wow.exe').is_file(): raise ValueError('wow.exe missing')
    if args.action=='install': install(client,use_existing=args.use_existing_d3d9)
    else:
        if args.backup: backup=args.backup.resolve(strict=True)
        else:
            choices=sorted((client/'renderer-backups').glob('*/transaction.json'))
            choices=[p.parent for p in choices if json.loads(p.read_text())['status']!='restored']
            if not choices: raise ValueError('No unrestored backup found')
            backup=choices[-1]
            print('Restore:',backup)
        restore(client,backup)

if __name__=='__main__':
    try: main()
    except Exception as e:
        print('ERROR:',e,file=sys.stderr); sys.exit(1)
