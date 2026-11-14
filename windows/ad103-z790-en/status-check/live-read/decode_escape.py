#!/usr/bin/env python3
"""Decode the captured NVIDIA escape buffers: dump the small ones, and scan all
for known RM control command IDs. Pure analysis of local capture files."""
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
CAP = HERE / "escape_capture"
records = json.loads((CAP / "capture_index.json").read_text())

# Known 2080-class control cmd IDs worth spotting.
KNOWN = {
    0x20800122: "GPU_EXEC_REG_OPS",
    0x20800110: "GPU_GET_ENGINES",
    0x20800101: "GPU_GET_INFO(?)",
    0x208001a4: "GPU_GET_INFO_V2(?)",
    0x20800182: "GR_GET_INFO(?)",
    0x20800123: "GPU_MIGRATABLE_OPS",
    0x20800b0b: "PERF_... (?)",
    0x2080014b: "GPU_GET_GID_INFO(?)",
}


def hexdump(b, base=0, n=None):
    n = len(b) if n is None else min(n, len(b))
    out = []
    for i in range(0, n, 16):
        chunk = b[i:i + 16]
        hexs = " ".join(f"{x:02x}" for x in chunk)
        asci = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
        out.append(f"  {base+i:04x}  {hexs:<47}  {asci}")
    return "\n".join(out)


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0] if o + 4 <= len(b) else None


print("== header check across all captures ==")
magics = set()
for r in records:
    b = (CAP / r["bin"]).read_bytes()
    magics.add((u32(b, 0), u32(b, 4), u32(b, 12)))
for m in sorted(magics):
    print(f"  magic=0x{m[0]:08x} ver=0x{m[1]:08x} tag=0x{m[2]:08x}  "
          f"('{struct.pack('<I', m[0]).decode('latin1')}' "
          f"'{struct.pack('<I', m[2]).decode('latin1')}')")

print("\n== scan every buffer for known control IDs (whole 512-byte prefix) ==")
for r in records:
    if r["label"].startswith("win32u"):
        continue  # same payload as the gdi32 twin
    b = (CAP / r["bin"]).read_bytes()
    for o in range(0, len(b) - 3, 4):
        v = u32(b, o)
        if v in KNOWN:
            print(f"  esc_{r['index']:03d} size={r['privateSize']:<6} off=0x{o:03x}  "
                  f"0x{v:08x} {KNOWN[v]}")

print("\n== full dump: the four smallest distinct buffers ==")
smalls = sorted((r for r in records if not r["label"].startswith("win32u")),
                key=lambda r: r["privateSize"])[:4]
for r in smalls:
    b = (CAP / r["bin"]).read_bytes()
    print(f"\n-- esc_{r['index']:03d}  size={r['privateSize']}  hAdapter={r['hAdapter']} --")
    print(hexdump(b))
    # Interpret body as little-endian dwords after the 16-byte header.
    print("  dwords after header:",
          " ".join(f"{u32(b, o):08x}" for o in range(16, min(len(b), 64), 4)))
