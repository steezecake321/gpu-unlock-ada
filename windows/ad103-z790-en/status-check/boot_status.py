"""Read this firmware boot's AD103 unlock handoff evidence; no GPU access."""
import argparse
import ctypes
from ctypes import wintypes as w
import datetime as dt
import json
from pathlib import Path
import struct

GUID = '{5f169bec-3cc0-49f4-a6c9-f4f39542b4a2}'
NAME = 'Ad103UnlockHandoff'


def decode(data, attributes):
    if attributes != 6 or len(data) != 32:
        raise ValueError('Wrong variable attributes or record size')
    magic, version, size, board, subsystem, state, checks, reserved = struct.unpack('<8I', data)
    if (magic, version, size, board, subsystem, reserved) != (0x31535547, 1, 32, 0x270510de, 0x513e1462, 0):
        raise ValueError('Record version or GPU fingerprint does not match')
    if state not in (1, 2, 3, 4) or checks != (7 if state == 4 else 0):
        raise ValueError('Invalid state/check combination')
    return {
        'handoff_state': {1: 'PENDING', 2: 'SKIPPED', 3: 'FAILED', 4: 'UNLOCK_VERIFIED_AT_EFI_HANDOFF'}[state],
        'live_gpu_state': 'UNKNOWN',
        'evidence': 'volatile UEFI runtime variable, final hardware checks passed' if state == 4 else 'no successful final handoff evidence',
        'attributes': attributes,
        'device_id': hex(board),
        'subsystem_id': hex(subsystem),
        'checks': checks,
    }


def read_record():
    adv = ctypes.WinDLL('advapi32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    class LUID(ctypes.Structure):
        _fields_ = [('LowPart', w.DWORD), ('HighPart', w.LONG)]
    class PRIVILEGE(ctypes.Structure):
        _fields_ = [('Count', w.DWORD), ('Luid', LUID), ('Attributes', w.DWORD)]
    kernel.GetCurrentProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    adv.OpenProcessToken.argtypes = [w.HANDLE, w.DWORD, ctypes.POINTER(w.HANDLE)]
    adv.LookupPrivilegeValueW.argtypes = [w.LPCWSTR, w.LPCWSTR, ctypes.POINTER(LUID)]
    adv.AdjustTokenPrivileges.argtypes = [w.HANDLE, w.BOOL, ctypes.POINTER(PRIVILEGE), w.DWORD, ctypes.c_void_p, ctypes.c_void_p]
    token = w.HANDLE()
    if not adv.OpenProcessToken(kernel.GetCurrentProcess(), 0x28, ctypes.byref(token)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        privilege = PRIVILEGE()
        privilege.Count = 1
        privilege.Attributes = 2
        if not adv.LookupPrivilegeValueW(None, 'SeSystemEnvironmentPrivilege', ctypes.byref(privilege.Luid)):
            raise ctypes.WinError(ctypes.get_last_error())
        ctypes.set_last_error(0)
        ok = adv.AdjustTokenPrivileges(token, False, ctypes.byref(privilege), 0, None, None)
        error = ctypes.get_last_error()
        if not ok or error:
            raise ctypes.WinError(error)
        read = kernel.GetFirmwareEnvironmentVariableExW
        read.argtypes = [w.LPCWSTR, w.LPCWSTR, ctypes.c_void_p, w.DWORD, ctypes.POINTER(w.DWORD)]
        read.restype = w.DWORD
        buffer = ctypes.create_string_buffer(256)
        attributes = w.DWORD()
        length = read(NAME, GUID, buffer, len(buffer), ctypes.byref(attributes))
        if not length:
            raise ctypes.WinError(ctypes.get_last_error())
        return decode(buffer.raw[:length], attributes.value)
    finally:
        kernel.CloseHandle(token)


def snapshot():
    result = {'observed_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
              'handoff_state': 'UNKNOWN', 'live_gpu_state': 'UNKNOWN'}
    try:
        # SystemTimeOfDayInformation's BootTime is an exact FILETIME value.
        # Unlike wall-clock minus uptime, it is stable across observations.
        nt = ctypes.WinDLL('ntdll')
        query = nt.NtQuerySystemInformation
        query.argtypes = [w.ULONG, ctypes.c_void_p, w.ULONG, ctypes.POINTER(w.ULONG)]
        query.restype = w.LONG
        buffer = ctypes.create_string_buffer(48)
        length = w.ULONG()
        if query(3, buffer, len(buffer), ctypes.byref(length)) != 0:
            raise OSError('Cannot obtain Windows boot identity')
        boot = struct.unpack_from('<Q', buffer.raw)[0]
        result['windows_boot_filetime'] = str(boot)
        result['windows_boot_utc'] = (dt.datetime(1601, 1, 1, tzinfo=dt.timezone.utc) + dt.timedelta(microseconds=boot // 10)).isoformat()
        result.update(read_record())
    except (OSError, ValueError) as error:
        result['reason'] = str(error)
        result['hint'] = 'Run as administrator. An absent variable means no evidence, not locked. The new EFI must run first.'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, help='Optional JSON snapshot; never used as current evidence')
    parser.add_argument('--pause', action='store_true')
    args = parser.parse_args()
    result = snapshot()
    print(json.dumps(result, indent=2))
    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    if args.pause:
        input('Press Enter to close...')
    return 0 if result['handoff_state'] == 'UNLOCK_VERIFIED_AT_EFI_HANDOFF' else 2


if __name__ == '__main__':
    raise SystemExit(main())
