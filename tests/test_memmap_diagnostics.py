#!/usr/bin/env python3
# northlight-test: requires=cxx
"""MEMMAP address-space snapshots (0.3.192): host test of the pure grouping/diff/format logic of
address_space_snapshot.h with fake region lists (-O2 and ASan/UBSan), and a source pin of the five
lifecycle call sites in renderer.cpp, the allowed-API set and the line formats. No game, Wine or DLL is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='memmap-') as folder:
    for mode,flags in [('O2',['-O2']),('san',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(folder)/('t-'+mode)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_memmap_diagnostics.cpp'),'-o',str(exe)],check=True)
        r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);print(r.stdout,r.stderr,flush=True);r.check_returncode()

r=fp.src('renderer.cpp').read_text();h=fp.src('address_space_snapshot.h').read_text()
def body(s,start,end):a=s.index(start);return s[a:s.index(end,a)]
dtor=body(r,'    ~Device() {','    HRESULT STDMETHODCALLTYPE QueryInterface')
ctor=body(r,'        diagnosticId=InterlockedIncrement(&deviceSerial);','    ~Device() {')
fin=body(r,'        ++frame;mirrorState.gate.frame.store','        // Per-type call counts')
checks={
 'destroy-begin before teardown (first statement after its DEVICE line)':dtor.index('memmap("destroy-begin");')<dtor.index('memoryDiagnostics.reset();') and dtor.index('DEVICE lifetime event=destroy-begin')<dtor.index('memmap("destroy-begin");'),
 'destroy-end after the backend Release':dtor.index('real->Release()')<dtor.index('DEVICE lifetime event=destroy-end')<dtor.index('memmap("destroy-end");'),
 'create right after the create line, after the device id is set':ctor.index('DEVICE lifetime event=create')<ctor.index('memmap("create");'),
 'frame 300 of every device, device 1 labelled baseline':'if(frame==300)memmap(diagnosticId==1?"baseline-frame300":"frame300");' in fin,
 'exactly four call sites (+ the definition)':r.count('memmap("')==3 and r.count('memmap(diagnosticId==1')==1 and r.count('void memmap(const char* point)')==1,
 'ungated by Diagnostics':'diagnostics()' not in dtor.split('memmap("destroy-end")')[0].split('memmap("destroy-begin")')[0][-200:] and 'NorthlightMemMap::snapshot(' in r and not re.search(r'diagnostics\(\)[^;{]*memmap\(',r),
 'never throws out of the device (try/catch around the snapshot, which also catches)':'void memmap(const char* point){\n        try{' in r and '}catch(...){}\n    }' in r and 'catch(...){}\n    busy().clear' in h,
 'one snapshot at a time, no re-entry':'test_and_set' in h and 'busy().clear' in h,
 'storage reserved once, no growth past capacity':'allocs_.reserve(MaxAllocs)' in h and 'allocs_.size()>=allocs_.capacity()' in h,
 'only kernel32/toolhelp APIs; optional ones via GetProcAddress':all(x in h for x in ('VirtualQuery(','GetProcessHeaps(','CreateToolhelp32Snapshot(','GetModuleFileNameA(','GetSystemInfo('))
   and 'GetProcAddress(k,"HeapSummary")' in h and 'GetProcAddress(k,"K32GetProcessMemoryInfo")' in h and 'HeapWalk' not in h.replace('no HeapWalk','') and not re.search(r'\b(?:EnumProcessModules|GetProcessMemoryInfo|NtQuery\w*|HeapSummary)\s*\(',h.replace('"HeapSummary"','')),
 'heap summary capped at 8 ms':'ms(t1,t2,freq)>8.0' in h,
 'whole user space walked (min..max application address)':'lpMinimumApplicationAddress' in h and 'lpMaximumApplicationAddress' in h,
 'tally from existing counters only (lock meter gauges, stream flags, mirror registry)':all(x in r for x in ('copyResidentBytes','copyLargeBytes','mirrorResources.size()','streamActive','memoryPressure')),
}
for tag in ['summary','top','diff-new','diff-gone','diff-changed','diff','process','heaps','heap','threads','northlight','done']:
    checks['line format MEMMAP '+tag]=('MEMMAP '+tag+' ' in h) or (tag in ('diff-new','diff-gone','diff-changed') and '"'+tag+'"' in h)
for key in ['commitMiB=','image=','mapped=','private=','freeMiB=','largestFreeMiB=','regions=','allocations=','walkMs=','base=0x','sizeMiB=','commitMiB=','type=%s','prot=0x','module=%s','wasMiB=','threads=%u modules=%u']:
    checks['field '+key]=key in h
checks['sink prints through logf (one MEMMAP line per call, label = device/point/frame/tick)']='logf("%s",line)' in r and 'device=%ld point=%s frame=%u tick=%lu' in r
for k,v in checks.items():print(('PASS ' if v else 'FAIL ')+k)
assert all(checks.values())
