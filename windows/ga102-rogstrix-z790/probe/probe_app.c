/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ASUS ROG STRIX Z790-A / RTX 3090 Ti (GA102) cold-boot diagnostic probe --
 * READ ONLY, single combined run. Adapted from and merges
 * ../../ad103-z790-en-REFERENCE/probe/probe_app.c (PCI config space, BAR0
 * registers, VBIOS hash) and
 * ../../ad103-z790-en-REFERENCE/ownership_probe/probe_app.c (protocol-
 * ownership graph: GPU/GOP/USB-host device paths, the three BY_DRIVER
 * agents bound to them, their loaded-image identity and section hashes) --
 * one boot, one log, instead of two separate USB boots.
 *
 * It never writes to PCI config space, never writes to BAR0, never performs
 * FLR, never allocates DMA, never touches the internal disk, the ESP, or
 * bootmgfw.efi, never starts Windows itself, and never opens any protocol
 * exclusively or BY_DRIVER, never calls DisconnectController/
 * ConnectController, never changes PCI command/attributes -- it returns to
 * the firmware boot menu when done, every time, whether it succeeds or
 * fails. GuWrite32/GuFlr/GuEnableMaster/GuDmaAllocate and the selector-latch
 * functions from ../port.c do not exist anywhere in this tree (see
 * port_probe.h/.c), so there is no code path that could do any of that even
 * by mistake. The only EFI boot-service calls beyond config/BAR reads are
 * read-only queries (HandleProtocol, LocateHandleBuffer, ProtocolsPerHandle,
 * OpenProtocolInformation) plus AllocatePool/FreePool for scratch memory --
 * no OpenProtocol call appears anywhere in this tree.
 */
#include "port_probe.h"
#include "checks_probe.h"
#include "graph.h"
#include "devpath.h"
#include "pehash.h"
#include "format.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>
#include <IndustryStandard/Acpi10.h>

/* No real pin exists yet -- this is the first capture on this board. 64 hex
 * zeros can never equal a real SHA-256 digest, so the mismatch-and-save path
 * below always runs. Once ../profile.c's GuGa102PromMatches() has a real
 * pinned hash from a saved GA102-PROM-N.BIN, update this to match it (then
 * a later run of this probe should start reporting prom_match=1). */
#define GU_PROM_EXPECTED_HASH "0000000000000000000000000000000000000000000000000000000000000000"
#define GU_PROM_OFFSET 0x300000U
#define GU_PROM_BYTES 1048576U

/* devpath.c's GuCaptureDevicePath() reads this exact global via `extern` --
 * the name is part of its contract, not a free choice here. */
EFI_SYSTEM_TABLE *GuProbeTable;
static EFI_FILE_PROTOCOL *LogFile;
static BOOLEAN LogOk;

/* Small label builders -- GuFormatV takes a va_list, not direct arguments,
 * so it isn't usable for this; both truncate rather than overrun Out. */
static VOID LabelS(char *Out, UINTN OutBytes, const char *Prefix, const char *Suffix) {
  UINTN len = 0;
  for (UINTN i = 0; Prefix[i] && len + 1 < OutBytes; i++) Out[len++] = Prefix[i];
  for (UINTN i = 0; Suffix[i] && len + 1 < OutBytes; i++) Out[len++] = Suffix[i];
  Out[len] = 0;
}
static VOID LabelN(char *Out, UINTN OutBytes, const char *Prefix, UINTN N, const char *Suffix) {
  UINTN len = 0;
  for (UINTN i = 0; Prefix[i] && len + 1 < OutBytes; i++) Out[len++] = Prefix[i];
  char digits[24]; UINTN d = 0, v = N;
  do { digits[d++] = (char)('0' + v % 10); v /= 10; } while (v && d < sizeof(digits));
  while (d && len + 1 < OutBytes) Out[len++] = digits[--d];
  for (UINTN i = 0; Suffix[i] && len + 1 < OutBytes; i++) Out[len++] = Suffix[i];
  Out[len] = 0;
}

static VOID Say(const CHAR16 *s) {
  if (GuProbeTable && GuProbeTable->ConOut) GuProbeTable->ConOut->OutputString(GuProbeTable->ConOut, (CHAR16 *)s);
}

/* Writes one line to the console and, if the log file is open, to it too --
 * flushed immediately (not buffered to the end): this run does considerably
 * more than either half alone (a full protocol-graph walk visits far more
 * handles than the register probe ever touches), so if it ever hangs
 * partway, the log up to that point is already saved, not lost. */
static VOID Logf(const char *fmt, ...) {
  char line[400];
  va_list args;
  va_start(args, fmt);
  size_t n = GuFormatV(line, sizeof(line), fmt, args);
  va_end(args);
  if (n == (size_t)-1) { Say(L"(a log line was too long for its buffer and was dropped)\r\n"); return; }

  static CHAR16 echo[404];
  UINTN j = 0;
  for (size_t i = 0; line[i] && j + 1 < sizeof(echo) / sizeof(echo[0]); i++) echo[j++] = (CHAR16)(unsigned char)line[i];
  echo[j] = 0;
  Say(echo);
  Say(L"\r\n");

  if (LogOk && LogFile) {
    static CHAR16 wide[404];
    UINTN k = 0;
    for (size_t i = 0; line[i] && k + 2 < sizeof(wide) / sizeof(wide[0]); i++) wide[k++] = (CHAR16)(unsigned char)line[i];
    wide[k++] = L'\r'; wide[k++] = L'\n';
    UINTN bytes = k * sizeof(CHAR16);
    if (EFI_ERROR(LogFile->Write(LogFile, &bytes, wide)) || EFI_ERROR(LogFile->Flush(LogFile))) {
      LogOk = FALSE;
      Say(L"(log write failed -- continuing to console only)\r\n");
    }
  }
}

/* Full 16-byte GUID as a standard 8-4-4-4-12 hex string -- the edge dump
 * elsewhere only ever showed protocol.Data1 (4 of 16 bytes), enough to
 * recognize most well-known protocols by eye but not enough to be sure
 * against a genuine collision. Used where that distinction matters:
 * identifying exactly what's bound to the physical GOP/console handles. */
static VOID LogGuidField(const char *Label, const EFI_GUID *G) {
  Logf("%s=%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", Label, G->Data1, G->Data2, G->Data3,
       (unsigned)G->Data4[0], (unsigned)G->Data4[1], (unsigned)G->Data4[2], (unsigned)G->Data4[3],
       (unsigned)G->Data4[4], (unsigned)G->Data4[5], (unsigned)G->Data4[6], (unsigned)G->Data4[7]);
}

/* Read-only: lists every protocol GUID a handle publishes, regardless of
 * whether anyone has opened it BY_DRIVER/BY_CHILD_CONTROLLER/EXCLUSIVE --
 * ProtocolsPerHandle alone, no OpenProtocolInformation, so this sees
 * protocols with zero "real" consumers too (which the edge graph elsewhere
 * in this file can't show, since it only records open-type relationships). */
static VOID DumpProtocols(const char *Label, EFI_HANDLE Handle, EFI_BOOT_SERVICES *Bs) {
  EFI_GUID **protocols = NULL;
  UINTN count = 0;
  if (!Handle) { Logf("%s_protocols: handle is NULL", Label); return; }
  EFI_STATUS s = Bs->ProtocolsPerHandle(Handle, &protocols, &count);
  if (EFI_ERROR(s) || !protocols || !count) {
    Logf("%s_protocols: ProtocolsPerHandle status=%#llx count=%zu", Label, (unsigned long long)s, (size_t)count);
    return;
  }
  Logf("%s_protocols: handle=%#llx count=%zu", Label, (unsigned long long)(UINTN)Handle, (size_t)count);
  for (UINTN i = 0; i < count && i < 64; i++) {
    char label[48];
    LabelN(label, sizeof(label), Label, i, "_protocol");
    if (protocols[i]) LogGuidField(label, protocols[i]);
  }
  Bs->FreePool(protocols);
}

/* Opens the EFI_FILE_PROTOCOL root of the volume this .EFI was loaded from --
 * never the internal disk/ESP, only wherever this image itself is running
 * from (the USB stick). Shared by OpenLog() and WriteBin() below. */
static EFI_STATUS OpenBootVolumeRoot(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs, EFI_FILE_PROTOCOL **Root) {
  EFI_GUID LoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
  EFI_GUID FsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
  EFI_STATUS s = Bs->HandleProtocol(Image, &LoadedImageGuid, (VOID **)&LoadedImage);
  if (EFI_ERROR(s) || !LoadedImage || !LoadedImage->DeviceHandle) { Say(L"log: no loaded-image device handle\r\n"); return EFI_NOT_FOUND; }
  s = Bs->HandleProtocol(LoadedImage->DeviceHandle, &FsGuid, (VOID **)&Fs);
  if (EFI_ERROR(s) || !Fs) { Say(L"log: no filesystem on boot volume\r\n"); return EFI_NOT_FOUND; }
  s = Fs->OpenVolume(Fs, Root);
  if (EFI_ERROR(s) || !*Root) { Say(L"log: could not open boot volume root\r\n"); return EFI_NOT_FOUND; }
  return EFI_SUCCESS;
}

/* Picks "<Prefix><n><Suffix>" for the lowest n (0..999) not already present
 * under Root, so repeated runs never overwrite an earlier log or capture. */
static VOID NextFreeName(EFI_FILE_PROTOCOL *Root, const CHAR16 *Prefix, const CHAR16 *Suffix, CHAR16 *NameOut) {
  for (UINT32 n = 0; n < 1000; n++) {
    UINTN len = 0;
    CHAR16 digits[8]; UINTN d = 0, v = n;
    do { digits[d++] = (CHAR16)(L'0' + v % 10); v /= 10; } while (v && d < 8);
    UINTN p = 0; while (Prefix[p]) NameOut[len++] = Prefix[p++];
    while (d) NameOut[len++] = digits[--d];
    UINTN q = 0; while (Suffix[q]) NameOut[len++] = Suffix[q++];
    NameOut[len] = 0;
    EFI_FILE_PROTOCOL *existing = NULL;
    EFI_STATUS s = Root->Open(Root, &existing, NameOut, EFI_FILE_MODE_READ, 0);
    if (s == EFI_NOT_FOUND) return;
    if (!EFI_ERROR(s)) existing->Close(existing);
  }
}

/* Opens GA102-DIAG-<n>.TXT for the combined log, flush-per-line (see Logf). */
static VOID OpenLog(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs) {
  EFI_FILE_PROTOCOL *Root = NULL;
  if (EFI_ERROR(OpenBootVolumeRoot(Image, Bs, &Root))) return;
  CHAR16 name[48];
  NextFreeName(Root, L"GA102-DIAG-", L".TXT", name);
  EFI_STATUS s = Root->Open(Root, &LogFile, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  Root->Close(Root);
  if (EFI_ERROR(s) || !LogFile) { Say(L"log: could not create log file\r\n"); return; }
  LogOk = TRUE;
  Say(L"Log: "); Say(name); Say(L"\r\n");
}

/* Writes a raw binary buffer to GA102-PROM-<n>.BIN -- used only when the live
 * PROM hash doesn't match, so the actual bytes can be compared offline. */
static VOID WriteBin(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs, const VOID *Data, UINTN Bytes) {
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  if (EFI_ERROR(OpenBootVolumeRoot(Image, Bs, &Root))) return;
  CHAR16 name[48];
  NextFreeName(Root, L"GA102-PROM-", L".BIN", name);
  EFI_STATUS s = Root->Open(Root, &File, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(s) || !File) { Say(L"prom: could not create capture file\r\n"); Root->Close(Root); return; }
  UINTN wrote = Bytes;
  s = File->Write(File, &wrote, (VOID *)Data);
  File->Close(File);
  Root->Close(Root);
  if (EFI_ERROR(s) || wrote != Bytes) { Say(L"prom: capture write failed\r\n"); return; }
  Say(L"Raw PROM capture written: "); Say(name); Say(L"\r\n");
}

/* Finds the RTX 3090 Ti over the PCI IO protocol and returns both its handle
 * (needed for the ownership-graph half) and its segment/bus/device/function
 * (needed for the register half's GuCheckConfigProbe call). */
static EFI_STATUS Discover(EFI_BOOT_SERVICES *Bs, EFI_HANDLE *PciHandleOut,
                            UINTN *Segment, UINTN *Bus, UINTN *Device, UINTN *Function) {
  EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID;
  EFI_HANDLE *h = NULL;
  EFI_PCI_IO_PROTOCOL *io = NULL;
  UINTN count = 0, found = 0;
  EFI_STATUS s = Bs->LocateHandleBuffer(ByProtocol, &PciGuid, NULL, &count, &h);
  if (s == EFI_SUCCESS && (!h || !count || count > 1024)) s = EFI_UNSUPPORTED;
  for (UINTN i = 0; s == EFI_SUCCESS && i < count; i++) {
    EFI_PCI_IO_PROTOCOL *p = NULL;
    UINT32 id = 0;
    s = Bs->HandleProtocol(h[i], &PciGuid, (VOID **)&p);
    if (s != EFI_SUCCESS || !p || !p->Pci.Read) { s = EFI_UNSUPPORTED; break; }
    s = p->Pci.Read(p, EfiPciIoWidthUint32, 0, 1, &id);
    if (s == EFI_SUCCESS && id == GU_BOARD_ID) { found++; io = p; *PciHandleOut = h[i]; }
  }
  if (h) Bs->FreePool(h);
  if (s != EFI_SUCCESS || found != 1 || !io) return EFI_UNSUPPORTED;
  if (!io->GetLocation || EFI_ERROR(io->GetLocation(io, Segment, Bus, Device, Function))) return EFI_UNSUPPORTED;
  Gu.pci = io;
  return EFI_SUCCESS;
}

/* The architecture-generic BAR0 offsets ../profile.c's Config()/Registers()
 * will check once filled in, plus the SS0/SS1 selector pair (0x82381c/
 * 0x823820, see docs/How Ampere was unlocked.md). Printed as-is; no expected
 * values to compare against yet -- this is the first capture on this board. */
static const UINT32 RegisterOffsets[] = {
    0, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828, 0x1fa7cc, 0x1fa7c4,
    0x9a0148, 0x823804, 0x823814, 0x823818, 0x1183a4, 0x625f04, 0x1fa7c8,
    0x1fa82c, 0x1fa830, 0x824148, 0x8241e0, 0x82381c, 0x823820};

static VOID DumpDriver(const char *Label, EFI_HANDLE Agent, EFI_BOOT_SERVICES *Bs) {
  if (!Agent) { Logf("%s_agent: not identified (0 or >1 BY_DRIVER edge -- see graph dump)", Label); return; }
  Logf("%s_agent handle=%#llx", Label, (unsigned long long)(UINTN)Agent);
  EFI_GUID BindingGuid = EFI_DRIVER_BINDING_PROTOCOL_GUID;
  EFI_DRIVER_BINDING_PROTOCOL *Binding = NULL;
  if (EFI_ERROR(Bs->HandleProtocol(Agent, &BindingGuid, (VOID **)&Binding)) || !Binding) {
    Logf("%s: no EFI_DRIVER_BINDING_PROTOCOL on this agent handle", Label);
    return;
  }
  Logf("%s: binding version=%#x driver_binding_handle=%#llx image_handle=%#llx stop=%#llx",
       Label, Binding->Version, (unsigned long long)(UINTN)Binding->DriverBindingHandle,
       (unsigned long long)(UINTN)Binding->ImageHandle, (unsigned long long)(UINTN)Binding->Stop);
  EFI_GUID ImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
  EFI_LOADED_IMAGE_PROTOCOL *Image = NULL;
  if (EFI_ERROR(Bs->HandleProtocol(Binding->ImageHandle, &ImageGuid, (VOID **)&Image)) || !Image) {
    Logf("%s: no EFI_LOADED_IMAGE_PROTOCOL on its ImageHandle", Label);
    return;
  }
  UINTN StopOffset = Binding->Stop ? (UINTN)Binding->Stop - (UINTN)Image->ImageBase : 0;
  Logf("%s: image_base=%#llx image_size=%llu code_type=%u device_handle=%#llx stop_offset=%#x",
       Label, (unsigned long long)(UINTN)Image->ImageBase, (unsigned long long)Image->ImageSize, Image->ImageCodeType,
       (unsigned long long)(UINTN)Image->DeviceHandle, (unsigned)StopOffset);

  /* Image->FilePath is already a dereferenced EFI_DEVICE_PATH_PROTOCOL
   * pointer -- walk it directly, rather than via HandleProtocol (the image
   * handle is not guaranteed to carry a plain EFI_DEVICE_PATH_PROTOCOL
   * instance of its own; FilePath is the one the spec actually promises). */
  UINT8 pathbuf[GU_DEVPATH_MAX_BYTES];
  UINTN pathlen = GuCaptureDevicePathFromPointer(Image->FilePath, pathbuf, sizeof(pathbuf));
  char label[64];
  LabelS(label, sizeof(label), Label, "_file_path");
  if (pathlen) GuDescribeDevicePath(label, pathbuf, pathlen, Logf);
  else Logf("%s_file_path: capture failed (FilePath pointer=%#llx)", Label, (unsigned long long)(UINTN)Image->FilePath);

  if (!Image->ImageBase || !Image->ImageSize) { Logf("%s: no readable image bytes", Label); return; }
  char whole_hash[65];
  if (GuHashImageRange(Image->ImageBase, Image->ImageSize, 0, Image->ImageSize, whole_hash))
    Logf("%s: whole_image_sha256=%s bytes=%llu", Label, whole_hash, (unsigned long long)Image->ImageSize);

  GU_PE_INFO pe;
  GuParsePeHeaders(Image->ImageBase, Image->ImageSize, &pe);
  if (!pe.parsed) { Logf("%s: PE header did not parse -- no per-section hash", Label); return; }
  Logf("%s: PE32%s machine=%#x sections=%u", Label, pe.is_pe32_plus ? "+" : "", pe.machine, (unsigned)pe.section_count);
  for (UINTN i = 0; i < pe.section_count; i++) {
    GU_PE_SECTION *sec = &pe.sections[i];
    Logf("%s: section[%u] name=%s va=%#x size=%#x characteristics=%#x code=%u",
         Label, (unsigned)i, sec->name, sec->virtual_address, sec->virtual_size, sec->characteristics, sec->looks_like_code);
    if (sec->looks_like_code && sec->virtual_size) {
      char sec_hash[65];
      if (GuHashImageRange(Image->ImageBase, Image->ImageSize, sec->virtual_address, sec->virtual_size, sec_hash))
        Logf("%s: section[%u] sha256=%s", Label, (unsigned)i, sec_hash);
    }
  }
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table) {
  GuProbeTable = Table;
  if (!Image || !Table || !Table->BootServices) return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *Bs = Table->BootServices;

  Say(L"\r\nASUS Z790-A / RTX 3090 Ti (GA102) combined diagnostic probe -- read only, no GPU writes, no driver detach.\r\n");
  OpenLog(Image, Bs);
  Logf("probe started (combined PCI/register/VBIOS + ownership-graph capture, one run)");
  Logf("console handles: out=%#llx in=%#llx err=%#llx (informational, not stable across boots)",
       (unsigned long long)(UINTN)Table->ConsoleOutHandle, (unsigned long long)(UINTN)Table->ConsoleInHandle,
       (unsigned long long)(UINTN)Table->StandardErrorHandle);
  DumpProtocols("console_out", Table->ConsoleOutHandle, Bs);

  UINTN seg = 0, bus = 0, dev = 0, fun = 0;
  EFI_HANDLE PciHandle = NULL;
  if (EFI_ERROR(Discover(Bs, &PciHandle, &seg, &bus, &dev, &fun))) {
    Logf("device 10de:2203 (GU_BOARD_ID=%#x) not found via PCI IO protocol enumeration -- stopping", GU_BOARD_ID);
    goto done;
  }
  Logf("found 10de:2203 at segment=%u bus=%u device=%u function=%u", seg, bus, dev, fun);

  GuInit(Bs, Gu.pci);

  /* ===== Part 1: PCI config space, BAR0 registers, VBIOS hash ===== */

  UINT32 c[64];
  BOOLEAN ok = TRUE;
  UINTN failedAt = 0;
  for (UINTN i = 0; i < 64 && ok; i++) {
    UINT32 v = 0;
    if (EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32, (UINT32)i * 4, &v))) { ok = FALSE; failedAt = i; }
    c[i] = v;
  }
  if (!ok) {
    Logf("config-space read failed at dword %u", (unsigned)failedAt);
  } else {
    for (UINTN i = 0; i < 64; i++) Logf("config[%#x]=0x%x", (unsigned)(i * 4), c[i]);
  }

  GU_CAPS caps = {0, 0, 0, 0, 0};
  BOOLEAN capsOk = ok && GuCheckConfigProbe(c, seg, dev, fun, &caps);
  Logf("GuCheckConfigProbe=%u pm=0x%x pcie=0x%x pcie_version=%u msi=0x%x msix=0x%x",
       capsOk, caps.pm, caps.pcie, caps.pcie_version, caps.msi, caps.msix);
  if (!capsOk) Logf("WARNING: capability walk or cold-state checks failed -- card may not be in the expected pre-driver state. Registers below are still printed for the record.");

  Gu.fault = FALSE; /* config-space checks use their own fault tracking; clear before BAR reads */
  for (UINTN i = 0; i < sizeof(RegisterOffsets) / sizeof(RegisterOffsets[0]); i++) {
    UINT32 off = RegisterOffsets[i];
    UINT32 v = GuRead32(off);
    Logf("bar0[0x%x]=0x%x%s", off, v, Gu.fault ? " (FAULT -- Mem.Read failed, stopping further BAR reads)" : "");
    if (Gu.fault) break;
  }

  if (Gu.fault) {
    Logf("prom: skipped, BAR reads faulted");
  } else {
    VOID *prom = NULL;
    EFI_STATUS s = Bs->AllocatePool(EfiLoaderData, GU_PROM_BYTES, &prom);
    if (EFI_ERROR(s) || !prom) {
      Logf("prom: could not allocate %u bytes", GU_PROM_BYTES);
    } else {
      /* Chunked Mem.Read, same 1024-byte chunking the full port's own
       * GuCopyFromIo will use for this same kind of large BAR0 read.
       * BAR-relative (BarIndex 0, offset GU_PROM_OFFSET) -- no absolute
       * physical address needed, same as every other read in this probe. */
      UINTN done = 0;
      BOOLEAN readOk = TRUE;
      while (done < GU_PROM_BYTES && readOk) {
        UINTN count = GU_PROM_BYTES - done < 1024 ? GU_PROM_BYTES - done : 1024;
        if (EFI_ERROR(Gu.pci->Mem.Read(Gu.pci, EfiPciIoWidthUint8, 0, GU_PROM_OFFSET + done, count, (UINT8 *)prom + done)))
          readOk = FALSE;
        done += count;
      }
      if (!readOk) {
        Logf("prom: read FAULT, no hash taken");
      } else {
        UINT8 digest[32];
        GuSha256(prom, GU_PROM_BYTES, digest);
        char hex[65];
        for (UINTN i = 0; i < 32; i++) {
          static const char *alpha = "0123456789abcdef";
          hex[i * 2] = alpha[digest[i] >> 4];
          hex[i * 2 + 1] = alpha[digest[i] & 0xf];
        }
        hex[64] = 0;
        const UINT8 *b = (const UINT8 *)prom;
        Logf("prom_first_bytes=0x%x 0x%x 0x%x 0x%x", b[0], b[1], b[2], b[3]);
        Logf("prom_sha256=%s", hex);
        Logf("prom_expected=%s (placeholder -- no real pin yet, see header comment)", GU_PROM_EXPECTED_HASH);
        BOOLEAN match = GuHashMatches(prom, GU_PROM_BYTES, GU_PROM_EXPECTED_HASH);
        Logf("prom_match=%u", match);
        if (!match) {
          Logf("prom: no pin yet (expected) -- saving the raw 1 MiB read to the USB stick so it can become the real pin");
          WriteBin(Image, Bs, prom, GU_PROM_BYTES);
        }
      }
      Bs->FreePool(prom);
    }
  }

  /* ===== Part 1.5: PCI_IO attributes and BAR0 descriptor -- read-only
   * queries (OperationGet/OperationSupported never mutate; only
   * OperationSet/Enable/Disable would, and none of those are called here).
   * This is exactly what a future ga102_release.c's Bar()/ClaimRestore()
   * checks would need pinned, per the AD103 reference's own pattern. ===== */
  {
    UINT64 attrs = 0, supported = 0;
    EFI_STATUS as = Gu.pci->Attributes ? Gu.pci->Attributes(Gu.pci, EfiPciIoAttributeOperationGet, 0, &attrs) : EFI_UNSUPPORTED;
    EFI_STATUS ss = Gu.pci->Attributes ? Gu.pci->Attributes(Gu.pci, EfiPciIoAttributeOperationSupported, 0, &supported) : EFI_UNSUPPORTED;
    Logf("pci_io_attributes: get_status=%#llx current=%#llx supported_status=%#llx supported=%#llx",
         (unsigned long long)as, (unsigned long long)attrs, (unsigned long long)ss, (unsigned long long)supported);
    EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *bar = NULL;
    UINT64 barsupport = 0;
    EFI_STATUS bs2 = Gu.pci->GetBarAttributes ? Gu.pci->GetBarAttributes(Gu.pci, 0, &barsupport, (VOID **)&bar) : EFI_UNSUPPORTED;
    if (!EFI_ERROR(bs2) && bar) {
      Logf("bar0_descriptor: desc=%#x len=%u res_type=%u addr_range_min=%#llx addr_range_max=%#llx addr_translation_offset=%#llx addr_len=%#llx",
           bar->Desc, bar->Len, bar->ResType, (unsigned long long)bar->AddrRangeMin, (unsigned long long)bar->AddrRangeMax,
           (unsigned long long)bar->AddrTranslationOffset, (unsigned long long)bar->AddrLen);
      Bs->FreePool(bar);
    } else {
      Logf("bar0_descriptor: GetBarAttributes status=%#llx", (unsigned long long)bs2);
    }
  }

  /* ===== Part 2: protocol-ownership graph (GPU/GOP/USB-host device paths,
   * the three BY_DRIVER agents, their identity and section hashes) ===== */

  {
    UINT8 pathbuf[GU_DEVPATH_MAX_BYTES];
    UINTN n = GuCaptureDevicePath(PciHandle, pathbuf, sizeof(pathbuf));
    GuDescribeDevicePath("gpu_pci_device_path", pathbuf, n, Logf);
  }
  DumpProtocols("gpu_pci", PciHandle, Bs);

  EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
  EFI_HANDLE *GopHandles = NULL;
  UINTN GopCount = 0;
  EFI_HANDLE PhysicalGop = NULL;
  UINTN PhysicalCount = 0;
  EFI_STATUS s = Bs->LocateHandleBuffer(ByProtocol, &GopGuid, NULL, &GopCount, &GopHandles);
  if (s == EFI_SUCCESS && GopCount && GopCount <= 16) {
    for (UINTN i = 0; i < GopCount; i++) {
      EFI_GRAPHICS_OUTPUT_PROTOCOL *g = NULL;
      if (EFI_ERROR(Bs->HandleProtocol(GopHandles[i], &GopGuid, (VOID **)&g)) || !g) continue;
      BOOLEAN physical = g->Mode && g->Mode->FrameBufferBase && g->Mode->FrameBufferSize;
      Logf("GOP handle[%u]=%#llx physical=%u", (unsigned)i, (unsigned long long)(UINTN)GopHandles[i], physical);
      if (physical && g->Mode->Info)
        Logf("  resolution=%ux%u pixel_format=%u framebuffer=%#llx size=%#llx",
             g->Mode->Info->HorizontalResolution, g->Mode->Info->VerticalResolution, g->Mode->Info->PixelFormat,
             (unsigned long long)(UINTN)g->Mode->FrameBufferBase, (unsigned long long)g->Mode->FrameBufferSize);
      UINT8 pathbuf[GU_DEVPATH_MAX_BYTES];
      char label[32];
      LabelN(label, sizeof(label), "GOP_device_path[", i, "]");
      UINTN n = GuCaptureDevicePath(GopHandles[i], pathbuf, sizeof(pathbuf));
      GuDescribeDevicePath(label, pathbuf, n, Logf);
      if (physical) { PhysicalGop = GopHandles[i]; PhysicalCount++; }
    }
  } else {
    Logf("GOP enumeration: status=%#llx count=%zu (not walked -- 0 or implausibly many)", (unsigned long long)s, (size_t)GopCount);
  }
  if (GopHandles) Bs->FreePool(GopHandles);
  if (PhysicalCount != 1) { Logf("physical GOP handles found: %zu (need exactly 1 to identify out/gfx agents)", (size_t)PhysicalCount); PhysicalGop = NULL; }
  if (PhysicalGop) DumpProtocols("physical_gop", PhysicalGop, Bs);

  /* USB host controller device path, located the same structural way
   * ad104-msi/msi_release.c's UsbBaseline does: the EFI_USB2_HC_PROTOCOL_GUID
   * edge where the controller manages itself (BY_DRIVER, controller==handle).
   * This GUID value is the standard UEFI spec constant, not board-specific. */
  EFI_GUID UsbHcGuid = {0x3e745226, 0x9818, 0x45b6, {0xa2, 0xac, 0xd7, 0xcd, 0x0e, 0x8b, 0xa2, 0xbc}};

  GU_GRAPH Graph;
  EFI_STATUS GraphStatus = GuSnapshotGraph(Bs, &Graph);
  Logf("graph snapshot: status=%#llx edges=%zu", (unsigned long long)GraphStatus, (size_t)Graph.count);
  if (GraphStatus != EFI_SUCCESS) Logf("WARNING: graph snapshot incomplete -- the rest of this log reflects a partial view");

  UINTN UsbHostCandidates = 0;
  for (UINTN i = 0; i < Graph.count; i++) {
    GU_EDGE *e = &Graph.edges[i];
    if (GuGuidEqual(&e->protocol, &UsbHcGuid) && e->attributes == 0x10 && e->controller == e->handle) {
      UsbHostCandidates++;
      Logf("usb_host candidate[%zu] handle=%#llx agent=%#llx", UsbHostCandidates - 1,
           (unsigned long long)(UINTN)e->handle, (unsigned long long)(UINTN)e->agent);
      UINT8 pathbuf[GU_DEVPATH_MAX_BYTES];
      char label[48];
      LabelN(label, sizeof(label), "usb_host_device_path[", UsbHostCandidates - 1, "]");
      UINTN n = GuCaptureDevicePath(e->handle, pathbuf, sizeof(pathbuf));
      GuDescribeDevicePath(label, pathbuf, n, Logf);
    }
  }
  if (!UsbHostCandidates) Logf("usb_host: no self-managed EFI_USB2_HC_PROTOCOL_GUID edge found");

  EFI_GUID PciGuid = EFI_PCI_IO_PROTOCOL_GUID;
  EFI_GUID TextGuid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
  /* EFI_EDID_DISCOVERED_PROTOCOL_GUID -- standard UEFI/EDK2 spec constant
   * (MdeModulePkg/Include/Protocol/EdidDiscovered.h), no vendored named
   * macro in this tree's minimal EDK2 header subset, same situation as
   * UsbHcGuid/PointerGuid below. Added 2026-11-11 after the first two cold
   * boots found no out_agent (no SimpleTextOut BY_DRIVER on the physical
   * GOP at all -- this board's console architecture publishes it directly
   * on the console handle instead) but DID find a second, previously-
   * unidentified agent bound BY_DRIVER to this exact protocol on the
   * physical GOP, stable across both runs. */
  EFI_GUID EdidDiscoveredGuid = {0x1c0c34f6, 0xd380, 0x41fa, {0xa0, 0x49, 0x8a, 0xd0, 0x6c, 0x1a, 0x66, 0xaa}};
  EFI_HANDLE NvAgent = NULL, OutAgent = NULL, GfxAgent = NULL, EdidAgent = NULL;
  UINTN NvCount = GuCountEdges(&Graph, PciHandle, &PciGuid, 0x10, &NvAgent);
  Logf("nv_agent: %zu BY_DRIVER edge(s) on the GPU's PCI_IO protocol", NvCount);
  if (PhysicalGop) {
    UINTN OutCount = GuCountEdges(&Graph, PhysicalGop, &TextGuid, 0x10, &OutAgent);
    UINTN GfxCount = GuCountEdges(&Graph, PhysicalGop, &GopGuid, 0x10, &GfxAgent);
    UINTN EdidCount = GuCountEdges(&Graph, PhysicalGop, &EdidDiscoveredGuid, 0x10, &EdidAgent);
    Logf("out_agent: %zu BY_DRIVER edge(s) on the physical GOP's SimpleTextOut protocol", OutCount);
    Logf("gfx_agent: %zu BY_DRIVER edge(s) on the physical GOP's GOP protocol", GfxCount);
    Logf("edid_agent: %zu BY_DRIVER edge(s) on the physical GOP's EDID_DISCOVERED protocol", EdidCount);
  } else {
    Logf("out_agent/gfx_agent/edid_agent: skipped (no single physical GOP handle)");
  }

  DumpDriver("nv", NvAgent, Bs);
  DumpDriver("out", OutAgent, Bs);
  DumpDriver("gfx", GfxAgent, Bs);
  DumpDriver("edid", EdidAgent, Bs);

  /* ===== Consumers()-equivalent tally, read-only: the exact classification
   * a future ga102_release.c's Consumers() will do on every boot (see the
   * AD103 reference's z790_release.c), run here once as diagnostics so any
   * surprise (e.g. out_agent above coming back unidentified) can be traced
   * to a specific edge instead of guessed at from the raw dump. Reports
   * only; does not gate or fail anything. */
  {
    EFI_GUID PathGuid2 = EFI_DEVICE_PATH_PROTOCOL_GUID;
    UINTN pciTally = 0, childTally = 0, textTally = 0, virtualTally = 0, gfxTally = 0, edidTally = 0, pciOther = 0, gopOther = 0;
    for (UINTN i = 0; i < Graph.count; i++) {
      GU_EDGE *e = &Graph.edges[i];
      if (e->handle == PciHandle) {
        if (GuGuidEqual(&e->protocol, &PciGuid) && e->attributes == 0x10 && e->controller == PciHandle) pciTally++;
        else if (GuGuidEqual(&e->protocol, &PathGuid2) && e->attributes == 8) {
          childTally++;
          Logf("consumers: pci child[%zu] controller=%#llx", childTally - 1, (unsigned long long)(UINTN)e->controller);
        } else {
          pciOther++;
          Logf("consumers: UNCLASSIFIED pci-edge agent=%#llx controller=%#llx attributes=%#x count=%u",
               (unsigned long long)(UINTN)e->agent, (unsigned long long)(UINTN)e->controller, e->attributes, e->count);
          LogGuidField("consumers: unclassified_pci_edge.protocol", &e->protocol);
        }
      } else if (PhysicalGop && e->handle == PhysicalGop) {
        if (GuGuidEqual(&e->protocol, &TextGuid) && e->attributes == 0x10 && e->controller == PhysicalGop) textTally++;
        else if (GuGuidEqual(&e->protocol, &TextGuid) && e->attributes == 8 && e->controller == Table->ConsoleOutHandle) virtualTally++;
        else if (GuGuidEqual(&e->protocol, &GopGuid) && e->attributes == 0x10 && e->controller == PhysicalGop) gfxTally++;
        else if (GuGuidEqual(&e->protocol, &EdidDiscoveredGuid) && e->attributes == 0x10 && e->controller == PhysicalGop) edidTally++;
        else {
          gopOther++;
          Logf("consumers: UNCLASSIFIED gop-edge agent=%#llx controller=%#llx attributes=%#x count=%u",
               (unsigned long long)(UINTN)e->agent, (unsigned long long)(UINTN)e->controller, e->attributes, e->count);
          LogGuidField("consumers: unclassified_gop_edge.protocol", &e->protocol);
        }
      }
    }
    Logf("consumers: tallies pci=%zu child=%zu text=%zu virtual=%zu gfx=%zu edid=%zu pci_other=%zu gop_other=%zu",
         pciTally, childTally, textTally, virtualTally, gfxTally, edidTally, pciOther, gopOther);
  }

  Logf("full graph dump follows (%zu edges, full protocol GUIDs)", (size_t)Graph.count);
  for (UINTN i = 0; i < Graph.count; i++) {
    GU_EDGE *e = &Graph.edges[i];
    Logf("edge[%zu] handle=%#llx agent=%#llx controller=%#llx attributes=%#x count=%u",
         i, (unsigned long long)(UINTN)e->handle, (unsigned long long)(UINTN)e->agent,
         (unsigned long long)(UINTN)e->controller, e->attributes, e->count);
    char label[32];
    LabelN(label, sizeof(label), "edge[", i, "].protocol");
    LogGuidField(label, &e->protocol);
  }

done:
  Logf("probe finished. Nothing was written to the GPU, PCI config space, disk, ESP, or any driver; no protocol was opened, connected or disconnected.");
  if (LogFile) { LogFile->Flush(LogFile); LogFile->Close(LogFile); }
  Say(L"\r\nDone. Press any key to return to the boot menu.\r\n");
  UINTN idx = 0;
  Bs->WaitForEvent(1, &Table->ConIn->WaitForKey, &idx);
  return EFI_SUCCESS;
}
