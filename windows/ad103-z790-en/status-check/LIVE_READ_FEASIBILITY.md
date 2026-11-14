# Live SS0/SS1 read on Windows through the loaded NVIDIA driver (option 3)

Date: 2026-11-14. Status: **plausible, not yet proven**. Nothing below has
sent anything to the driver; all of it is source reading and read-only
string scans.

Goal: read BAR0+0x82381c (SS0, unlock = 0x88888888) and BAR0+0x823820
(SS1, unlock = 8) after Windows/driver load. Do this with no new kernel
driver, no test signing and no Secure Boot change.

## What is established

1. **An RM control can read any BAR0 offset.** `NV2080_CTRL_CMD_GPU_EXEC_REG_OPS`
   (0x20800122, op `NV2080_CTRL_GPU_REG_OP_READ_32`). Source:
   `fw-cache/assets.src/NVIDIA-kernel-module-source-610.43.03/src/nvidia/src/kernel/gpu/subdevice/subdevice_ctrl_gpu_regops.c`.
   - Its control flags are `0x10118`, which includes `RMCTRL_FLAGS_NON_PRIVILEGED`.
     Any RM client may call it (`generated/g_subdevice_nvoc.c`).
   - `gpuValidateRegOffset_IMPL` (`gpu/gpu_access.c:1212`) checks only
     `offset <= BAR0 size - 4`. It also checks the user allowlist, but
     **`osIsAdministrator()` bypasses the allowlist**. An elevated client can
     therefore read 0x82381c. Nsight's "GPU performance counters: admin only"
     setting is this same gate.
   - On GSP-client systems, CPU-RM validates first and then sends the op
     over RPC to GSP-RM. GSP-RM runs with a higher priv level than the CPU.
2. **The shipped 621.24 kernel driver contains the handler.** Strings in
   `DriverStore\...\nvlddmkm.sys`: `GPU_EXEC_REG_OPS`, `CTRL_DBG_EXEC_REG_OPS`,
   `CTRL_B0CC_EXEC_REG_OPS`.
3. **NVIDIA's own user-mode code reaches RM controls through `D3DKMTEscape`.**
   `D3DKMTEscape` is a documented Windows API. Strings in DriverStore
   `nvml.dll`: `D3DKMTEscape`, `cDeviceScopeRmControl`. nvcuda64.dll also
   imports `D3DKMTEscape`. The transport is already in place and
   WHQL-signed.

## What is unknown

- **The escape packet framing.** The private NVIDIA header that wraps an
  RmControl inside the D3DKMTEscape buffer is undocumented. It has to be
  recovered from nvml.dll. Two ways: static analysis, or a Frida capture of
  the escape buffers while `nvidia-smi` runs normally. Either way is pure
  observation.
- **Whether the escape gate forwards arbitrary control IDs.** The Windows
  escape dispatcher may filter which RM controls a process can send.
- **The post-driver priv-level mask on 0x8238xx.** CPU reads worked at EFI.
  After load, the driver or GSP may tighten the PLM, so a read could return
  0xbadfxxxx. If that happens, the result counts as "unreadable", never as
  "locked".
- **Stability across driver updates.** The control ID and parameter
  struct are SDK ABI and change rarely. The escape framing could change
  between driver branches, so re-validate after each driver update.
  Fallback: ratio-v2 performance check (`check_status.py`).

## Risk

The ops are reads only, to registers the EFI path already reads safely.
The main risk is a **malformed escape** reaching nvlddmkm's escape
handler, which has a CVE history (CVE-2022-28184 and others). The worst
case is a bugcheck/BSOD during development. There is no firmware, Secure
Boot or signing change.

## Step 1 result (2026-11-14): escape framing recovered — OBSERVE ONLY

Captured nvidia-smi's own `D3DKMTEscape` calls with Frida 17.26 (spawn
`nvidia-smi -q` paused, hook `gdi32!D3DKMTEscape` + `win32u!NtGdiDdDDIEscape`,
dump each `pPrivateDriverData`, resume). 49 unique escapes, 45 of them
RM-control. Nothing was sent to the driver; only nvidia-smi's own buffers were
read. Capture + decoders are in the session scratchpad
(`escape_capture/`, `decode_escape.py`, `parse_rmcontrol.py`).

Every escape uses `D3DKMT_ESCAPE.Type = 0` (DRIVERPRIVATE) and this wrapper:

```
off 0x00  u32 magic     = 0x4e564441  "NVDA"
off 0x04  u32 version   = 0x00010002
off 0x08  u32 totalSize = PrivateDriverDataSize
off 0x0c  u32 tag       = 0x4e562a2a  "NV**"
off 0x10  u32 subOp     = 0x0500002b for RM CONTROL
                          (0x0500002a/0x0500000c/0x01000001 = alloc/map/free/other)
off 0x14  20 bytes, mostly zero (reserved + out-status)
off 0x30  u32 hClient   = 0xc1d001f2   (per-process RM client; constant in a run)
off 0x34  u32 hObject   = target handle (subdevice = 0xa55a0020, device = 0xa55a0010)
off 0x38  u32 cmd       = RM control id
off 0x3c  u32 paramSize = totalSize - 0x44
off 0x40  u32 out-status (0 on entry)
off 0x44  params[paramSize]
```

Confirmed self-consistent: control class (cmd >> 16) always matches the handle
it targets — 0x0000 client controls hit hObject=hClient, 0x0080 device
controls hit the device handle, and all 0x2080 **subdevice** controls hit
hObject=0xa55a0020. nvidia-smi issued 40+ distinct 0x2080-class controls this
way (e.g. 0x20800102, 0x2080014b, 0x20803404).

**`EXEC_REG_OPS` = 0x20800122 is the same 0x2080 subdevice class and targets
the same hObject=0xa55a0020.** So reading SS0/SS1 means reusing this exact
wrapper with `cmd=0x20800122` and a regops param block, sent to the subdevice
handle. nvidia-smi did not call regops itself (confirmed absent in the
capture), which is expected — `-q` has no reason to.

Still to confirm before any write to the driver: the live `hClient`/subdevice
handle must be harvested at call time (they are per-process; the 0xc1d001f2 /
0xa55a0020 values are this capture's, not constants to hardcode), and the
regops param struct (`NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS`) must be laid out
from the SDK header already in `fw-cache`.

## Proposed validation ladder (each step needs explicit go-ahead)

1. Observe only: capture nvidia-smi's real escape buffers (Frida /
   `rea` tools), then decode the framing. No custom calls.
2. Replay one benign, known control (e.g. GPU name string) with the
   decoded framing. Compare it byte-for-byte with nvml's.
3. Run a regops READ_32 of `PMC_BOOT_0` (BAR0+0x0) and check it against
   the known AD103 chip ID.
4. Read SS0/SS1, elevated. Cross-check against ratio-v2: UNLOCKED should
   show 0x88888888 / 8.

## Step 2/3 result (2026-11-14): PMC_BOOT_0 read, confirmed — LIVE CALL

Harness: `live-read/step2_regops.js` (generic `EXEC_REG_OPS READ_32` sender,
reused by step 4) + `live-read/step2_driver.py`. Spawns a resident
`nvidia-smi -l 1`, hooks `D3DKMTEscape` to harvest that process's live
`hClient`/subdevice `hObject` (handles are per-process, never hardcoded),
then sends one crafted escape: the recovered 68-byte NVDA wrapper, `cmd =
0x20800122` (`GPU_EXEC_REG_OPS`), `paramSize = 0x30`, carrying
`NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS` (`regOpCount=1`, `regOps` = pointer to
one 32-byte `NV2080_CTRL_GPU_REG_OP{regOp=READ_32, regType=GLOBAL,
regOffset=0x0}`, `grRouteInfo` zeroed = ROUTE_TYPE_NONE).

- **Non-elevated run:** `D3DKMTEscape` returned `NTSTATUS 0` (accepted,
  no crash) but RM's private status came back `0x1f`. Checked against
  `kernel-open/common/inc/nvstatuscodes.h:60`: `0x1f = NV_ERR_INVALID_ARGUMENT`.
  Traced to `subdevice_ctrl_gpu_regops.c:84-97` (`gpuValidateRegOps`) — *any*
  failure of `gpuValidateRegOffset` (including the non-admin allowlist
  check) is folded into this same `INVALID_ARGUMENT` return when
  `bNonTransactional=0`, so this is the exact signature predicted for "right
  framing, blocked by the admin-only gate," not a malformed packet.
- **Elevated run:** `wrapperStatus=0x0` (NV_OK), `regStatus=0x0` (SUCCESS),
  `regValueLo=0x193000a1`. `0x193` = AD103 chip ID (Ada: `0x192`=AD102,
  `0x193`=AD103, `0x194`=AD104, `0x196`=AD106, `0x197`=AD107), `0xa1` =
  revision/stepping A1 — the exact, correct chip ID for this RTX 4070 Ti
  SUPER. No crash on either run.

This validates the entire chain end to end: live handle harvest → crafted
escape wrapper → RM `EXEC_REG_OPS` dispatch → BAR0 → reply parsing, with a
hardware-verifiable answer.

## Step 4 result (2026-11-14): SS0/SS1 read, confirmed — LIVE CALL, elevated

Harness: `live-read/step4_driver.py` (same `step2_regops.js` reader, two
calls). Elevated run:

- `SS0 (0x82381c)`: `wrapperStatus=0x0`, `regStatus=0x0`, `regValueLo=0x88888888`
- `SS1 (0x823820)`: `wrapperStatus=0x0`, `regStatus=0x0`, `regValueLo=0x8`

Both match the predicted **UNLOCKED** signature exactly (`0x88888888` / `8`),
agreeing with the independent ratio-v2 performance-ratio classifier result
(`check_status.py`, same machine, same date: UNLOCKED, fp16_fp32=1.002,
bf16_fp32=1.003, fp8=1.994). Two independent methods — direct register read
and benchmark-timing ratios — agree. No crash. Validation ladder complete;
option 3 is no longer "plausible," it is demonstrated working on this
machine/driver (621.24).

Open items if this is turned into a standing checker: re-validate the escape
framing after driver updates (the framing is undocumented ABI, not SDK ABI,
so it is the part most likely to shift between branches); it still requires
elevation, same as Nsight's own "GPU performance counters: admin only" gate.
