#!/usr/bin/env python3
"""Step 4: read SS0 (BAR0+0x82381c) and SS1 (BAR0+0x823820), the tensor-unlock
selector registers, through the same validated EXEC_REG_OPS escape path as
step 2/3. Read-only. Must run elevated (same allowlist gate as PMC_BOOT_0)."""
import sys
import time
from pathlib import Path

import frida

HERE = Path(__file__).resolve().parent
SMI = r"C:\WINDOWS\system32\nvidia-smi.exe"
SCRIPT = HERE / "step2_regops.js"   # generic reader, reused as-is

SS0 = 0x82381c
SS1 = 0x823820


def on_message(message, data):
    if message.get("type") == "send":
        print("[msg]", message["payload"])
    elif message.get("type") == "error":
        print("[error]", message.get("description"), file=sys.stderr)
        print(message.get("stack", ""), file=sys.stderr)


def main():
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
        print("\n== SS0 (0x82381c) ==")
        try:
            print(api.read(SS0))
        except Exception as e:
            print("READ CALL RAISED:", e)

        print("\n== SS1 (0x823820) ==")
        try:
            print(api.read(SS1))
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
