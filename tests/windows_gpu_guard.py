"""Toolhelp game-process guard for standalone Windows GPU oracles."""
import ctypes
from ctypes import wintypes

class ProcessEntry(ctypes.Structure):
    _fields_ = [('size', wintypes.DWORD), ('usage', wintypes.DWORD), ('pid', wintypes.DWORD),
                ('heap', ctypes.c_size_t), ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                ('parent', wintypes.DWORD), ('priority', wintypes.LONG), ('flags', wintypes.DWORD),
                ('name', wintypes.WCHAR * 260)]


def kernel():
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    for name in ('Process32FirstW', 'Process32NextW'):
        getattr(api, name).argtypes = [wintypes.HANDLE, ctypes.POINTER(ProcessEntry)]
        getattr(api, name).restype = wintypes.BOOL
    return api


def processes():
    api = kernel(); handle = api.CreateToolhelp32Snapshot(2, 0)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    result = []
    try:
        entry = ProcessEntry(); entry.size = ctypes.sizeof(entry)
        more = api.Process32FirstW(handle, ctypes.byref(entry))
        if not more:
            raise ctypes.WinError(ctypes.get_last_error())
        while more:
            result.append({'pid': entry.pid, 'name': entry.name})
            more = api.Process32NextW(handle, ctypes.byref(entry))
    finally:
        api.CloseHandle(handle)
    if any(p['name'].lower().startswith('wow') and p['name'].lower().endswith('.exe') for p in result):
        raise RuntimeError('Game process exists; defer this stand without changing the game')
    return result
