/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD103/Z790 cold-boot diagnostic probe -- READ ONLY.
 *
 * Finds the RTX 4070 Ti SUPER (10de:2705), reads its PCI config space and a
 * fixed table of BAR0 registers (the same offsets windows/ad104-msi/profile.c
 * checks before any transaction), then does a live, read-only hash of the
 * BAR0+0x300000 VBIOS aperture and compares it against the pinned PROM hash
 * (the same check GuAd103Prepare does, just without anything downstream of
 * it) -- and writes everything to a log file next to this .EFI on the USB
 * stick it was booted from, plus the console. On a hash mismatch, it also
 * saves the raw 1 MiB it read to the USB stick for offline comparison; this
 * is how a real derivation error (an nvflash dump is not the same bytes as a
 * live pre-driver BAR0 read) was caught and fixed.
 *
 * It never writes to PCI config space, never writes to BAR0, never performs
 * FLR, never allocates DMA, never touches the internal disk, the ESP, or
 * bootmgfw.efi, and never starts Windows itself -- it returns control to the
 * firmware boot menu when done. GuWrite32/GuFlr/GuEnableMaster/GuDmaAllocate
 * and the selector-latch functions from ad104-msi/port.c do not exist in this
 * tree at all (see port_probe.h/.c), so there is no code path that could do
 * any of that even by mistake. AllocatePool/FreePool (boot services, not
 * hardware access) hold the 1 MiB read buffer; everything that touches the
 * GPU itself still goes only through GuConfigRead/GuRead32/the chunked
 * Mem.Read below, none of which can write.
 */
#include "port_probe.h"
#include "checks_probe.h"
#include "format.h"
#include "sha256.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

/* From GuAd103PromMatches() in profile.c -- see that file's header comment
 * for why this must come from a live pre-driver EFI read, not an OS-level
 * dump tool's output. */
#define GU_PROM_EXPECTED_HASH "1a6631d6317739908aa6fb246858306dfef15901b43237b35b73d424b3880d9e"
#define GU_PROM_OFFSET 0x300000U
#define GU_PROM_BYTES 1048576U

static CHAR16 LogBuf[32768];
static UINTN LogLen;
static EFI_SYSTEM_TABLE *Sys;

static VOID Say(const CHAR16 *s) {
  if (Sys && Sys->ConOut) Sys->ConOut->OutputString(Sys->ConOut, (CHAR16 *)s);
}

/* Formats one line (format.h's GuFormatV), widening ASCII->UCS-2 into both the
 * console and the in-memory log that WriteLog() saves at the end. */
static VOID Logf(const char *fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  size_t n = GuFormatV(line, sizeof(line), fmt, args);
  va_end(args);
  if (n == (size_t)-1) return;

  static CHAR16 echo[260];
  UINTN j = 0;
  for (size_t i = 0; line[i] && j + 1 < sizeof(echo) / sizeof(echo[0]); i++) echo[j++] = (CHAR16)(unsigned char)line[i];
  echo[j] = 0;
  Say(echo);
  Say(L"\r\n");

  for (size_t i = 0; line[i] && LogLen + 2 < sizeof(LogBuf) / sizeof(LogBuf[0]); i++)
    LogBuf[LogLen++] = (CHAR16)(unsigned char)line[i];
  if (LogLen + 2 < sizeof(LogBuf) / sizeof(LogBuf[0])) { LogBuf[LogLen++] = L'\r'; LogBuf[LogLen++] = L'\n'; }
}

/* Opens the EFI_FILE_PROTOCOL root of the volume this .EFI was loaded from --
 * never the internal disk/ESP, only wherever this image itself is running
 * from (the USB stick). Shared by WriteLog() and WriteBin() below. */
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
    EFI_FILE_PROTOCOL *Probe = NULL;
    EFI_STATUS s = Root->Open(Root, &Probe, NameOut, EFI_FILE_MODE_READ, 0);
    if (s == EFI_NOT_FOUND) return;
    if (!EFI_ERROR(s)) Probe->Close(Probe);
  }
}

/* Writes LogBuf to AD103-PROBE-<n>.TXT. Best-effort: a write failure is
 * reported on console, never retried against a different volume or path. */
static VOID WriteLog(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs) {
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  if (EFI_ERROR(OpenBootVolumeRoot(Image, Bs, &Root))) return;
  CHAR16 name[48];
  NextFreeName(Root, L"AD103-PROBE-", L".TXT", name);
  EFI_STATUS s = Root->Open(Root, &File, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(s) || !File) { Say(L"log: could not create log file\r\n"); Root->Close(Root); return; }
  UINTN bytes = LogLen * sizeof(CHAR16);
  s = File->Write(File, &bytes, LogBuf);
  File->Close(File);
  Root->Close(Root);
  if (EFI_ERROR(s)) { Say(L"log: write failed\r\n"); return; }
  Say(L"Log written: "); Say(name); Say(L"\r\n");
}

/* Writes a raw binary buffer to AD103-PROM-<n>.BIN -- used only when the live
 * PROM hash doesn't match, so the actual bytes can be compared offline. */
static VOID WriteBin(EFI_HANDLE Image, EFI_BOOT_SERVICES *Bs, const VOID *Data, UINTN Bytes) {
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  if (EFI_ERROR(OpenBootVolumeRoot(Image, Bs, &Root))) return;
  CHAR16 name[48];
  NextFreeName(Root, L"AD103-PROM-", L".BIN", name);
  EFI_STATUS s = Root->Open(Root, &File, name, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (EFI_ERROR(s) || !File) { Say(L"prom: could not create capture file\r\n"); Root->Close(Root); return; }
  UINTN wrote = Bytes;
  s = File->Write(File, &wrote, (VOID *)Data);
  File->Close(File);
  Root->Close(Root);
  if (EFI_ERROR(s) || wrote != Bytes) { Say(L"prom: capture write failed\r\n"); return; }
  Say(L"Raw PROM capture written: "); Say(name); Say(L"\r\n");
}

static EFI_STATUS Discover(EFI_BOOT_SERVICES *Bs, UINTN *Segment, UINTN *Bus, UINTN *Device, UINTN *Function) {
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
    if (s == EFI_SUCCESS && id == GU_BOARD_ID) { found++; io = p; }
  }
  if (h) Bs->FreePool(h);
  if (s != EFI_SUCCESS || found != 1 || !io) return EFI_UNSUPPORTED;
  if (!io->GetLocation || EFI_ERROR(io->GetLocation(io, Segment, Bus, Device, Function))) return EFI_UNSUPPORTED;
  Gu.pci = io;
  return EFI_SUCCESS;
}

/* The exact offsets windows/ad104-msi/profile.c's Registers() reads, plus the
 * selector pair (0x82381c/0x823820) -- same architecture, first-time capture
 * for this board. Printed as-is; no expected values to compare against yet. */
static const UINT32 RegisterOffsets[] = {
    0, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828, 0x1fa7cc, 0x1fa7c4,
    0x9a0148, 0x823804, 0x823814, 0x823818, 0x1183a4, 0x625f04, 0x1fa7c8,
    0x1fa82c, 0x1fa830, 0x824148, 0x8241e0, 0x82381c, 0x823820};

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table) {
  Sys = Table;
  if (!Image || !Table || !Table->BootServices) return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *Bs = Table->BootServices;

  Say(L"\r\nAD103/Z790 cold-boot diagnostic probe -- read only, no GPU writes.\r\n");
  Logf("probe started");

  UINTN seg = 0, bus = 0, dev = 0, fun = 0;
  if (EFI_ERROR(Discover(Bs, &seg, &bus, &dev, &fun))) {
    Logf("device 10de:2705 (GU_BOARD_ID=%#x) not found via PCI IO protocol enumeration", GU_BOARD_ID);
    WriteLog(Image, Bs);
    Say(L"Press any key to return to the boot menu.\r\n");
    UINTN waitIdx = 0;
    Bs->WaitForEvent(1, &Table->ConIn->WaitForKey, &waitIdx);
    return EFI_NOT_FOUND;
  }
  Logf("found 10de:2705 at segment=%u bus=%u device=%u function=%u", seg, bus, dev, fun);

  GuInit(Bs, Gu.pci);

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

  Logf("registers done, saving log before the VBIOS read");
  WriteLog(Image, Bs);

  if (Gu.fault) {
    Logf("prom: skipped, BAR reads faulted");
  } else {
    VOID *prom = NULL;
    EFI_STATUS s = Bs->AllocatePool(EfiLoaderData, GU_PROM_BYTES, &prom);
    if (EFI_ERROR(s) || !prom) {
      Logf("prom: could not allocate %u bytes", GU_PROM_BYTES);
    } else {
      /* Chunked Mem.Read, same 1024-byte chunking ad104-msi's own
       * GuCopyFromIo uses for this same kind of large BAR0 read. BAR-relative
       * (BarIndex 0, offset GU_PROM_OFFSET) -- no absolute physical address
       * needed, same as every other read in this probe. */
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
        Logf("prom_expected=%s", GU_PROM_EXPECTED_HASH);
        BOOLEAN match = GuHashMatches(prom, GU_PROM_BYTES, GU_PROM_EXPECTED_HASH);
        Logf("prom_match=%u", match);
        if (!match) {
          Logf("prom: MISMATCH, saving the raw 1 MiB read to the USB stick for comparison");
          WriteBin(Image, Bs, prom, GU_PROM_BYTES);
        }
      }
      Bs->FreePool(prom);
    }
  }

  Logf("probe finished. Nothing was written to the GPU, PCI config space, disk, or ESP.");
  WriteLog(Image, Bs);
  Say(L"\r\nDone. Press any key to return to the boot menu.\r\n");
  UINTN idx = 0;
  Bs->WaitForEvent(1, &Table->ConIn->WaitForKey, &idx);
  return EFI_SUCCESS;
}
