#!/usr/bin/env python3
"""Parse every captured escape against the recovered NVIDIA-wrapper layout and
tabulate hClient / hObject / RM command id. Confirms field offsets are stable."""
import json
import struct
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAP = HERE / "escape_capture"
records = json.loads((CAP / "capture_index.json").read_text())


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0] if o + 4 <= len(b) else None


MAGIC, VER, TAG = 0x4e564441, 0x00010002, 0x4e562a2a
clients, cmds = Counter(), Counter()
print(f"{'idx':>4} {'size':>6} {'subcmd':>10} {'hClient':>10} {'hObject':>10} {'cmd':>10} {'p@0x3c':>8}")
for r in records:
    if r["label"].startswith("win32u"):
        continue
    b = (CAP / r["bin"]).read_bytes()
    if (u32(b, 0), u32(b, 4), u32(b, 12)) != (MAGIC, VER, TAG):
        print(f"{r['index']:>4}  HEADER MISMATCH"); continue
    sub = u32(b, 0x10)
    hc, ho, cmd, p = u32(b, 0x30), u32(b, 0x34), u32(b, 0x38), u32(b, 0x3c)
    is_rmctrl = (sub == 0x0500002b)
    if is_rmctrl:
        clients[hc] += 1
        cmds[cmd] += 1
    tag = "" if is_rmctrl else "   (non-rmctrl sub)"
    print(f"{r['index']:>4} {r['privateSize']:>6} 0x{sub:08x} "
          f"0x{hc:08x} 0x{ho:08x} 0x{cmd:08x} {p:>8}{tag}")

print("\n== distinct hClient values (RmControl subcmd only) ==")
for c, n in clients.most_common():
    print(f"  0x{c:08x}  x{n}")
print("\n== distinct RM command IDs seen (class from high 16 bits) ==")
for c, n in sorted(cmds.items()):
    cls = c >> 16
    print(f"  0x{c:08x}  class=0x{cls:04x}  x{n}")
print("\nEXEC_REG_OPS (0x20800122) present:", 0x20800122 in cmds)
