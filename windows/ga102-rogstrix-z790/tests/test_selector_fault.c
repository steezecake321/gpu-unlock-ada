#include "../port.c"
#include <assert.h>
#include <stdio.h>
VOID GuLog(const char *Format, ...) {(void)Format;}
static UINT32 ss0,ss1;
static unsigned reads,writes,configs;
static BOOLEAN fail_write,fail_read;
static EFI_STATUS EFIAPI mr(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 b,UINT64 o,UINTN n,VOID *v) {
 reads++;*(UINT32*)v=o==0x82381c?ss0:o==0x823820?ss1:0x170000a1;return fail_read?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI mw(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT8 b,UINT64 o,UINTN n,VOID *v) {
 writes++;if(fail_write)return EFI_DEVICE_ERROR;if(o==0x82381c)ss0=*(UINT32*)v;if(o==0x823820)ss1=*(UINT32*)v;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI cr(EFI_PCI_IO_PROTOCOL *p,EFI_PCI_IO_PROTOCOL_WIDTH w,UINT32 o,UINTN n,VOID *v) {
 configs++;UINT32 id=GU_BOARD_ID;memcpy(v,&id,(size_t)1<<w);return EFI_SUCCESS;
}
static EFI_PCI_IO_PROTOCOL pci;
static void setup(void) {
 memset(&pci,0,sizeof(pci));pci.Mem.Read=mr;pci.Mem.Write=mw;pci.Pci.Read=cr;
 GuInit(NULL,&pci);Gu.profile_verified=Gu.armed=Gu.owned=Gu.display_ok=Gu.prom_verified=TRUE;
 Gu.bar_size=0x1000000;ss0=0;ss1=2;reads=writes=configs=0;fail_write=fail_read=FALSE;assert(GuLatchEntrySelectors(ss0,ss1));
}
static void advance(void) {
 GuWrite32(0x82381c,0x88888888);assert(GuSelectorPhase(GU_SELECTOR_PHASE_SS0_WRITTEN));assert(GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE));
}
int main(void) {
 UINT32 chip,id;EFI_STATUS cs,ps;unsigned old;
 setup();GuWrite32(0x823820,8);assert(Gu.fault && writes==0);old=reads;
 assert(GuRead32(0)==0xffffffffU && reads==old);assert(GuConfigRead(EfiPciIoWidthUint32,0,&id)==EFI_DEVICE_ERROR && configs==0);
 puts("PASS missing phases block SS1 and poison normal diagnostics");
 setup();advance();GuWrite32(0x823820,8);assert(!Gu.fault && writes==2 && ss1==8);assert(GuSelectorPhase(GU_SELECTOR_PHASE_TARGET));
 GuWrite32(0x823820,8);assert(Gu.fault && writes==2);puts("PASS valid phases reach callback once; duplicate blocked");
 setup();assert(!GuSelectorPhase(GU_SELECTOR_PHASE_SS1_WRITE) && Gu.fault && writes==0);
 setup();advance();fail_write=TRUE;GuWrite32(0x823820,8);assert(Gu.fault && writes==2);GuWrite32(0x823820,8);assert(writes==2);
 puts("PASS invalid order and failed callback cannot retry");
 old=reads;GuDiagProbe(&chip,&cs,&id,&ps);assert(Gu.fault && reads==old+1 && configs==1 && cs==EFI_SUCCESS && ps==EFI_SUCCESS && id==GU_BOARD_ID);
 fail_read=TRUE;GuDiagProbe(&chip,&cs,&id,&ps);assert(Gu.fault && cs==EFI_DEVICE_ERROR);
 Gu.owned=FALSE;old=reads;GuDiagProbe(&chip,&cs,&id,&ps);assert(reads==old && cs==EFI_NOT_READY);puts("PASS diagnostic status independent; latch persists");
 setup();GuStarted();Gu.fault=TRUE;GU_DMA entry={0};entry.cpu=&entry;entry.dma=4096;Gu.allocations=&entry;
 assert(GuDmaFree(&entry,4096)==EFI_ACCESS_DENIED && Gu.retained && !GuSafeToReturn());puts("PASS fault retains active DMA");return 0;
}
