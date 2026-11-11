# AD103/Z790 cold-boot diagnostic probe

**Superseded 2026-11-10 by `../combined_probe/`**, which does this capture
and `../ownership_probe/`'s capture together in one boot (both are read-only,
non-overlapping snapshots of the same one-time machine state, so there was
never a reason they needed separate boots). Kept here for the record; use
`../combined_probe/` going forward.

A read-only UEFI application that fills in `../profile.c`'s gaps: the PCI
config-space and BAR0 register values `Config()` and `Registers()` need, and
a live hash-and-compare of the VBIOS aperture `GuAd103Prepare()` checks —
all of which must come from this exact machine's own hardware and cannot be
copied from `ad104-msi`'s captured values (a different board).

## What it does and does not do

**Does:** finds the RTX 4070 Ti SUPER (`10de:2705`) over the PCI IO protocol,
reads its 256-byte config space, walks its capability list (PCIe, MSI, MSI-X,
power management), reads the same ~19 BAR0 register offsets
`ad104-msi/profile.c`'s `Registers()` checks (plus the SS0/SS1 selector pair),
then reads 1 MiB from BAR0+0x300000 (the VBIOS aperture `GuAd103Prepare()`
reads at the real moment of truth) and hashes it with SHA-256, comparing
against the hash pinned in `../profile.c`'s `GuAd103PromMatches()`. It writes
everything to a log file next to this `.EFI` on the USB stick it was booted
from, and to the console. On a hash mismatch it also saves the raw 1 MiB it
read to that USB stick, so the actual bytes can be compared offline — this is
exactly how an earlier, wrong derivation of that hash (from an `nvflash`
dump instead of a live read) was caught: see `../profile.c`'s header comment.

**Does not:** write to PCI config space, write to BAR0, perform FLR, allocate
DMA, touch the internal disk, the ESP, or `bootmgfw.efi`, or start Windows
itself. It returns to the firmware boot menu when done, every time, whether
it succeeds or fails.

This isn't a policy on top of otherwise-capable code -- the write-capable
functions (`GuWrite32`, `GuFlr`, `GuEnableMaster`, `GuDmaAllocate`, the
selector-latch functions) from `ad104-msi/port.c` simply do not exist in this
tree at all (see `port_probe.h`/`port_probe.c`). `AllocatePool`/`FreePool`
(boot services, not hardware access) hold the 1 MiB read buffer; the actual
GPU reads still only ever go through `GuConfigRead`/`GuRead32`/the chunked
`Mem.Read` for the VBIOS aperture, none of which can write. There is no code
path here that could touch the GPU beyond a register or memory read.

## Build

Same toolchain as `ad104-msi` (Windows x64, MinGW-w64 GCC, MSVC x64 linker
tools; `vswhere` discovers MSVC automatically):

```powershell
.\Build-Probe.ps1
```

Output: `build\AD103-PROBE.EFI`.

## Run

1. Copy `build\AD103-PROBE.EFI` to `EFI\BOOT\BOOTX64.EFI` on a separate FAT32
   USB stick you control (not your Windows install's own USB, and don't
   overwrite an existing bootable stick's files without backing them up).
2. Secure Boot must be off to boot an unsigned EFI application at all --
   this is a firmware-level requirement, not something this probe checks or
   depends on, since it's read-only regardless.
3. Boot from that USB. It runs immediately (no key prompt to start, unlike
   the real transaction tool) and writes `AD103-PROBE-0.TXT` (or the next
   free number) to the same USB stick, plus printing to the screen.
4. Press any key when it says it's done -- this returns you to the firmware
   boot menu. Pick your normal Windows entry as usual.
5. Paste back the contents of `AD103-PROBE-N.TXT`: specifically the `bar0[...]`
   lines, the `config[...]` lines, the `found 10de:2705 at segment=...`
   line, the `GuCheckConfigProbe=...` line, and the `prom_...` lines.
6. If `prom_match=0`, also send the saved `AD103-PROM-N.BIN` (or at least its
   sha256) so the pinned hash in `GuAd103PromMatches()` can be corrected --
   do not treat a mismatch as a probe bug before checking that first.

## What happens next

The `bar0[...]` values go into `Registers()`, the `config[...]` values and
discovered bus/device/function into `Config()`'s asserts (the same way
`ad104-msi/profile.c` pins its own board's `c[2]`, `c[4]` = BAR0 base,
`c[5]`..`c[9]`, and capability offsets), and `prom_sha256` into
`GuAd103PromMatches()` if it doesn't already match. Re-run this probe after
any such change to confirm `prom_match=1` before trusting the gate. Only
once all of that reflects real captured values does the profile represent
an attempt-ready configuration, and only then does the manual cold-boot
procedure in `ad104-msi/README.md` apply.

## A real example of why the live PROM check matters

An earlier version of `GuAd103PromMatches()` was built from an `nvflash
--save` dump taken from inside a running Windows session, on the (never
actually verified) assumption that it would be byte-identical to a live
pre-driver BAR0 read. This probe's PROM check exists specifically to test
assumptions like that before they end up load-bearing in a write-capable
tool. Running it found that assumption was wrong — the live read and the
nvflash dump are not the same bytes — and the mismatch was caught here, with
nothing at risk, rather than surfacing later as a confusing failure in a
tool that actually touches the GPU.
