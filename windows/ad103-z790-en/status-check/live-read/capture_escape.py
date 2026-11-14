#!/usr/bin/env python3
"""Spawn nvidia-smi under Frida, capture its D3DKMTEscape private buffers, save them.
Observe-only: hooks the caller's own escape calls and dumps bytes. Nothing is written
to the driver or GPU."""
import json
import sys
import time
from pathlib import Path

import frida

HERE = Path(__file__).resolve().parent
OUT = HERE / "escape_capture"
OUT.mkdir(exist_ok=True)
SMI = r"C:\WINDOWS\system32\nvidia-smi.exe"
ARGS = sys.argv[1:] or ["-q"]

records = []


def on_message(message, data):
    if message.get("type") == "send":
        payload = message["payload"]
        if "note" in payload:
            print("[script]", payload)
            return
        idx = len(records)
        payload["index"] = idx
        if data:
            (OUT / f"esc_{idx:03d}.bin").write_bytes(data)
            payload["bin"] = f"esc_{idx:03d}.bin"
        records.append(payload)
        print(f"[{idx:03d}] {payload['label']} type={payload['escapeType']} "
              f"size={payload['privateSize']} first16={payload['first16']}")
    elif message.get("type") == "error":
        print("[error]", message.get("description"), file=sys.stderr)


def main():
    pid = frida.spawn([SMI, *ARGS])
    session = frida.attach(pid)
    script = session.create_script((HERE / "escape_hook.js").read_text(encoding="utf-8"))
    script.on("message", on_message)
    script.load()
    frida.resume(pid)
    # nvidia-smi -q runs a second or two; give it room, then stop.
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            if not _alive(pid):
                break
        except Exception:
            break
        time.sleep(0.3)
    time.sleep(0.5)
    try:
        session.detach()
    except Exception:
        pass
    (OUT / "capture_index.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    print(f"\n{len(records)} unique escape signatures captured -> {OUT}")


def _alive(pid):
    for p in frida.enumerate_processes():
        if p.pid == pid:
            return True
    return False


if __name__ == "__main__":
    main()
