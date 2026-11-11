# AD103/Z790 ownership-graph diagnostic probe

**Superseded 2026-11-10 by `../combined_probe/`**, which does this capture
and `../probe/`'s capture together in one boot (both are read-only,
non-overlapping snapshots of the same one-time machine state, so there was
never a reason they needed separate boots). Kept here for the record; use
`../combined_probe/` going forward.

A second, separate read-only UEFI application (next to `../probe/`, which
captures PCI config space, BAR0 registers and the VBIOS hash). This one
captures what a future `windows/ad103-z790` display/driver-ownership module
-- the Z790 equivalent of `windows/ad104-msi/msi_release.c` -- would need
real, board-specific values for. None of it is derivable from a file; it
has to come from a live walk of this exact machine's own boot-time
protocol graph, the same way `../probe/` captures registers that can't be
guessed from another board's values.

## What it does and does not do

**Does:** finds the RTX 4070 Ti SUPER over the PCI IO protocol (same
discovery as `../probe/`); reads and hex-dumps the device paths of the
GPU's PCI_IO handle, every Graphics Output Protocol handle it finds (and
which one looks physical, by framebuffer base/size), and any USB host
controller it finds that manages itself; takes a full read-only snapshot
of the system's protocol-ownership graph (handle/agent/controller/
protocol/attributes/count, the same shape `msi_release.c`'s `Snapshot()`
builds, via `LocateHandleBuffer`/`ProtocolsPerHandle`/
`OpenProtocolInformation` -- none of which open, close, connect or
disconnect anything); from that graph, identifies which driver is bound
`BY_DRIVER` to the GPU's PCI_IO protocol, to the physical GOP's console-text
protocol, and to the physical GOP's own GOP protocol; and for each of
those three drivers, reads its `EFI_DRIVER_BINDING_PROTOCOL` and
`EFI_LOADED_IMAGE_PROTOCOL`, hex-dumps its file path, walks its in-memory
PE32/PE32+ headers (bounds-checked against the image's own reported size
at every step), and SHA-256-hashes the whole loaded image plus each
section whose characteristics mark it as code. Everything goes to a log
file next to this `.EFI` on the USB stick it was booted from, and to the
console.

**Does not:** open any protocol exclusively or `BY_DRIVER`, call
`DisconnectController`/`ConnectController`, change PCI command or
attributes, write to PCI config space or BAR0, perform FLR, touch the
internal disk, the ESP, or `bootmgfw.efi`, or start Windows itself. It
returns to the firmware boot menu when done, every time, whether it
succeeds or fails.

This isn't a policy on top of otherwise-capable code -- `port_probe.c`/
`port_probe.h` (copied unmodified from `../probe/`) implement only
`GuInit`/`GuConfigRead`/`GuRead32`; the write-capable functions from
`ad104-msi/port.c` (`GuWrite32`, `GuFlr`, `GuEnableMaster`,
`GuDmaAllocate`, the selector-latch functions) do not exist anywhere in
this tree. The only EFI boot-service calls this tool makes beyond that are
read-only queries (`HandleProtocol`, `LocateHandleBuffer`,
`ProtocolsPerHandle`, `OpenProtocolInformation`) plus `AllocatePool`/
`FreePool` for its own scratch memory -- no `OpenProtocol` call appears
anywhere in this tree.

## Build

Same toolchain as `../probe/` (Windows x64, MinGW-w64 GCC, MSVC x64 linker
tools; `vswhere` discovers MSVC automatically):

```powershell
.\Build-OwnershipProbe.ps1
```

Output: `build\AD103-OWNERSHIP.EFI`.

## Run

Same procedure as `../probe/README.md`: copy `build\AD103-OWNERSHIP.EFI`
to `EFI\BOOT\BOOTX64.EFI` on a separate FAT32 USB stick, Secure Boot off,
boot from it via the one-time boot menu. It runs immediately and writes
`AD103-OWNERSHIP-0.TXT` (or the next free number) to the same USB stick,
flushing each line as it goes rather than buffering the whole log in
memory -- so if it ever hangs partway (walking the full handle graph on a
real machine visits far more handles than `../probe/` ever touches), the
log up to that point is already saved, not lost. Press any key when it
says it's done to return to the firmware boot menu, then boot Windows as
usual.

Bring back the whole log: the `*_device_path` hex dumps and decoded nodes
for the GPU, every GOP, and any USB host candidates; the
`nv_agent`/`out_agent`/`gfx_agent` BY_DRIVER edge counts; each driver's
`binding`/`image_base`/`stop_offset`/`file_path`/`whole_image_sha256`/
per-section lines; and the full graph dump at the end.

## What happens next

The captured device-path bytes become the Z790 equivalent of
`msi_release.c`'s `PciPath`/`GopPath`/`UsbHostPath` arrays. The three
drivers' captured `file_path`, `image_size`, `stop_offset` and section
hashes become the Z790 equivalent of its `Driver()` function's per-driver
pins -- using the per-section hash over a PE-header-identified code
section is a deliberate change from how the original board's drivers were
apparently pinned (by hand-picked offset/length, per its own "CAP0004"
comment): hashing a section the loaded image's own headers say is code is
reproducible and checkable by a second person, rather than resting on an
unrecorded manual step. If the BY_DRIVER edge counts for `nv_agent`/
`out_agent`/`gfx_agent` are not each exactly 1, that itself is important
information -- it means this board's firmware does not expose the simple
three-driver structure `msi_release.c` assumes, and the ownership module
will need to be designed around whatever the full graph dump actually
shows, not forced to match AD104-MSI's shape.

Only once those are real captured values -- not placeholders -- does
writing the AD103/Z790 ownership module (the piece that will actually
call `DisconnectController` and claim the GPU exclusively) become
possible, and only then does a write-capable transaction tool have
anything to detach display/driver ownership from before it runs.

## Licensing

GPL-2.0-only, same as the rest of this repository.
