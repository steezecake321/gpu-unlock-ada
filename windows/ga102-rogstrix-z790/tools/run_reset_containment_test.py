from pathlib import Path
import subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1]
s=(Path(sys.argv[1]) if len(sys.argv)>1 else r/'vendor/unlock_610.c').read_text();a=s.index('static int unlock_transaction_reset(');b=s.index('\nint gpu_unlock_610_load_shared',a);body=s[a:b]
preamble=r'''
#include "compat/compat.h"
#include "port.c"
#include "vendor/gfw_eval.h"
#include <assert.h>
#include <stdio.h>
#define GU_SS0_UNLOCK 0x88888888u
#define GU_SS1_UNLOCK 8u
#define GU_RESULT(x) (x)
#define dev_info(d, ...) ((void)0)
#define dev_err(d, ...) ((void)0)
struct unlock_transaction_work {struct pci_dev *pdev; unsigned signature_calls; bool master,quiesced; u32 bars[6],permission_after[4];};
static int calls,fail_at,post,ad,late_fault,bad_wpr,bad_selector,unmaps,frees;
static int step(void){return ++calls==fail_at?-EIO:0;}
VOID GuLog(const char *f,...){(void)f;}
static u32 pci_identity(struct pci_dev *p){(void)p;return GU_BOARD_ID;}
static bool is_ad102(struct unlock_transaction_work *w){(void)w;return ad;}
void pci_clear_master(struct pci_dev *p){(void)p;}
int pcie_flr(struct pci_dev *p){(void)p;post=1;return step();}
void pci_restore_state(struct pci_dev *p){(void)p;}
static int gpu_unlock_remove_apertures(struct pci_dev *p){(void)p;return step();}
static int diag_pci_word(struct unlock_transaction_work *w,bool cap,int o,u16 *v){(void)w;*v=cap?0:PCI_COMMAND_MEMORY;(void)o;return step();}
static int diag_pci_dword(struct unlock_transaction_work *w,bool cap,int o,u32 *v){(void)w;(void)cap;*v=o==PCI_VENDOR_ID?GU_BOARD_ID:0;return step();}
static bool inaccessible(u32 v){return v==~0u || (v&0xffff0000u)==0xbadf0000u;}
static int read_bar(struct unlock_transaction_work *w,u32 o,u32 *v){
 (void)w;*v=o==GU_FEAT_SS0_OFFSET?(bad_selector&&post?0:GU_SS0_UNLOCK):o==GU_FEAT_SS1_OFFSET?8:o==GU_GFW_CPUCTL_OFFSET?GU_GFW_CPUCTL_HALTED:o==0x001fa828?(bad_wpr?16:0):o==GU_FEAT_PLM_OFFSET?(ad?~0u:0x8b8f):o==0?0xb72000a1:0;
 return step();
}
static int wait_post_flr_bar(struct unlock_transaction_work *w,u32 c){(void)w;(void)c;return step();}
static int wait_post_flr_gfw(struct unlock_transaction_work *w){(void)w;return step();}
static int hold_sec2_reset(struct unlock_transaction_work *w,const char *s){(void)w;(void)s;return step();}
static int ad102_sample_permissions(struct unlock_transaction_work *w,const char *s,u32 *v,bool t){(void)w;(void)s;(void)v;(void)t;return step();}
static int ad102_readout(struct unlock_transaction_work *w,const char *s,bool t){(void)w;(void)s;(void)t;if(late_fault)Gu.fault=TRUE;return step();}
static EFI_STATUS EFIAPI unmap(EFI_PCI_IO_PROTOCOL *p,VOID *m){(void)p;(void)m;unmaps++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI freebuf(EFI_PCI_IO_PROTOCOL *p,UINTN n,VOID *v){(void)p;(void)n;(void)v;frees++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI pool(VOID *p){(void)p;return EFI_SUCCESS;}
static EFI_PCI_IO_PROTOCOL io;static EFI_BOOT_SERVICES bs;static GU_DMA entry;
static void setup(struct unlock_transaction_work *w){
 memset(w,0,sizeof(*w));w->signature_calls=2;GuInit(&bs,&io);GuStarted();calls=post=unmaps=frees=0;
 memset(&entry,0,sizeof(entry));entry.cpu=&entry;entry.dma=4096;entry.mapped=TRUE;Gu.allocations=&entry;Gu.dma_count=1;
 io.Unmap=unmap;io.FreeBuffer=freebuf;bs.FreePool=pool;
}
'''
# compat defines these logging macros; keep the test silent.
preamble=preamble.replace('#define dev_info(d, ...)','\n#undef dev_info\n#define dev_info(d, ...)').replace('#define dev_err(d, ...)','\n#undef dev_err\n#define dev_err(d, ...)')
main=r'''
int main(void){struct unlock_transaction_work w;int n,limit,negative=0;
 for(ad=0;ad<=1;ad++){
  fail_at=bad_wpr=bad_selector=late_fault=0;setup(&w);assert(unlock_transaction_reset(&w)==0);limit=calls;
  assert(w.quiesced && Gu.quiesced && !Gu.retained);assert(GuDmaFree(&entry,4096)==EFI_SUCCESS);assert(unmaps==1&&frees==1&&GuSafeToReturn());
  for(n=1;n<=limit;n++){fail_at=n;setup(&w);assert(unlock_transaction_reset(&w)!=0);assert(!w.quiesced&&!Gu.quiesced);assert(GuDmaFree(&entry,4096)==EFI_ACCESS_DENIED);assert(!unmaps&&!frees&&!GuSafeToReturn());negative++;}
  fail_at=0;bad_wpr=1;setup(&w);assert(unlock_transaction_reset(&w)!=0&&!w.quiesced&&!Gu.quiesced);bad_wpr=0;
  bad_selector=1;setup(&w);assert(unlock_transaction_reset(&w)!=0&&!w.quiesced&&!Gu.quiesced);bad_selector=0;
  setup(&w);w.signature_calls=1;assert(unlock_transaction_reset(&w)!=0&&!Gu.quiesced);
 }
 ad=1;late_fault=1;setup(&w);assert(unlock_transaction_reset(&w)!=0&&!w.quiesced&&!Gu.quiesced);
 printf("PASS reset containment: two success paths; %d injected access failures; WPR/selector/signature/fault failures retain DMA\n",negative);return 0;}
'''
(r/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='reset-test-',dir=r/'build') as tmp:
 c=Path(tmp)/'reset.c';exe=Path(tmp)/'reset.exe';c.write_text(preamble+body+main)
 cmd=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-DGU_UEFI_PORT','-DGU_AD104_TRANSACTION','-DGU_ENTRY_LATCHED_SS1_FULL','-I.','-Icompat','-Ivendor/edk2/Include','-Ivendor/edk2/Include/X64','-ffunction-sections','-fdata-sections',str(c),'-Wl,--gc-sections','-o',str(exe)]
 subprocess.run(cmd,cwd=r,check=True);subprocess.run([str(exe)],check=True)
