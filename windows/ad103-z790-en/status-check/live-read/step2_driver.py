#!/usr/bin/env python3
"""Step 2: spawn a long-lived nvidia-smi, harvest its live RM handles via the
D3DKMTEscape hook, then issue ONE EXEC_REG_OPS READ_32 of PMC_BOOT_0
(BAR0+0x0, the chip-ID register) through the same escape path. Read-only.
No register is written; no new driver is loaded."""
import sys
import time
from pathlib import Path

import frida

HERE = Path(__file__).resolve().parent
SMI = r"C:\WINDOWS\system32\nvidia-smi.exe"
SCRIPT = HERE / "step2_regops.js"


def on_message(message, data):
    if message.get("type") == "send":
        print("[msg]", message["payload"])
    elif message.get("type") == "error":
        print("[error]", message.get("description"), file=sys.stderr)
        print(message.get("stack", ""), file=sys.stderr)


def main():
    # -l 1: loop query once a second so there's a steady stream of escapes
    # to harvest handles from, and so the process stays alive while we work.
    pid = frida.spawn([SMI, "-l", "1"])
    session = frida.attach(pid)
    script = session.create_script(SCRIPT.read_text(encoding="utf-8"))
    script.on("message", on_message)
    script.load()
    frida.resume(pid)

    api = script.exports_sync
    deadline = time.time() + 10
    harvested = False
    while time.time() < deadline:
        if api.ready():
            harvested = True
            break
        time.sleep(0.2)

    if not harvested:
        print("FAILED: handles not harvested within 10s")
    else:
        print("\n== issuing EXEC_REG_OPS READ_32 of PMC_BOOT_0 (offset 0x0) ==")
        try:
            result = api.read(0x0)
            print(result)
        except Exception as e:
            print("READ CALL RAISED:", e)

    try:
        session.detach()
    except Exception:
        pass
    try:
        frida.kill(pid)
    except Exception:
        pass


if __name__ == "__main__":
    main()
