/* SPDX-License-Identifier: GPL-2.0-only */
/* Adapted from windows/ad104-msi/app.c. Changes: z790_release.h instead of
 * msi_release.h; GuAd104Prepare/GuAd104ColdEntry -> GuAd103Prepare/
 * GuAd103ColdEntry; GuMsi* -> GuZ790* throughout; the pci_dev literal's
 * .device is this board's real GPU ID (0x2705, RTX 4070 Ti SUPER/AD103 --
 * GU_AD103_RTX_4070_TI_SUPER in vendor/arch_610.h, not ad104-msi's 0x2782)
 * so vendor/unlock_610.c's own unlock_assets()/is_ad102() dispatch picks
 * the AD102-family Booter profile, which it already does natively for this
 * exact id -- see profile.c's header comment. Everything else, including
 * Discover()'s c[2]==0x030000a1 check (common across AD10x, confirmed
 * against this board's own capture earlier in this port), is unchanged.
 *
 * v0.1.1 (2026-11-10, owner's explicit request): auto-unlock mode for a
 * true one-shot-reboot flow (bcdedit bootsequence -> this -> back to
 * Windows, no console interaction required). GuBootChoose() in boot_io.c
 * now defaults to unlock after a 5s window instead of defaulting to skip
 * after 30s (S/Esc still bypasses to stock Windows instantly, U still
 * confirms instantly) -- see boot_io.c's header for the full rationale.
 * A watchdog (GuBootArmWatchdog/GuBootDisarmWatchdog, boot_io.c) is armed
 * for the silent-screen transaction window below and disarmed on every
 * exit from it, as the safety net an unattended run needs in place of the
 * operator who used to be required to watch for a hang.
 *
 * v0.1.2 (2026-11-10, owner's explicit request): optional GU_SIGNED_BUILD
 * compile flag (set by Build-Experimental.ps1 -Signed) flips the
 * SecureBoot check below so a locally self-signed, firmware-enrolled build
 * can run WITH Secure Boot on instead of requiring it off -- see
 * SECURE_BOOT.md for the key-generation/enrollment procedure. The default,
 * unflagged build is byte-for-byte the same reviewed unsigned-requires-SB-
 * off check as before; nothing about it changed. */
#include "port.h"
#include "checks.h"
#include "profile.h"
#include "z790_release.h"
#include "boot_io.h"
#include "compat/compat.h"
#include "vendor/unlock_610.h"
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID;
static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,0x03,0x2b,0x8c}};
static EFI_STATUS Discover(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  EFI_HANDLE *h=NULL,target=NULL;EFI_PCI_IO_PROTOCOL *io=NULL;UINTN count=0,found=0;
  EFI_STATUS s=t->BootServices->LocateHandleBuffer(ByProtocol,&PciGuid,NULL,&count,&h);
  if(s==EFI_SUCCESS&&(!h||!count||count>1024))s=EFI_UNSUPPORTED;
  for(UINTN i=0;s==EFI_SUCCESS&&i<count;i++) {
    EFI_PCI_IO_PROTOCOL *p=NULL;UINT32 id=0;
    s=t->BootServices->HandleProtocol(h[i],&PciGuid,(VOID **)&p);
    if(s!=EFI_SUCCESS||!p||!p->Pci.Read){s=EFI_UNSUPPORTED;break;}
    s=p->Pci.Read(p,EfiPciIoWidthUint32,0,1,&id);
    if(s==EFI_SUCCESS&&id==GU_BOARD_ID){found++;target=h[i];io=p;}
  }
  if(h)t->BootServices->FreePool(h);
  if(s!=EFI_SUCCESS||found!=1||!io)return EFI_UNSUPPORTED;
  GuInit(t->BootServices,io);Gu.image=image;Gu.handle=target;
  Gu.bar_base=GU_BAR0_BASE;Gu.bar_size=0x1000000;
  UINT32 c[64];UINTN seg,bus,dev,fun;GU_CAPS caps;
  if(!io->GetLocation)return EFI_UNSUPPORTED;
  s=io->GetLocation(io,&seg,&bus,&dev,&fun);
  if(s==EFI_SUCCESS)s=io->Pci.Read(io,EfiPciIoWidthUint32,0,64,c);
  if(s!=EFI_SUCCESS||!GuCheckConfig(c,seg,bus,dev,fun,&caps)||c[2]!=0x030000a1)return EFI_UNSUPPORTED;
  Gu.pm_cap=caps.pm;Gu.pcie_cap=caps.pcie;Gu.pcie_version=caps.pcie_version;Gu.msi_cap=caps.msi;Gu.msix_cap=caps.msix;
  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(!image||!t||!t->BootServices||!t->RuntimeServices||!t->RuntimeServices->GetVariable)return EFI_INVALID_PARAMETER;
  UINT8 secure=1;UINTN n=1;UINT32 attrs=0;
  EFI_STATUS s=t->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,&attrs,&n,&secure);
#ifdef GU_SIGNED_BUILD
  /* Signed build (2026-11-10, owner's explicit request, see SECURE_BOOT.md):
   * this binary is Authenticode-signed by a key enrolled into this board's
   * own firmware Secure Boot db, specifically so it can run WITH Secure
   * Boot on -- that's the entire point of signing it. Refuse if SB reports
   * off: this exact binary should never be the thing running in a state
   * where Secure Boot (and everything else it would otherwise block) isn't
   * enforcing. This is the mirror image of the unsigned build's check
   * below, not its removal -- see REVIEW_2026-11-10.md's note on that
   * check for why the *unsigned* build refuses the opposite way. */
  if(s!=EFI_SUCCESS||n!=1||!secure)return EFI_SECURITY_VIOLATION;
#else
  /* Unsigned experimental build: must never run with Secure Boot on -- see
   * REVIEW_2026-11-10.md, "app.c refuses when Secure Boot is on". Firmware
   * should already refuse to load unsigned code under SB; this is the
   * defense-in-depth check in code, not just by convention. */
  if(s!=EFI_SUCCESS||n!=1||secure)return EFI_SECURITY_VIOLATION;
#endif
  s=GuBootOpen(image,t);if(s!=EFI_SUCCESS)return s;
  const char *stage="discover";BOOLEAN unlock=FALSE;
  s=Discover(image,t);if(s!=EFI_SUCCESS)goto failed;
  GuLog("AD103 Z790 EXPERIMENTAL v0.1.1: hardware unvalidated; USB auto-unlock one-shot (5s bypass window, S/Esc for stock Windows); bounded USB enumeration additions/removals; GPU ownership pinned\n");
  stage="assets";s=GuBootLoadAssets();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="read-only-cold-profile";s=GuAd103Prepare();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="stage-windows-image";s=GuBootLoadWindows();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="manual-choice";s=GuBootChoose(&unlock);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!unlock) {
    GuLog("Operator skipped: no GPU transaction, no driver release; stock Windows\n");
    if(Gu.fault||GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
  stage="Z790-preflight";s=GuZ790Preflight(t,image,Gu.handle);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  GuBootSay(L"\r\nChecks passed. Releasing display; watch for Windows.\r\n");
  GuLog("Auto-unlock: one physical attempt; no retries; display callbacks disabled; watchdog armed\n");
  if(Gu.fault){s=EFI_DEVICE_ERROR;goto failed;}
  /* 300s/5min watchdog: this is the only safety net for an unattended run
   * during the silent-screen window below (see boot_io.c's header comment
   * for why auto-unlock needs one). Must be armed before GuBootSilence()
   * since nothing can report a hang once the screen goes dark. Disarmed
   * either right after the transaction finishes cleanly (below) or at the
   * top of `failed:` -- never left armed into GuBootHalt()'s deliberate
   * containment loop, where a watchdog-forced reset would be worse than
   * the documented "power off completely" recovery. */
  stage="arm-watchdog";s=GuBootArmWatchdog(300);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  GuBootSilence();
  stage="Z790-release";s=GuZ790Release();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!GuZ790Released()||!GuAd103ColdEntry()){s=EFI_NOT_READY;goto failed;}
  Gu.display_check=GuZ790Released;Gu.display_ok=TRUE;Gu.armed=TRUE;
  UINT64 mask=0xffffffffffffULL;
  /* .device=0x2705 (RTX 4070 Ti SUPER, AD103) -- this board's real id, not
   * ad104-msi's 0x2782. unlock_610.c's unlock_assets()/is_ad102() already
   * route this exact id to the AD102-family Booter profile upstream. */
  struct pci_dev dev={.vendor=0x10de,.device=0x2705,.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},
    .current_state=PCI_D0,.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  stage="shared-assets";int result=gpu_unlock_610_load_shared(&dev,&shared);
  if(!result) {stage="firmware-transaction";result=gpu_unlock_610_unlock(&dev,&shared);}
  gpu_unlock_610_release_shared(&shared);
  GuLog("Transaction result=%d safe=%u retained=%u started=%u quiesced=%u flrs=%u DMA_live=%u\n",
    result,GuSafeToReturn(),Gu.retained,Gu.started,Gu.quiesced,Gu.flrs,Gu.dma_count);
  if(result||Gu.fault||!GuSafeToReturn()){s=EFI_DEVICE_ERROR;goto failed;}
  stage="release-exclusive-claim";s=GuZ790Finish();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="disarm-watchdog";s=GuBootDisarmWatchdog();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  /* The final pre-Windows check runs after the log closes: this line is not proof of a Windows start. */
  GuLog("TRANSACTION OK: hardware readbacks passed, DMA released; final pre-Windows check follows unlogged; benchmark still required\n");
  stage="close-USB-log";s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="final-Windows-verification";if(!GuZ790ReadyForWindows()){s=EFI_NOT_READY;goto failed;}
  stage="start-Windows";s=GuBootWindows();
  /* A returned Windows image is not a successful handoff. */
  GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
failed:
  /* Best-effort: disarm unconditionally before anything else below, so a
   * watchdog armed above never fires into GuBootHalt()'s containment loop
   * (see the arm-watchdog comment above) and the clean preflight-refused
   * return path isn't left with a watchdog ticking into plain Windows. */
  (VOID)GuBootDisarmWatchdog();
  if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  GuLog("STOP: stage=%s status=%#llx mutated=%u retained=%u fault=%u\n",stage,(unsigned long long)s,GuZ790Mutated(),Gu.retained,Gu.fault);
  if(GuZ790Mutated()||Gu.started||Gu.retained) {
    /* Fault-independent containment, no freeing of unverified live mappings. */
    if(Gu.started||Gu.master_attributes_pending)(VOID)GuClearMaster();
    (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
  }
  GuBootSay(L"\r\nPreflight refused. No driver release or GPU transaction. Use Windows Boot Manager.\r\n");
  (VOID)GuBootClose();return s;
}
