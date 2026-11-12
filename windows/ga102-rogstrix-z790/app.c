/* SPDX-License-Identifier: GPL-2.0-only */
/* Adapted from windows/ad103-z790-en-REFERENCE/app.c. Changes:
 * ga102_release.h instead of z790_release.h; GuGa102Prepare/GuGa102ColdEntry
 * instead of GuAd103Prepare/GuAd103ColdEntry; GuZ790* -> GuGa102Release*
 * throughout; the pci_dev literal's .device is this board's real GPU ID
 * (0x2203, RTX 3090 Ti/GA102 -- not the AD103 reference's 0x2705) so
 * vendor/unlock_610.c's own unlock_assets()/is_ad102() dispatch picks the
 * generic ga102 ops table, which it already does natively for this exact
 * id (GA102 is first-class/non-experimental there -- see profile.c's
 * header comment). Everything else, including Discover()'s
 * c[2]==0x030000a1 check (this board's own captured value too, confirmed
 * earlier in this port, not assumed from the AD103 reference), is
 * unchanged.
 *
 * STATUS (2026-11-10): ga102_release.h/.c, which this file depends on for
 * the actual GPU-ownership release (DisconnectController/exclusive
 * EFI_PCI_IO_PROTOCOL claim -- the first mutating code in this whole
 * port), is currently a DELIBERATE FAIL-CLOSED STUB, not the real
 * ownership-release logic from this board's own ownership-graph capture.
 * This file and boot_io.c are otherwise complete and build/link into a
 * real GA102-EXPERIMENTAL.EFI, but GuGa102Preflight() always
 * refuses, so execution can never reach GuGa102Release() or the real
 * unlock_610 transaction call below -- see ga102_release.c's own header
 * comment for why that's deliberately deferred rather than written here. */
#include "port.h"
#include "checks.h"
#include "profile.h"
#include "ga102_release.h"
#include "boot_io.h"
#include "compat/compat.h"
#include "vendor/unlock_610.h"
#include "format.h"
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
/* 2026-11-11: on-screen diagnostic for the safe-refusal path only (never
 * reached once GuBootSilence() has run -- see GuBootSay()'s own Silent
 * check -- so this never touches the GuGa102Mutated()/started/retained
 * branch below, which already holds the screen forever via GuBootHalt()
 * and must keep doing so, not be shortened to a timed hold). Without
 * this, a stop that happens before anything is logged to screen just
 * flashes and returns to the firmware's boot picker, readable only by
 * pulling the USB stick and reading I:\GA102-UNLOCK\RUNxxxx.TXT
 * afterward -- which is exactly what made an assets-stage refusal look
 * like a load failure on first real-hardware attempt. Widening is plain
 * byte-widen, not a UTF-8 decode: safe here because every %s argument
 * this is ever called with (the `stage` literals in UefiMain below) and
 * every %#llx digit are pure ASCII. */
static size_t Format(char *dst,size_t cap,const char *fmt,...) {
  va_list args;va_start(args,fmt);size_t n=GuFormatV(dst,cap,fmt,args);va_end(args);return n;
}
static VOID SayStage(EFI_SYSTEM_TABLE *t,const char *stage,EFI_STATUS s) {
  char msg[256];CHAR16 wide[256];
  size_t n=Format(msg,sizeof(msg),
    "\r\nGA102 EXPERIMENTAL: stopped at stage=%s status=%#llx\r\n"
    "No driver release or GPU transaction occurred.\r\n"
    "See I:\\GA102-UNLOCK\\RUNxxxx.TXT for detail.\r\n"
    "Returning to Windows Boot Manager in 15s...\r\n",
    stage,(unsigned long long)s);
  if(n==(size_t)-1)return;
  UINTN i=0;for(;i<n&&i+1<sizeof(wide);i++)wide[i]=(CHAR16)(UINT8)msg[i];wide[i]=0;
  GuBootSay(wide);
  if(t->BootServices&&t->BootServices->Stall)t->BootServices->Stall(15000000);
}
EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image,EFI_SYSTEM_TABLE *t) {
  if(!image||!t||!t->BootServices||!t->RuntimeServices||!t->RuntimeServices->GetVariable)return EFI_INVALID_PARAMETER;
  UINT8 secure=1;UINTN n=1;UINT32 attrs=0;
  EFI_STATUS s=t->RuntimeServices->GetVariable(L"SecureBoot",&GlobalGuid,&attrs,&n,&secure);
  if(s!=EFI_SUCCESS||n!=1||secure)return EFI_SECURITY_VIOLATION;
  s=GuBootOpen(image,t);if(s!=EFI_SUCCESS)return s;
  const char *stage="discover";BOOLEAN unlock=FALSE;
  s=Discover(image,t);if(s!=EFI_SUCCESS)goto failed;
  GuLog("GA102 ROG STRIX Z790-A EXPERIMENTAL v0.1.0: hardware unvalidated; USB manual one-shot; bounded USB enumeration additions/removals; GPU ownership pinned\n");
  stage="assets";s=GuBootLoadAssets();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="read-only-cold-profile";s=GuGa102Prepare();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="stage-windows-image";s=GuBootLoadWindows();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="manual-choice";s=GuBootChoose(&unlock);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!unlock) {
    GuLog("Operator skipped: no GPU transaction, no driver release; stock Windows\n");
    if(Gu.fault||GuBootClose()!=EFI_SUCCESS)return EFI_DEVICE_ERROR;
    return GuBootWindows();
  }
  stage="GA102-release-preflight";s=GuGa102Preflight(t,image,Gu.handle);if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  GuBootSay(L"\r\nChecks passed. Releasing display; watch for Windows.\r\n");
  GuLog("Operator U: one physical attempt; no retries; display callbacks disabled\n");
  if(Gu.fault){s=EFI_DEVICE_ERROR;goto failed;}
  GuBootSilence();
  stage="GA102-release";s=GuGa102Release();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  if(!GuGa102Released()||!GuGa102ColdEntry()){s=EFI_NOT_READY;goto failed;}
  Gu.display_check=GuGa102Released;Gu.display_ok=TRUE;Gu.armed=TRUE;
  UINT64 mask=0xffffffffffffULL;
  /* .device=0x2203 (RTX 3090 Ti, GA102) -- this board's real id, not the
   * AD103 reference's 0x2705. unlock_610.c's unlock_assets()/is_ad102()
   * already route this exact id to the generic ga102 Booter profile
   * upstream -- see profile.c's header comment. */
  struct pci_dev dev={.vendor=0x10de,.device=0x2203,.dev={.dma_mask=&mask,.coherent_dma_mask=0xfffffffffULL},
    .current_state=PCI_D0,.pm_cap=Gu.pm_cap,.refs=1};
  struct gpu_unlock_610_shared shared={0};
  stage="shared-assets";int result=gpu_unlock_610_load_shared(&dev,&shared);
  if(!result) {stage="firmware-transaction";result=gpu_unlock_610_unlock(&dev,&shared);}
  gpu_unlock_610_release_shared(&shared);
  GuLog("Transaction result=%d safe=%u retained=%u started=%u quiesced=%u flrs=%u DMA_live=%u\n",
    result,GuSafeToReturn(),Gu.retained,Gu.started,Gu.quiesced,Gu.flrs,Gu.dma_count);
  if(result||Gu.fault||!GuSafeToReturn()){s=EFI_DEVICE_ERROR;goto failed;}
  stage="release-exclusive-claim";s=GuGa102Finish();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  /* The final pre-Windows check runs after the log closes: this line is not proof of a Windows start. */
  GuLog("TRANSACTION OK: hardware readbacks passed, DMA released; final pre-Windows check follows unlogged; benchmark still required\n");
  stage="close-USB-log";s=GuBootClose();if(s!=EFI_SUCCESS||Gu.fault)goto failed;
  stage="final-Windows-verification";if(!GuGa102ReadyForWindows()){s=EFI_NOT_READY;goto failed;}
  stage="start-Windows";s=GuBootWindows();
  /* A returned Windows image is not a successful handoff. */
  GuBootHalt();return s==EFI_SUCCESS?EFI_ABORTED:s;
failed:
  if(s==EFI_SUCCESS)s=EFI_DEVICE_ERROR;
  GuLog("STOP: stage=%s status=%#llx mutated=%u retained=%u fault=%u\n",stage,(unsigned long long)s,GuGa102Mutated(),Gu.retained,Gu.fault);
  if(GuGa102Mutated()||Gu.started||Gu.retained) {
    /* Fault-independent containment, no freeing of unverified live mappings. */
    if(Gu.started||Gu.master_attributes_pending)(VOID)GuClearMaster();
    (VOID)GuBootClose();GuBootHalt();return EFI_ABORTED;
  }
  SayStage(t,stage,s);
  (VOID)GuBootClose();return s;
}
