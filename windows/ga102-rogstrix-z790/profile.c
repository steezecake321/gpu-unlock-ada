/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * RTX 3090 Ti (10de:2203, GA102) on ASUS ROG STRIX Z790-A GAMING WIFI D4.
 * Adapted from windows/ad103-z790-en-REFERENCE/profile.c, which pins a
 * different card/board/generation (AD103/Ada, not GA102/Ampere).
 *
 * STATUS (2026-11-10): Config()/Registers()/GuGa102PromMatches()/
 * GuGa102FwsecMatches() below are now filled in from this machine's own
 * real, live, pre-driver captures (probe/, run twice across two separate
 * genuine cold boots -- power fully off, Fast Startup disabled -- and
 * byte-identical both times: GA102-DIAG-0.TXT/GA102-DIAG-1.TXT,
 * GA102-PROM-0.BIN/GA102-PROM-1.BIN). This is NOT yet hardware-qualified --
 * nothing has executed a transaction, written to the GPU, or performed FLR.
 *
 * GuGa102AssetsValid() is now also real: GA102's firmware assets
 * (BOOTER_FILE_GA102, V67_FILE_GA102, BOOT_FILE_GA102, BOOT_DESC_FILE_GA102;
 * GSP_FILE is shared with AD102/Ada) were generated locally per
 * ../../firmware/README.md on 2026-11-10 and are hash-verified below, same
 * five-entry pattern as the AD103 reference's own Pins[] table. The pinned
 * nvidia-utils-610.43.03 package the manifest originally pointed at
 * (archive.cachyos.org) had been pruned from that rolling archive by the
 * time this ran (same thing happened building the AD103 reference) --
 * `gsp_ga10x.bin` was instead extracted from the identical-version package
 * at Arch Linux Archive (archive.archlinux.org, the permanent, unpruned
 * official Arch package archive; same upstream NVIDIA firmware blob, a
 * different distro's repackaging of it) and confirmed byte-identical
 * against the manifest's own already-pinned GSP_SHA256 before use -- an
 * independent hash of the firmware content itself, not of either outer
 * package, so this substitution changes nothing about what's actually
 * trusted here. `booter_load_ga102.bin`/`gsp_boot_ga102.bin`/
 * `gsp_boot_desc_ga102.bin` came from the NVIDIA kernel-module source
 * archive (fetched fine from its original pinned URL, no substitution
 * needed) via `firmware/extract_boot_assets.py --chip ga102`. All 5 files'
 * hashes below match `firmware/artifacts-610.43.03.json`'s existing pins
 * exactly. Kept local only (`firmware/gpu-unlock/.gitignore`), never
 * committed or redistributed -- GPL firmware-redistribution rule.
 *
 * Captured so far (2026-11-10):
 *   GPU:     NVIDIA GeForce RTX 3090 Ti, 10de:2203, subsystem 3842:4985
 *   Board:   ASUSTeK ROG STRIX Z790-A GAMING WIFI D4
 *   ESP:     disk 2 ("SPCC M.2 PCIe SSD"), partition 1, 100 MiB, GUID in
 *            local/boot_profile.h (gitignored, see that file's own comment)
 *
 * Config()/Registers(), captured twice across two separate genuine cold
 * boots (2026-11-10): found 10de:2203 at segment=0 bus=1 device=0
 * function=0; GuCheckConfigProbe=1 pm=0x60 pcie=0x78 pcie_version=2
 * msi=0x68 msix=0x0; BAR0 base 0x84000000 (config dword 4, low 4
 * type/prefetch bits already 0). Across both runs, every one of the 19
 * fixed register offsets, every checked config dword, SS0 AND SS1 all read
 * byte-identical -- unlike the AD103 reference's own SS0/SS1 history (see
 * its profile.c header and ../SS1_FINDINGS_2026-11-10.md), neither selector
 * has yet been observed to vary on this card across cold boots. Per
 * SEED.md lesson #2, this is NOT treated as license to pin a literal value
 * anyway: two runs is not enough evidence that it never varies, and the
 * AD103 reference's own SS0 varied between its first two captured sessions
 * while SS1 did not vary until a later, third run exposed it too. SS0/SS1
 * are both handled the same structural-only way as the AD103 reference
 * (reject only unambiguous bad/unlocked readings, never require a specific
 * "correct" value) -- see Registers() below.
 *
 * PROM: live, read-only, pre-driver BAR0+0x300000 capture by probe/, 1 MiB,
 * sha256 confirmed directly against the saved file (not just the printed
 * log line) on both runs: d554535071e2eb9e8829dcac05818197022e71f594cd36f
 * 865bd416a2d5aaaa7. Per the AD103 reference's own IMPORTANT CORRECTION,
 * this MUST come from a live pre-driver EFI read, never an OS-level dump
 * tool (e.g. nvflash) -- which is exactly what probe/ did here.
 *
 * FWSEC/FRTS geometry: re-derived locally with tools/fwsec_dump.c against
 * this board's own GA102-PROM-0.BIN (not assumed, not copied from AD103 --
 * a different chip generation's VBIOS has entirely different image/data/
 * sig offsets and even a different ucode/signature scheme in general; here
 * ucode_id and engine_mask happened to match AD103's own values, which is
 * plausible since FWSEC/Booter is shared SEC2 firmware code across this
 * firmware family, not VBIOS-specific -- but every field below is pinned
 * from this board's own parse regardless of what happened to match).
 *
 * NOT YET DONE -- everything past the read-only verification gate:
 * app.c/boot_io.c/a Z790-specific ownership module. boot_io.c specifically
 * is what populates GuAssets[] from disk at boot (see the AD103
 * reference's own boot_io.c for the pattern) -- until it's written,
 * GuAssets[] stays empty and GuGa102AssetsValid() correctly fails closed
 * on `!GuAssets[i].name` even though the firmware files themselves now
 * exist and are hash-verified. Not a buildable unlock attempt yet either
 * way, for that reason.
 *
 * See docs/How Ampere was unlocked.md for this generation's exploit and
 * register semantics; SEED.md for the process lessons this file follows.
 */
#include "profile.h"
#include "checks.h"
#include "sha256.h"
#include "compat/compat.h"
/* GuLog is declared by port.h, already pulled in transitively via
 * profile.h -- no separate include needed here (confirmed by this port's
 * own Build.ps1 compile, not by inspection). Every GuLog call below only
 * reports a comparison already being made; none changes what passes or
 * fails. */

/* Architecture-pinned Ampere/GA10x Booter+v67 payload (see
 * docs/How Ampere was unlocked.md) -- NOT board-specific, generated locally
 * 2026-11-10 per ../../firmware/README.md and hash-verified against
 * ../../firmware/artifacts-610.43.03.json's existing pins (see header
 * comment for the one source substitution that needed, and why it doesn't
 * weaken this check). Order matches boot_io.c's own load order exactly
 * (booter, v67 payload, gsp, boot image, boot descriptor) -- same pattern
 * as the AD103 reference's Pins[5]. */
static const struct {
  const char *name, *hash;
  UINTN bytes;
} Pins[5] = {
 {"gpu-unlock/610.43.03/booter_load_ga102.bin","056cbc064a888f80717726c54d7f64fcd68044d123870c95a74f37f4975dcaa4",61256},
 {"gpu-unlock/610.43.03/v67-ga102-610.43.03.bin","2a0aa1f86d0b6afff5214d641d87d46dc98ef06ed908ff05d0ddbad5ffb7e197",64000},
 {"gpu-unlock/610.43.03/gsp_ga10x.bin","572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a",84277400},
 {"gpu-unlock/610.43.03/gsp_boot_ga102.bin","3df651acb174fd5f5561a1996965b192f50c5d763168446ed0d4ead19d895768",24576},
 {"gpu-unlock/610.43.03/gsp_boot_desc_ga102.bin","cc5a3fc1f4c47f2fe107b1b68f0176d8a448e6dec84b575e5560ccef5262bf15",84}
};
BOOLEAN GuGa102AssetsValid(VOID) {
  for (UINTN i = 0; i < 5; i++)
    if (!GuAssets[i].name || strcmp(GuAssets[i].name, Pins[i].name) ||
        GuAssets[i].firmware.size != Pins[i].bytes ||
        !GuHashMatches(GuAssets[i].firmware.data, Pins[i].bytes, Pins[i].hash))
      return FALSE;
  return TRUE;
}

/* From a live, read-only, pre-driver capture of BAR0+0x300000 by probe/,
 * confirmed byte-identical across two separate genuine cold boots
 * (2026-11-10) and hash-confirmed directly against the saved 1 MiB file. */
BOOLEAN GuGa102PromMatches(const VOID *Data, UINTN Bytes) {
  return Bytes == 1048576 && GuHashMatches(Data, Bytes,
    "d554535071e2eb9e8829dcac05818197022e71f594cd36f865bd416a2d5aaaa7");
}

/* Re-derived with tools/fwsec_dump.c against this board's own
 * GA102-PROM-0.BIN (2026-11-10). */
BOOLEAN GuGa102FwsecMatches(const struct gpu_unlock_fwsec *f) {
  return f && f->image_off == 0x41758 && f->image_size == 0xea00 &&
    f->imem_pa == 0 && f->imem_size == 0xe200 && f->imem_va == 0 &&
    f->dmem_pa == 0 && f->data_off == 0xe200 && f->dmem_size == 0x800 &&
    f->pkc_off == 0x5a4 && f->interface_off == 0x1c && f->ucode_id == 9 &&
    f->engine_mask == 0x400 && f->sig_count == 3 && f->sig_versions == 7 &&
    f->sig_off == 0x412d8 && f->sig_bytes == 1152;
}

static BOOLEAN Config(VOID) {
  UINT32 c[64]; UINTN s, b, d, f; GU_CAPS caps;
  if (!Gu.pci || !Gu.pci->GetLocation || !Gu.pci->Pci.Read ||
      Gu.bar_base != GU_BAR0_BASE || Gu.bar_size != 0x1000000 ||
      EFI_ERROR(Gu.pci->GetLocation(Gu.pci, &s, &b, &d, &f))) {
    GuLog("Config: FAIL early (pci=%#llx bar_base=%#llx bar_size=%#llx GetLocation)\n",
      (unsigned long long)(UINTN)Gu.pci, (unsigned long long)Gu.bar_base, (unsigned long long)Gu.bar_size);
    return FALSE;
  }
  for (UINTN i = 0; i < 64; i++)
    if (EFI_ERROR(GuConfigRead(EfiPciIoWidthUint32, (UINT32)i * 4, &c[i]))) {
      GuLog("Config: FAIL config-space read at dword %u\n", (unsigned)i);
      return FALSE;
    }
  /* This board's own captured dwords -- not copied from the AD103
     reference (c[6]/c[8]/c[9] differ there; c[5]/c[7] happen to match,
     pinned from our own capture regardless). GuCheckConfig itself also
     requires Bus==1, which this board's capture satisfies (segment=0
     bus=1 device=0 function=0). */
  BOOLEAN checkConfig = GuCheckConfig(c, s, b, d, f, &caps);
  if (!checkConfig || c[2] != 0x030000a1 || c[4] != GU_BAR0_BASE || c[5] != 0xc ||
      c[6] != 0x40 || c[7] != 0xc || c[8] != 0x48 || c[9] != 0x5001) {
    GuLog("Config: FAIL GuCheckConfig=%u seg=%u bus=%u dev=%u fun=%u c[2]=%#x(want 0x30000a1) c[4]=%#x(want %#llx) c[5]=%#x(want 0xc) c[6]=%#x(want 0x40) c[7]=%#x(want 0xc) c[8]=%#x(want 0x48) c[9]=%#x(want 0x5001)\n",
      checkConfig, (unsigned)s, (unsigned)b, (unsigned)d, (unsigned)f, c[2], c[4], (unsigned long long)GU_BAR0_BASE, c[5], c[6], c[7], c[8], c[9]);
    return FALSE;
  }
  /* Gu.*_cap was set by app.c's Discover() from this exact same capability
     walk over unchanging config-space bytes, so comparing against it alone
     only checks self-consistency (did anything change since Discover ran),
     not an independent pin -- same gap the AD103 reference's own Config()
     has. Literal values below are this board's own captured capability
     offsets (same numeric layout as AD103 happened to capture, but pinned
     from our own run, not copied). */
  BOOLEAN capsOk = caps.pm == Gu.pm_cap && caps.pcie == Gu.pcie_cap &&
    caps.pcie_version == Gu.pcie_version && caps.msi == Gu.msi_cap && caps.msix == Gu.msix_cap &&
    caps.pm == 0x60 && caps.pcie == 0x78 && caps.pcie_version == 2 && caps.msi == 0x68 && caps.msix == 0;
  if (!capsOk)
    GuLog("Config: FAIL caps pm=%#x(Gu=%#x,want 0x60) pcie=%#x(Gu=%#x,want 0x78) pcie_ver=%u(Gu=%u,want 2) msi=%#x(Gu=%#x,want 0x68) msix=%#x(Gu=%#x,want 0)\n",
      caps.pm, Gu.pm_cap, caps.pcie, Gu.pcie_cap, caps.pcie_version, Gu.pcie_version, caps.msi, Gu.msi_cap, caps.msix, Gu.msix_cap);
  return capsOk;
}

static BOOLEAN Registers(UINT32 *Ss0, UINT32 *Ss1) {
  static const UINT32 offsets[] = {0, 0x118128, 0x118234, 0x110100, 0x1fa824, 0x1fa828,
    0x1fa7cc, 0x1fa7c4, 0x9a0148, 0x823804, 0x823814, 0x823818, 0x1183a4, 0x625f04, 0x1fa7c8, 0x1fa82c, 0x1fa830, 0x824148, 0x8241e0};
  static const UINT32 values[] = {0xb72000a1, 0x8b8f, 0x3ff, 0x10, 0x1ffffe00, 0,
    0x4cb8f, 0x4cb8f, 0xffffff0f, 0xffffff8f, 0x233, 0x400080, 0x5ff4, 1, 0x45cb8f, 0x100000, 0x5cf3ff0, 1, 3};
  for (UINTN i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
    UINT32 v = GuRead32(offsets[i]);
    if (v != values[i] || Gu.fault) {
      GuLog("Registers: FAIL offset=%#x got=%#x want=%#x fault=%u\n", offsets[i], v, values[i], Gu.fault);
      return FALSE;
    }
  }
  *Ss0 = GuRead32(0x82381c); *Ss1 = GuRead32(0x823820);
  /* SS0/SS1: not pinned to a literal even though both read byte-identical
     across the two cold boots captured so far (0x12100052 / 0x6) -- see
     header comment. Structural check only, same pattern as the AD103
     reference: `!(Ss1 & 0x8)` rather than `!=<observed value>`, so a
     partially- or fully-unlocked state (bit 3 set) is rejected regardless
     of which locked value this card happens to show on a given cold boot. */
  BOOLEAN ok = !Gu.fault && *Ss0 != 0xffffffff && (*Ss0 & 0xffff0000) != 0xbadf0000 &&
    *Ss0 != 0x88888888 && !(*Ss1 & 0x8);
  if (!ok)
    GuLog("Registers: FAIL ss0=%#x ss1=%#x (unlock bit must be clear) fault=%u\n", *Ss0, *Ss1, Gu.fault);
  return ok;
}

BOOLEAN GuGa102ColdEntry(VOID) {
  UINT32 ss0, ss1;
  return !Gu.started && !Gu.fault && !Gu.retained && Config() &&
    Registers(&ss0, &ss1) && GuEntrySelectorsMatch(ss0, ss1);
}

EFI_STATUS GuGa102Prepare(VOID) {
  UINT32 ss0, ss1, after0, after1; VOID *prom = NULL; struct gpu_unlock_fwsec fw;
  if (Gu.started || Gu.fault || Gu.retained || Gu.allocations || Gu.profile_verified ||
      Gu.prom_verified || !Config() || !Registers(&ss0, &ss1))
    return EFI_NOT_READY;
  if (!GuGa102AssetsValid())
    return EFI_CRC_ERROR; /* expected for now: GuAssets[] is still empty,
                              boot_io.c doesn't exist yet -- see header */
  prom = GuAlloc(1048576);
  if (!prom) return EFI_OUT_OF_RESOURCES;
  GuCopyFromIo(prom, (VOID *)(UINTN)(Gu.bar_base + 0x300000), 1048576);
  BOOLEAN valid = !Gu.fault && GuGa102PromMatches(prom, 1048576) &&
    !gpu_unlock_vbios_fwsec(prom, 1048576, &fw) && GuGa102FwsecMatches(&fw) &&
    Config() && Registers(&after0, &after1) && ss0 == after0 && ss1 == after1;
  GuFree(prom);
  if (!valid) return EFI_COMPROMISED_DATA;
  Gu.prom_verified = TRUE;
  if (!GuLatchEntrySelectors(ss0, ss1)) { Gu.prom_verified = FALSE; return EFI_NOT_READY; }
  Gu.profile_verified = TRUE;
  return EFI_SUCCESS;
}
