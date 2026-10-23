"""Transactional installer for the WoW 3.3.5a HD client mod. Never launches WoW.

The renderer is installed as the game-folder d3d9.dll (a D3D9 proxy); wow.exe is
never read or written, so any wow.exe works. A foreign game-folder d3d9.dll
(DXVK, ReShade) is moved to renderer-backends/legacy/legacy_d3d9.dll: only the
proxy may carry the module name d3d9.dll. Restore returns every file exactly.
A transaction copies each file it replaces, so an interrupted run rolls back byte for byte. Once a run is installed,
consolidate() keeps only the copies of the files as they were before Northlight first changed them (the copies of
our own older versions are deleted); restore then puts the originals back."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import uuid
from datetime import datetime

PROXY = 'd3d9.dll'
LEGACY = 'renderer-backends/legacy/legacy_d3d9.dll'
DXVK = 'renderer-backends/dxvk/dxvk_d3d9.dll'
DXVK2 = 'renderer-backends/dxvk2/dxvk2_d3d9.dll'   # DXVK 2.7.1, the alternative backend
# Written by the 0.3.189-0.3.194 proxies; the current proxy ignores it. The installer removes it as a leftover;
# never packaged or recorded.
LEGACY_DXVK3_MARKER = 'renderer-backends/dxvk/northlight-dxvk3-init.pending'
DXVK_FOLDERS = {'dxvk': 'renderer-backends/dxvk/', 'dxvk2': 'renderer-backends/dxvk2/'}
CONFIG = 'northlight-renderer.ini'
GAME_CONFIG = 'WTF/Config.wtf'
MARKER = b'Northlight renderer '
PROXY_SIGNATURE = b'PROXY module=%ls root=%ls'   # the proxy's start-up log line; carries no product name
PACKAGE = Path(__file__).resolve().parent

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024*1024), b''): h.update(data)
    return h.hexdigest()

_sha_cache = {}

def package_sha(path):
    """sha256 of a package file, computed once per (path, size, mtime): the front end and payload_plan both
    verify every payload file, and DXVK builds are large."""
    st = path.stat()
    key = (str(path), st.st_size, st.st_mtime_ns)
    if key not in _sha_cache: _sha_cache[key] = sha(path)
    return _sha_cache[key]

def other_dxvk(path, selected):
    """The DXVK backend ('dxvk'/'dxvk2') that owns the payload path when it is not the selected backend, else None
    (the one predicate for 'this DXVK file may be absent'; 'native' and 'legacy' select neither)."""
    for name, folder in DXVK_FOLDERS.items():
        if path.startswith(folder) and name != selected: return name
    return None

def package_problem(path, selected, missing, label=None):
    """Refusal text for a package file that is missing or does not match the manifest. A missing/damaged file of
    the selected DXVK backend names the alternatives (the other DXVK backend, or native)."""
    label = label or path
    owner = next((n for n, folder in DXVK_FOLDERS.items() if path.startswith(folder)), None)
    if missing and owner:
        text = 'Package file '+label+' is missing; an antivirus product may have removed it. Allow the file and unzip the package again'
    else:
        text = 'Package file '+label+(' is missing' if missing else ' is damaged or modified')+'; download and unzip the package again'
    if owner:
        alternatives = ['--backend '+n for n in DXVK_FOLDERS if n != owner]+['--backend native']
        text += ', or install with '+' / '.join(alternatives)
    return text+'.'

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

def afters(e):
    """Every digest the transaction entry may have left at its path: its after, plus those of the entries merged into it."""
    return {e['after'], *e.get('afters', [])}

def is_ours(client, name=PROXY):
    """client/name is a renderer build, or holds exactly what one of our unrestored transactions wrote there
    (ownership by recorded digest: a build is recognised without matching any name inside it)."""
    path = safe_path(client, name)
    if not path.is_file(): return False
    data = path.read_bytes()
    return is_renderer(data) or hashlib.sha256(data).hexdigest() in \
        {a for _, r in transactions(client) for e in r['files'] if e['path'] == name for a in afters(e)}

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
            ok = {e['before']} | afters(e)
            # A user-editable settings file that was edited after installation is left as it is.
            if e.get('preserve') and current not in ok: continue
            if current not in ok:
                problems.append((backup, 'File changed after installation: '+str(safe_path(client, e['path'])))); continue
            if e['before']:
                copy = safe_path(backup/'before', e['path'])
                if not copy.is_file() or sha(copy) != e['before']: problems.append((backup, 'Backup damaged: '+e['path']))
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
        if current == e['before'] or (e.get('preserve') and current not in afters(e)): continue
        if e['before'] is None: target.unlink(missing_ok=True)
        else: replace_file(safe_path(backup/'before', e['path']), target)
    record['status'] = 'restored'
    atomic_json(backup/'transaction.json', record)
    print('Restored previous files:', backup)

def install_order(t):
    """Sort key of (folder, record): the second, then the exact creation time (same-second installs), then the folder name."""
    return (t[0].name[:15], t[1].get('created', ''), t[0].name)

def load_records(root, kind, client, restored=False):
    """Records of this kind and client under root/*/transaction.json, oldest first: [(folder, record)]. Unreadable
    JSON is skipped. restored=False leaves out the restored ones."""
    found = []
    for path in root.glob('*/transaction.json'):
        try:
            record = json.loads(path.read_text(encoding='utf-8'))
            if record.get('kind', 'package') == kind and Path(record.get('client', '')).resolve() == client.resolve() and \
                    (restored or record.get('status') != 'restored'):
                found.append((path.parent, record))
        except (OSError, ValueError, AttributeError, TypeError): continue
    return sorted(found, key=install_order)

def transactions(client, root=None):
    """Unrestored package transactions of this client, oldest first: [(backup folder, record)]."""
    return load_records(root or client/'renderer-backups', 'package', client)

def tree_size(path):
    return sum(f.stat().st_size for f in path.rglob('*') if f.is_file() and not f.is_symlink()) if path.is_dir() else 0

def consolidate_records(records):
    """Keep only the originals of the records [(folder, record)] (oldest first, every status, one kind, one client).
    Per path, an entry is a start when its before is not something an earlier live entry (or a restored record) of
    that path wrote; the rest are dropped, together with their copies. A start's after becomes the last after of its
    segment and its afters the union of the segment's, so every digest we wrote stays recognisable.
    Returns (bytes freed, removed folders, reason or None): with a reason nothing was written or deleted. Every
    intermediate state is a valid chain: records are rewritten oldest first, deletion comes last."""
    live = [(b, r) for b, r in records if r.get('status') != 'restored']
    dead = [b for b, r in records if r.get('status') == 'restored']
    def listed(e):
        return [*e.get('afters', []), e['after']]
    try:
        for _, r in live:
            for e in r['files']: e['path'], e['before'], e['after'], listed(e)
    except (KeyError, TypeError, AttributeError): return 0, [], 'a transaction record is malformed'
    known, starts, last = {}, {}, {}   # per path key: digests seen | start entries | the latest start
    for b, r in records:
        if r.get('status') == 'restored' and isinstance(r.get('files'), list):
            for e in r['files']:
                try: known.setdefault(e['path'].lower(), set()).update(listed(e))
                except (KeyError, TypeError, AttributeError): pass
    restored_afters = {k: set(v) for k, v in known.items()}
    keep = []   # (folder, record, entries to keep)
    for b, r in live:
        entries = []
        for e in r['files']:
            key = e['path'].lower()
            if key in last and e['before'] in known[key]:
                start = last[key]
                start['after'] = e['after']
                start['afters'] += [a for a in listed(e) if a not in start['afters']]
            else:
                start = dict(e); start['afters'] = list(dict.fromkeys(listed(e)))
                last[key] = start; entries.append(start); known.setdefault(key, set())
            known[key].update(listed(e))
        keep.append((b, r, entries))
    for key, start in last.items():
        start['afters'] += [a for a in sorted(restored_afters.get(key, ()), key=str) if a not in start['afters']]
    for _, _, entries in keep:
        for start in entries:
            if set(start['afters']) == {start['after']}: del start['afters']
    # Validate before any write: every kept original must be there and intact.
    used = set()
    for b, _, entries in keep:
        for start in entries:
            if start['before'] is None: continue
            try:
                copy = safe_path(b/'before', start['path'])
                if not copy.is_file() or sha(copy) != start['before']: return 0, [], 'the saved original of '+start['path']+' is missing or damaged'
            except (OSError, ValueError): return 0, [], 'the saved original of '+start['path']+' cannot be read'
            st = copy.stat(); used.add((st.st_dev, st.st_ino))   # by identity: a case-insensitive disk may spell the folders differently
    # Rewrite oldest first, emptied records too (files: []), so no half-merged record is left under a rewritten one.
    for b, r, entries in keep:
        if entries != r['files']: atomic_json(b/'transaction.json', {**r, 'files': entries})
    # Delete only after every rewrite.
    freed, removed = 0, []
    for b, _, entries in keep:
        if not entries: freed += tree_size(b); shutil.rmtree(b); removed.append(b)
    for b, _, entries in keep:
        if not entries: continue
        before = b/'before'
        for f in sorted(before.rglob('*'), reverse=True) if before.is_dir() else ():
            if f.is_file() or f.is_symlink():
                if f.is_symlink() or (f.stat().st_dev, f.stat().st_ino) not in used: freed += f.lstat().st_size; f.unlink()
            elif not any(f.iterdir()): f.rmdir()
        if before.is_dir() and not any(before.iterdir()): before.rmdir()
        freed += tree_size(b/'staged'); shutil.rmtree(b/'staged', ignore_errors=True)
    for b in dead:
        freed += tree_size(b); shutil.rmtree(b); removed.append(b)
    return freed, removed, None

def consolidate(client):
    """consolidate_records over this client's package transactions: (bytes freed, removed folders, reason or None)."""
    root = client/'renderer-backups'
    return consolidate_records(load_records(root, 'package', client, restored=True)) if root.is_dir() else (0, [], None)

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

QUALITY_KEY = re.compile(r'^\s*;?\s*([A-Za-z][A-Za-z0-9]*)\s*=')
QUALITY_ADDED = '; Added by the Northlight installer: settings this file did not mention yet. Commented out, so the preset\'s values apply.'
QUALITY_UPGRADE = re.compile(r'^\s*; Northlight Quality upgrade: ([a-z0-9-]+) ((?:[A-Za-z][A-Za-z0-9]*=1\s*)+)$', re.M)


def upgrade_quality_features(text, template):
    """Enable newly introduced Quality features once; later player edits stay intact."""
    preset, section = 'quality', ''
    for raw in text.splitlines():
        line = re.split('[;#]', raw, maxsplit=1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip().lower()
        elif section == 'quality' and '=' in line:
            key, value = (part.strip().lower() for part in line.split('=', 1))
            if key == 'preset' and value in ('quality', 'balanced', 'performance'):
                preset = value
    if preset != 'quality':
        return text
    lines, reset = [], set()
    for feature, settings in QUALITY_UPGRADE.findall(template):
        applied = '; Northlight Quality upgrade applied: ' + feature
        if applied in text.splitlines():
            continue
        lines.append(applied)
        reset.update(setting.split('=', 1)[0].lower() for setting in settings.split())
    if not lines:
        return text
    # Remove obsolete overrides rather than pinning new active ones: a later
    # change to Balanced/Performance must still use that preset's defaults.
    original, section = [], ''
    for raw in text.splitlines(keepends=True):
        line = re.split('[;#]', raw, maxsplit=1)[0].strip()
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip().lower()
        elif section == 'quality' and '=' in line and line.split('=', 1)[0].strip().lower() in reset:
            raw = '; Previous Quality override: ' + raw
        original.append(raw)
    eol = '\r\n' if '\r\n' in text else '\n'
    return ''.join(original).rstrip('\r\n') + eol + eol + eol.join(lines) + eol

def merge_quality_ini(user, template):
    """The user's northlight-quality.ini with every setting of the package template it does not mention yet appended:
    each with its comment block, as written in the template (commented out). A key counts as mentioned when any line
    sets it, active or commented. New Quality feature bundles are enabled once; other values remain intact.
    None when nothing is missing or upgraded, or the file is
    not UTF-8 text (left alone)."""
    try: text, tmpl = user.decode('utf-8-sig'), template.decode('utf-8-sig')
    except UnicodeDecodeError: return None
    if '\x00' in text: return None
    original = text
    text = upgrade_quality_features(text, tmpl)
    have = {m.group(1).lower() for m in map(QUALITY_KEY.match, text.splitlines()) if m}
    blocks, comments = [], []
    for line in tmpl.splitlines():
        m = QUALITY_KEY.match(line)
        if m and not line.lstrip().startswith('['):
            blocks.append((m.group(1).lower(), comments+[line])); comments = []
        elif not line.strip(): comments = []
        elif line.lstrip().startswith(';'): comments.append(line)
        else: comments = []
    missing, seen = [], set()
    for key, block in blocks:
        if key in have or key in seen: continue
        seen.add(key); missing.append(block)
    if not missing and text == original: return None
    eol = '\r\n' if '\r\n' in text else '\n'
    out = text if text.endswith(('\n', '\r')) or not text else text+eol
    if missing: out += eol+QUALITY_ADDED+eol
    for block in missing: out += eol+eol.join(block)+eol
    bom = user.startswith(b'\xef\xbb\xbf')
    return (b'\xef\xbb\xbf' if bom else b'')+out.encode('utf-8')

def resolve_backend(client, backend=None):
    """The one place that picks the backend. An explicit backend wins; else an installed Backend=dxvk2 or
    native is kept; else dxvk. An installed Backend=legacy resolves to dxvk here: switch_plan keeps the legacy
    selection by itself while the foreign d3d9.dll is still there (and writes dxvk when it is gone)."""
    if backend is not None: return backend
    installed = config_backend(safe_path(client, CONFIG))
    return installed if installed in ('dxvk2', 'native') else 'dxvk'

def config_bytes(backend):
    return ('[Renderer]\r\nBackend='+backend+'\r\n').encode('ascii')

def native_shadow_config(data):
    """Disable WoW's separate detailed shadows; retain all unrelated config bytes."""
    encoding = 'utf-16-le' if data.startswith(b'\xff\xfe') else 'utf-16-be' if data.startswith(b'\xfe\xff') else None
    bom = data[:2] if encoding else b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    body = data[len(bom):]
    if encoding:
        body = body.decode(encoding).encode('utf-8')
    pattern = rb'(?im)^([ \t]*SET[ \t]+extShadowQuality[ \t]+"?)[^"\r\n \t]+("?[^\r\n]*)'
    if re.search(pattern, body):
        body = re.sub(pattern, lambda m: m[1]+b'0'+m[2], body)
    else:
        eol = b'\r\n' if b'\r\n' in body else b'\n'
        if body and not body.endswith((b'\n', b'\r')): body += eol
        body += b'SET extShadowQuality "0"'+eol
    return bom+(body.decode('utf-8').encode(encoding) if encoding else body)

def payload_plan(client, package, use_existing=False, backend=None, extra=None, proxy=True):
    """(plan, staged, foreign): the entries that bring client to the package payload.
    backend is already resolved (resolve_backend; None only with proxy=False, which writes no ini): 'dxvk'/'dxvk2'/'native'
    writes Backend=<backend>; 'legacy' (like use_existing) selects a foreign d3d9.dll kept as the legacy backend.
    A DXVK file of an unselected DXVK backend that is MISSING from the package is skipped with a notice, and an older
    client copy of it is removed through the transaction (so the renderer never loads a copy this install did not
    put there); an installed ini whose Backend already is the resolved one is left untouched; a present file that does not match the manifest is always refused.
    extra {client path: bytes} adds staged files (the art layer). proxy=False (macOS, where
    migrate_mac_proxy owns d3d9.dll and northlight-renderer.ini) leaves the game-folder d3d9.dll alone."""
    if backend is None and proxy: raise ValueError('payload_plan needs a resolved backend (resolve_backend)')
    if backend == 'legacy': use_existing = True
    first, staged, config, foreign = switch_plan(client, use_existing) if proxy else ([], {}, None, False)
    effective = 'legacy' if config else backend if backend in ('dxvk', 'dxvk2', 'native') else None
    if config is None and backend in ('dxvk', 'dxvk2', 'native'): config = config_bytes(backend)
    manifest = json.loads((package/'payload-manifest.json').read_text(encoding='utf-8'))
    if proxy and PROXY not in [e['path'] for e in manifest]: raise ValueError('Package lacks the renderer '+PROXY)
    if foreign:
        print('Existing', PROXY, 'is not this renderer; it is kept as', LEGACY, flush=True)
        if not is_dxvk(safe_path(client, PROXY).read_bytes()):
            print('Note: its settings (e.g. ReShade.ini, reshade-shaders, enbseries.ini) stay in the game folder;'
                  ' a ReShade/ENB used as Backend=legacy may not find them there.', flush=True)
    plan, proxy_entry = list(first), []
    # The selected DXVK backend's files must verify. A file missing from another DXVK folder (an antivirus
    # product may have removed it) is skipped with a notice; 'native' and 'legacy' need neither.
    selected = backend or 'dxvk'
    # An installed ini that already says the effective backend is kept byte for byte (BackendPath= and other keys survive).
    keep_ini = proxy and effective is not None and config_backend(safe_path(client, CONFIG)) == effective
    if proxy and backend is not None and not keep_ini and CONFIG not in [e['path'] for e in manifest]:
        manifest = manifest+[{'path':CONFIG, 'sha256':None}]
    print('Verifying package and comparing installed files...', flush=True)
    for e in manifest:
        if e['path'] == CONFIG and keep_ini: continue
        dst = safe_path(client, e['path'])
        if e['sha256'] is not None:
            src = safe_path(package/'payload', e['path'])
            other = other_dxvk(e['path'], selected)
            if src.is_file():
                if package_sha(src) != e['sha256']: raise ValueError(package_problem(e['path'], selected, False))
            elif other is None: raise ValueError(package_problem(e['path'], selected, True))
            else:
                print('Note:', e['path'], 'is missing from the package (an antivirus product may have removed it);'
                      ' skipping it. Backend', other, 'will not be available.', flush=True)
                if dst.exists():
                    print('Removing the older', e['path'], 'from the game folder (it was not verified by this install).', flush=True)
                    plan.append({'path':e['path'], 'before':sha(dst), 'after':None})
                continue
        old = sha(dst) if dst.exists() else None
        # Existing settings keep ordinary overrides. New Quality feature bundles migrate once;
        # the complete result is backed up and installed through this same transaction.
        if e.get('preserve') and dst.exists():
            merged = merge_quality_ini(dst.read_bytes(), safe_path(package/'payload', e['path']).read_bytes()) if e['sha256'] is not None else None
            if merged is None:
                print('Keeping your existing', e['path'], flush=True); continue
            print('Updating', e['path'], '(new settings and Quality feature defaults; other overrides kept)', flush=True)
            staged[e['path']] = merged
            plan.append({'path':e['path'], 'before':old, 'after':hashlib.sha256(merged).hexdigest(), 'source':'staged', 'preserve':True}); continue
        entry = {'path':e['path'], 'before':old, 'after':e['sha256'], **({'preserve':True} if e.get('preserve') else {})}
        if e.get('preserve') and e['path'] == 'northlight-quality.ini' and not dst.exists():
            template = safe_path(package/'payload', e['path']).read_bytes()
            merged = merge_quality_ini(template, template)
            if merged is not None:
                staged[e['path']] = merged
                entry.update(after=hashlib.sha256(merged).hexdigest(), source='staged')
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
    if any(e['path'].casefold() == GAME_CONFIG.casefold() for e in manifest) or any(name.casefold() == GAME_CONFIG.casefold() for name in (extra or {})):
        raise ValueError('The game shadow setting is generated locally; the package must not supply '+GAME_CONFIG)
    dst = safe_path(client, GAME_CONFIG)
    original = dst.read_bytes() if dst.exists() else b''
    updated = native_shadow_config(original)
    if updated != original:
        staged[GAME_CONFIG] = updated
        plan.append({'path':GAME_CONFIG, 'before':sha(dst) if dst.exists() else None,
                     'after':hashlib.sha256(updated).hexdigest(), 'source':'staged', 'preserve':True})
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
    plan, staged, foreign = payload_plan(client, package, use_existing, resolve_backend(client, backend), extra)
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
            # Updates keep only the originals (consolidate), so there is no single step back: restore the whole chain.
            chain=list(reversed(transactions(client)))
            if not chain: raise ValueError('No unrestored backup found')
            problems=chain_problems(client,chain)
            if problems: raise ValueError(problems[0][1])
            for backup,_ in chain:
                print('Restore:',backup); restore(client,backup)
            return
        restore(client,backup)

if __name__=='__main__':
    try: main()
    except Exception as e:
        print('ERROR:',e,file=sys.stderr); sys.exit(1)
