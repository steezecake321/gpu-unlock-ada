"""Live BAR0 SS0/SS1 tensor-unlock selector read, for the Rainmeter publisher.

Reuses the validated live-read/step2_regops.js reader (D3DKMTEscape ->
NV2080_CTRL_CMD_GPU_EXEC_REG_OPS through the loaded driver). Read-only, no
register writes, no new kernel driver. See LIVE_READ_FEASIBILITY.md for the
validation ladder this was confirmed against.

Requires an elevated caller: SS0/SS1 are behind gpuValidateRegOffset_IMPL's
admin-only allowlist, same gate as Nsight's "GPU performance counters: admin
only" setting. A non-elevated call returns regStatus=0x1f and is reported as
UNKNOWN here, not LOCKED -- an unreadable register is never evidence either way.
"""
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / 'live-read' / 'step2_regops.js'
SMI = r'C:\WINDOWS\system32\nvidia-smi.exe'

SS0, SS1 = 0x82381c, 0x823820
# Confirmed live on this board/driver (LIVE_READ_FEASIBILITY.md, 2026-11-14).
UNLOCKED = (0x88888888, 0x8)
# Stock Ada selector pattern from the RTX 4090 BAR0 diff
# (docs/evidence/rtx4090-610.57.04/comparison.json); not yet re-confirmed via
# this live-read path on this exact board, so treated as a known reference,
# not an empirically closed loop the way UNLOCKED is.
LOCKED = (0x47131055, 0x1)


def _read_offset(api, offset):
    result = api.read(offset)
    if result.get('wrapperStatus') != '0x0' or result.get('regStatus') != '0x0':
        return None
    return int(result['regValueLo'], 16)


def read_live_state(harvest_timeout=8.0):
    """Returns 'LOCKED', 'UNLOCKED', or 'UNKNOWN'. Never raises."""
    try:
        import frida
    except ImportError:
        return 'UNKNOWN'

    pid = None
    session = None
    try:
        pid = frida.spawn([SMI, '-l', '1'])
        session = frida.attach(pid)
        script = session.create_script(SCRIPT.read_text(encoding='utf-8'))
        script.on('message', lambda *_: None)
        script.load()
        frida.resume(pid)

        api = script.exports_sync
        deadline = time.time() + harvest_timeout
        while time.time() < deadline and not api.ready():
            time.sleep(0.1)
        if not api.ready():
            return 'UNKNOWN'

        v0 = _read_offset(api, SS0)
        v1 = _read_offset(api, SS1)
        if v0 is None or v1 is None:
            return 'UNKNOWN'
        if (v0, v1) == UNLOCKED:
            return 'UNLOCKED'
        if (v0, v1) == LOCKED:
            return 'LOCKED'
        return 'UNKNOWN'
    except Exception:
        return 'UNKNOWN'
    finally:
        if session is not None:
            try:
                session.detach()
            except Exception:
                pass
        if pid is not None:
            try:
                frida.kill(pid)
            except Exception:
                pass


if __name__ == '__main__':
    print(read_live_state())
